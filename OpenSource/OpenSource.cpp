#include <Kapusha/sys/Log.h>
#include <Kapusha/math/types.h>
#include <Kapusha/gl/OpenGL.h>
#include <Kapusha/gl/Buffer.h>
#include <Kapusha/gl/Object.h>
#include <Kapusha/gl/Program.h>
#include <Kapusha/io/StreamFile.h>
#include "Materializer.h"
#include "BSP.h"
#include "OpenSource.h"

OpenSource::OpenSource(
  const std::string& path,
  const std::string& file,
  int depth)
: path_(path)
, depth_(depth)
, camera_(math::vec3f(0,10,0), math::vec3f(0), math::vec3f(0,0,1), 60.f, 1.7, 10.f, 100000.f)
, forward_speed_(0), right_speed_(0), pitch_speed_(0), yaw_speed_(0)
{
  maps_to_load_.push_back(file);
}

OpenSource::~OpenSource(void)
{
}

void OpenSource::init(kapusha::ISystem* system)
{
  system_ = system;

  Materializer materializer;

  while (depth_ > 0 && !maps_to_load_.empty())
  {
    L("maps to load left %d %d", maps_to_load_.size(), depth_);

    std::string map = maps_to_load_.front();
    maps_to_load_.pop_front();

    L("Loading map %s", map.c_str());

    BSP *bsp = new BSP;
    kapusha::StreamFile *stream = new kapusha::StreamFile;
    KP_ENSURE(stream->open((path_ + map + ".bsp").c_str()) == kapusha::Stream::ErrorNone);
    if (stream->error_)
    {
      L("cannot load map %s", map.c_str());
      delete stream;
      delete bsp;
      continue;
    }
    bsp->load(stream, &materializer);

    const BSP::MapLink& link = bsp->getMapLinks();
    {
      bool link_found = false;
      for(auto ref = link.maps.begin(); ref != link.maps.end(); ++ref)
      {
        auto found = levels_.find(ref->first);
        if (found != levels_.end())
        {
          if (!link_found)
          {
            auto minemark = link.landmarks.find(ref->second);
            KP_ASSERT(minemark != link.landmarks.end());

            auto landmark = found->second->getMapLinks().landmarks.find(ref->second);
            KP_ASSERT(landmark != found->second->getMapLinks().landmarks.end());

            bsp->translation() = landmark->second - minemark->second + found->second->translation();
            L("%s links to %s", map.c_str(), ref->first.c_str());
            link_found = true;
          }
        } else {
          if (map != ref->first && 
              std::find(maps_to_load_.begin(),
                        maps_to_load_.end(), ref->first) == maps_to_load_.end())
            maps_to_load_.push_back(ref->first);

        }
      }

      if (!link_found)
        L("%s doesn't link to anything!", map.c_str());
    }

    levels_[map] = bsp;
    depth_--;
  }

  {
    materializer.print();
  }

  {
    L("List of all loaded maps:");
    int i = 0;
    for (auto it = levels_.begin(); it != levels_.end(); ++it, ++i)
      L("%d: %s", i, it->first.c_str());
  }

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
  const float speed = 1000.f;
  camera_.moveForward(forward_speed_ * dt * speed);
  camera_.moveRigth(right_speed_ * dt * speed);
//  camera_.rotatePitch(pitch_speed_ * dt);
  //camera_.rotateYaw(yaw_speed_ * dt);
//  camera_.rotateAxis(math::vec3f(0.f, 1.f, 0.f), yaw_speed_ * dt);
  camera_.update();

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  for (auto it = levels_.begin(); it != levels_.end(); ++it)
    it->second->draw(camera_);
 
  if (forward_speed_ != 0 || right_speed_ != 0)
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
  camera_.rotateAxis(math::vec3f(0.f, 0.f, 1.f), -rel.x);
  
  system_->pointerReset();
  system_->redraw();
}
