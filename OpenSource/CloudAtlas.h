#pragma once
#include <Kapusha/sys/System.h>
#include <Kapusha/math/types.h>

namespace kapusha {
  class Texture;
}

class CloudAtlas
{
public:
  CloudAtlas(math::vec2i size);
  ~CloudAtlas(void);

  math::rect2f addImage(math::vec2i size, const void *p);
  kapusha::Texture *texture() const;

private:
  math::vec2i size_;
  math::vec2f pix2tex_;
  kapusha::u32 *pixels_;

  struct Node {
    math::rect2i rect;
    Node *next, *prev;

    Node(math::rect2i _rect)
      : rect(_rect)
    { next = prev = 0; }
    bool insert(math::vec2i size);
  };
  Node *root_;
};

