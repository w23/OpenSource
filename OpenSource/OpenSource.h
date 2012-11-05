#pragma once

#include <vector>

#include <Kapusha/sys/IViewport.h>
#include <Kapusha/gl/Camera.h>

namespace kapusha {
  class Object;
}

class BSP;

class OpenSource : public kapusha::IViewport
{
public:
  OpenSource(const char* file);
  virtual ~OpenSource();

public: // IViewport
  virtual void init(kapusha::ISystem* system);
  virtual void resize(int width, int height);
  virtual void draw(int ms, float dt);
  void keyEvent(const kapusha::IViewport::KeyEvent &event);
  void pointerEvent(const kapusha::IViewport::PointerEvent &event);

private:
  const char* filename_;
  kapusha::ISystem *system_;
  std::vector<BSP*> levels_;
  
  math::rect2f viewport_;
  kapusha::Camera camera_;
  float forward_speed_;
  float right_speed_;
  float pitch_speed_;
  float yaw_speed_;
};

