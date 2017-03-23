#include "bsp.h"
#include "atlas.h"
#include "vbsp.h"
#include "collection.h"
#include "mempools.h"
#include <stdint.h>
#include <string.h> /* memset */
#include <stdio.h> /* printf */

#define STR1(m) #m
#define STR(m) STR1(m)
#define PRINT(fmt, ...) fprintf(stderr, __FILE__ ":" STR(__LINE__) ": " fmt "\n", __VA_ARGS__)
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
	BSPLUMP(TexDataStringTable, int32_t, texdatastringtable);

#define BSPLUMP(name,type,field) struct{const type *p;uint32_t n;} field
	LIST_LUMPS
#undef BSPLUMP
};

/* data needed for making lightmap atlas */
struct VisibleFace {
	const struct VBSPLumpFace *face;
	/* read directly from lumps */
	unsigned int vertices;
	int indices;
	unsigned int width, height;
	const struct VBSPLumpLightMap *samples;
	const struct VBSPLumpTexInfo *texinfo;
	const struct VBSPLumpTexData *texdata;
	const struct VBSPLumpDispInfo *dispinfo;
	int dispquadvtx[4]; // filled only when displaced
	int dispstartvtx;
	const char *texture;

	/* filled as a result of atlas allocation */
	unsigned int atlas_x, atlas_y;
};

struct LoadModelContext {
	struct TemporaryPool *tmp;
	const struct Lumps *lumps;
	const struct VBSPLumpModel *model;
	struct VisibleFace *faces;
	unsigned int faces_count;
	unsigned int vertices;
	int indices;
	unsigned int max_draw_vertices;
	struct {
		unsigned int pixels;
		unsigned int max_width;
		unsigned int max_height;
		AGLTexture texture;
	} lightmap;
	unsigned int draws_to_alloc;
};

/* TODO change this to Ok|Skip|Inconsistent,
 * print verbose errors for inconsistent */
enum FaceProbe {
	FaceProbe_Ok,
	FaceProbe_Skip,
	FaceProbe_Inconsistent
};

static inline int shouldSkipFace(const struct VBSPLumpFace *face, const struct Lumps *lumps) {
	(void)(face); (void)(lumps);
	//const struct VBSPLumpTexInfo *tinfo = lumps->texinfos.p + face->texinfo;
	return /*(tinfo->flags & (VBSP_Surface_NoDraw | VBSP_Surface_NoLight)) ||*/ face->lightmap_offset == 0xffffffffu
		|| face->lightmap_offset < 4;
}

static enum FaceProbe bspFaceProbe(struct LoadModelContext *ctx,
		struct VisibleFace *vis_face, unsigned index) {
	const struct Lumps * const lumps = ctx->lumps;
#define FACE_CHECK(cond) \
	if (!(cond)) { PRINT("F%d: check failed: (%s)", index, #cond); return FaceProbe_Inconsistent; }
	FACE_CHECK(index < lumps->faces.n);

	const struct VBSPLumpFace * const face = lumps->faces.p + index;
	vis_face->face = face;

	if (face->texinfo < 0) return FaceProbe_Skip;
	FACE_CHECK((unsigned)face->texinfo < lumps->texinfos.n);
	vis_face->texinfo = lumps->texinfos.p + face->texinfo;

	if (shouldSkipFace(face, lumps)) return FaceProbe_Skip;
	FACE_CHECK(vis_face->texinfo->texdata < lumps->texdata.n);
	vis_face->texdata = lumps->texdata.p + vis_face->texinfo->texdata;

	FACE_CHECK(vis_face->texdata->name_string_table_id < lumps->texdatastringtable.n);
	const int32_t texdatastringdata_offset = lumps->texdatastringtable.p[vis_face->texdata->name_string_table_id];
	FACE_CHECK(texdatastringdata_offset >= 0 && (uint32_t)texdatastringdata_offset < lumps->texdatastringdata.n);
	/* FIXME validate string: has \0 earlier than end */
	vis_face->texture = lumps->texdatastringdata.p + texdatastringdata_offset;
	//PRINT("F%u: texture %s", index, vis_face->texture);

	if (face->dispinfo >= 0) {
		FACE_CHECK((unsigned)face->dispinfo < lumps->dispinfos.n);
		vis_face->dispinfo = lumps->dispinfos.p + face->dispinfo;
		const int side = (1 << vis_face->dispinfo->power) + 1;
		FACE_CHECK(face->num_edges == 4);
		vis_face->vertices = side * side;
		vis_face->indices = (side - 1) * (side - 1) * 6; /* triangle list */
		if (vis_face->dispinfo->min_tess != 0)
			PRINT("Power: %d, min_tess: %d, vertices: %d",
				vis_face->dispinfo->power, vis_face->dispinfo->min_tess, vis_face->vertices);
		vis_face->dispstartvtx = 0;
	} else {
		vis_face->dispinfo = 0;
		vis_face->vertices = face->num_edges;
		vis_face->indices = (vis_face->vertices - 2) * 3;
	}

	/* Check for basic reference consistency */
	FACE_CHECK(face->plane < lumps->planes.n);
	FACE_CHECK(face->num_edges > 2);
	FACE_CHECK(face->first_edge < lumps->surfedges.n && lumps->surfedges.n - face->first_edge >= (unsigned)face->num_edges);

	FACE_CHECK(face->lightmap_offset % sizeof(struct VBSPLumpLightMap) == 0);

	const unsigned int lm_width = face->lightmap_size[0] + 1;
	const unsigned int lm_height = face->lightmap_size[1] + 1;
	const size_t lightmap_size = lm_width * lm_height;
	const size_t sample_offset = face->lightmap_offset / sizeof(struct VBSPLumpLightMap);
	FACE_CHECK(sample_offset < lumps->lightmaps.n && lumps->lightmaps.n - sample_offset >= lightmap_size);

	const int32_t *surfedges = lumps->surfedges.p + face->first_edge;
	unsigned int prev_end = 0xffffffffu;
	for (int i = 0; i < face->num_edges; ++i) {
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
			PRINT("Error: face%u surfedge%u/%u references edge %u > max edges %u",
					index, i, face->num_edges, edge_index, lumps->edges.n);
			return FaceProbe_Inconsistent;
		}

		const unsigned int vstart = lumps->edges.p[edge_index].v[istart];
		const unsigned int vend = lumps->edges.p[edge_index].v[1^istart];

		if (vis_face->dispinfo) {
			vis_face->dispquadvtx[i] = vstart;
			if (fabs(lumps->vertices.p[vstart].x - vis_face->dispinfo->start_pos.x) < .5f
					&& fabs(lumps->vertices.p[vstart].y - vis_face->dispinfo->start_pos.y) < .5f
					&& fabs(lumps->vertices.p[vstart].z - vis_face->dispinfo->start_pos.z) < .5f) {
				vis_face->dispstartvtx = i;
			}
		}

		FACE_CHECK(vstart < lumps->vertices.n);
		FACE_CHECK(prev_end == 0xffffffffu || prev_end == vstart);

		prev_end = vend;
	}

	vis_face->width = lm_width;
	vis_face->height = lm_height;
	vis_face->samples = lumps->lightmaps.p + sample_offset;
	if (lm_width > ctx->lightmap.max_width) ctx->lightmap.max_width = lm_width;
	if (lm_height > ctx->lightmap.max_height) ctx->lightmap.max_height = lm_height;

	ctx->lightmap.pixels += lightmap_size;
	ctx->vertices += vis_face->vertices;
	ctx->indices += vis_face->indices;
	ctx->faces_count++;

	return FaceProbe_Ok;
}

const unsigned int c_max_draw_vertices = 65536;

static enum BSPLoadResult bspLoadModelCollectFaces(struct LoadModelContext *ctx) {
	ctx->faces = tmpGetCursor(ctx->tmp);

	unsigned int current_draw_vertices = 0;

	for (int i = 0; i < ctx->model->num_faces; ++i) {
		struct VisibleFace face;
		const enum FaceProbe result = bspFaceProbe(ctx, &face, ctx->model->first_face + i);
		if (result == FaceProbe_Ok) {
			if (current_draw_vertices + face.vertices > c_max_draw_vertices) {
				if (ctx->max_draw_vertices < current_draw_vertices)
					ctx->max_draw_vertices = current_draw_vertices;
				++ctx->draws_to_alloc;
				current_draw_vertices = 0;
			}
			current_draw_vertices += face.vertices;

			struct VisibleFace *stored_face = tmpAdvance(ctx->tmp, sizeof(struct VisibleFace));
			if (!stored_face) {
				PRINT("Error: cannot allocate %zu temp bytes", sizeof(struct VisibleFace));
				return BSPLoadResult_ErrorTempMemory;
			}
			*stored_face = face;
			continue;
		}

		if (result != FaceProbe_Skip) {
			return BSPLoadResult_ErrorFileFormat;
		}
	}

	if (!ctx->faces_count) {
		PRINT("Error: no visible faces found%s", "");
		return BSPLoadResult_ErrorFileFormat; /* FIXME handle this */
	}

	if (ctx->max_draw_vertices < current_draw_vertices)
		ctx->max_draw_vertices = current_draw_vertices;
	++ctx->draws_to_alloc;
	return BSPLoadResult_Success;
}

static enum BSPLoadResult bspLoadModelLightmaps(struct LoadModelContext *ctx) {
	/* TODO optional sort lightmaps */

	struct AtlasContext atlas_context;
	atlas_context.temp_storage.ptr = tmpGetCursor(ctx->tmp);
	atlas_context.temp_storage.size = tmpGetLeft(ctx->tmp);
	atlas_context.width = 16; /* TODO opengl caps */
	atlas_context.height = 16;
	atlas_context.rects = (void*)(&ctx->faces[0].width);
	atlas_context.rects_count = ctx->faces_count;
	atlas_context.rects_stride = sizeof(ctx->faces[0]);
	atlas_context.pos = (void*)(&ctx->faces[0].atlas_x);
	atlas_context.pos_stride = sizeof(ctx->faces[0]);
	while (atlas_context.width < ctx->lightmap.max_width) atlas_context.width <<= 1;
	while (atlas_context.height < ctx->lightmap.max_height) atlas_context.height <<= 1;
	while (atlas_context.width * atlas_context.height < ctx->lightmap.pixels)
		if (atlas_context.width < atlas_context.height) atlas_context.width <<= 1; else atlas_context.height <<= 1;

	for(;;) {
		const enum AtlasResult result = atlasCompute(&atlas_context);

		PRINT("atlas: %u %u %u", atlas_context.width, atlas_context.height, result);

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
	uint16_t *const pixels = tmpAdvance(ctx->tmp, atlas_size);
	if (!pixels) return BSPLoadResult_ErrorTempMemory;
	memset(pixels, 0x0f, atlas_size); /* TODO debug pattern */

	for (unsigned int i = 0; i < ctx->faces_count; ++i) {
		const struct VisibleFace *const face = ctx->faces + i;
		ASSERT(face->atlas_x + face->width <= atlas_context.width);
		ASSERT(face->atlas_y + face->height <= atlas_context.height);
		for (unsigned int y = 0; y < face->height; ++y) {
			for (unsigned int x = 0; x < face->width; ++x) {
				const struct VBSPLumpLightMap *const pixel = face->samples + x + y * face->width;

				unsigned int
					r = pixel->r,
					g = pixel->g,
					b = pixel->b;

				if (pixel->exponent >= 0) {
					r <<= pixel->exponent;
					g <<= pixel->exponent;
					b <<= pixel->exponent;
				} else {
					r >>= -pixel->exponent;
					g >>= -pixel->exponent;
					b >>= -pixel->exponent;
				}

				(r > 255) ? r = 255 : 0;
				(g > 255) ? g = 255 : 0;
				(b > 255) ? b = 255 : 0;

				pixels[face->atlas_x + x + (face->atlas_y + y) * atlas_context.width]
					= ((r&0xf8) << 8) | ((g&0xfc) << 3) | (b >> 3);
			} /* for x */
		} /* for y */
	} /* fot all visible faces */

	ctx->lightmap.texture = aGLTextureCreate();
	AGLTextureUploadData upload;
	upload.x = upload.y = 0;
	upload.width = atlas_context.width;
	upload.height = atlas_context.height;
	upload.format = AGLTF_U565_RGB;
	upload.pixels = pixels;
	aGLTextureUpload(&ctx->lightmap.texture, &upload);

	/* pixels buffer is not needed anymore */
	tmpReturnToPosition(ctx->tmp, pixels);

	return BSPLoadResult_Success;
}

inline static struct AVec3f aVec3fLumpVec(struct VBSPLumpVertex v) { return aVec3f(v.x, v.y, v.z); }
inline static struct AVec3f aVec3fMix(struct AVec3f a, struct AVec3f b, float t) {
	return aVec3fAdd(a, aVec3fMulf(aVec3fSub(b, a), t));
}
inline static struct AVec2f aVec2fMulf(struct AVec2f v, float f) { return aVec2f(v.x * f, v.y * f); }
inline static struct AVec2f aVec2fMix(struct AVec2f a, struct AVec2f b, float t) {
	return aVec2fAdd(a, aVec2fMulf(aVec2fSub(b, a), t));
}
inline static struct AVec4f aVec4fNeg(struct AVec4f v) { return aVec4f(-v.x, -v.y, -v.z, -v.w); }
inline static float aVec3fLength2(struct AVec3f v) { return aVec3fDot(v,v); }
#define MAKE_MAX(type) \
	inline static type type##Max(type a, type b) { return (a > b) ? a : b; }
MAKE_MAX(float)
inline static float aVec2fLength(struct AVec2f v) { return sqrtf(aVec2fDot(v, v)); }

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
	//PRINT("mappedU=%f mappedV=%f maxDX=%f, maxDY=%f", mappedU, mappedV, maxDX, maxDY);
	return (mappedU > mappedV) != (maxDX > maxDY);
}

static enum BSPLoadResult bspLoadDisplacement(const struct LoadModelContext *ctx,
		const struct VisibleFace *face,
		struct BSPModelVertex *out_vertices, uint16_t *out_indices, int index_shift) {
 	const int side = (1 << face->dispinfo->power) + 1;
	const struct VBSPLumpVertex *const vertices = ctx->lumps->vertices.p;
	const struct VBSPLumpTexInfo * const tinfo = face->texinfo;
	const struct VBSPLumpDispVert *const dispvert = ctx->lumps->dispverts.p + face->dispinfo->vtx_start;
	
	//if (face->dispstartvtx != 0) PRINT("dispstartvtx = %d", face->dispstartvtx);

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
	
	const int swap = shouldSwapUV(
				aVec3f(tinfo->lightmap_vecs[0][0],tinfo->lightmap_vecs[0][1],tinfo->lightmap_vecs[0][2]),
				aVec3f(tinfo->lightmap_vecs[1][0],tinfo->lightmap_vecs[1][1],tinfo->lightmap_vecs[1][2]), vec);

	const struct AVec2f atlas_scale = aVec2f(1.f / ctx->lightmap.texture.width, 1.f / ctx->lightmap.texture.height);
	const struct AVec2f atlas_offset = aVec2f(
			.5f + face->atlas_x + tinfo->lightmap_vecs[0][3]*0 - 0*face->face->lightmap_min[0],
			.5f + face->atlas_y + tinfo->lightmap_vecs[1][3]*0 - 0*face->face->lightmap_min[1]);

	if (length_lm_u < 0. || length_lm_u >= face->width
		|| length_lm_v < 0. || length_lm_v >= face->height) {
		PRINT("LM OOB: (%f, %f) (%d, %d)", length_lm_u, length_lm_v, face->width, face->height);
		if (length_lm_u >= face->width) length_lm_u = face->width - 1;
		if (length_lm_v >= face->height) length_lm_v = face->height - 1;
	}

	/*
	PRINT("%f %f %f %f",
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
			v->vertex = aVec3fAdd(aVec3fMix(vl, vr, tx), aVec3fMulf(aVec3f(dv->x, dv->y, dv->z), dv->dist));

			if (v->lightmap_uv.x < 0 || v->lightmap_uv.y < 0 || v->lightmap_uv.x > face->width || v->lightmap_uv.y > face->height)
				PRINT("Error: DISP OOB LM F:V%u: x=%f y=%f z=%f tx=%f, ty=%f u=%f v=%f w=%d h=%d",
						x + y * side, v->vertex.x, v->vertex.y, v->vertex.z, tx, ty, v->lightmap_uv.x, v->lightmap_uv.y, face->width, face->height);

			v->lightmap_uv = aVec2fMul(aVec2fAdd(v->lightmap_uv, atlas_offset), atlas_scale);

			/* FIXME normal */
			v->normal = aVec3f(face->dispstartvtx/3.f, swap, dv->dist / 100.f);
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

	return BSPLoadResult_Success;
}

static enum BSPLoadResult bspLoadModelDraws(const struct LoadModelContext *ctx, struct MemoryPool *pool,
		struct BSPModel *model) {
	struct BSPModelVertex * const vertices_buffer
		= tmpAdvance(ctx->tmp, sizeof(struct BSPModelVertex) * ctx->max_draw_vertices);
	if (!vertices_buffer) return BSPLoadResult_ErrorTempMemory;

	/* each vertex after second in a face is a new triangle */
	uint16_t * const indices_buffer = tmpAdvance(ctx->tmp, sizeof(uint16_t) * ctx->indices);
	if (!indices_buffer) return BSPLoadResult_ErrorTempMemory;

	size_t vertex_pos = 0;
	size_t draw_indices_start = 0, indices_pos = 0;

	model->draws_count = ctx->draws_to_alloc;
	model->draws = POOL_ALLOC(pool, sizeof(struct BSPDraw) * ctx->draws_to_alloc);

	unsigned int idraw = 0;
	for (unsigned int iface = 0; iface < ctx->faces_count + 1; ++iface) {
		const struct VisibleFace *vis_face = ctx->faces + iface;

		if (iface == ctx->faces_count || vis_face->vertices + vertex_pos >= c_max_draw_vertices) {
			struct BSPDraw *draw = model->draws + idraw;
			memset(draw, 0, sizeof *draw);
			draw->count = indices_pos - draw_indices_start;
			draw->start = draw_indices_start;

			PRINT("Adding draw=%u start=%u count=%u", idraw, draw->start, draw->count);

			draw->vbo = aGLBufferCreate(AGLBT_Vertex);
			aGLBufferUpload(&draw->vbo, sizeof(struct BSPModelVertex) * vertex_pos, vertices_buffer);

			if (iface == ctx->faces_count) break;
			vertex_pos = 0;
			draw_indices_start = indices_pos;
			++idraw;
			ASSERT(idraw < ctx->draws_to_alloc);
		}

		if (vis_face->dispinfo) {
			bspLoadDisplacement(ctx, vis_face, vertices_buffer + vertex_pos, indices_buffer + indices_pos, vertex_pos);
			vertex_pos += vis_face->vertices;
			indices_pos += vis_face->indices;
		} else {
			const struct VBSPLumpFace *face = vis_face->face;
			const struct VBSPLumpTexInfo * const tinfo = vis_face->texinfo;
			struct AVec3f normal;
			normal.x = ctx->lumps->planes.p[face->plane].x;
			normal.y = ctx->lumps->planes.p[face->plane].y;
			normal.z = ctx->lumps->planes.p[face->plane].z;
			if (face->side) normal = aVec3fNeg(normal);

			const struct AVec4f lm_map_u = aVec4f(
						tinfo->lightmap_vecs[0][0], tinfo->lightmap_vecs[0][1],
						tinfo->lightmap_vecs[0][2], tinfo->lightmap_vecs[0][3] - face->lightmap_min[0]);
			const struct AVec4f lm_map_v = aVec4f(
						tinfo->lightmap_vecs[1][0], tinfo->lightmap_vecs[1][1],
						tinfo->lightmap_vecs[1][2], tinfo->lightmap_vecs[1][3] - face->lightmap_min[1]);

			const int32_t * const surfedges = ctx->lumps->surfedges.p + face->first_edge;
			const size_t indices_count =  (face->num_edges - 2) * 3;
			ASSERT(indices_count + indices_pos <= (unsigned)ctx->indices);
			ASSERT(vertex_pos + face->num_edges <= ctx->max_draw_vertices);
			uint16_t * const indices = indices_buffer + indices_pos;
			struct BSPModelVertex * const vertices = vertices_buffer + vertex_pos;
			for (int iedge = 0; iedge < face->num_edges; ++iedge) {
				const uint16_t vstart = (surfedges[iedge] >= 0)
					? ctx->lumps->edges.p[surfedges[iedge]].v[0]
					: ctx->lumps->edges.p[-surfedges[iedge]].v[1];

				const struct VBSPLumpVertex * const lv = ctx->lumps->vertices.p + vstart;
				struct BSPModelVertex * const vertex = vertices + iedge;

				vertex->vertex = aVec3f(lv->x, lv->y, lv->z);
				vertex->normal = aVec3f(0.f, 0.f, 0.f);// FIXME normal;
				vertex->lightmap_uv = aVec2f(
					aVec4fDot(aVec4f3(vertex->vertex, 1.f),	lm_map_u),
					aVec4fDot(aVec4f3(vertex->vertex, 1.f), lm_map_v));

				if (vertex->lightmap_uv.x < 0 || vertex->lightmap_uv.y < 0 || vertex->lightmap_uv.x > vis_face->width || vertex->lightmap_uv.y > vis_face->height)
					PRINT("Error: OOB LM F%u:V%u: x=%f y=%f z=%f u=%f v=%f w=%d h=%d", iface, iedge, lv->x, lv->y, lv->z, vertex->lightmap_uv.x, vertex->lightmap_uv.y, vis_face->width, vis_face->height);

				vertex->lightmap_uv.x = (vertex->lightmap_uv.x + vis_face->atlas_x + .5f) / ctx->lightmap.texture.width;
				vertex->lightmap_uv.y = (vertex->lightmap_uv.y + vis_face->atlas_y + .5f) / ctx->lightmap.texture.height;

				if (iedge > 1) {
					indices[(iedge-2)*3+0] = vertex_pos + 0;
					indices[(iedge-2)*3+1] = vertex_pos + iedge;
					indices[(iedge-2)*3+2] = vertex_pos + iedge - 1;
				}
			}

			indices_pos += indices_count;
			vertex_pos += face->num_edges;
		}
	}

	model->ibo = aGLBufferCreate(AGLBT_Index);
	aGLBufferUpload(&model->ibo, sizeof(uint16_t) * ctx->indices, indices_buffer);

	/*for (unsigned int i = 0; i < (indices_buffer_count<256?indices_buffer_count:256); ++i)
		PRINT("%u: %u", i, indices_buffer[i]);
	*/

	return BSPLoadResult_Success;
}

static enum BSPLoadResult bspLoadModel(struct BSPModel *model, struct MemoryPool *pool, struct TemporaryPool *temp,
		const struct Lumps *lumps, unsigned index) {
	struct LoadModelContext context;
	memset(&context, 0, sizeof context);

	ASSERT(index < lumps->models.n);

	context.tmp = temp;
	context.lumps = lumps;
	context.model = lumps->models.p + index;

	/* Step 1. Collect lightmaps for all faces */
	enum BSPLoadResult result = bspLoadModelCollectFaces(&context);
	if (result != BSPLoadResult_Success) {
		PRINT("Error: bspLoadModelCollectFaces() => %s", R2S(result));
		return result;
	}

	/* Step 2. Build an atlas of all lightmaps */
	result = bspLoadModelLightmaps(&context);
	if (result != BSPLoadResult_Success) {
		PRINT("Error: bspLoadModelLightmaps() => %s", R2S(result));
		return result;
	}

	/* Step 3. Generate draw operations data */
	result = bspLoadModelDraws(&context, pool, model);
	if (result != BSPLoadResult_Success) {
		aGLTextureDestroy(&context.lightmap.texture);
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
}

static int lumpRead(const char *name, const struct VBSPLumpHeader *header,
		struct IFile *file, struct TemporaryPool *pool,
		struct AnyLump *out_ptr, uint32_t item_size) {
	out_ptr->p = tmpAdvance(pool, header->size);
	if (!out_ptr->p) {
		PRINT("Not enough temp memory to allocate storage for lump %s", name);
		return -1;
	}

	const size_t bytes = file->read(file, header->file_offset, header->size, (void*)out_ptr->p);
	if (bytes != header->size) {
		PRINT("Cannot read full lump %s, read only %zu bytes out of %u", name, bytes, header->size);
		return -1;
	}

	PRINT("Read lump %s, offset %u, size %u bytes / %u item = %u elements",
			name, header->file_offset, header->size, item_size, header->size / item_size);

	out_ptr->n = header->size / item_size;
	return 1;
}

enum BSPLoadResult bspLoadWorldspawn(struct BSPLoadModelContext context, const char *mapname) {
	enum BSPLoadResult result = BSPLoadResult_Success;
	struct IFile *file = 0;
	if (CollectionOpen_Success !=
			collectionChainOpen(context.collection, mapname, File_Map, &file)) {
		return BSPLoadResult_ErrorFileOpen;
	}

	void *tmp_cursor = tmpGetCursor(context.tmp);

	struct VBSPHeader vbsp_header;
	size_t bytes = file->read(file, 0, sizeof vbsp_header, &vbsp_header);
	if (bytes < sizeof(vbsp_header)) {
		PRINT("Size is too small: %zu <= %zu", bytes, sizeof(struct VBSPHeader));
		result = BSPLoadResult_ErrorFileFormat;
		goto exit;
	}

	if (vbsp_header.ident[0] != 'V' || vbsp_header.ident[1] != 'B' ||
			vbsp_header.ident[2] != 'S' || vbsp_header.ident[3] != 'P') {
		PRINT("Error: invalid ident => %c%c%c%c != VBSP",
				vbsp_header.ident[0], vbsp_header.ident[1], vbsp_header.ident[2], vbsp_header.ident[3]);
		result = BSPLoadResult_ErrorFileFormat;
		goto exit;
	}

	if (vbsp_header.version != 19 && vbsp_header.version != 20) {
		PRINT("Error: invalid version: %d != 19 or 20", vbsp_header.version);
		result = BSPLoadResult_ErrorFileFormat;
		goto exit;
	}

	PRINT("VBSP version %u opened", vbsp_header.version);

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

	result = bspLoadModel(context.model, context.pool, context.tmp, &lumps, 0);
	if (result != BSPLoadResult_Success)
		PRINT("Error: bspLoadModel() => %s", R2S(result));

exit:
	tmpReturnToPosition(context.tmp, tmp_cursor);
	if (file) file->close(file);
	return result;
}
