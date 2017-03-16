#include "vbsp.h"
#include "bsp.h"
#include "atlas.h"
#include "filemap.h"
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
	FaceProbe_OOB,
	FaceProbe_Inconsistent,
	FaceProbe_PlaneOOB,
	FaceProbe_TooFewEdges,
	FaceProbe_SurfedgesOOB,
	FaceProbe_OutOfMemory,
	FaceProbe_EdgesOOB,
	FaceProbe_InconsistentEdge,
	FaceProbe_UnalignedLightmap,
	FaceProbe_LightmapOOB,
	FaceProbe_TexInfoOOB,
	FaceProbe_TexDataOOB,
	FaceProbe_TexDataStringTableOOB,
	FaceProbe_TexDataStringDataOOB,
	FaceProbe_VertexOOB,
	FaceProbe_DispInfoOOB
};

static const char *faceProbeError(enum FaceProbe code) {
	switch(code) {
		case FaceProbe_Ok: return "FaceProbe_Ok"; break;
		case FaceProbe_Skip: return "FaceProbe_Skip"; break;
		case FaceProbe_OOB: return "FaceProbe_OOB"; break;
		case FaceProbe_PlaneOOB: return "FaceProbe_PlaneOOB"; break;
		case FaceProbe_TooFewEdges: return "FaceProbe_TooFewEdges"; break;
		case FaceProbe_SurfedgesOOB: return "FaceProbe_SurfedgesOOB"; break;
		case FaceProbe_OutOfMemory: return "FaceProbe_OutOfMemory"; break;
		case FaceProbe_EdgesOOB: return "FaceProbe_EdgesOOB"; break;
		case FaceProbe_InconsistentEdge: return "FaceProbe_InconsistentEdge"; break;
		case FaceProbe_UnalignedLightmap: return "FaceProbe_UnalignedLightmap"; break;
		case FaceProbe_LightmapOOB: return "FaceProbe_LightmapOOB"; break;
		case FaceProbe_TexInfoOOB: return "FaceProbe_TexInfoOOB"; break;
		case FaceProbe_TexDataOOB: return "FaceProbe_TexDataOOB"; break;
		case FaceProbe_TexDataStringTableOOB: return "FaceProbe_TexDataStringTableOOB"; break;
		case FaceProbe_TexDataStringDataOOB: return "FaceProbe_TexDataStringDataOOB"; break;
		case FaceProbe_VertexOOB: return "FaceProbe_VertexOOB"; break;
		case FaceProbe_DispInfoOOB: return "FaceProbe_DispInfoOOB"; break;
		case FaceProbe_Inconsistent: return "FaceProbe_Inconsistent"; break;
	}
	return "UNKNOWN";
}

static inline int shouldSkipFace(const struct VBSPLumpFace *face, const struct Lumps *lumps) {
	(void)(face); (void)(lumps);
	//const struct VBSPLumpTexInfo *tinfo = lumps->texinfos.p + face->texinfo;
	return /*(tinfo->flags & (VBSP_Surface_NoDraw | VBSP_Surface_NoLight)) ||*/ face->lightmap_offset == 0xffffffffu
		|| face->lightmap_offset < 4;
}

static enum FaceProbe bspFaceProbe(struct LoadModelContext *ctx,
		struct VisibleFace *vis_face, unsigned index) {
	const struct Lumps * const lumps = ctx->lumps;
	if (index >= lumps->faces.n) return FaceProbe_OOB;
	const struct VBSPLumpFace * const face = lumps->faces.p + index;
	vis_face->face = face;

	if (face->texinfo < 0)
		return FaceProbe_Skip;
	if ((unsigned)face->texinfo > lumps->texinfos.n)
		return FaceProbe_TexInfoOOB;
	vis_face->texinfo = lumps->texinfos.p + face->texinfo;
	if (shouldSkipFace(face, lumps))
		return FaceProbe_Skip;
	if (vis_face->texinfo->texdata >= lumps->texdata.n)
		return FaceProbe_TexDataOOB;
	vis_face->texdata = lumps->texdata.p + vis_face->texinfo->texdata;
	if (vis_face->texdata->name_string_table_id >= lumps->texdatastringtable.n)
		return FaceProbe_TexDataStringTableOOB;
	const int32_t texdatastringdata_offset = lumps->texdatastringtable.p[vis_face->texdata->name_string_table_id];
	if (texdatastringdata_offset < 0 || (uint32_t)texdatastringdata_offset >= lumps->texdatastringdata.n)
		return FaceProbe_TexDataStringDataOOB;
	/* FIXME validate string: has \0 earlier than end */
	vis_face->texture = lumps->texdatastringdata.p + texdatastringdata_offset;

	if (face->dispinfo >= 0) {
		if ((unsigned)face->dispinfo >= lumps->dispinfos.n)
			return FaceProbe_DispInfoOOB;
		vis_face->dispinfo = lumps->dispinfos.p + face->dispinfo;
		const int side = (1 << vis_face->dispinfo->power) + 1;
		if (face->num_edges != 4) {
			PRINT("Displaced face has invalid num_edges %d", face->num_edges);
			return FaceProbe_Inconsistent;
		}
		vis_face->vertices = side * side;
		vis_face->indices = (side - 1) * (side - 1) * 6; /* triangle list */
		PRINT("Power: %d, min_tess: %d, vertices: %d",
				vis_face->dispinfo->power, vis_face->dispinfo->min_tess, vis_face->vertices);
		vis_face->dispstartvtx = 0;
	} else {
		vis_face->dispinfo = 0;
		vis_face->vertices = face->num_edges;
		vis_face->indices = (vis_face->vertices - 2) * 3;
	}

	/* Check for basic reference consistency */
	if (face->plane >= lumps->planes.n) return FaceProbe_PlaneOOB;
	if (face->num_edges < 3) return FaceProbe_TooFewEdges;
	if (face->first_edge >= lumps->surfedges.n || lumps->surfedges.n - face->first_edge < (unsigned)face->num_edges)
		return FaceProbe_SurfedgesOOB;

	if (face->lightmap_offset % sizeof(struct VBSPLumpLightMap) != 0) {
		PRINT("Error: face%u references lightmap at unaligned offset %u: %zu",
				index, face->lightmap_offset, face->lightmap_offset % sizeof(struct VBSPLumpLightMap));
		return FaceProbe_UnalignedLightmap;
	}

	const unsigned int lm_width = face->lightmap_size[0] + 1;
	const unsigned int lm_height = face->lightmap_size[1] + 1;
	const size_t lightmap_size = lm_width * lm_height;
	const size_t sample_offset = face->lightmap_offset / sizeof(struct VBSPLumpLightMap);
	if (sample_offset >= lumps->lightmaps.n || lumps->lightmaps.n - sample_offset < lightmap_size)
		return FaceProbe_LightmapOOB;

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
			return FaceProbe_EdgesOOB;
		}

		const unsigned int vstart = lumps->edges.p[edge_index].v[istart];
		const unsigned int vend = lumps->edges.p[edge_index].v[1^istart];

		if (vis_face->dispinfo) {
			vis_face->dispquadvtx[i] = vstart;
			PRINT("%f %f %f -- %f %f %f",
				vis_face->dispinfo->start_pos.x,
				vis_face->dispinfo->start_pos.y,
				vis_face->dispinfo->start_pos.z,
				lumps->vertices.p[vstart].x,
				lumps->vertices.p[vstart].y,
				lumps->vertices.p[vstart].z);
			if (lumps->vertices.p[vstart].x == vis_face->dispinfo->start_pos.x
					&& lumps->vertices.p[vstart].y == vis_face->dispinfo->start_pos.y
					&& lumps->vertices.p[vstart].z == vis_face->dispinfo->start_pos.z) {
				PRINT("%d matches disp_start", i);
				vis_face->dispstartvtx = i;
			}
		}

		if (vstart > lumps->vertices.n)
			return FaceProbe_VertexOOB;

		if (prev_end != 0xffffffffu && prev_end != vstart) {
			return FaceProbe_InconsistentEdge;
		}

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
			PRINT("Error: bspFaceProbe returned %s (%d)", faceProbeError(result), result);
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

static enum BSPLoadResult bspLoadDisplacement(const struct LoadModelContext *ctx,
		const struct VisibleFace *face,
		struct BSPModelVertex *out_vertices, uint16_t *out_indices, int index_shift) {
 	const int side = (1 << face->dispinfo->power) + 1;
	const struct VBSPLumpVertex *const vertices = ctx->lumps->vertices.p;
	const struct VBSPLumpTexInfo * const tinfo = face->texinfo;

	const struct AVec3f
		bl = aVec3fLumpVec(vertices[face->dispquadvtx[(face->dispstartvtx + 0)%4]]),
		tl = aVec3fLumpVec(vertices[face->dispquadvtx[(face->dispstartvtx + 1)%4]]),
		tr = aVec3fLumpVec(vertices[face->dispquadvtx[(face->dispstartvtx + 2)%4]]),
		br = aVec3fLumpVec(vertices[face->dispquadvtx[(face->dispstartvtx + 3)%4]]);

	const struct AVec4f lm_map_u = aVec4f(
				tinfo->lightmap_vecs[0][0], tinfo->lightmap_vecs[0][1],
				tinfo->lightmap_vecs[0][2], tinfo->lightmap_vecs[0][3] - face->face->lightmap_min[0]);
	const struct AVec4f lm_map_v = aVec4f(
				tinfo->lightmap_vecs[1][0], tinfo->lightmap_vecs[1][1],
				tinfo->lightmap_vecs[1][2], tinfo->lightmap_vecs[1][3] - face->face->lightmap_min[1]);

	const struct VBSPLumpDispVert *const dispvert = ctx->lumps->dispverts.p + face->dispinfo->vtx_start;
	for (int y = 0; y < side; ++y) {
		const float ty = (float)y / (side - 1);
		const struct AVec3f vl = aVec3fMix(bl, tl, ty);
		const struct AVec3f vr = aVec3fMix(br, tr, ty);
		for (int x = 0; x < side; ++x) {
			const float tx = (float)x / (side - 1);
			struct BSPModelVertex * const v = out_vertices + y * side + x;
			const struct VBSPLumpDispVert * const dv = dispvert + y * side + x;

			v->vertex = aVec3fMix(vl, vr, tx);
			v->lightmap_uv = aVec2f(
				aVec4fDot(aVec4f3(v->vertex, 1.f),	lm_map_u),
				aVec4fDot(aVec4f3(v->vertex, 1.f), lm_map_v));
			v->vertex = aVec3fAdd(aVec3fMix(vl, vr, tx), aVec3fMulf(aVec3f(dv->x, dv->y, dv->z), dv->dist));

			if (v->lightmap_uv.x < 0 || v->lightmap_uv.y < 0 || v->lightmap_uv.x > face->width || v->lightmap_uv.y > face->height)
				PRINT("Error: OOB LM F:V%u: x=%f y=%f z=%f u=%f v=%f w=%d h=%d",
						x + y * side, v->vertex.x, v->vertex.y, v->vertex.z, v->lightmap_uv.x, v->lightmap_uv.y, face->width, face->height);

			v->lightmap_uv.x = (v->lightmap_uv.x + face->atlas_x + .5f) / ctx->lightmap.texture.width;
			v->lightmap_uv.y = (v->lightmap_uv.y + face->atlas_y + .5f) / ctx->lightmap.texture.height;

			/* FIXME normal */
			v->normal = aVec3f(0, 0, 1);
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
				vertex->normal = normal;
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

static int initLump(const char *name, const struct VBSPLumpHeader *header, const struct AFileMap *map,
		struct AnyLump *ptr, uint32_t item_size) {
	if (map->size <= header->file_offset || map->size - header->file_offset < header->size)
		return 0;

	PRINT("Lump %s, offset %u, size %u bytes / %u item = %u elements",
			name, header->file_offset, header->size, item_size, header->size / item_size);

	ptr->p = (const char*)map->map + header->file_offset;
	ptr->n = header->size / item_size;
	return 1;
}

enum BSPLoadResult bspLoadWorldspawn(struct BSPLoadModelContext context) {
	enum BSPLoadResult result = BSPLoadResult_Success;

	struct AFileMap file = aFileMapOpen(context.filename);
	if (!file.map) return BSPLoadResult_ErrorFileOpen;
	if (file.size <= sizeof(struct VBSPHeader)) {
		PRINT("Size is too small: %zu <= %zu", file.size, sizeof(struct VBSPHeader));
		result = BSPLoadResult_ErrorFileFormat;
		goto exit;
	}

	const struct VBSPHeader *hdr = file.map;
	if (hdr->ident[0] != 'V' || hdr->ident[1] != 'B' ||
			hdr->ident[2] != 'S' || hdr->ident[3] != 'P') {
		PRINT("Error: invalid ident => %c%c%c%c != VBSP",
				hdr->ident[0], hdr->ident[1], hdr->ident[2], hdr->ident[3]);
		result = BSPLoadResult_ErrorFileFormat;
		goto exit;
	}

	if (hdr->version != 19 && hdr->version != 20) {
		PRINT("Error: invalid version: %d != 19 or 20", hdr->version);
		result = BSPLoadResult_ErrorFileFormat;
		goto exit;
	}

	PRINT("VBSP version %u opened", hdr->version);

	struct Lumps lumps;
	lumps.version = hdr->version;
#define BSPLUMP(name, type, field) \
	if (!initLump(#name, hdr->lump_headers + VBSP_Lump_##name, &file, \
			(struct AnyLump*)&lumps.field, sizeof(type))) { \
		result = BSPLoadResult_ErrorFileFormat; \
		goto exit; \
	}
	LIST_LUMPS
#undef BSPLUMP

	result = bspLoadModel(context.model, context.pool, context.tmp, &lumps, 0);
	if (result != BSPLoadResult_Success) {
		PRINT("Error: bspLoadModel() => %s", R2S(result));
	}

exit:
	aFileMapClose(&file);
	return result;
}
