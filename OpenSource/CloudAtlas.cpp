#include <string.h>
#include <Kapusha/gl/Texture.h>
#include "CloudAtlas.h"

CloudAtlas::CloudAtlas(math::vec2i size)
  : size_(size)
  , pix2tex_(1.f / size.x, 1.f / size.y)
  , pixels_(new kapusha::u32[size_.x * size_.y])
  , root_(math::rect2i(size))
{
}

CloudAtlas::~CloudAtlas(void)
{
  delete pixels_;
}

kapusha::Texture *CloudAtlas::texture() const
{
  kapusha::Texture *ret = new kapusha::Texture();
  ret->upload(kapusha::Texture::ImageDesc(size_.x, size_.y), pixels_);
  return ret;
}

math::rect2f CloudAtlas::addImage(math::vec2i size, const void *data)
{
  const Node *nd = root_.insert(size);
  if (!nd) return math::rect2f(-1.f);

  const kapusha::u32 *p_in = reinterpret_cast<const kapusha::u32*>(data);
  kapusha::u32 *p_out = pixels_ + nd->rect.bottom() * size_.x + nd->rect.left();
  for (int y = 0; y < size.y; ++y, p_out += size_.x, p_in += size.x)
    memcpy(p_out, p_in, 4 * size.x);

  return math::rect2f(math::vec2f(nd->rect.left(), nd->rect.top()) * pix2tex_,
                      math::vec2f(nd->rect.right(), nd->rect.bottom()) * pix2tex_);
}

CloudAtlas::Node::~Node()
{
  delete child[0];
  delete child[1];
}

CloudAtlas::Node *CloudAtlas::Node::insert(math::vec2i size)
{
  if (occupied)
  {
    // if we're not a leaf
    if (child[0])
    {
      if (Node *ret = child[0]->insert(size)) return ret;
      return child[1]->insert(size);
    }

    // a leaf, already occupied one
    return 0;
  }

  // early check whether it'll fit
  if (rect.width() < size.x || rect.height() < size.y)
    return 0;
  
  // from now on this is marked occupied anyway
  occupied = true;

  // if it fits exactly, return it
  if (rect.size() == size)
    return this;

  // split
  math::vec2i diff = rect.size() - size;
  math::rect2i r[2];
  if (diff.x > diff.y)
  {
    r[0] = math::rect2i(rect.left(), rect.bottom(),
                        rect.right() - diff.x, rect.top());
    r[1] = math::rect2i(rect.right() - diff.x, rect.bottom(),
                        rect.right(), rect.top());
  } else {
    r[0] = math::rect2i(rect.left(), rect.bottom(),
                        rect.right(), rect.top() - diff.y);
    r[1] = math::rect2i(rect.left(), rect.top() - diff.y,
                        rect.right(), rect.top());
  }

  child[0] = new Node(r[0]);
  child[1] = new Node(r[1]);

  return child[0]->insert(size);
}