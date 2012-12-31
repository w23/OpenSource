#pragma once
#include <kapusha/core/Core.h>
#include <kapusha/math/types.h>

namespace kapusha {
  class Texture;
}

class CloudAtlas
{
public:
  CloudAtlas(kapusha::vec2i size);
  ~CloudAtlas(void);

  kapusha::rect2f addImage(kapusha::vec2i size, const void *p);
  kapusha::Texture *texture() const;

private:
  kapusha::vec2i size_;
  kapusha::vec2f pix2tex_;
  kapusha::u16 *pixels_;

  struct Node {
    kapusha::rect2i rect;
    Node *next, *prev;

    Node(kapusha::rect2i _rect)
      : rect(_rect)
    { next = prev = 0; }
    bool insert(kapusha::vec2i size);
  };
  Node *root_;
};

