#pragma once

#include <vector>

#include <Kapusha/sys/IApplication.h>

namespace kapusha {
  class Object;
}

class BSP;

class OpenSource : public kapusha::IApplication
{
public:
  OpenSource();
  virtual ~OpenSource();

public: // IApplication
  virtual void init(kapusha::ISystem* system);
  virtual void resize(int width, int height);
  virtual void draw(int ms);

private:
  std::vector<BSP*> levels_;
  kapusha::Object *overlay_;
};

