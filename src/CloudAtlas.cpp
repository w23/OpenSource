#include <string.h>
#include <kapusha/render/Texture.h>
#include "CloudAtlas.h"

CloudAtlas::CloudAtlas(kapusha::vec2i size)
  : size_(size)
  , pix2tex_(1.f / size.x, 1.f / size.y)
  , pixels_(new kapusha::u32[size_.x * size_.y])
{
  for (int i = 0; i < (size_.x * size_.y); ++i)
    pixels_[i] = 0x00ff0000;
  root_ = new Node(kapusha::rect2i(size_));
}

CloudAtlas::~CloudAtlas(void)
{
  for(Node *n = root_; n != 0;)
  {
    Node *next = n->next;
    delete n;
    n = next;
  }

  delete pixels_;
}

kapusha::Texture *CloudAtlas::texture() const
{
  kapusha::Texture *ret = new kapusha::Texture();
  ret->upload(kapusha::Texture::ImageDesc(size_.x, size_.y), pixels_);
  return ret;
}

kapusha::rect2f CloudAtlas::addImage(kapusha::vec2i size, const void *data)
{
  for(Node *n = root_;;)
  {
    if (n->insert(size))
    {
      kapusha::rect2i r = n->rect;
      KP_ASSERT(r.size() == size);

      if (n == root_)
        root_ = n->next;
      if (n->prev)
        n->prev->next = n->next;
      if (n->next)
        n->next->prev = n->prev;
      delete n;

      const kapusha::u32 *p_in = reinterpret_cast<const kapusha::u32*>(data);
      kapusha::u32 *p_out = pixels_ + r.bottom() * size_.x + r.left();
      for (int y = 0; y < size.y; ++y, p_out += size_.x, p_in += size.x)
        memcpy(p_out, p_in, 4 * size.x);

      return kapusha::rect2f(kapusha::vec2f(r.left()+.5f, r.top()) * pix2tex_,
                          kapusha::vec2f(r.right(), r.bottom()+.5f) * pix2tex_);
    }

    if (!n->next)
      break;

    // detect unlink
    if (n->next->prev != n)
    {
      if (n == root_)
        root_ = n->next;
      Node *t = n->next;
      if (n->prev)
        n->prev->next = t;
      if (n->next)
        n->next->prev = n->prev;
      delete n;
      n = t;
    } else {
      n = n->next;
    }
  }

  return kapusha::rect2f(-1.f);
}

bool CloudAtlas::Node::insert(kapusha::vec2i size)
{
  // early check whether it'll fit
  if (rect.width() < size.x || rect.height() < size.y)
    return false;
  
  // if it fits exactly, return it
  if (rect.size() == size)
    return true;

  // split
  kapusha::vec2i diff = rect.size() - size;
  kapusha::rect2i r[2];
  if (diff.x > diff.y)
  {
    r[0] = kapusha::rect2i(rect.left(), rect.bottom(),
                        rect.right() - diff.x, rect.top());
    r[1] = kapusha::rect2i(rect.right() - diff.x, rect.bottom(),
                        rect.right(), rect.top());
  } else {
    r[0] = kapusha::rect2i(rect.left(), rect.bottom(),
                        rect.right(), rect.top() - diff.y);
    r[1] = kapusha::rect2i(rect.left(), rect.top() - diff.y,
                        rect.right(), rect.top());
  }

  // the insertage
  Node *newn[2];
  newn[0] = new Node(r[0]);
  newn[1] = new Node(r[1]);

  newn[0]->next = newn[1];
  newn[1]->prev = newn[0];

  if (prev)
  {
    prev->next = newn[0];
    newn[0]->prev = prev;
  }

  if (next)
  {
    next->prev = newn[1];
    newn[1]->next = next;
  }

  next = newn[0];

  return false;
}
