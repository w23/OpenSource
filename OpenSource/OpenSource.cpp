#include <Kapusha/sys/Log.h>
#include <Kapusha/math/types.h>
#include <Kapusha/gl/OpenGL.h>
#include <Kapusha/gl/Buffer.h>
#include <Kapusha/gl/Object.h>
#include <Kapusha/gl/Program.h>
#include <Kapusha/io/StreamFile.h>
#include "BSP.h"
#include "OpenSource.h"

OpenSource::OpenSource(void)
: camera_(math::vec3f(0,0,10))
, forward_speed_(0), right_speed_(0), pitch_speed_(0), yaw_speed_(0)
{
}

OpenSource::~OpenSource(void)
{
}

void OpenSource::init(kapusha::ISystem* system)
{
  system_ = system;
  
  BSP *bsp = new BSP;
  kapusha::StreamFile *stream = new kapusha::StreamFile;
  KP_ENSURE(stream->open("c1a1c.bsp") == kapusha::Stream::ErrorNone);

  bsp->load(stream);

  levels_.push_back(bsp);

  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  glFrontFace(GL_CW);
}

void OpenSource::resize(int width, int height)
{
  glViewport(0, 0, width, height);
  viewport_ = math::rect2f(0, height, width, 0);
  camera_.setAspect((float)width / (float)height);
}

void OpenSource::draw(int ms, float dt)
{
  camera_.moveForward(forward_speed_ * dt * 10.f);
  camera_.moveRigth(right_speed_ * dt * 10.f);
//  camera_.rotatePitch(pitch_speed_ * dt);
  //camera_.rotateYaw(yaw_speed_ * dt);
//  camera_.rotateAxis(math::vec3f(0.f, 1.f, 0.f), yaw_speed_ * dt);
  camera_.update();

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  for (auto it = levels_.begin(); it != levels_.end(); ++it)
    (*it)->draw(camera_);
  
  system_->redraw();
}

void OpenSource::keyEvent(const kapusha::IViewport::KeyEvent &event)
{
  switch (event.key()) {
    case 'w':
      forward_speed_ += event.isPressed() ? 1.f : -1.f;
      break;
    case 's':
      forward_speed_ += event.isPressed() ? -1.f : 1.f;
      break;
    case 'a':
      right_speed_ += event.isPressed() ? -1.f : 1.f;
      break;
    case 'd':
      right_speed_ += event.isPressed() ? 1.f : -1.f;
      break;
    case KeyEvent::KeyUp:
      pitch_speed_ += event.isPressed() ? 1.f : -1.f;
      break;
    case KeyEvent::KeyDown:
      pitch_speed_ += event.isPressed() ? -1.f : 1.f;
      break;
    case KeyEvent::KeyLeft:
      yaw_speed_ += event.isPressed() ? 1.f : -1.f;
      break;
    case KeyEvent::KeyRight:
      yaw_speed_ += event.isPressed() ? -1.f : 1.f;
      break;
    case KeyEvent::KeyEsc:
      system_->quit(0);
	  break;
      
    default:
      L("key %d is unknown", event.key());
  }
}

void OpenSource::pointerEvent(const kapusha::IViewport::PointerEvent &event)
{
  math::vec2f rel = viewport_.relative(event.main().point)*2.f - 1.f;
  //yaw_speed_ = -rel.x * 100.f;
  //pitch_speed_ = rel.y * 100.f;
  
  camera_.rotatePitch(rel.y);
  camera_.rotateAxis(math::vec3f(0.f, 1.f, 0.f), -rel.x);
  
  system_->pointerReset();
}
