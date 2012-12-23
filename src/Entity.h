#pragma once
#include <string>
#include <map>
#include <kapusha/io/Stream.h>
#include <kapusha/math/types.h>

class Entity
{
public:
  ~Entity(void);

  static Entity* readNextEntity(kapusha::Stream* stream);

  const std::map<std::string, std::string>& params() { return params_; }

  const std::string* getParam(const std::string& name) const;
  kapusha::vec3f getVec3Param(const std::string& name) const;
  kapusha::vec4f getVec4Param(const std::string& name) const;

  void print() const;

private:
  Entity(void);
  std::map<std::string, std::string> params_;
};

