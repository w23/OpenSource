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
  lumpTexinfo = 6,
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

struct texinfo_t
{
  float textureVecs[2][4];
  float lightmapVecs[2][4];
  u32 flags;
  u32 ref_texdata;
};

struct face_t
{
  u16 ref_plane;
  u8 plane_side;
  u8 on_node;
  u32 ref_surfedge;
  u16 surfedges_count;
  u16 ref_texinfo;
  u16 ref_dispinfo;
  u16 _unknown0; // ??
  u8 lightstyles[4]; // ??
  u32 ref_lightmap_offset;
  float area;
  u32 lightmapTextureMinsInLuxels[2]; // ??
  u32 lightmapTexturesizeInLuxels[2]; // ??
  u32 ref_original_face;
  u16 primitives_count;
  u16 ref_primitive;
  u32 lightmap_smoothing_group; // ??
};

}

struct Face {
};

class BSP::Impl {
private:
  

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

    if (header_.version != 19)
    {
      L("Still not sure how to load versions other than 19");
      stream_.reset();
      return;
    }

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
    Buffer* vertex_buffer = new Buffer;
    {
      const bsp::lump_t *lump_vtx = header_.lumps + bsp::lumpVertexes;
      L("Vertices: %d", lump_vtx->length / 12);
      stream->seek(lump_vtx->offset, StreamSeekable::ReferenceStart);
      vertex_buffer->load(stream, lump_vtx->length);
    }

    // preload surfedges
    // load edges
    const bsp::lump_t *lump_edges = header_.lumps + bsp::lumpEdges;
    int num_edges = lump_edges->length / 4;
    L("Edges: %d", num_edges);
    stream->seek(lump_edges->offset, StreamSeekable::ReferenceStart);
    struct edge_t {
      u16 a, b;
    } *edges = new edge_t[num_edges];
    stream->copy(edges, num_edges * 4);

    // load surfedges
    const bsp::lump_t *lump_surfedges = header_.lumps + bsp::lumpSurfedges;
    int num_surfedges = lump_surfedges->length / 4;
    L("Surfedges: %d", num_surfedges);
    stream->seek(lump_surfedges->offset, StreamSeekable::ReferenceStart);
    int *surfedges_i = new int[num_surfedges];
    u16 *surfedges = new u16[num_surfedges];
    stream->copy(surfedges_i, num_surfedges * 4);
    for(int i = 0; i < num_surfedges; ++i)
      if (surfedges_i[i] >= 0)
        surfedges[i] = edges[surfedges_i[i]].a;
      else 
        surfedges[i] = edges[-surfedges_i[i]].b;
    delete surfedges_i;

    int* buf_edges = new int[num_edges * 2], *p = buf_edges;
    for (int i = 0; i < num_edges; ++i)
    {
      *p++ = edges[i].a;
      *p++ = edges[i].b;
    }
    Buffer *edgeb = new Buffer();
    edgeb->load(buf_edges, (p - buf_edges) * 4);
    delete buf_edges;
    delete edges;

    // load texinfo
    const bsp::lump_t *lump_texinfo = header_.lumps + bsp::lumpTexinfo;
    int num_texinfo = lump_texinfo->length / sizeof(bsp::texinfo_t);
    L("Texinfos: %d", num_texinfo);
    stream->seek(lump_texinfo->offset, StreamSeekable::ReferenceStart);
    bsp::texinfo_t *texinfo = new bsp::texinfo_t[num_texinfo];
    stream->copy(texinfo, lump_texinfo->length);

    // load faces
    const bsp::lump_t *lump_faces = header_.lumps + bsp::lumpFaces;
    int num_faces = lump_faces->length / sizeof(bsp::face_t);
    L("Faces: %d", num_faces);
    stream->seek(lump_faces->offset, StreamSeekable::ReferenceStart);
    std::vector<int>  face_indices;
    for (int i = 0; i < num_faces; ++i)
    {
      bsp::face_t face;
      stream->copy(&face, sizeof(bsp::face_t));

      if (texinfo[face.ref_texinfo].flags & 0x401)//0x400)//0x286)
      {
//        L("Drop invisible face %d", i);
        continue;
      }
//        else L("%08x", texinfo[face.ref_texinfo].flags);

      int first = surfedges[face.ref_surfedge];
      int prev = surfedges[face.ref_surfedge+1];

      for (int j = 2; j < face.surfedges_count; ++j)
      {
        face_indices.push_back(first);
        face_indices.push_back(prev);
        prev = surfedges[face.ref_surfedge + j];
        face_indices.push_back(prev);
      }
    }
    delete texinfo;
    delete surfedges;
    Buffer *poly_index_buffer = new Buffer();
    poly_index_buffer->load(&(*face_indices.begin()), face_indices.size() * 4);
    int poly_indices = face_indices.size();
    
    const char* svtx =
      "uniform mat4 mview, mproj;\n"
      "attribute vec4 vtx;\n"
      "void main(){\n"
      "gl_Position = mproj * mview * (vtx.xzyw * vec4(.01905, .01905, -.01905, 1.));\n"
      "}"
    ;
    const char* sfrg =
      "uniform vec4 color;\n"
      "void main(){\n"
      "gl_FragColor = vec4(color);\n"
      "}"
    ;
    Program* prg = new Program(svtx, sfrg);

    Batch* batch = new Batch();
    Material* mat = new Material(prg);
    mat->setUniform("color", vec4f(1.f));
    batch->setMaterial(mat);
    batch->setAttribSource("vtx", vertex_buffer);
    batch->setGeometry(Batch::GeometryLineList, 0, num_edges * 2, edgeb);  
    edges_ = new Object(batch);

    batch = new Batch();
    mat = new Material(prg);
    mat->setUniform("color", vec4f(.5f));
    batch->setMaterial(mat);
    batch->setAttribSource("vtx", vertex_buffer);
    batch->setGeometry(Batch::GeometryTriangleList, 0, poly_indices, poly_index_buffer);
    polys_ = new Object(batch);

  }

  bool isValid() {
    return stream_.get() != 0;
  }

  void draw(const kapusha::Camera& cam) const
  {
    polys_->draw(cam.getView(), cam.getProjection());
    edges_->draw(cam.getView(), cam.getProjection());
  }

private:
  std::auto_ptr<StreamSeekable> stream_;
  bsp::header_t header_;
  Object* edges_;
  Object* polys_;
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
