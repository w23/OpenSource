#include <stdlib.h>
#include <errno.h>
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
    fprintf(stderr, "Usage: %s path_to_gcf/root/gamename first_map_name [map_count_limit(=128 by default)]\n", argv[0]);
    return 1;
  }
  KP_LOG_OPEN("OpenSource.log", new StderrLog);

  int map_limit = 128;
  if (argc > 3)
  {
  	map_limit = strtol(argv[3], 0, 0);
	if (map_limit < 1) {
		fprintf(stderr, "Invalid map count limit %s, must be a (reasonable) number > 0\n", argv[3]);
		return EINVAL;
	}
  }

  return kapusha::KPSDL(new OpenSource(argv[1], argv[2], map_limit), 1280, 720);
}

