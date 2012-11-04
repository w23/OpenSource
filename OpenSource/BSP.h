#pragma once


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

private:
  class Impl;
  std::auto_ptr<Impl> pimpl_;
};