#include <windows.h>
#include <Kapusha/sys/Log.h>
#include <Kapusha/gl/OpenGL.h>
#include "OpenSource.h"

#define WIDTH 1024
#define HEIGHT 768

///////////////////////////////////////////////////////////////////////////////
using namespace kapusha;

class SystemWin : public ISystem
{
public:
  SystemWin() : need_redraw_(true) {}

  virtual void quit(int code)
  {
    PostQuitMessage(code);
  }

  virtual void redraw()
  {
    need_redraw_ = true;
  }

public:
  bool need_redraw_;
};

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
IApplication *application;

LRESULT CALLBACK windowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
  case WM_SIZE:
    application->resize(lParam & 0xffff, lParam >> 16);
    break;

  case WM_CLOSE:
  case WM_DESTROY:
  	PostQuitMessage(0);
    return 0;
	}
  return DefWindowProc(hWnd, msg, wParam, lParam);
}

///////////////////////////////////////////////////////////////////////////////

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdLine, int cmdShow)
{
  UNREFERENCED_PARAMETER(hPrevInst);
  UNREFERENCED_PARAMETER(cmdLine);
  UNREFERENCED_PARAMETER(cmdShow);

  SP_LOG_OPEN("OpenSource.log", new OutputDebugLog());
  L("O NOES!!");

  SystemWin system;
  application = new OpenSource();

  L("Creating window");

  WNDCLASSEX wndclass;
  ZeroMemory(&wndclass, sizeof wndclass);
  wndclass.cbSize = sizeof wndclass;
  wndclass.style = CS_OWNDC;
  wndclass.lpfnWndProc = windowProc;
  wndclass.hInstance = hInst;
  wndclass.lpszClassName = L"OpenSourceWindowClass";
  RegisterClassEx(&wndclass);

  HWND hWnd = CreateWindow(wndclass.lpszClassName, 0, WS_POPUP|WS_VISIBLE|WS_OVERLAPPEDWINDOW,
    0, 0, WIDTH, HEIGHT, 0, 0, hInst, 0);

  L("hWnd = %d", hWnd);

  HDC hDC = GetDC(hWnd);
  static const PIXELFORMATDESCRIPTOR pfd = {
    sizeof(PIXELFORMATDESCRIPTOR), 1, PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER, PFD_TYPE_RGBA,
    32, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 32, 0, 0, PFD_MAIN_PLANE, 0, 0, 0, 0 };
  SetPixelFormat(hDC,ChoosePixelFormat(hDC,&pfd),&pfd);
  wglMakeCurrent(hDC,wglCreateContext(hDC));

  glewInit();

  application->init(&system);

  L("will enter message loop");

  MSG message;
  while (GetMessage(&message, NULL, 0, 0))
	{
			TranslateMessage(&message);
			DispatchMessage(&message);

      application->draw(GetTickCount());
      SwapBuffers(hDC);
	}

  delete application;
  application = 0;

  L("Exiting");

	return (int) message.wParam;
}