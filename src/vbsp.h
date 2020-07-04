#ifndef VBSP_H__INCLUDED
#define VBSP_H__INCLUDED

#include <stdint.h>

#ifdef _MSC_VER
#pragma warning(disable:4214)
#endif

enum {
	VBSP_Lump_Entity = 0,
	VBSP_Lump_Plane = 1,
	VBSP_Lump_TexData = 2,
	VBSP_Lump_Vertex = 3,
	VBSP_Lump_Visibility = 4,
	VBSP_Lump_Node = 5,
	VBSP_Lump_TexInfo = 6,
	VBSP_Lump_Face = 7,
	VBSP_Lump_LightMap = 8,

	VBSP_Lump_Leaf = 10,

	VBSP_Lump_Edge = 12,
	VBSP_Lump_Surfedge = 13,
	VBSP_Lump_Model = 14,

	VBSP_Lump_LeafFace = 16,

	VBSP_Lump_DispInfo = 26,

	VBSP_Lump_DispVerts = 33,

	VBSP_Lump_PakFile = 40,

	VBSP_Lump_TexDataStringData = 43,
	VBSP_Lump_TexDataStringTable = 44,

	VBSP_Lump_LightMapHDR = 53,

	VBSP_Lump_FaceHDR = 58,

	VBSP_Lump_COUNT = 64
};

#pragma pack(1)
struct VBSPLumpHeader {
	uint32_t file_offset;
	uint32_t size;
	uint32_t version;
	char fourCC[4];
};
struct VBSPHeader {
	char ident[4];
	uint32_t version;
	struct VBSPLumpHeader lump_headers[VBSP_Lump_COUNT];
	uint32_t map_revision;
};
struct VBSPLumpPlane { float x, y, z, d; };
struct VBSPLumpTexData {
	struct { float x, y, z; } reflectivity;
	uint32_t name_string_table_id;
	uint32_t width, height;
	uint32_t view_width, view_height;
};
struct VBSPLumpVertex { float x, y, z; };
struct VBSPLumpVisibility {
	uint32_t num_clusters;
	uint32_t byte_offsets[8][2];
};
struct VBSPLumpNode {
	uint32_t plane;
	uint32_t children[2];
	int16_t min[3], max[3];
	uint16_t first_face, num_faces;
	int16_t area;
	int16_t padding_;
};
struct VBSPLumpTexInfo {
	float texture_vecs[2][4];
	float lightmap_vecs[2][4];
	uint32_t flags;
	uint32_t texdata;
};
struct VBSPLumpFace {
	uint16_t plane;
	uint8_t side, node;
	uint32_t first_edge;
	int16_t num_edges;
	int16_t texinfo;
	int16_t dispinfo;
	int16_t surface_fog_volume_id;
	uint8_t styles[4];
	uint32_t lightmap_offset;
	float area;
	int32_t lightmap_min[2];
	int32_t lightmap_size[2];
	uint32_t orig_face;
	uint16_t num_primitives;
	uint16_t first_primitive;
	uint32_t lightmap_smoothing_group;
};
struct VBSPLumpLightMap {
	uint8_t r, g, b;
	int8_t exponent;
};
struct VBSPLumpLeaf {
	uint32_t contents;
	uint16_t cluster;
	uint16_t area:9;
	uint16_t flags:7;
	int16_t min[3], max[3];
	uint16_t first_leafface, num_leaffaces;
	uint16_t first_leafbrush, num_leafbrushes;
	int16_t water_data_id;
};
struct VBSPLumpEdge {
	uint16_t v[2];
};
struct VBSPLumpModel {
	struct { float x, y, z; } min;
	struct { float x, y, z; } max;
	struct { float x, y, z; } origin;
	int32_t head_node;
	int32_t first_face, num_faces;
};
struct VBSPLumpDispInfo {
	struct { float x, y, z; } start_pos;
	int32_t vtx_start;
	int32_t tri_start;
	int32_t power;
	int32_t min_tess;
	float smoothing_angle;
	int32_t contents;
	uint16_t face;
	int32_t lightmap_alpha_start; /* not used? */
	int32_t lightmap_sample_position_start;
	/* FIXME a mistake here?
	struct {
		struct {
			uint16_t index;
			uint8_t orientation;
			uint8_t span;
			uint8_t neighbor_span;
		} sub_neighbors[2];
	} edge_neighbors[4];
	struct {
		uint16_t indices[4];
		uint8_t num;
	} corner_neighbors[4];
	*/
	uint8_t fixme_padding[90];
	uint32_t allowed_verts[10];
};
struct VBSPLumpDispVert {
	float x, y, z, dist, alpha;
};
#pragma pack()

enum VBSPSurfaceFlags {
	VBSP_Surface_Light = 0x0001,
	VBSP_Surface_Sky2D = 0x0002,
	VBSP_Surface_Sky = 0x0004,
	VBSP_Surface_NoDraw = 0x0080,
	VBSP_Surface_NoLight = 0x0400
};

#endif /* ifndef VBSP_H__INCLUDED */
