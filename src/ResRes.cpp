#include <kapusha/core/Core.h>
#include <kapusha/io/StreamFile.h>
#include "ResRes.h"

ResRes::ResRes(const char* path)
  : path_(path)
{
}

ResRes::~ResRes(void)
{
}

kapusha::StreamSeekable* ResRes::open(const char* name, ResourceType type) const
{
  std::string fullpath;
  switch (type)
  {
  case ResourceMap:
    fullpath = path_ + "/maps/" + name + ".bsp";
    break;
  case ResourceTexture:
    fullpath = path_ + "/materials/" + name + ".vtf";
    break;
  default:
    L("Unsupported resource type %d", type);
    return 0;
  }

  if (fullpath.empty())
    return 0;

  kapusha::StreamFile *stream = new kapusha::StreamFile;
  if (stream->open(fullpath.c_str()) != kapusha::Stream::ErrorNone)
  {
    L("no such resource: %s", fullpath.c_str());
    delete stream;
    stream = 0;
  }
  return stream;
}