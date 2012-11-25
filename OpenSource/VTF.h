#pragma once
#include <kapusha/math/types.h>

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
  kapusha::vec2i size() const { return size_; }

private:
  kapusha::vec2i size_;
};

