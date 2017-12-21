#include "bsp.h"
#include "atlas.h"
#include "vbsp.h"
#include "collection.h"
#include "mempools.h"
#include "vmfparser.h"
#include "common.h"

// DEBUG
#include "texture.h"

#define R2S(r) bspLoadResultString(r)

const char *bspLoadResultString(enum BSPLoadResult result) {
	switch(result) {
		case BSPLoadResult_Success: return "BSPLoadResult_Success";
		case BSPLoadResult_ErrorFileOpen: return "BSPLoadResult_ErrorFileOpen";
		case BSPLoadResult_ErrorFileFormat: return "BSPLoadResult_ErrorFileFormat";
		case BSPLoadResult_ErrorMemory: return "BSPLoadResult_ErrorMemory";
		case BSPLoadResult_ErrorTempMemory: return "BSPLoadResult_ErrorTempMemory";
		case BSPLoadResult_ErrorCapabilities: return "BSPLoadResult_ErrorCapabilities";
		default: return "UNKNOWN";
	}
}

struct AnyLump {
	const void *p;
	uint32_t n;
};

struct Lumps {
	uint32_t version;
#define LIST_LUMPS \
	BSPLUMP(Entity, char, entities); \
	BSPLUMP(Plane, struct VBSPLumpPlane, planes); \
	BSPLUMP(TexData, struct VBSPLumpTexData, texdata); \
	BSPLUMP(Vertex, struct VBSPLumpVertex, vertices); \
	\
	BSPLUMP(Node, struct VBSPLumpNode, nodes); \
	BSPLUMP(TexInfo, struct VBSPLumpTexInfo, texinfos); \
	BSPLUMP(Face, struct VBSPLumpFace, faces); \
	BSPLUMP(LightMap, struct VBSPLumpLightMap, lightmaps); \
	\
	BSPLUMP(Leaf, struct VBSPLumpLeaf, leaves); \
	\
	BSPLUMP(Edge, struct VBSPLumpEdge, edges); \
	BSPLUMP(Surfedge, int32_t, surfedges); \
	BSPLUMP(Model, struct VBSPLumpModel, models); \
	\
	BSPLUMP(LeafFace, uint16_t, leaffaces); \
	\
	BSPLUMP(DispInfo, struct VBSPLumpDispInfo, dispinfos); \
	\
	BSPLUMP(DispVerts, struct VBSPLumpDispVert, dispverts); \
	\
	BSPLUMP(PakFile, uint8_t, pakfile); \
	\
	BSPLUMP(TexDataStringData, char, texdatastringdata); \
	BSPLUMP(TexDataStringTable, int32_t, texdatastringtable); \
	\
	BSPLUMP(FaceHDR, struct VBSPLumpFace, faces_hdr); \
	\
	BSPLUMP(LightMapHDR, struct VBSPLumpLightMap, lightmaps_hdr); \


#define BSPLUMP(name,type,field) struct{const type *p;uint32_t n;} field
	LIST_LUMPS
#undef BSPLUMP
};

/* data needed for making lightmap atlas */
struct Face {
	const struct VBSPLumpFace *vface;
	/* read directly from lumps */
	int vertices;
	int indices;
	int width, height;
	const struct VBSPLumpLightMap *samples;
	const struct VBSPLumpTexInfo *texinfo;
	const struct VBSPLumpTexData *texdata;
	const struct VBSPLumpDispInfo *dispinfo;
	int dispquadvtx[4]; // filled only when displaced
	int dispstartvtx;
	const Material *material;

	/* filled as a result of atlas allocation */
	int atlas_x, atlas_y;
};

struct LoadModelContext {
	struct Stack *tmp;
	struct ICollection *collection;
	const struct Lumps *lumps;
	const struct VBSPLumpModel *model;
	struct Face *faces;
	int faces_count;
	int vertices;
	int indices;
	int max_draw_vertices;
	struct {
		int pixels;
		int max_width;
		int max_height;
		RTexture texture;
	} lightmap;
};

enum FacePreload {
	FacePreload_Ok,
	FacePreload_Skip,
	FacePreload_Inconsistent
};

static struct {
	const Material *coarse_material;
	struct {
		int color[256];
		int exponent[256];
	} lightmap_tables;
} bsp_global;

static inline int shouldSkipFace(const struct VBSPLumpFace *face, const struct Lumps *lumps) {
	(void)(face); (void)(lumps);
	//const struct VBSPLumpTexInfo *tinfo = lumps->texinfos.p + face->texinfo;
	return /*(tinfo->flags & (VBSP_Surface_NoDraw | VBSP_Surface_NoLight)) ||*/ face->lightmap_offset == 0xffffffffu
		|| face->lightmap_offset < 4;
}

static enum FacePreload bspFacePreloadMetadata(struct LoadModelContext *ctx,
		struct Face *face, unsigned index) {
	const struct Lumps * const lumps = ctx->lumps;
#define FACE_CHECK(cond) \
	if (!(cond)) { PRINTF("F%d: check failed: (%s)", index, #cond); return FacePreload_Inconsistent; }
	FACE_CHECK(index < lumps->faces.n);

	const struct VBSPLumpFace * const vface = lumps->faces.p + index;
	face->vface = vface;

	if (vface->texinfo < 0) return FacePreload_Skip;
	FACE_CHECK((unsigned)vface->texinfo < lumps->texinfos.n);
	face->texinfo = lumps->texinfos.p + vface->texinfo;

	if (shouldSkipFace(vface, lumps)) return FacePreload_Skip;
	FACE_CHECK(face->texinfo->texdata < lumps->texdata.n);
	face->texdata = lumps->texdata.p + face->texinfo->texdata;

	FACE_CHECK(face->texdata->name_string_table_id < lumps->texdatastringtable.n);
	const int32_t texdatastringdata_offset = lumps->texdatastringtable.p[face->texdata->name_string_table_id];
	FACE_CHECK(texdatastringdata_offset >= 0 && (uint32_t)texdatastringdata_offset < lumps->texdatastringdata.n);
	/* FIXME validate string: has \0 earlier than end */
	const char *texture = lumps->texdatastringdata.p + texdatastringdata_offset;
	//PRINTF("F%u: texture %s", index, face->texture);
	face->material = materialGet(texture, ctx->collection, ctx->tmp);
	if (!face->material)
		return FacePreload_Skip;

	if (vface->dispinfo >= 0) {
		FACE_CHECK((unsigned)vface->dispinfo < lumps->dispinfos.n);
		face->dispinfo = lumps->dispinfos.p + vface->dispinfo;
		const int side = (1 << face->dispinfo->power) + 1;
		FACE_CHECK(vface->num_edges == 4);
		face->vertices = side * side;
		face->indices = (side - 1) * (side - 1) * 6; /* triangle list */

		/* TODO
		 * some of the episode 2 maps have the min_tess set to flag mode, and flags are 0xe
		 *
		if (face->dispinfo->min_tess != 0) {
			if ((uint32_t)face->dispinfo->min_tess & 0x80000000u) {
				if ((uint32_t)face->dispinfo->min_tess & 0x7fffffffu)
					PRINTF("min_tess has flags: %x", (uint32_t)face->dispinfo->min_tess & 0x7fffffffu);
			} else
				PRINTF("Power: %d, min_tess: %d, vertices: %d",
					face->dispinfo->power, face->dispinfo->min_tess, face->vertices);
		}
		*/

		face->dispstartvtx = 0;
	} else {
		face->dispinfo = 0;
		face->vertices = vface->num_edges;
		face->indices = (face->vertices - 2) * 3;
	}

	/* Check for basic reference consistency */
	FACE_CHECK(vface->plane < lumps->planes.n);
	FACE_CHECK(vface->num_edges > 2);
	FACE_CHECK(vface->first_edge < lumps->surfedges.n && lumps->surfedges.n - vface->first_edge >= (unsigned)vface->num_edges);

	FACE_CHECK(vface->lightmap_offset % sizeof(struct VBSPLumpLightMap) == 0);

	const int lm_width = vface->lightmap_size[0] + 1;
	const int lm_height = vface->lightmap_size[1] + 1;
	const unsigned lightmap_size = lm_width * lm_height;
	const unsigned sample_offset = vface->lightmap_offset / sizeof(struct VBSPLumpLightMap);
	FACE_CHECK(sample_offset < lumps->lightmaps.n && lumps->lightmaps.n - sample_offset >= lightmap_size);

	const int32_t *surfedges = lumps->surfedges.p + vface->first_edge;
	unsigned int prev_end = 0xffffffffu;
	for (int i = 0; i < vface->num_edges; ++i) {
		uint32_t edge_index;
		int istart;

		if (surfedges[i] >= 0) {
			edge_index = surfedges[i];
			istart = 0;
		} else {
			edge_index = -surfedges[i];
			istart = 1;
		}

		if (edge_index >= lumps->edges.n) {
			PRINTF("Error: face%u surfedge%u/%u references edge %u > max edges %u",
					index, i, vface->num_edges, edge_index, lumps->edges.n);
			return FacePreload_Inconsistent;
		}

		const unsigned int vstart = lumps->edges.p[edge_index].v[istart];
		const unsigned int vend = lumps->edges.p[edge_index].v[1^istart];

		if (face->dispinfo) {
			face->dispquadvtx[i] = vstart;
			if (fabs(lumps->vertices.p[vstart].x - face->dispinfo->start_pos.x) < .5f
					&& fabs(lumps->vertices.p[vstart].y - face->dispinfo->start_pos.y) < .5f
					&& fabs(lumps->vertices.p[vstart].z - face->dispinfo->start_pos.z) < .5f) {
				face->dispstartvtx = i;
			}
		}

		FACE_CHECK(vstart < lumps->vertices.n);
		FACE_CHECK(prev_end == 0xffffffffu || prev_end == vstart);

		prev_end = vend;
	}

	face->width = lm_width;
	face->height = lm_height;
	face->samples = lumps->lightmaps.p + sample_offset;
	if (lm_width > ctx->lightmap.max_width) ctx->lightmap.max_width = lm_width;
	if (lm_height > ctx->lightmap.max_height) ctx->lightmap.max_height = lm_height;

	ctx->lightmap.pixels += lightmap_size;
	ctx->vertices += face->vertices;
	ctx->indices += face->indices;
	ctx->faces_count++;

	return FacePreload_Ok;
}

const int c_max_draw_vertices = 65536;

static int scaleLightmapColor(int c, int exp) {
	const int c2 =
		(bsp_global.lightmap_tables.exponent[exp+128] * bsp_global.lightmap_tables.color[c]) >> 12;

	//const int c1 = 255.f * pow(c * powf(2.f, exp) / 255.f, 1.0f / 2.2f) * .5f;
	//PRINTF("%d^%d => %d, %d => %d", c, exp, c1, c2, c2 - c1);

	return c2 < 255 ? c2 : 255;
}

static enum BSPLoadResult bspLoadModelPreloadFaces(struct LoadModelContext *ctx) {
	ctx->faces = stackGetCursor(ctx->tmp);

	int current_draw_vertices = 0;

	for (int i = 0; i < ctx->model->num_faces; ++i) {
		struct Face face;
		const enum FacePreload result = bspFacePreloadMetadata(ctx, &face, ctx->model->first_face + i);
		if (result == FacePreload_Ok) {
			current_draw_vertices += face.vertices;

			struct Face *stored_face = stackAlloc(ctx->tmp, sizeof(struct Face));
			if (!stored_face) {
				PRINTF("Error: cannot allocate %zu temp bytes", sizeof(struct Face));
				return BSPLoadResult_ErrorTempMemory;
			}
			*stored_face = face;
			continue;
		}

		if (result != FacePreload_Skip)
			return BSPLoadResult_ErrorFileFormat;
	}

	if (!ctx->faces_count) {
		PRINTF("Error: no visible faces found%s", "");
		return BSPLoadResult_ErrorFileFormat; /* FIXME handle this */
	}

	if (ctx->max_draw_vertices < current_draw_vertices)
		ctx->max_draw_vertices = current_draw_vertices;
	return BSPLoadResult_Success;
}

static enum BSPLoadResult bspLoadModelLightmaps(struct LoadModelContext *ctx) {
	/* TODO optional sort lightmaps */

	struct AtlasContext atlas_context;
	atlas_context.temp_storage.ptr = stackGetCursor(ctx->tmp);
	atlas_context.temp_storage.size = stackGetFree(ctx->tmp);
	atlas_context.width = 16; /* TODO opengl caps */
	atlas_context.height = 16;
	atlas_context.rects = (void*)(&ctx->faces[0].width);
	atlas_context.rects_count = ctx->faces_count;
	atlas_context.rects_stride = sizeof(ctx->faces[0]);
	atlas_context.pos = (void*)(&ctx->faces[0].atlas_x);
	atlas_context.pos_stride = sizeof(ctx->faces[0]);
	while (atlas_context.width < (unsigned)ctx->lightmap.max_width) atlas_context.width <<= 1;
	while (atlas_context.height < (unsigned)ctx->lightmap.max_height) atlas_context.height <<= 1;
	while (atlas_context.width * atlas_context.height < (unsigned)ctx->lightmap.pixels)
		if (atlas_context.width < atlas_context.height) atlas_context.width <<= 1; else atlas_context.height <<= 1;

	for(;;) {
		const enum AtlasResult result = atlasCompute(&atlas_context);

		PRINTF("atlas: %u %u %u", atlas_context.width, atlas_context.height, result);

		if (result == Atlas_Success)
			break;

		if (result == Atlas_ErrorInsufficientTemp)
			return BSPLoadResult_ErrorTempMemory;

		if (atlas_context.width < atlas_context.height) atlas_context.width <<= 1; else atlas_context.height <<= 1;
		if (atlas_context.width > 2048 || atlas_context.height > 2048) /* TODO limit based on GL driver caps */
			return BSPLoadResult_ErrorCapabilities;
	}

	/* Build an atlas texture based on calculated fragment positions */
	const size_t atlas_size = sizeof(uint16_t) * atlas_context.width * atlas_context.height;
	uint16_t *const pixels = stackAlloc(ctx->tmp, atlas_size);
	if (!pixels) return BSPLoadResult_ErrorTempMemory;
	memset(pixels, 0x0f, atlas_size); /* TODO debug pattern */

	for (int i = 0; i < ctx->faces_count; ++i) {
		const struct Face *const face = ctx->faces + i;
		ASSERT((unsigned)face->atlas_x + face->width <= atlas_context.width);
		ASSERT((unsigned)face->atlas_y + face->height <= atlas_context.height);
		for (int y = 0; y < face->height; ++y) {
			for (int x = 0; x < face->width; ++x) {
				const struct VBSPLumpLightMap *const pixel = face->samples + x + y * face->width;

				const unsigned int
					r = scaleLightmapColor(pixel->r, pixel->exponent),
					g = scaleLightmapColor(pixel->g, pixel->exponent),
					b = scaleLightmapColor(pixel->b, pixel->exponent);

				pixels[face->atlas_x + x + (face->atlas_y + y) * atlas_context.width]
					= ((r&0xf8) << 8) | ((g&0xfc) << 3) | (b >> 3);
			} /* for x */
		} /* for y */
	} /* fot all visible faces */

	RTextureUploadParams upload;
	upload.width = atlas_context.width;
	upload.height = atlas_context.height;
	upload.format = RTexFormat_RGB565;
	upload.pixels = pixels;
	upload.generate_mipmaps = 0;
	upload.type = RTexType_2D;
	upload.wrap = RTexWrap_Clamp;
	renderTextureInit(&ctx->lightmap.texture);
	renderTextureUpload(&ctx->lightmap.texture, upload);
	//ctx->lightmap.texture.min_filter = RTmF_Nearest;

	/* pixels buffer is not needed anymore */
	stackFreeUpToPosition(ctx->tmp, pixels);

	return BSPLoadResult_Success;
}

static inline struct AVec3f aVec3fLumpVec(struct VBSPLumpVertex v) { return aVec3f(v.x, v.y, v.z); }

static inline float clamp(float x, float min, float max) {
	return x < min ? min : (x > max ? max : x);
}

#ifdef DEBUG_DISP_LIGHTMAP
static int shouldSwapUV(struct AVec3f mapU, struct AVec3f mapV, const struct AVec3f *v) {
	float mappedU = 0.f, mappedV = 0.f;
	for (int i = 0; i < 4; ++i) {
		const float U = aVec3fDot(mapU, aVec3fSub(v[(i+1)%4], v[i]));
		if (U > mappedU) mappedU = U;
		const float V = aVec3fDot(mapV, aVec3fSub(v[(i+1)%4], v[i]));
		if (V > mappedV) mappedV = V;
	}

	const float dX1 = aVec3fLength2(aVec3fSub(v[3], v[0]));
	const float dX2 = aVec3fLength2(aVec3fSub(v[2], v[1]));
	const float dY1 = aVec3fLength2(aVec3fSub(v[1], v[0]));
	const float dY2 = aVec3fLength2(aVec3fSub(v[2], v[3]));
	const float maxDX = (dX1 > dX2) ? dX1 : dX2;
	const float maxDY = (dY1 > dY2) ? dY1 : dY2;
	//PRINTF("mappedU=%f mappedV=%f maxDX=%f, maxDY=%f", mappedU, mappedV, maxDX, maxDY);
	return (mappedU > mappedV) != (maxDX > maxDY);
}
#endif /* DEBUG_DISP_LIGHTMAP */

static void bspLoadDisplacement(
		const struct LoadModelContext *ctx,
		const struct Face *face,
		struct BSPModelVertex *out_vertices, uint16_t *out_indices, int index_shift) {
 	const int side = (1 << face->dispinfo->power) + 1;
	const struct VBSPLumpVertex *const vertices = ctx->lumps->vertices.p;
	const struct VBSPLumpTexInfo * const tinfo = face->texinfo;
	const struct VBSPLumpDispVert *const dispvert = ctx->lumps->dispverts.p + face->dispinfo->vtx_start;
	
	//if (face->dispstartvtx != 0) PRINTF("dispstartvtx = %d", face->dispstartvtx);

	const struct AVec3f vec[4] = { /* bl, tl, tr, br */
		aVec3fLumpVec(vertices[face->dispquadvtx[(face->dispstartvtx + 0)%4]]),
		aVec3fLumpVec(vertices[face->dispquadvtx[(face->dispstartvtx + 1)%4]]),
		aVec3fLumpVec(vertices[face->dispquadvtx[(face->dispstartvtx + 2)%4]]),
		aVec3fLumpVec(vertices[face->dispquadvtx[(face->dispstartvtx + 3)%4]])};

	/*
	const struct AVec3f ovec[4] = {
			aVec3fAdd(vec[0], aVec3fMulf(aVec3f(dispvert[0].x, dispvert[0].y, dispvert[0].z), dispvert[0].dist)),
			aVec3fAdd(vec[1], aVec3fMulf(aVec3f(dispvert[side*(side-1)].x, dispvert[side*(side-1)].y, dispvert[side*(side-1)].z), dispvert[side*(side-1)].dist)),
			aVec3fAdd(vec[2], aVec3fMulf(aVec3f(dispvert[side*side-1].x, dispvert[side*side-1].y, dispvert[side*side-1].z), dispvert[side*side-1].dist)),
			aVec3fAdd(vec[3], aVec3fMulf(aVec3f(dispvert[side-1].x, dispvert[side-1].y, dispvert[side-1].z), dispvert[side-1].dist))};
	*/

	const struct AVec3f lm_map_u = aVec3f(
		tinfo->lightmap_vecs[0][0], tinfo->lightmap_vecs[0][1], tinfo->lightmap_vecs[0][2]);
	const float luxels_per_unit = aVec3fLength(lm_map_u);
	float length_lm_u = luxels_per_unit * floatMax(
			aVec3fLength(aVec3fSub(vec[3], vec[0])),
			aVec3fLength(aVec3fSub(vec[2], vec[1])));
	float length_lm_v = luxels_per_unit * floatMax(
			aVec3fLength(aVec3fSub(vec[1], vec[0])),
			aVec3fLength(aVec3fSub(vec[2], vec[3])));

	const struct AVec4f tex_map_u = aVec4f(
				tinfo->texture_vecs[0][0], tinfo->texture_vecs[0][1],
				tinfo->texture_vecs[0][2], tinfo->texture_vecs[0][3]);
	const struct AVec4f tex_map_v = aVec4f(
				tinfo->texture_vecs[1][0], tinfo->texture_vecs[1][1],
				tinfo->texture_vecs[1][2], tinfo->texture_vecs[1][3]);

#ifdef DEBUG_DISP_LIGHTMAP
	const int swap = shouldSwapUV(
				aVec3f(tinfo->lightmap_vecs[0][0], tinfo->lightmap_vecs[0][1], tinfo->lightmap_vecs[0][2]),
				aVec3f(tinfo->lightmap_vecs[1][0], tinfo->lightmap_vecs[1][1], tinfo->lightmap_vecs[1][2]), vec);
#endif /*ifdef DEBUG_DISP_LIGHTMAP*/

	const struct AVec2f atlas_scale = aVec2f(1.f / ctx->lightmap.texture.width, 1.f / ctx->lightmap.texture.height);
	const struct AVec2f atlas_offset = aVec2f(
			.5f + face->atlas_x /*+ tinfo->lightmap_vecs[0][3] - face->face->lightmap_min[0]*/,
			.5f + face->atlas_y /*+ tinfo->lightmap_vecs[1][3] - face->face->lightmap_min[1]*/);

	if (length_lm_u < 0. || length_lm_u >= face->width
		|| length_lm_v < 0. || length_lm_v >= face->height) {
		PRINTF("LM OOB: (%f, %f) (%d, %d)", length_lm_u, length_lm_v, face->width, face->height);
		if (length_lm_u >= face->width) length_lm_u = (float)(face->width - 1);
		if (length_lm_v >= face->height) length_lm_v = (float)(face->height - 1);
	}

	/*
	PRINTF("%f %f %f %f",
			tinfo->lightmap_vecs[0][3] * atlas_scale.x, face->face->lightmap_min[0] * atlas_scale.x,
			tinfo->lightmap_vecs[1][3] * atlas_scale.y, face->face->lightmap_min[1] * atlas_scale.y);
	*/

	const float div_side = 1.f / (side - 1);
	for (int y = 0; y < side; ++y) {
		const float ty = (float)y * div_side;
		const struct AVec3f vl = aVec3fMix(vec[0], vec[1], ty);
		const struct AVec3f vr = aVec3fMix(vec[3], vec[2], ty);
		for (int x = 0; x < side; ++x) {
			const float tx = (float)x * div_side;
			struct BSPModelVertex * const v = out_vertices + y * side + x;
			const struct VBSPLumpDispVert * const dv = dispvert + y * side + x;

			v->vertex = aVec3fMix(vl, vr, tx);
			v->lightmap_uv = aVec2f(tx * length_lm_u, ty * length_lm_v);
			v->tex_uv = aVec2f(
				aVec4fDot(aVec4f3(v->vertex, 1.f), tex_map_u),
				aVec4fDot(aVec4f3(v->vertex, 1.f), tex_map_v));
			v->vertex = aVec3fAdd(aVec3fMix(vl, vr, tx), aVec3fMulf(aVec3f(dv->x, dv->y, dv->z), dv->dist));

			if (v->lightmap_uv.x < 0 || v->lightmap_uv.y < 0 || v->lightmap_uv.x > face->width || v->lightmap_uv.y > face->height)
				PRINTF("Error: DISP OOB LM F:V%u: x=%f y=%f z=%f tx=%f, ty=%f u=%f v=%f w=%d h=%d",
						x + y * side, v->vertex.x, v->vertex.y, v->vertex.z, tx, ty, v->lightmap_uv.x, v->lightmap_uv.y, face->width, face->height);

			v->lightmap_uv = aVec2fMul(aVec2fAdd(v->lightmap_uv, atlas_offset), atlas_scale);

#if 0
#ifdef DEBUG_DISP_LIGHTMAP
			v->normal = aVec3f(face->dispstartvtx/3.f, swap, dv->dist / 100.f);
#else
			/* FIXME normal */
			v->normal = aVec3ff(0.f);
#endif
#endif
		}
	}

	for (int y = 0; y < side - 1; ++y) {
		for (int x = 0; x < side - 1; ++x) {
			const int base = index_shift + y * side + x;
			*out_indices++ = base;
			*out_indices++ = base + side + 1;
			*out_indices++ = base + side;
			*out_indices++ = base;
			*out_indices++ = base + 1;
			*out_indices++ = base + side + 1;
		}
	}
}

static void bspLoadFace(
		const struct LoadModelContext *ctx,
		const struct Face *face,
		struct BSPModelVertex *out_vertices, uint16_t *out_indices, int index_shift) {
	const struct VBSPLumpFace *vface = face->vface;
	const struct VBSPLumpTexInfo * const tinfo = face->texinfo;
	struct AVec3f normal;
	normal.x = ctx->lumps->planes.p[vface->plane].x;
	normal.y = ctx->lumps->planes.p[vface->plane].y;
	normal.z = ctx->lumps->planes.p[vface->plane].z;
	if (vface->side) normal = aVec3fNeg(normal);

	const struct AVec4f lm_map_u = aVec4f(
				tinfo->lightmap_vecs[0][0], tinfo->lightmap_vecs[0][1],
				tinfo->lightmap_vecs[0][2], tinfo->lightmap_vecs[0][3] - vface->lightmap_min[0]);
	const struct AVec4f lm_map_v = aVec4f(
				tinfo->lightmap_vecs[1][0], tinfo->lightmap_vecs[1][1],
				tinfo->lightmap_vecs[1][2], tinfo->lightmap_vecs[1][3] - vface->lightmap_min[1]);

	const struct AVec4f tex_map_u = aVec4f(
				tinfo->texture_vecs[0][0], tinfo->texture_vecs[0][1],
				tinfo->texture_vecs[0][2], tinfo->texture_vecs[0][3]);
	const struct AVec4f tex_map_v = aVec4f(
				tinfo->texture_vecs[1][0], tinfo->texture_vecs[1][1],
				tinfo->texture_vecs[1][2], tinfo->texture_vecs[1][3]);

	const int32_t * const surfedges = ctx->lumps->surfedges.p + vface->first_edge;
	for (int iedge = 0; iedge < vface->num_edges; ++iedge) {
		const uint16_t vstart = (surfedges[iedge] >= 0)
			? ctx->lumps->edges.p[surfedges[iedge]].v[0]
			: ctx->lumps->edges.p[-surfedges[iedge]].v[1];

		const struct VBSPLumpVertex * const lv = ctx->lumps->vertices.p + vstart;
		struct BSPModelVertex * const vertex = out_vertices + iedge;

		vertex->vertex = aVec3f(lv->x, lv->y, lv->z);
		//vertex->normal = normal;
		vertex->lightmap_uv = aVec2f(
			aVec4fDot(aVec4f3(vertex->vertex, 1.f),	lm_map_u),
			aVec4fDot(aVec4f3(vertex->vertex, 1.f), lm_map_v));
		vertex->tex_uv = aVec2f(
			aVec4fDot(aVec4f3(vertex->vertex, 1.f), tex_map_u),
			aVec4fDot(aVec4f3(vertex->vertex, 1.f), tex_map_v));

		vertex->lightmap_uv.x = clamp(vertex->lightmap_uv.x, 0.f, face->width);
		vertex->lightmap_uv.y = clamp(vertex->lightmap_uv.y, 0.f, face->height);

		/*
		if (vertex->lightmap_uv.x < 0 || vertex->lightmap_uv.y < 0 || vertex->lightmap_uv.x > face->width || vertex->lightmap_uv.y > face->height)
			PRINTF("Error: OOB LM F:V%u: x=%f y=%f z=%f u=%f v=%f w=%d h=%d", iedge, lv->x, lv->y, lv->z, vertex->lightmap_uv.x, vertex->lightmap_uv.y, face->width, face->height);
		*/

		vertex->lightmap_uv.x = (vertex->lightmap_uv.x + face->atlas_x + .5f) / ctx->lightmap.texture.width;
		vertex->lightmap_uv.y = (vertex->lightmap_uv.y + face->atlas_y + .5f) / ctx->lightmap.texture.height;

		if (iedge > 1) {
			out_indices[(iedge-2)*3+0] = index_shift + 0;
			out_indices[(iedge-2)*3+1] = index_shift + iedge;
			out_indices[(iedge-2)*3+2] = index_shift + iedge - 1;
		}
	}
}

static int faceMaterialCompare(const void *a, const void *b) {
	const struct Face *fa = a, *fb = b;

	if (fa->material == fb->material)
		return 0;

	return fa->material->base_texture.texture - fb->material->base_texture.texture;
}

static enum BSPLoadResult bspLoadModelDraws(const struct LoadModelContext *ctx, struct Stack *persistent,
		struct BSPModel *model) {
	void * const tmp_cursor = stackGetCursor(ctx->tmp);

	struct BSPModelVertex * const vertices_buffer
		= stackAlloc(ctx->tmp, sizeof(struct BSPModelVertex) * ctx->max_draw_vertices);
	if (!vertices_buffer) return BSPLoadResult_ErrorTempMemory;

	/* each vertex after second in a vface is a new triangle */
	uint16_t * const indices_buffer = stackAlloc(ctx->tmp, sizeof(uint16_t) * ctx->indices);
	if (!indices_buffer) return BSPLoadResult_ErrorTempMemory;

	qsort(ctx->faces, ctx->faces_count, sizeof(*ctx->faces), faceMaterialCompare);

	{
		int vbo_offset = 0, vertex_pos = 0;
		model->detailed.draws_count = 1;
		model->coarse.draws_count = 1;
		for (int iface = 0; iface < ctx->faces_count; ++iface) {
			const struct Face *face = ctx->faces + iface;

			const int update_vbo_offset = (vertex_pos - vbo_offset) + face->vertices >= c_max_draw_vertices;
			if (update_vbo_offset || (iface > 0 && faceMaterialCompare(ctx->faces+iface-1,face) != 0)) {
				//PRINTF("%p -> %p", (void*)ctx->faces[iface-1].material->base_texture[0], (void*)face->material->base_texture[0]);
				++model->detailed.draws_count;
			}

			if (update_vbo_offset) {
				vbo_offset = vertex_pos;
				++model->coarse.draws_count;
			}

			vertex_pos += face->vertices;
		}
	}

	PRINTF("Faces: %d -> %d detailed draws", ctx->faces_count, model->detailed.draws_count);

	model->detailed.draws = stackAlloc(persistent, sizeof(struct BSPDraw) * model->detailed.draws_count);
	model->coarse.draws = stackAlloc(persistent, sizeof(struct BSPDraw) * model->coarse.draws_count);

	int vertex_pos = 0;
	int draw_indices_start = 0, indices_pos = 0;
	int vbo_offset = 0;
	int idraw = 0;
	struct BSPDraw *detailed_draw = model->detailed.draws - 1,
								 *coarse_draw = model->coarse.draws - 1;

	for (int iface = 0; iface < ctx->faces_count/* + 1*/; ++iface) {
		const struct Face *face = ctx->faces + iface;

		const int update_vbo_offset = (vertex_pos - vbo_offset) + face->vertices >= c_max_draw_vertices;

		if (update_vbo_offset) {
			PRINTF("vbo_offset %d -> %d", vbo_offset, vertex_pos);
			vbo_offset = vertex_pos;
		}

		if (update_vbo_offset || iface == 0 || faceMaterialCompare(ctx->faces+iface-1,face) != 0) {
			++detailed_draw;
			detailed_draw->start = draw_indices_start;
			detailed_draw->count = 0;
			detailed_draw->vbo_offset = vbo_offset;
			detailed_draw->material = face->material;

			++idraw;
			ASSERT(idraw <= model->detailed.draws_count);
		}

		if (update_vbo_offset || iface == 0) {
			++coarse_draw;
			coarse_draw->start = draw_indices_start;
			coarse_draw->count = 0;
			coarse_draw->vbo_offset = vbo_offset;
			coarse_draw->material = bsp_global.coarse_material;
		}

		if (face->dispinfo) {
			bspLoadDisplacement(ctx, face, vertices_buffer + vertex_pos, indices_buffer + indices_pos, vertex_pos - vbo_offset);
		} else {
			bspLoadFace(ctx, face, vertices_buffer + vertex_pos, indices_buffer + indices_pos, vertex_pos - vbo_offset);
		}

		for (int i = 0; i < face->vertices; ++i) {
			vertices_buffer[vertex_pos + i].average_color.r =
				(uint8_t)(face->material->average_color.x * 255.f);
			vertices_buffer[vertex_pos + i].average_color.g =
				(uint8_t)(face->material->average_color.y * 255.f);
			vertices_buffer[vertex_pos + i].average_color.b =
				(uint8_t)(face->material->average_color.z * 255.f);
		}

		vertex_pos += face->vertices;
		indices_pos += face->indices;

		detailed_draw->count += indices_pos - draw_indices_start;
		coarse_draw->count += indices_pos - draw_indices_start;

		//vertex_pos = 0;
		draw_indices_start = indices_pos;
	}
	ASSERT(idraw == model->detailed.draws_count);

	renderBufferCreate(&model->ibo, RBufferType_Index, sizeof(uint16_t) * ctx->indices, indices_buffer);
	renderBufferCreate(&model->vbo, RBufferType_Vertex, sizeof(struct BSPModelVertex) * vertex_pos, vertices_buffer);

	stackFreeUpToPosition(ctx->tmp, tmp_cursor);
	return BSPLoadResult_Success;
}

static enum BSPLoadResult bspLoadModel(
		struct ICollection *collection, struct BSPModel *model, struct Stack *persistent, struct Stack *temp,
		const struct Lumps *lumps, unsigned index) {
	struct LoadModelContext context;
	memset(&context, 0, sizeof context);

	ASSERT(index < lumps->models.n);

	context.tmp = temp;
	context.collection = collection;
	context.lumps = lumps;
	context.model = lumps->models.p + index;

	/* Step 1. Collect lightmaps for all faces */
	enum BSPLoadResult result = bspLoadModelPreloadFaces(&context);
	if (result != BSPLoadResult_Success) {
		PRINTF("Error: bspLoadModelPreloadFaces() => %s", R2S(result));
		return result;
	}

	/* Step 2. Build an atlas of all lightmaps */
	result = bspLoadModelLightmaps(&context);
	if (result != BSPLoadResult_Success) {
		PRINTF("Error: bspLoadModelLightmaps() => %s", R2S(result));
		return result;
	}

	/* Step 3. Generate draw operations data */
	result = bspLoadModelDraws(&context, persistent, model);
	if (result != BSPLoadResult_Success) {
		//aGLTextureDestroy(&context.lightmap.texture);
		return result;
	}

	model->lightmap = context.lightmap.texture;
	model->aabb.min.x = context.model->min.x;
	model->aabb.min.y = context.model->min.y;
	model->aabb.min.z = context.model->min.z;
	model->aabb.max.x = context.model->max.x;
	model->aabb.max.y = context.model->max.y;
	model->aabb.max.z = context.model->max.z;

	return BSPLoadResult_Success;
} // bspLoadModel()

static const char *bsp_skybox_suffix[6] = {
	"rt", "lf", "ft", "bk", "up", "dn" };

static void bspLoadSkybox(StringView name, ICollection *coll, Stack *tmp, struct BSPModel *model) {
	PRINTF("Loading skybox %.*s", name.length, name.str);

	char *zname = alloca(name.length + 3 + 7);
	memset(zname, 0, name.length + 3 + 7);
	memcpy(zname, "skybox/", 7);
	memcpy(zname + 7, name.str, name.length);

	for (int i = 0; i < 6; ++i) {
		memcpy(zname + name.length + 7, bsp_skybox_suffix[i], 2);
		model->skybox[i] = materialGet(zname, coll, tmp);
	}
}

typedef struct {
	const char *name;
	StringView value;
} EntityProp;

#define ENTITY_LIST_PROPS \
	ENTITY_PROP(ClassName, classname) \
	ENTITY_PROP(TargetName, targetname) \
	ENTITY_PROP(Origin, origin) \
	ENTITY_PROP(SkyName, skyname) \
	ENTITY_PROP(Landmark, landmark) \
	ENTITY_PROP(Map, map) \

typedef enum {
#define ENTITY_PROP(name, string) \
	EntityPropIndex_##name,
ENTITY_LIST_PROPS
#undef ENTITY_PROP
} EntityPropIndex;

typedef struct {
	BSPLoadModelContext *ctx;
	EntityProp *props;
	int props_count;
} Entity;

typedef BSPLoadResult (*BspProcessEntityProc)(BSPLoadModelContext *ctx, const Entity *entity);

BSPLoadResult bspReadAndAddLandmark(BSPLoadModelContext *ctx, StringView target_name, StringView origin) {

	struct BSPModel *model = ctx->model;
	if (model->landmarks_count == BSP_MAX_LANDMARKS) {
		PRINT("Too many landmarks");
		return BSPLoadResult_ErrorMemory;
	}

	struct BSPLandmark *landmark = model->landmarks + model->landmarks_count;
	if (target_name.length >= (int)sizeof(landmark->name)) {
		PRINTF("Landmark name \"%.*s\" is too long",
			PRI_SVV(target_name));
		return BSPLoadResult_ErrorMemory;
	}

	memcpy(landmark->name, target_name.str, target_name.length);
	landmark->name[target_name.length] = '\0';

	// FIXME props[EntityPropIndex_Origin].value is not null-terminated suman
	if (3 != sscanf(origin.str, "%f %f %f",
			&landmark->origin.x,
			&landmark->origin.y,
			&landmark->origin.z))
	{
		PRINTF("Cannot read x, y, z from origin=\"%.*s\"", PRI_SVV(origin));
		return BSPLoadResult_ErrorFileFormat;
	}

	++model->landmarks_count;

	return BSPLoadResult_Success;
}

static BSPLoadResult bspProcessEntityInfoLandmark(BSPLoadModelContext *ctx, const Entity *entity) {
	const StringView target_name = entity->props[EntityPropIndex_TargetName].value;
	const StringView origin = entity->props[EntityPropIndex_Origin].value;

	return bspReadAndAddLandmark(ctx, target_name, origin);
}

static BSPLoadResult bspProcessEntityInfoLandmarkEntry(BSPLoadModelContext *ctx, const Entity *entity) {
	const int landmark_name_length = ctx->prev_map_name.length + ctx->name.length + 5;
	char *landmark_name_buf = stackAlloc(ctx->tmp, landmark_name_length);
	memcpy(landmark_name_buf, ctx->prev_map_name.str, ctx->prev_map_name.length);
	memcpy(landmark_name_buf + ctx->prev_map_name.length, "_to_", 4);
	memcpy(landmark_name_buf + ctx->prev_map_name.length + 4, ctx->name.str, ctx->name.length);
	landmark_name_buf[landmark_name_length-1] = '\0';

	const StringView target_name = { .str = landmark_name_buf, .length = landmark_name_length };
	const StringView origin = entity->props[EntityPropIndex_Origin].value;

	const BSPLoadResult result = bspReadAndAddLandmark(ctx, target_name, origin);
	stackFreeUpToPosition(ctx->tmp, landmark_name_buf);

	return result;
}

static BSPLoadResult bspProcessEntityInfoLandmarkExit(BSPLoadModelContext *ctx, const Entity *entity) {
	const int landmark_name_length = ctx->next_map_name.length + ctx->name.length + 5;
	char *landmark_name_buf = stackAlloc(ctx->tmp, landmark_name_length);
	memcpy(landmark_name_buf, ctx->name.str, ctx->name.length);
	memcpy(landmark_name_buf + ctx->name.length, "_to_", 4);
	memcpy(landmark_name_buf + ctx->name.length + 4, ctx->next_map_name.str, ctx->next_map_name.length);
	landmark_name_buf[landmark_name_length-1] = '\0';

	const StringView target_name = { .str = landmark_name_buf, .length = landmark_name_length };
	const StringView origin = entity->props[EntityPropIndex_Origin].value;

	const BSPLoadResult result = bspReadAndAddLandmark(ctx, target_name, origin);
	stackFreeUpToPosition(ctx->tmp, landmark_name_buf);

	return result;
}

static BSPLoadResult bspProcessEntityTriggerChangelevel(struct BSPLoadModelContext *ctx, const Entity *entity) {
	(void)ctx;
	openSourceAddMap(entity->props[EntityPropIndex_Map].value);
	return BSPLoadResult_Success;
}

static BSPLoadResult bspProcessEntityWorldspawn(struct BSPLoadModelContext *ctx, const Entity *entity) {
	(void)ctx;
	const StringView skyname = entity->props[EntityPropIndex_SkyName].value;

	if (skyname.length > 0) {
		const StringView sky = { skyname.str, skyname.length };
		bspLoadSkybox(sky, ctx->collection, ctx->tmp, ctx->model);
	}

	return BSPLoadResult_Success;
}

static struct {
	const char *classname;
	BspProcessEntityProc proc;
} entity_procs[] = {
	{"info_landmark", bspProcessEntityInfoLandmark},
	{"info_landmark_entry", bspProcessEntityInfoLandmarkEntry},
	{"info_landmark_exit", bspProcessEntityInfoLandmarkExit},
	{"trigger_changelevel", bspProcessEntityTriggerChangelevel},
	{"worldspawn", bspProcessEntityWorldspawn},
};

static VMFAction bspReadEntityProps(VMFState *state, VMFEntryType entry, const VMFKeyValue *kv) {
	Entity *entity = state->user_data;

	switch (entry) {
		case VMFEntryType_KeyValue:
			for (int i = 0; i < entity->props_count; ++i) {
				if (strncmp(entity->props[i].name, kv->key.str, kv->key.length) == 0) {
					entity->props[i].value = kv->value;
					break;
				}
			}
			break;
		case VMFEntryType_SectionOpen:
			for (int i = 0; i < entity->props_count; ++i) {
				entity->props[i].value.str = NULL;
				entity->props[i].value.length = 0;
			}

			break;
		case VMFEntryType_SectionClose:
			for (int i = 0; i < (int)COUNTOF(entity_procs); ++i) {
				const StringView classname = entity->props[EntityPropIndex_ClassName].value;
				if (strncmp(entity_procs[i].classname, classname.str, classname.length) == 0) {
					entity_procs[i].proc(entity->ctx, entity);
					break;
				}
			}
			break;
	}

	return VMFAction_Continue;
}

BSPLoadResult bspReadEntities(BSPLoadModelContext *ctx, const char *str, int length) {
	ctx->model->landmarks_count = 0;

	EntityProp props[] = {
#define ENTITY_PROP(name, string) \
		{#string, {NULL, 0}},
ENTITY_LIST_PROPS
#undef ENTITY_PROP
	};

	Entity entity = {
		.ctx = ctx,
		.props = props,
		.props_count = COUNTOF(props),
	};

	VMFState parser_state = {
		.user_data = &entity,
		.data = { .str = str, .length = length },
		.callback = bspReadEntityProps,
	};

	return VMFResult_Success == vmfParse(&parser_state)
		? BSPLoadResult_Success : BSPLoadResult_ErrorFileFormat;
}

static int lumpRead(const char *name, const struct VBSPLumpHeader *header,
		struct IFile *file, struct Stack *tmp,
		struct AnyLump *out_ptr, uint32_t item_size) {
	out_ptr->p = stackAlloc(tmp, header->size);
	if (!out_ptr->p) {
		PRINTF("Not enough temp memory to allocate storage for lump %s; need: %d (%x)", name, header->size, header->size);
		return -1;
	}

	const size_t bytes = file->read(file, header->file_offset, header->size, (void*)out_ptr->p);
	if (bytes != header->size) {
		PRINTF("Cannot read full lump %s, read only %zu bytes out of %u", name, bytes, header->size);
		return -1;
	}

	PRINTF("Read lump %s, offset %u, size %u bytes / %u item = %u elements",
			name, header->file_offset, header->size, item_size, header->size / item_size);

	out_ptr->n = header->size / item_size;
	return 1;
}

enum BSPLoadResult bspLoadWorldspawn(BSPLoadModelContext context) {
	enum BSPLoadResult result = BSPLoadResult_Success;
	struct IFile *file = 0;
	if (CollectionOpen_Success !=
			collectionChainOpen(context.collection, context.name.str /* FIXME assumes null-terminated string */, File_Map, &file)) {
		return BSPLoadResult_ErrorFileOpen;
	}

	void *tmp_cursor = stackGetCursor(context.tmp);
	struct ICollection *pakfile = NULL;

	struct VBSPHeader vbsp_header;
	size_t bytes = file->read(file, 0, sizeof vbsp_header, &vbsp_header);
	if (bytes < sizeof(vbsp_header)) {
		PRINTF("Size is too small: %zu <= %zu", bytes, sizeof(struct VBSPHeader));
		result = BSPLoadResult_ErrorFileFormat;
		goto exit;
	}

	if (vbsp_header.ident[0] != 'V' || vbsp_header.ident[1] != 'B' ||
			vbsp_header.ident[2] != 'S' || vbsp_header.ident[3] != 'P') {
		PRINTF("Error: invalid ident => %c%c%c%c != VBSP",
				vbsp_header.ident[0], vbsp_header.ident[1], vbsp_header.ident[2], vbsp_header.ident[3]);
		result = BSPLoadResult_ErrorFileFormat;
		goto exit;
	}

	if (vbsp_header.version < 19 && vbsp_header.version > 21) {
		PRINTF("Error: invalid version: %d != 19 or 20 or 21", vbsp_header.version);
		result = BSPLoadResult_ErrorFileFormat;
		goto exit;
	}

	PRINTF("VBSP version %u opened", vbsp_header.version);

	struct Lumps lumps;
	lumps.version = vbsp_header.version;
#define BSPLUMP(name, type, field) \
	if (1 != lumpRead(#name, vbsp_header.lump_headers + VBSP_Lump_##name, file, context.tmp, \
			(struct AnyLump*)&lumps.field, sizeof(type))) { \
		result = BSPLoadResult_ErrorFileFormat; \
		goto exit; \
	}
	LIST_LUMPS
#undef BSPLUMP

	if (lumps.lightmaps.n == 0) {
		memcpy(&lumps.lightmaps, &lumps.lightmaps_hdr, sizeof(lumps.lightmaps));
		memcpy(&lumps.faces, &lumps.faces_hdr, sizeof(lumps.faces));
	}

	if (lumps.pakfile.n > 0) {
		struct Memories memories = { context.tmp, context.tmp };
		pakfile = collectionCreatePakfile(&memories, lumps.pakfile.p, lumps.pakfile.n);
		if (pakfile)
			pakfile->next = context.collection;
	}

	result = bspLoadModel(pakfile ? pakfile : context.collection, context.model, context.persistent, context.tmp, &lumps, 0);
	if (result != BSPLoadResult_Success) {
		PRINTF("Error: bspLoadModel() => %s", R2S(result));
		goto exit;
	}

	result = bspReadEntities(&context, lumps.entities.p, lumps.entities.n);
	if (result != BSPLoadResult_Success)
		PRINTF("Error: bspReadEntities() => %s", R2S(result));

exit:
	if (pakfile)
		pakfile->close(pakfile);

	stackFreeUpToPosition(context.tmp, tmp_cursor);
	if (file) file->close(file);
	return result;
}

void bspInit() {
	bsp_global.coarse_material = materialGet("opensource/coarse", NULL, NULL);

	const int scaling_factor = 4096;
	for (int i = 0; i < 256; ++i) {
		const int exp = i - 128;
		bsp_global.lightmap_tables.exponent[i] = (exp < -15 || exp > 15) ? 0 :
			(int)((float)scaling_factor * powf(2.f, (float)exp / 2.2f - 1.f));

		bsp_global.lightmap_tables.color[i] =
			(int)(255.f * powf((float)i / 255.f, 1.f / 2.2f));
	}
}
