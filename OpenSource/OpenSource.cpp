#include <Kapusha/math/types.h>
#include <Kapusha/gl/OpenGL.h>
#include <Kapusha/gl/Buffer.h>
#include <Kapusha/gl/Object.h>
#include <Kapusha/gl/Program.h>
#include <Kapusha/io/StreamFile.h>
#include "BSP.h"
#include "OpenSource.h"

OpenSource::OpenSource(void)
{
}

OpenSource::~OpenSource(void)
{
}

void OpenSource::init(kapusha::ISystem* system)
{
  BSP *bsp = new BSP;
  kapusha::StreamFile *stream = new kapusha::StreamFile;
  stream->open("c1a1c.bsp");
  bsp->load(stream);

  levels_.push_back(bsp);

  overlay_ = new kapusha::Object();

  const char* svtx =
    "attribute vec4 vtx;\n"
    "void main(){\n"
    "gl_Position = vtx;\n"
    "}"
  ;
  const char* sfrg =
    "void main(){\n"
    "gl_FragColor = vec4(1.,0.,0.,0.);\n"
    "}"
  ;
  kapusha::Program *prog = new kapusha::Program(svtx, sfrg);
  overlay_->setProgram(prog);

  math::vec2f rect[4] = {
    math::vec2f(-1.f, -1.f),
    math::vec2f(-1.f,  1.f),
    math::vec2f( 1.f,  1.f),
    math::vec2f( 1.f, -1.f)
  };
  kapusha::Buffer *fsrect = new kapusha::Buffer();
  fsrect->load(rect, sizeof rect);
  overlay_->setAttribSource("vtx", fsrect, 2);

  overlay_->setGeometry(0, 0, 4, kapusha::Object::GeometryTriangleFan);

  //glClearColor(0.f, 0.f, 0.f, 0.f);
}

void OpenSource::resize(int width, int height)
{
  glViewport(0, 0, width, height);
}

void OpenSource::draw(int ms)
{
  glClear(GL_COLOR_BUFFER_BIT);

  for (auto it = levels_.begin(); it != levels_.end(); ++it)
    (*it)->draw();

  //overlay_->draw();
}