#pragma once

#include <string>
#include <vector>
#include <deque>
#include <map>

#include <Kapusha/core/IViewport.h>
#include <Kapusha/render/Camera.h>

#include "ResRes.h"

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
  virtual void init(kapusha::IViewportController *viewctrl);
  virtual void resize(kapusha::vec2i size);
  virtual void draw(int ms, float dt);
  virtual void close() {}
  virtual void inputPointer(const kapusha::PointerState& pointers);
  virtual void inputKey(const kapusha::KeyState& keys);

private:
  ResRes resources_;
  int depth_;
  kapusha::IViewportController *viewctrl_;
  std::map<std::string, BSP*> levels_;
  std::vector<BSP*> levelsv_;
  std::deque<std::string> maps_to_load_;
  
  kapusha::rect2f viewport_;
  kapusha::Camera camera_;
  float forward_speed_;
  float right_speed_;
  float pitch_speed_;
  float yaw_speed_;

  int selection_;
  bool show_selection_;
};