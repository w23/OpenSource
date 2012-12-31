#include <kapusha/core/Log.h>
#include <kapusha/math/types.h>
#include <kapusha/render/Render.h>
#include <kapusha/io/StreamFile.h>
#include "Materializer.h"
#include "BSP.h"
#include "OpenSource.h"

OpenSource::OpenSource(
  const char *path,
  const char *file,
  int depth)
: resources_(path)
, depth_(depth)
, camera_(kapusha::vec3f(0,10,0), kapusha::vec3f(0), kapusha::vec3f(0,0,1), 60.f, 1.7, 10.f, 100000.f)
, forward_speed_(0), right_speed_(0), pitch_speed_(0), yaw_speed_(0)
, selection_(0)
, show_selection_(false)
, mouselook_(false)
{
  maps_to_load_.push_back(file);
}

OpenSource::~OpenSource(void)
{
}

void OpenSource::init(kapusha::IViewportController* viewctrl)
{
  viewctrl_ = viewctrl;
  render_ = new kapusha::Render;

  Materializer materializer(resources_);

  while (depth_ > 0 && !maps_to_load_.empty())
  {
    //L("maps to load left %d %d", maps_to_load_.size(), depth_);

    std::string map = maps_to_load_.front();
    maps_to_load_.pop_front();

    L("Loading map %s", map.c_str());

    kapusha::StreamSeekable *stream = resources_.open(map.c_str(), ResRes::ResourceMap);
    if (!stream)
    {
      L("cannot load map %s", map.c_str());
      continue;
    }
    BSP *bsp = new BSP;
    KP_ENSURE(bsp->load(stream, &materializer));
    delete stream;
    
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

            bsp->setParent(found->second, landmark->second - minemark->second);
            //L("%s links to %s", map.c_str(), ref->first.c_str());
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
    levelsv_.push_back(bsp);
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

  render_->cullFace().on();
  render_->depthTest().on();
  glFrontFace(GL_CW);
}

void OpenSource::resize(kapusha::vec2i s)
{
  glViewport(0, 0, s.x, s.y);
  viewport_ = kapusha::rect2f(0, (float)s.x, (float)s.y, 0);
  camera_.setAspect((float)s.x / (float)s.y);
}

void OpenSource::draw(int ms, float dt)
{
  const float speed = 1000.f *
  (viewctrl_->keyState().isShiftPressed() ? 5.f : 1);
  camera_.moveForward(forward_speed_ * dt * speed);
  camera_.moveRigth(right_speed_ * dt * speed);
  camera_.update();

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  for (auto it = levelsv_.begin(); it != levelsv_.end(); ++it)
    (*it)->draw(camera_);

  if (show_selection_)
    levelsv_[selection_]->drawContours(camera_);
  
  if (forward_speed_ != 0 || right_speed_ != 0)
    viewctrl_->requestRedraw();
}

void OpenSource::inputKey(const kapusha::KeyState &keys)
{
  using kapusha::KeyState;
  switch (keys.getLastKey()) {
    case KeyState::KeyW:
      forward_speed_ += keys.isLastKeyPressed() ? 1.f : -1.f;
      break;
    case KeyState::KeyS:
      forward_speed_ += keys.isLastKeyPressed() ? -1.f : 1.f;
      break;
    case KeyState::KeyA:
      right_speed_ += keys.isLastKeyPressed() ? -1.f : 1.f;
      break;
    case KeyState::KeyD:
      right_speed_ += keys.isLastKeyPressed() ? 1.f : -1.f;
      break;
    case KeyState::KeyZ:
      if (keys.isLastKeyPressed()) show_selection_ = !show_selection_;
      break;
    case KeyState::KeyE:
      {
        if (!keys.isLastKeyPressed()) break;
        ++selection_;
        selection_ %= levelsv_.size();
      }
      break;
    case KeyState::KeyQ:
      {
        if (!keys.isLastKeyPressed()) break;
        --selection_;
        if (selection_ < 0) selection_ += levelsv_.size();
      }
      break;
    case KeyState::KeyY:
      if (keys.isLastKeyPressed()) {
        levelsv_[selection_]->updateShift(levelsv_[selection_]->shift() - kapusha::vec3f(1000.f,0,0));
        for (auto it = levelsv_.begin(); it != levelsv_.end(); ++it)
          (*it)->updateShift((*it)->shift());
      }
      break;
    case 'U':
      if (keys.isLastKeyPressed()) {
        levelsv_[selection_]->updateShift(levelsv_[selection_]->shift() + kapusha::vec3f(1000.f,0,0));
        for (auto it = levelsv_.begin(); it != levelsv_.end(); ++it)
          (*it)->updateShift((*it)->shift());
      }
      break;
    case 'H':
      if (keys.isLastKeyPressed()) {
        levelsv_[selection_]->updateShift(levelsv_[selection_]->shift() - kapusha::vec3f(0,1000.f,0));
        for (auto it = levelsv_.begin(); it != levelsv_.end(); ++it)
          (*it)->updateShift((*it)->shift());
      }
      break;
    case 'J':
      if (keys.isLastKeyPressed()) {
        levelsv_[selection_]->updateShift(levelsv_[selection_]->shift() + kapusha::vec3f(0,1000.f,0));
        for (auto it = levelsv_.begin(); it != levelsv_.end(); ++it)
          (*it)->updateShift((*it)->shift());
      }
      break;
    case 'N':
      if (keys.isLastKeyPressed()) {
        levelsv_[selection_]->updateShift(levelsv_[selection_]->shift() - kapusha::vec3f(0,0,1000.f));
        for (auto it = levelsv_.begin(); it != levelsv_.end(); ++it)
          (*it)->updateShift((*it)->shift());
      }
      break;
    case 'm':
      if (keys.isLastKeyPressed()) {
        levelsv_[selection_]->updateShift(levelsv_[selection_]->shift() + kapusha::vec3f(0,0,1000.f));
        for (auto it = levelsv_.begin(); it != levelsv_.end(); ++it)
          (*it)->updateShift((*it)->shift());
      }
      break;
    case KeyState::KeyUp:
      pitch_speed_ += keys.isLastKeyPressed() ? 1.f : -1.f;
      break;
    case KeyState::KeyDown:
      pitch_speed_ += keys.isLastKeyPressed() ? -1.f : 1.f;
      break;
    case KeyState::KeyLeft:
      yaw_speed_ += keys.isLastKeyPressed() ? 1.f : -1.f;
      break;
    case KeyState::KeyRight:
      yaw_speed_ += keys.isLastKeyPressed() ? -1.f : 1.f;
      break;
    case KeyState::KeyEsc:
      if (keys.isLastKeyPressed())
      {
        if (mouselook_)
        {
          viewctrl_->limitlessPointer(false);
          viewctrl_->hideCursor(false);
          mouselook_ = false;
        } else
          viewctrl_->quit(0);
      }
      break;
      
    default:
      return;
  }
  
  viewctrl_->requestRedraw();
}

void OpenSource::inputPointer(const kapusha::PointerState &pointers)
{
  if (mouselook_)
  {
    camera_.rotatePitch(pointers.main().movement.y);
    camera_.rotateAxis(kapusha::vec3f(0.f, 0.f, 1.f), -pointers.main().movement.x);
    viewctrl_->requestRedraw();
  } else if (pointers.wasPressed())
  {
    viewctrl_->limitlessPointer(true);
    viewctrl_->hideCursor(true);
    mouselook_ = true;
  }
}
