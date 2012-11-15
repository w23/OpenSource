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
    bool occupied;
    math::rect2i rect;
    Node *child[2];

    Node(math::rect2i _rect)
      : occupied(false), rect(_rect)
    { child[0] = child[1] = 0; }
    ~Node();
    Node *insert(math::vec2i size);
  };
  Node root_;
};

