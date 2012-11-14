#pragma once
#include <Kapusha/math/types.h>

namespace kapusha {
  class StreamSeekable;
  class Texture;
}

class VTF
{
public:
  VTF(void);
  ~VTF(void);

  kapusha::Texture *load(kapusha::StreamSeekable& stream);
  math::vec2i size() const { return size_; }

private:
  math::vec2i size_;
};

