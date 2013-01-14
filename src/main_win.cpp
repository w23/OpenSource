#include <kapusha/sys/win/Window.h>
#include <kapusha/core/Log.h>
#include "OpenSource.h"

#define WIDTH 1024
#define HEIGHT 768

using namespace kapusha;

///////////////////////////////////////////////////////////////////////////////

class OutputDebugLog : public Log::ISystemLog {
public:
  virtual void write(const char *string)
  {
    OutputDebugStringA(string);
    OutputDebugStringA("\n");
  }
};

///////////////////////////////////////////////////////////////////////////////

std::string extractMapName(const char* s)
{
  const char* mapspos = strstr(s, "\\maps\\");
  const char* bsppos = strstr(s, ".bsp");

  if (mapspos == 0 || bsppos == 0) return std::string();

  return std::string(mapspos + 6, bsppos);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdLine, int cmdShow)
{
  UNREFERENCED_PARAMETER(hPrevInst);
  UNREFERENCED_PARAMETER(cmdLine);
  UNREFERENCED_PARAMETER(cmdShow);

  KP_LOG_OPEN("OpenSource.log", new OutputDebugLog());
  if (__argc < 2)
  {
    L("Feed me some *.bsp file kudasai");
    MessageBox(0, L"Feed me some *.bsp file kudasai", L"THE CAPTION",
      MB_OK | MB_ICONEXCLAMATION);
    return 1;
  }

  int infilenamelen = strlen(__argv[1]);
  char *infile = new char[infilenamelen+1];
  strcpy(infile, __argv[1]);
  char* mapspos = strstr(infile, "\\maps\\");
  char* bsppos = strstr(infile, ".bsp");
  
  if (mapspos == 0 || bsppos == 0)
  {
    L("File %s doesn't seem to be .bsp or lie in \"maps\" dir", infile);
    MessageBox(0,
      L"Specified file doesn't seem to be .bsp or lie in \"maps\" dir",
      L"THE CAPTION", MB_OK | MB_ICONEXCLAMATION);
    return 2;
  }

  *mapspos = 0;
  *bsppos = 0;

  char *filepart = mapspos + 6;

  OpenSource *os = new OpenSource(infile, filepart, 128);

  for (int i = 2; i < __argc; ++i)
  {
    std::string mapname = extractMapName(__argv[i]);
    if (!mapname.empty())
      os->addMapRestriction(mapname);
  }

  return RunWindow(hInst, os, WIDTH, HEIGHT, false);
}