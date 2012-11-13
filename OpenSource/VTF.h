#pragma once
#include <Kapusha/math/types.h>

namespace kapusha {
  class Stream;
}

class VTF
{
public:
  VTF(void);
  ~VTF(void);

  bool load(kapusha::Stream& stream);
  math::vec3f averageColor() const { return average_color_; }
  math::vec2i size() const { return size_; }

private:
  math::vec2i size_;
  math::vec3f average_color_;
};

