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

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdLine, int cmdShow)
{
  UNREFERENCED_PARAMETER(hPrevInst);
  UNREFERENCED_PARAMETER(cmdLine);
  UNREFERENCED_PARAMETER(cmdShow);

  KP_LOG_OPEN("OpenSource.log", new OutputDebugLog());
  L("O NOES!!");

  OpenSource *os = new OpenSource(__argv[1], __argv[2], 128);

  return RunWindow(hInst, os, WIDTH, HEIGHT, false);
}