#include <string.h>
#include <vector>
#include <map>
#include <kapusha/core/Log.h>
#include <kapusha/io/Stream.h>
#include <kapusha/math/types.h>
#include <kapusha/render/Render.h>
#include <kapusha/render/Camera.h>
#include "Materializer.h"
#include "Entity.h"
#include "CloudAtlas.h"
#include "BSP.h"

using namespace kapusha;

namespace bsp {

#pragma pack(push)
#pragma pack(1)

struct lump_t {
  int offset;
  int length;
  int version;
  int identifier;
};

enum lumpType {
  lumpEntities = 0,
  lumpTexdata = 2,
  lumpVertexes = 3,
  lumpTexinfo = 6,
  lumpFaces = 7,
  lumpLightmap = 8,
  lumpEdges = 12,
  lumpSurfedges = 13,
  lumpTexStrdata = 43,
  lumpTexStrtbl = 44,
  lumpLightmapHDR = 53,
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

struct texdata_t
{
  vec3f reflectivity;
  u32 nameStrtbl;
  u32 width, height;
  u32 vwidth, vheight;
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
  s32 lightmapMins[2]; // ??
  u32 lightmapSize[2]; // ??
  u32 ref_original_face;
  u16 primitives_count;
  u16 ref_primitive;
  u32 lightmap_smoothing_group; // ??
};

struct luxel_t
{
   u8 r, g, b;
   s8 exp;
};

#pragma pack(pop)

vec2f lightmapTexelAtVertex(vec3f v, const face_t &face,
                            const texinfo_t* texinfo,
                            rect2f atlas)
{
  vec2f lmap_size = vec2f(face.lightmapSize[0] + 1.f, face.lightmapSize[1] + 1.f);
  const texinfo_t *t = &texinfo[face.ref_texinfo];
  vec2f l(
    (t->lightmapVecs[0][0]*v.x + t->lightmapVecs[0][1] * v.y + 
     t->lightmapVecs[0][2]*v.z + t->lightmapVecs[0][3] - (float)face.lightmapMins[0]) / lmap_size.x,
    1.f-(t->lightmapVecs[1][0]*v.x + t->lightmapVecs[1][1] * v.y + 
         t->lightmapVecs[1][2]*v.z + t->lightmapVecs[1][3] - (float)face.lightmapMins[1]) / lmap_size.y);

  return l * atlas.size() + atlas.bottomLeft();
}

} // namespace bsp

BSP::BSP(void)
  : parent_(0)
  , relative_(0)
  , translation_(0)
  , shift_(0)
  , contours_(0)
{
}

BSP::~BSP(void)
{
  for(auto it = objects_.begin(); it != objects_.end(); ++it)
    delete *it;
  delete contours_;
}

bool BSP::load(StreamSeekable* stream, Materializer* materializer)
{
  //! \fixme check for errors
  bsp::header_t header;
  stream->copy(&header, sizeof header);

  //! \fixme check format
  //L("BSP magic: %c%c%c%c", header.magic[0], header.magic[1],
  //  header.magic[2], header.magic[3]);
  //L("BSP version: %d", header.version);
  //L("BSP revision %d", header.revision);

  if (header.version != 19)
  {
    L("BSP version: %d", header.version);
    L("Still not sure how to load versions other than 19");
    //return false;
  }

  // show lightmap info
  bsp::luxel_t* lightmap;
  int lmap_luxels;
  {
    const bsp::lump_t *lump_lmap = header.lumps + ((header.version < 20)? bsp::lumpLightmap : bsp::lumpLightmapHDR);
    lmap_luxels = lump_lmap->length / sizeof(bsp::luxel_t);
    //L("Lmap size: %d (%08x)", lump_lmap->length, lump_lmap->length);
    stream->seek(lump_lmap->offset, StreamSeekable::ReferenceStart);
    lightmap = new bsp::luxel_t[lmap_luxels];
    stream->copy(lightmap, lump_lmap->length);

    // pre-compute colors
    for (int i = 0; i < lmap_luxels; ++i)
    {
      vec3i c = vec3i(lightmap[i].r, lightmap[i].g, lightmap[i].b);
      if (lightmap[i].exp > 0)
      {
        c.x <<= lightmap[i].exp;
        c.y <<= lightmap[i].exp;
        c.z <<= lightmap[i].exp;
      } else {
        c.x >>= -lightmap[i].exp;
        c.y >>= -lightmap[i].exp;
        c.z >>= -lightmap[i].exp;
      }

#define CLAMP(f,min,max) (((f)<(min))?(min):((f)>(max)?(max):(f)))
      c.x = CLAMP(c.x, 0, 255);
      c.y = CLAMP(c.y, 0, 255);
      c.z = CLAMP(c.z, 0, 255);
#undef CLAMP

      *reinterpret_cast<u32*>(&lightmap[i]) = 0xff000000 | c.x << 16 | c.y << 8 | c.z;
    }
  }

  // guess combined lightmap size
  vec2i lmap_atlas_size(1);
  for (;lmap_atlas_size.x * lmap_atlas_size.x < lmap_luxels; lmap_atlas_size.x <<= 1);
  {
    int lmap_min_height = lmap_luxels / lmap_atlas_size.x;
    for (;lmap_atlas_size.y < lmap_min_height; lmap_atlas_size.y <<= 1);
  }
  CloudAtlas lmap_atlas(lmap_atlas_size);

  // find linked maps in entities
  {
    const bsp::lump_t *lump_ent = header.lumps + bsp::lumpEntities;
    stream->seek(lump_ent->offset, StreamSeekable::ReferenceStart);

    for(;;)
    {
      Entity *ent = Entity::readNextEntity(stream);
      if (!ent) break;

      const std::string *classname = ent->getParam("classname");
      if (classname)
      {
        if (*classname == "info_landmark")
        {
          KP_ASSERT(ent->getParam("targetname"));
          KP_ASSERT(ent->getParam("origin"));
          links_.landmarks[*ent->getParam("targetname")] = ent->getVec3Param("origin");
        } else if (*classname == "trigger_changelevel")
        {
          KP_ASSERT(ent->getParam("landmark"));
          KP_ASSERT(ent->getParam("map"));
          links_.maps[*ent->getParam("map")] = *ent->getParam("landmark");
        }
      }

      delete ent;
    }
  }

  // load vertices
  vec3f *vertices;
  int num_vertices;
  {
    const bsp::lump_t *lump_vtx = header.lumps + bsp::lumpVertexes;
    num_vertices = lump_vtx->length / sizeof(vec3f);
    vertices = new vec3f[num_vertices];
    //L("Vertices: %d", lump_vtx->length / 12);
    stream->seek(lump_vtx->offset, StreamSeekable::ReferenceStart);
    stream->copy(vertices, lump_vtx->length);
  }

  // preload surfedges
  // load edges
  const bsp::lump_t *lump_edges = header.lumps + bsp::lumpEdges;
  int num_edges = lump_edges->length / 4;
  //L("Edges: %d", num_edges);
  stream->seek(lump_edges->offset, StreamSeekable::ReferenceStart);
  struct edge_t {
    u16 a, b;
  } *edges = new edge_t[num_edges];
  stream->copy(edges, lump_edges->length);

  // load surfedges
  const bsp::lump_t *lump_surfedges = header.lumps + bsp::lumpSurfedges;
  int num_surfedges = lump_surfedges->length / 4;
  //L("Surfedges: %d", num_surfedges);
  stream->seek(lump_surfedges->offset, StreamSeekable::ReferenceStart);
  int *surfedges_i = new int[num_surfedges];
  u16 *surfedges = new u16[num_surfedges];
  stream->copy(surfedges_i, num_surfedges * 4);
  for(int i = 0; i < num_surfedges; ++i)
    if (surfedges_i[i] >= 0)
      surfedges[i] = edges[surfedges_i[i]].a;
    else 
      surfedges[i] = edges[-surfedges_i[i]].b;

  // load texinfo
  const bsp::lump_t *lump_texinfo = header.lumps + bsp::lumpTexinfo;
  int num_texinfo = lump_texinfo->length / sizeof(bsp::texinfo_t);
  //L("Texinfos: %d", num_texinfo);
  stream->seek(lump_texinfo->offset, StreamSeekable::ReferenceStart);
  bsp::texinfo_t *texinfo = new bsp::texinfo_t[num_texinfo];
  stream->copy(texinfo, lump_texinfo->length);

  // load texdata
  const bsp::lump_t *lump_texdata = header.lumps + bsp::lumpTexdata;
  int num_texdata = lump_texdata->length / sizeof(bsp::texdata_t);
  //L("Texdatas: %d", num_texdata);
  stream->seek(lump_texdata->offset, StreamSeekable::ReferenceStart);
  bsp::texdata_t *texdata = new bsp::texdata_t[num_texdata];
  stream->copy(texdata, lump_texdata->length);

  // load texstrdata
  const bsp::lump_t *lump_texstrdata = header.lumps + bsp::lumpTexStrdata;
  //L("Texstrdata: %d", lump_texstrdata->length);
  stream->seek(lump_texstrdata->offset, StreamSeekable::ReferenceStart);
  char* texstrdata = new char[lump_texstrdata->length];
  stream->copy(texstrdata, lump_texstrdata->length);

  // load texstrtbl
  const bsp::lump_t *lump_texstrtbl = header.lumps + bsp::lumpTexStrtbl;
  //L("Texstrtbl: %d", lump_texstrtbl->length / 4);
  stream->seek(lump_texstrtbl->offset, StreamSeekable::ReferenceStart);
  int* texstrtbl = new int[lump_texstrtbl->length / 4];
  stream->copy(texstrtbl, lump_texstrtbl->length);

  // load faces
  const bsp::lump_t *lump_faces = header.lumps + bsp::lumpFaces;
  int num_faces = lump_faces->length / sizeof(bsp::face_t);
  //L("Faces: %d", num_faces);
  stream->seek(lump_faces->offset, StreamSeekable::ReferenceStart);

  struct MapVertex {
    vec3f vertex;
    vec2f tc_lightmap;

    MapVertex() {}
    MapVertex(vec3f _vertex, vec2f _lightmap)
      : vertex(_vertex), tc_lightmap(_lightmap) {}
  };
  std::vector<MapVertex> tmp_vtx;
  std::vector<int> tmp_idx;
  std::vector<int> tmp_idx_cont;
  for (int i = 0; i < num_faces; ++i)
  {
    bsp::face_t face;
    stream->copy(&face, sizeof(bsp::face_t));

    /*L("Face %d", i);
    L("\tedges: %d->%d", face.ref_surfedge, face.ref_surfedge+face.surfedges_count);
    L("\tlightmap: %d", face.ref_lightmap_offset);
    L("\tstyles: %d %d %d %d",
      face.lightstyles[0], face.lightstyles[1],
      face.lightstyles[2], face.lightstyles[3]);
    L("\tluxels: %d x %d", face.lightmapSize[0], face.lightmapSize[1]);
    L("\tluxels (mins ??): %d x %d", face.lightmapMins[0], face.lightmapMins[1]);
    */

    //if (face.ref_lightmap_offset < 4) continue;
    //if (face.ref_texinfo < 0) continue;
    //if (texinfo[face.ref_texinfo].ref_texdata == -1) continue;

    if (texinfo[face.ref_texinfo].flags & 0x401) {
      int tex_table_index = texdata[texinfo[face.ref_texinfo].ref_texdata].nameStrtbl;
      KP_ASSERT(tex_table_index < lump_texstrtbl->length / 4);
      int tex_data_index = texstrtbl[tex_table_index];
      KP_ASSERT(tex_data_index < lump_texstrdata->length);
      //L("%d %d %08x %s", i, face.on_node, texinfo[face.ref_texinfo].flags, texstrdata + tex_data_index);
      if (strstr(texstrdata + tex_data_index, "TOOL"))
        continue;
    }

    vec2i lmap_size = vec2i(1, 1);
    static u32 white = 0xffffffff;
    const void *luxels = &white;
    if (face.ref_lightmap_offset/4 < (unsigned)lmap_luxels)
    {
      lmap_size = vec2i(face.lightmapSize[0] + 1, face.lightmapSize[1] + 1);
      luxels = &lightmap[face.ref_lightmap_offset / 4];
    }
    
    rect2f lmap_region = lmap_atlas.addImage(lmap_size, luxels);
    KP_ASSERT(lmap_region.bottom() >= 0.f);

    int index_shift = static_cast<int>(tmp_vtx.size());

    vec3f vtx = vertices[surfedges[face.ref_surfedge]];
    tmp_vtx.push_back(MapVertex(vtx, bsp::lightmapTexelAtVertex(vtx, face, texinfo, lmap_region)));
    
    vtx = vertices[surfedges[face.ref_surfedge+1]];
    tmp_vtx.push_back(MapVertex(vtx, bsp::lightmapTexelAtVertex(vtx, face, texinfo, lmap_region)));

    edge_t *e = &edges[abs(surfedges_i[face.ref_surfedge])];
    if (e->a != e->b)
    {
      tmp_idx_cont.push_back(index_shift + 0);
      tmp_idx_cont.push_back(index_shift + 1);
      e->a = e->b; // mark as drawn
    }
    
    for (int j = 2; j < face.surfedges_count; ++j)
    {
      vtx = vertices[surfedges[face.ref_surfedge + j]];
      tmp_vtx.push_back(MapVertex(vtx, bsp::lightmapTexelAtVertex(vtx, face, texinfo, lmap_region)));

      tmp_idx.push_back(index_shift + 0);
      tmp_idx.push_back(index_shift + j-1);
      tmp_idx.push_back(index_shift + j);

      // store contours
      edge_t *e = &edges[abs(surfedges_i[face.ref_surfedge + j])];
      if (e->a != e->b)
      {
        tmp_idx_cont.push_back(index_shift + j-1);
        tmp_idx_cont.push_back(index_shift + j);
        e->a = e->b;
      }
    }
  }
  //L("Total: vertices %d, indices %d", tmp_vtx.size(), tmp_idx.size());
  if (tmp_vtx.size() > 65535)
    L("WARNING: total vertices size exceeds 64k: %d", tmp_vtx.size());

  delete texstrtbl;
  delete texstrdata;
  delete texdata;
  delete texinfo;
  delete surfedges_i;
  delete edges;
  delete vertices;
  delete surfedges;
  delete lightmap;
  
  // common
  Buffer *attribs_buffer = new Buffer;
  attribs_buffer->load(&tmp_vtx[0], tmp_vtx.size() * sizeof(MapVertex));

  { // map geometry
    Buffer *index_buffer = new Buffer;
    index_buffer->load(&tmp_idx[0], tmp_idx.size() * sizeof(int));

    Batch* batch = new Batch();
    batch->setMaterial(materializer->loadMaterial("__lightmap_only"));
    batch->setAttribSource("av4_vertex", attribs_buffer, 3, 0, sizeof(MapVertex));
    batch->setAttribSource("av2_lightmap", attribs_buffer, 2, offsetof(MapVertex, tc_lightmap), sizeof(MapVertex));
    batch->setGeometry(Batch::GeometryTriangleList, 0, tmp_idx.size(), index_buffer);
    objects_.push_back(new Object(batch));

    lightmap_ = lmap_atlas.texture();
  }

  { // contours
    Buffer *index_buffer = new Buffer;
    index_buffer->load(&tmp_idx_cont[0], tmp_idx_cont.size() * sizeof(int));

    Batch* batch = new Batch();
    batch->setMaterial(materializer->loadMaterial("__white"));
    batch->setAttribSource("av4_vertex", attribs_buffer, 3, 0, sizeof(MapVertex));
    batch->setGeometry(Batch::GeometryLineList, 0, tmp_idx_cont.size(), index_buffer);
    contours_ = new Object(batch);
  }

  /*
  { // debug lightmap atlas
    const char *vtx =
      "attribute vec4 pos;\n"
      "varying vec2 p;\n"
      "void main(){\n"
        "gl_Position = pos;\n"
        "p = pos.xy * .5f + .5f;\n"
      "}";
    const char *frg =
      "uniform sampler2D tex;\n"
      "varying vec2 p;\n"
      "void main(){\n"
        "gl_FragColor = texture2D(tex, p);\n"
      "}";
    Material *mat = new Material(new Program(vtx, frg));
    mat->setTexture("tex", lightmap_);
    
    vec2f qd[] = {
      vec2f(-1, -1), vec2f(-1, 1), vec2f(1, 1), vec2f(1, -1)
    };
    Buffer *buf = new Buffer;
    buf->load(qd, sizeof qd);

    tstmp = new Batch();
    tstmp->setMaterial(mat);
    tstmp->setAttribSource("pos", buf, 2);
    tstmp->setGeometry(Batch::GeometryTriangleFan, 0, 4);
  }
  */

  return true;
}

void BSP::draw(const kapusha::Camera& cam) const
{
  for(auto it = objects_.begin(); it != objects_.end(); ++it)
  {
    (*it)->getBatch()->getMaterial()->setUniform("uv4_trans", vec4f(translation_));
    (*it)->getBatch()->getMaterial()->setTexture("us2_lightmap", lightmap_);
    (*it)->draw(cam.getView(), cam.getProjection());
  }
}

void BSP::drawContours(const kapusha::Camera& cam) const
{
  if (contours_)
  {
    contours_->getBatch()->getMaterial()->setUniform("uv4_trans", vec4f(translation_));
    contours_->draw(cam.getView(), cam.getProjection());
  }

  //tstmp->prepare();
  //tstmp->draw();
}

void BSP::setParent(const BSP* parent, vec3f relative)
{
  parent_ = parent;
  relative_ = relative;
  updateShift(shift_);
}

void BSP::updateShift(vec3f shift)
{
  shift_ = shift;
  translation_ = relative_ + shift_;
  if (parent_)
    translation_ += parent_->translation();
}
