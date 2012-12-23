#include <kapusha/sys/SDL/KPSDL.h>
#include <kapusha/core/Log.h>
#include "OpenSource.h"

class StderrLog : public kapusha::Log::ISystemLog
{
public:
  virtual void write(const char* msg) {
    fprintf(stderr, "[KP] %s\n", msg);
  }
};

int main(int argc, char* argv[])
{
  if (argc < 3)
  {
    fprintf(stderr, "Usage: %s path_to_gcf/root/gamename first_map_name\n", argv[0]);
    return 1;
  }
  KP_LOG_OPEN("OpenSource.log", new StderrLog);
  return kapusha::KPSDL(new OpenSource(argv[1], argv[2], 128), 1280, 720);
}

