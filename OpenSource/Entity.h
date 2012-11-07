#pragma once
#include <string>
#include <map>
#include <Kapusha/io/Stream.h>
#include <Kapusha/math/types.h>

class Entity
{
public:
  ~Entity(void);

  static Entity* readNextEntity(kapusha::Stream* stream);

  const std::map<std::string, std::string>& params() { return params_; }

  const std::string* getParam(const std::string& name) const;
  math::vec3f getVec3Param(const std::string& name) const;

  void print() const;

private:
  Entity(void);
  std::map<std::string, std::string> params_;
};

