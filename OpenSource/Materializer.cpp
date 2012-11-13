#include <Kapusha/sys/Log.h>
#include <Kapusha/io/Stream.h>
#include <Kapusha/gl/Program.h>
#include <Kapusha/gl/Material.h>
#include <Kapusha/gl/Texture.h>
#include "ResRes.h"
#include "VTF.h"
#include "Materializer.h"

static const char* shader_vertex =
  "uniform mat4 mview, mproj;\n"
  "uniform vec4 trans;\n"
  "uniform vec2 u_v2_texscale;\n"
  "attribute vec4 vtx, tex;\n"
  "varying vec2 v_v2_tex;\n"
  "void main(){\n"
    "gl_Position = mproj * mview * (vtx + trans);\n"
    "v_v2_tex = tex * u_v2_texscale;\n"
  "}"
;

static const char* shader_fragment =
  "uniform vec4 color;\n"
  "varying vec2 v_v2_tex;\n"
  "void main(){\n"
    "gl_FragColor = vec4(color) + .1 * vec4(mod(v_v2_tex, 1.), 0., 0.);\n"
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

  auto fm = cached_materials_.find(name);
  if (fm != cached_materials_.end())
  {
    return fm->second;
  }

  // get average color
  math::vec3f color(1, 0, 0);
  kapusha::StreamSeekable* restream = resources_.open(name.c_str(), ResRes::ResourceTexture);
  kapusha::Material* mat = new kapusha::Material(UBER_SHADER1111_);
  if (restream)
  {
    VTF tex;
    if (tex.load(*restream))
    {
      color = tex.averageColor();
      mat->setUniform("u_v2_texscale", math::vec2f(1.f / tex.size().x, 1.f / tex.size().y));
    }
    delete restream;
  }
  
  if (name != "__BSP_edge")
    mat->setUniform("color", math::vec4f(color, 1.f));//math::vec4f(math::frand(), math::frand(), math::frand(), 1.f));
  else
    mat->setUniform("color", math::vec4f(1.f));

  cached_materials_[name] = mat;

  return mat;
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