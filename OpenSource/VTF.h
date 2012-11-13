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

private:
  math::vec3f average_color_;
};

