#pragma once

#include <string>

namespace kapusha {
  class StreamSeekable;
}

class ResRes
{
public:
  ResRes(const char* path);
  ~ResRes(void);

  enum ResourceType {
    ResourceGuess = 0,
    ResourceMap,
    ResourceMaterial,
    ResourceTexture
  };

  kapusha::StreamSeekable* open(const char* name,
                                ResourceType type = ResourceGuess) const;

private:
  std::string path_;
};

