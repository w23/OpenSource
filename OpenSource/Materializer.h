#pragma once
#include <string>
#include <map>

namespace kapusha {
  class Program;
  class Material;
}

class Materializer
{
public:
  Materializer(void);
  ~Materializer(void);

  kapusha::Material* loadMaterial(const char* name);

  void print() const;

private:
  kapusha::Program *UBER_SHADER1111_;
  std::map<std::string, kapusha::Material*> cached_materials_;
};

