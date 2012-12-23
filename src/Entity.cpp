#include <sstream>
#include <kapusha/core/Log.h>
#include "Entity.h"

Entity::Entity(void)
{
}


Entity::~Entity(void)
{
}

off_t streamSkipSpacesUntilAfterChars(kapusha::Stream* stream, const char* chars)
{
  off_t ret = -1;
  while(stream->error_ == kapusha::Stream::ErrorNone)
  {
    for(; stream->cursor_ < stream->end_; ++stream->cursor_)
    {
      if (ret != -1)
        return ret;

      if (!isspace(*stream->cursor_))
      {
        for(const char* p = chars; *p != 0; ++p)
          if (*stream->cursor_ == *p)
          {
            // do one more cycle to reload 
            ret = p - chars;
            break;
          }
        if (ret == -1)
          return -1;
      }
    }
    stream->refill();
  }

  return ret;
}

off_t streamExtractUntilAfterChars(kapusha::Stream* stream, char end,
                                         char* out, int outmax)
{
  char *p = out;
  while(stream->error_ == kapusha::Stream::ErrorNone)
  {
    for(; stream->cursor_ < stream->end_; ++stream->cursor_)
    {
      if (*stream->cursor_ == end)
      {
          ++stream->cursor_;
          *p = 0;
          return p - out;
      }
      *p++ = *stream->cursor_;
      KP_ASSERT(p < (out + outmax));
    }
    stream->refill();
  }
  return -1;
}

Entity* Entity::readNextEntity(kapusha::Stream* stream)
{
  if (0 != streamSkipSpacesUntilAfterChars(stream, "{"))
    return 0;

  char key[32];
  char value[1024];
  
  Entity *ret = new Entity();

  for(;;)
  {
    off_t chidx = streamSkipSpacesUntilAfterChars(stream, "\"}");
    if (chidx == 1)
      return ret;

    if (chidx == -1)
      break;
    
    if (-1 == streamExtractUntilAfterChars(stream, '"', key, sizeof key))
      break;

    if (-1 == streamSkipSpacesUntilAfterChars(stream, "\""))
      break;

    if (-1 == streamExtractUntilAfterChars(stream, '"', value, sizeof value))
      break;

    ret->params_[key] = value;
  }

  delete ret;
  return 0;
}

const std::string* Entity::getParam(const std::string& name) const
{

  auto entry = params_.find(name);
  if (entry == params_.end())
    return 0;

  return &entry->second;
}

kapusha::vec3f Entity::getVec3Param(const std::string& name) const
{
  auto entry = params_.find(name);
  if (entry == params_.end())
    return kapusha::vec3f();

  kapusha::vec3f value;
  std::stringstream ss(entry->second);
  ss >> value.x;
  ss >> value.y;
  ss >> value.z;

  return value;
}

kapusha::vec4f Entity::getVec4Param(const std::string& name) const
{
  auto entry = params_.find(name);
  if (entry == params_.end())
    return kapusha::vec4f();

  kapusha::vec4f value;
  std::stringstream ss(entry->second);
  ss >> value.x;
  ss >> value.y;
  ss >> value.z;
  ss >> value.w;

  return value;
}

void Entity::print() const
{
  L("Entity {");
  for(auto it = params_.begin(); it != params_.end(); ++it)
  {
    L("\t%s: %s", it->first.c_str(), it->second.c_str());
  }
  L("}");
}
