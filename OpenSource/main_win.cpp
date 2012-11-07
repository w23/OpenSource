#include <windows.h>
#include <Kapusha/sys/Log.h>
#include <Kapusha/gl/OpenGL.h>
#include "OpenSource.h"

#define WIDTH 1024
#define HEIGHT 768

///////////////////////////////////////////////////////////////////////////////
using namespace kapusha;

int w = WIDTH, h = HEIGHT;
HWND hWnd;

class SystemWin : public ISystem
{
public:
  SystemWin() : need_redraw_(true), ignore_move_(false) {}

  virtual void quit(int code)
  {
    PostQuitMessage(code);
  }

  virtual void redraw()
  {
    need_redraw_ = true;
  }

  virtual void pointerReset()
  {
    POINT pt;
    pt.x = w / 2;
    pt.y = h / 2;
    ClientToScreen(hWnd, &pt);
    SetCursorPos(pt.x, pt.y);
    ignore_move_ = true;
  }

public:
  bool need_redraw_;
  bool ignore_move_;
};

static SystemWin system_win;

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
IViewport *viewport;

int keyTranslate(int key)
{
  if (key > 0x29 && key < 0x40)
    return key;

  if (key > 0x40 && key < 0x5B)
      return key + 0x20;

  switch (key)
  {
  case VK_UP:
    return IViewport::KeyEvent::KeyUp;
  case VK_DOWN:
    return IViewport::KeyEvent::KeyDown;
  case VK_LEFT:
    return IViewport::KeyEvent::KeyLeft;
  case VK_RIGHT:
    return IViewport::KeyEvent::KeyRight;
  default:
    L("Uknown key code %d", key);
    return key;
  }
}

LRESULT CALLBACK windowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
  case WM_SIZE:
    {
      w = lParam & 0xffff;
      h = lParam >> 16;
      viewport->resize(lParam & 0xffff, lParam >> 16);
    }
    break;

  case WM_KEYDOWN:
    if (!(lParam & (1<<30))) // ignore repeat
      viewport->userEvent(IViewport::KeyEvent(keyTranslate(wParam), 0));
    break;
  
  case WM_KEYUP:
    viewport->userEvent(IViewport::KeyEvent(keyTranslate(wParam), 0, false));
    break;

  case WM_MOUSEMOVE:
    {
      if (system_win.ignore_move_) { system_win.ignore_move_ = false; break; }
      viewport->userEvent(IViewport::PointerEvent(math::vec2f(lParam&0xffff, lParam >> 16),
                                                  IViewport::PointerEvent::Pointer::Move));
    }
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

  KP_LOG_OPEN("OpenSource.log", new OutputDebugLog());
  L("O NOES!!");

  viewport = new OpenSource(__argv[1], __argv[2]);

  L("Creating window");

  WNDCLASSEX wndclass;
  ZeroMemory(&wndclass, sizeof wndclass);
  wndclass.cbSize = sizeof wndclass;
  wndclass.style = CS_OWNDC;
  wndclass.lpfnWndProc = windowProc;
  wndclass.hInstance = hInst;
  wndclass.lpszClassName = L"OpenSourceWindowClass";
  RegisterClassEx(&wndclass);

  hWnd = CreateWindow(wndclass.lpszClassName, 0, WS_POPUP|WS_VISIBLE|WS_OVERLAPPEDWINDOW,
                      0, 0, WIDTH, HEIGHT, 0, 0, hInst, 0);

  L("hWnd = %d", hWnd);

  HDC hDC = GetDC(hWnd);
  static const PIXELFORMATDESCRIPTOR pfd = {
    sizeof(PIXELFORMATDESCRIPTOR), 1, PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER, PFD_TYPE_RGBA,
    32, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 32, 0, 0, PFD_MAIN_PLANE, 0, 0, 0, 0 };
  SetPixelFormat(hDC,ChoosePixelFormat(hDC,&pfd),&pfd);
  wglMakeCurrent(hDC,wglCreateContext(hDC));

  glewInit();

  viewport->init(&system_win);

  L("will enter message loop");

  MSG message;
  int prev = GetTickCount(), now;
  while (true)
  {
    if (system_win.need_redraw_)
      while (PeekMessage(&message, NULL, 0, 0, PM_REMOVE))
	    {
			  TranslateMessage(&message);
			  DispatchMessage(&message);
      }
    else 
      if (!GetMessage(&message, NULL, 0, 0))
        break;

    if (message.message == WM_QUIT)
        break;

    now = GetTickCount();
    viewport->draw(now, (now - prev) / 1000.f);
    SwapBuffers(hDC);
    prev = now;
	}

  delete viewport;
  viewport = 0;

  L("Exiting");

	return (int) message.wParam;
}