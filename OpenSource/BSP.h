#pragma once

#include <map>
#include <memory>


namespace kapusha {
  class StreamSeekable;
  class Camera;
}

using kapusha::StreamSeekable;

class BSP
{
public:
  BSP(void);
  ~BSP(void);

  bool load(StreamSeekable *stream);

  void draw(const kapusha::Camera&) const;

  math::vec3f& translation();
  const math::vec3f& translation() const;

public:
  struct MapLink {
    std::map<std::string, math::vec3f> landmarks;
    std::map<std::string, std::string> maps;
  };
  const MapLink& getMapLinks() const;

private:
  class Impl;
  std::auto_ptr<Impl> pimpl_;
};
