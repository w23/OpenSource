#include <kapusha/core/Core.h>
#include <kapusha/io/Stream.h>
#include <kapusha/render/Render.h>
#include "ResRes.h"
#include "VTF.h"
#include "Materializer.h"

static const char* shader_vertex =
  "uniform mat4 um4_view, um4_proj;\n"
  "uniform vec4 uv4_trans;\n"
  "uniform vec2 uv2_texscale;\n"
  "attribute vec4 av4_vtx, av4_tex;\n"
  "varying vec2 vv2_tex;\n"
  "void main(){\n"
    "gl_Position = um4_proj * um4_view * (av4_vtx + uv4_trans);\n"
    "vv2_tex = av4_tex.xy * uv2_texscale;\n"
  "}"
;

static const char* shader_fragment =
  "uniform sampler2D us2_texture;\n"
  "varying vec2 vv2_tex;\n"
  "void main(){\n"
    "gl_FragColor = texture2D(us2_texture, vv2_tex);\n"
  "}"
;

static const char* shader_vertex_lightmap =
  "uniform mat4 um4_view, um4_proj;\n"
  "uniform vec4 uv4_trans;\n"
  "attribute vec4 av4_vertex;\n"
  "attribute vec2 av2_lightmap;\n"
  "varying vec2 vv2_lightmap;\n"
  "void main(){\n"
    "gl_Position = um4_proj * um4_view * (av4_vertex + uv4_trans);\n"
    "vv2_lightmap = av2_lightmap;\n"
  "}"
;

static const char* shader_fragment_lightmap =
  "uniform sampler2D us2_lightmap;\n"
  "varying vec2 vv2_lightmap;\n"
  "void main(){\n"
    "gl_FragColor = texture2D(us2_lightmap, vv2_lightmap) + vec4(.02);\n"
  "}"
;

static const char* shader_vertex_white =
  "uniform mat4 um4_view, um4_proj;\n"
  "uniform vec4 uv4_trans;\n"
  "attribute vec4 av4_vertex;\n"
  "void main(){\n"
    "gl_Position = um4_proj * um4_view * (av4_vertex + uv4_trans);\n"
  "}"
;

static const char* shader_fragment_white =
  "void main(){\n"
    "gl_FragColor = vec4(1.);\n"
  "}"
;

Materializer::Materializer(const ResRes& resources)
  : resources_(resources)
{
  UBER_SHADER1111_ = new kapusha::Program(shader_vertex,
                                          shader_fragment);
}

Materializer::~Materializer(void)
{
}

kapusha::Material* Materializer::loadMaterial(const char *name_raw)
{
  std::string name(name_raw);

  //! \fixme broken atm
  KP_ASSERT(name != "__BSP_edge");

  auto fm = cached_materials_.find(name);
  if (fm != cached_materials_.end())
    return fm->second;

  if (name == "__lightmap_only")
  {
    kapusha::Program *prog = new kapusha::Program(shader_vertex_lightmap,
                                                  shader_fragment_lightmap);
    kapusha::Material* mat = new kapusha::Material(prog);
    cached_materials_[name] = mat;
    return mat;
  } else if (name == "__white")
  {
    kapusha::Program *prog = new kapusha::Program(shader_vertex_white,
                                                  shader_fragment_white);
    kapusha::Material* mat = new kapusha::Material(prog);
    cached_materials_[name] = mat;
    return mat;
  }

#if 0 // texture support is no more
  kapusha::StreamSeekable* restream = resources_.open(name.c_str(), ResRes::ResourceTexture);
  kapusha::Material* mat = new kapusha::Material(UBER_SHADER1111_);
  if (restream)
  {
    VTF tex;
    kapusha::Texture *texture = tex.load(*restream);
    if (texture)
    {
      mat->setUniform("uv2_texscale",
                      kapusha::vec2f(1.f / tex.size().x, 1.f / tex.size().y));
      mat->setTexture("us2_texture", texture);
    }
    delete restream;
  }
  
  cached_materials_[name] = mat;
#endif

  KP_ASSERT(!"true materials are not supported");

  return 0;

}

void Materializer::print() const
{
  L("Cached materials:");
  int i = 0;
  for (auto it = cached_materials_.begin();
    it != cached_materials_.end(); ++it, ++i)
    L("\t%d: %s", i, it->first.c_str());
  L("TOTAL: %d", cached_materials_.size());
}
