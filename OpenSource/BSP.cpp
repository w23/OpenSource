#include <Kapusha/sys/Log.h>
#include <Kapusha/io/Stream.h>
#include <Kapusha/math/types.h>
#include <Kapusha/gl/Buffer.h>
#include <Kapusha/gl/Program.h>
#include <Kapusha/gl/Material.h>
#include <Kapusha/gl/Batch.h>
#include <Kapusha/gl/Object.h>
#include <Kapusha/gl/Camera.h>
#include "BSP.h"

using namespace math;
using namespace kapusha;

namespace bsp {

struct lump_t {
  int offset;
	int length;
	int version;
	int identifier;
};

enum lumpType {
  lumpEntities = 0,
  lumpVertexes = 3,
  lumpFaces = 7,
  lumpEdges = 12,
  lumpSurfedges = 13,
  _lumpsCount = 64
};

struct header_t
{
  char magic[4];
	int version;
	lump_t lumps[_lumpsCount];
	int	revision;
};

}

class BSP::Impl {
public:
  Impl(StreamSeekable *stream)
    : stream_(stream)
  {
    //! \fixme check for errors
    stream->copy(&header_, sizeof header_);

    //! \fixme check format

    L("BSP magic: %c%c%c%c", header_.magic[0], header_.magic[1],
      header_.magic[2], header_.magic[3]);
    L("BSP version: %d", header_.version);
    L("BSP revision %d", header_.revision);

    // dump entities
    /*
    {
      const bsp::lump_t *lump_ent = header_.lumps + bsp::lumpEntities;
      stream->seek(lump_ent->offset, StreamSeekable::ReferenceStart);
      char* ent_txt = new char[lump_ent->length];
      stream->copy(ent_txt, lump_ent->length);
      L("Entities: %s", ent_txt);
      delete ent_txt;
    }
    */

    // load vertices
    const bsp::lump_t *lump_vtx = header_.lumps + bsp::lumpVertexes;
    stream->seek(lump_vtx->offset, StreamSeekable::ReferenceStart);
    L("Vertices: %d", lump_vtx->length / 12);

    KP_ASSERT(sizeof(vec3f) == 12);
    int bytes_vertices = lump_vtx->length;
    int num_vertices = bytes_vertices / sizeof(vec3f);
    vec3f *vertices = new vec3f[num_vertices];
    stream->copy(vertices, bytes_vertices);
    for (int i = 0; i < num_vertices; ++i)
      vertices[i] = vertices[i].xzy() * vec3f(1,1,-1) * .01905f;
    Buffer *vtxb = new Buffer;
    vtxb->load(vertices, bytes_vertices);
    delete vertices;

    // load edges
    const bsp::lump_t *lump_edges = header_.lumps + bsp::lumpEdges;
    stream->seek(lump_edges->offset, StreamSeekable::ReferenceStart);
    int num_edges = lump_edges->length / 4; 
    L("Edges: %d", num_edges);
    int* edges = new int[num_edges * 2], *p = edges;
    for (int i = 0; i < num_edges; ++i)
    {
      const unsigned short *edge = static_cast<const unsigned short*>(stream->read(4));
      *p++ = edge[0];
      *p++ = edge[1];
    }
    Buffer *edgeb = new Buffer();
    edgeb->load(edges, num_edges * sizeof *edges * 2);
    
    Batch* batch = new Batch();
    const char* svtx =
      "uniform mat4 mview, mproj;\n"
      "attribute vec4 vtx;\n"
      "void main(){\n"
      "gl_Position = mproj * mview * vtx;\n"
      "}"
    ;
    const char* sfrg =
      "void main(){\n"
      "gl_FragColor = vec4(1.);\n"
      "}"
    ;
    batch->setMaterial(new Material(new Program(svtx, sfrg)));
    batch->setAttribSource("vtx", vtxb);
    batch->setGeometry(Batch::GeometryLineList, 0, num_edges * 2, edgeb);
    
    edges_ = new Object(batch);
  }

  bool isValid() {
    return stream_.get() != 0;
  }

  void draw(const kapusha::Camera& cam) const
  {
    edges_->draw(cam.getView(), cam.getProjection());
  }

private:
  std::auto_ptr<StreamSeekable> stream_;
  bsp::header_t header_;
  Object* edges_;
};

BSP::BSP(void)
{
}

BSP::~BSP(void)
{
}

bool BSP::load(StreamSeekable* stream)
{
  pimpl_.reset(new Impl(stream));

  return pimpl_->isValid();
}

void BSP::draw(const kapusha::Camera& cam) const
{
  pimpl_->draw(cam);
}
