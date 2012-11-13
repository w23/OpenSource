#pragma once
#include <string>
#include <map>

namespace kapusha {
  class Program;
  class Material;
}

class ResRes;

class Materializer
{
public:
  Materializer(const ResRes& resources);
  ~Materializer(void);

  kapusha::Material* loadMaterial(const char* name);

  void print() const;

private:
  const ResRes& resources_;
  kapusha::Program *UBER_SHADER1111_;
  std::map<std::string, kapusha::Material*> cached_materials_;
};

