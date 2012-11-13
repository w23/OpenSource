#pragma once

#include <string>
#include <vector>
#include <deque>
#include <map>

#include <Kapusha/sys/IViewport.h>
#include <Kapusha/gl/Camera.h>

#include "ResRes.h"

namespace kapusha {
  class Object;
}

class BSP;

class OpenSource : public kapusha::IViewport
{
public:
  OpenSource(
    const char *path,
    const char *file,
    int depth = 32);
  virtual ~OpenSource();

public: // IViewport
  virtual void init(kapusha::ISystem* system);
  virtual void resize(int width, int height);
  virtual void draw(int ms, float dt);
  void keyEvent(const kapusha::IViewport::KeyEvent &event);
  void pointerEvent(const kapusha::IViewport::PointerEvent &event);

private:
  ResRes resources_;
  int depth_;
  kapusha::ISystem *system_;
  std::map<std::string, BSP*> levels_;
  std::vector<BSP*> levelsv_;
  std::deque<std::string> maps_to_load_;
  
  math::rect2f viewport_;
  kapusha::Camera camera_;
  float forward_speed_;
  float right_speed_;
  float pitch_speed_;
  float yaw_speed_;

  int selection_;
};