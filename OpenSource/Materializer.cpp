#include <Kapusha/sys/Log.h>
#include <Kapusha/gl/Program.h>
#include <Kapusha/gl/Material.h>
#include "Materializer.h"

static const char* shader_vertex =
  "uniform mat4 mview, mproj;\n"
  "uniform vec4 trans;\n"
  "attribute vec4 vtx;\n"
  "void main(){\n"
    "gl_Position = mproj * mview * (vtx + trans);\n"
  "}"
;

static const char* shader_fragment =
  "uniform vec4 color;\n"
  "void main(){\n"
    "gl_FragColor = vec4(color);\n"
  "}"
;

Materializer::Materializer(void)
{
  UBER_SHADER1111_ = new kapusha::Program(shader_vertex,
                                          shader_fragment);
}

Materializer::~Materializer(void)
{
}

kapusha::Material* Materializer::loadMaterial(const char *name_raw)
{
  // trim trailing numbers and shit -- no need in them anyway
  std::string name(name_raw);
  while (!name.empty())
  {
    if (!isalpha(name[name.length()-1]))
      name.resize(name.length()-1);
    else break;
  }
  auto fm = cached_materials_.find(name);
  if (fm != cached_materials_.end())
  {
    return fm->second;
  }

  kapusha::Material* mat = new kapusha::Material(UBER_SHADER1111_);
  if (name != "__BSP_edge")
    mat->setUniform("color", math::vec4f(math::frand(), math::frand(), math::frand(), 1.f));
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