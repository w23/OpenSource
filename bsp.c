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
#define BSPLUMP(type,name) struct{const type *p;uint32_t n;} name
	BSPLUMP(struct VBSPLumpModel, models);
	BSPLUMP(struct VBSPLumpNode, nodes);
	BSPLUMP(struct VBSPLumpPlane, planes);
	BSPLUMP(struct VBSPLumpLeaf, leaves);
	BSPLUMP(uint16_t, leaffaces);
	BSPLUMP(struct VBSPLumpFace, faces);
	BSPLUMP(int32_t, surfedges);
	BSPLUMP(struct VBSPLumpEdge, edges);
	BSPLUMP(struct VBSPLumpVertex, vertices);
	BSPLUMP(struct VBSPLumpTexInfo, texinfos);
	BSPLUMP(struct VBSPLumpLightMap, lightmaps);
	BSPLUMP(char, entities);
#undef BSPLUMP
};

/* data needed for making lightmap atlas */
struct VisibleFace {
	unsigned int face_index;
	/* read directly from lumps */
	unsigned int vertices;
	unsigned int width, height;
	const struct VBSPLumpLightMap *samples;

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
	unsigned int max_draw_vertices;
	struct {
		unsigned int pixels;
		unsigned int max_width;
		unsigned int max_height;
		AGLTexture texture;
	} lightmap;
	unsigned int draws_to_alloc;
};

enum FaceProbe {
	FaceProbe_Ok,
	FaceProbe_Skip,
	FaceProbe_OOB,
	FaceProbe_PlaneOOB,
	FaceProbe_TooFewEdges,
	FaceProbe_SurfedgesOOB,
	FaceProbe_OutOfMemory,
	FaceProbe_EdgesOOB,
	FaceProbe_InconsistentEdge,
	FaceProbe_UnalignedLightmap,
	FaceProbe_LightmapOOB,
	FaceProbe_TexinfoOOB,
	FaceProbe_VertexOOB
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
		case FaceProbe_TexinfoOOB: return "FaceProbe_TexinfoOOB"; break;
		case FaceProbe_VertexOOB: return "FaceProbe_VertexOOB"; break;
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

	if (face->texinfo > lumps->texinfos.n)
		return FaceProbe_TexinfoOOB;
	if (shouldSkipFace(face, lumps))
		return FaceProbe_Skip;

	/* Check for basic reference consistency */
	if (face->plane >= lumps->planes.n) return FaceProbe_PlaneOOB;
	if (face->num_edges < 3) return FaceProbe_TooFewEdges;
	if (face->first_edge >= lumps->surfedges.n || lumps->surfedges.n - face->first_edge < face->num_edges)
		return FaceProbe_SurfedgesOOB;

	if (face->lightmap_offset % sizeof(struct VBSPLumpLightMap) != 0) {
		PRINT("Error: face%u references lightmap at unaligned offset %u: %lu",
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

		if (vstart > lumps->vertices.n)
			return FaceProbe_VertexOOB;

		if (prev_end != 0xffffffffu && prev_end != vstart) {
			return FaceProbe_InconsistentEdge;
		}

		prev_end = vend;
	}

	vis_face->vertices = face->num_edges;
	vis_face->width = lm_width;
	vis_face->height = lm_height;
	vis_face->samples = lumps->lightmaps.p + sample_offset;
	if (lm_width > ctx->lightmap.max_width) ctx->lightmap.max_width = lm_width;
	if (lm_height > ctx->lightmap.max_height) ctx->lightmap.max_height = lm_height;
	vis_face->face_index = index;

	ctx->lightmap.pixels += lightmap_size;
	ctx->vertices += face->num_edges;
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

static enum BSPLoadResult bspLoadModelDraws(const struct LoadModelContext *ctx, struct MemoryPool *pool,
		struct BSPModel *model) {
	struct BSPModelVertex * const vertices_buffer
		= tmpAdvance(ctx->tmp, sizeof(struct BSPModelVertex) * ctx->max_draw_vertices);
	if (!vertices_buffer) return BSPLoadResult_ErrorTempMemory;

	/* each vertex after second in a face is a new triangle */
	const size_t indices_buffer_count = ctx->vertices * 3 - ctx->faces_count * 6;
	uint16_t * const indices_buffer = tmpAdvance(ctx->tmp, sizeof(uint16_t) * indices_buffer_count);
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

		const struct VBSPLumpFace *face = ctx->lumps->faces.p + vis_face->face_index;
		const struct VBSPLumpTexInfo *tinfo = ctx->lumps->texinfos.p + face->texinfo;
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
		ASSERT(indices_count + indices_pos <= indices_buffer_count);
		ASSERT(vertex_pos + face->num_edges <= ctx->max_draw_vertices);
		uint16_t * const indices = indices_buffer + indices_pos;
		struct BSPModelVertex * const vertices = vertices_buffer + vertex_pos;
		for (unsigned int iedge = 0; iedge < face->num_edges; ++iedge) {
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

			vertex->lightmap_uv.x = (vertex->lightmap_uv.x + vis_face->atlas_x) / ctx->lightmap.texture.width;
			vertex->lightmap_uv.y = (vertex->lightmap_uv.y + vis_face->atlas_y) / ctx->lightmap.texture.height;

			if (iedge > 1) {
				indices[(iedge-2)*3+0] = vertex_pos + 0;
				indices[(iedge-2)*3+1] = vertex_pos + iedge;
				indices[(iedge-2)*3+2] = vertex_pos + iedge - 1;
			}
		}

		indices_pos += indices_count;
		vertex_pos += face->num_edges;
	}

	model->ibo = aGLBufferCreate(AGLBT_Index);
	aGLBufferUpload(&model->ibo, sizeof(uint16_t) * indices_buffer_count, indices_buffer);

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

static int initLump(const struct VBSPLumpHeader *header, const struct AFileMap *map,
		struct AnyLump *ptr, uint32_t item_size) {
	if (map->size <= header->file_offset || map->size - header->file_offset < header->size)
		return 0;

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
#define INIT_LUMP_T(name, type, field) \
	if (!initLump(hdr->lump_headers + VBSP_Lump_##name, &file, \
			(struct AnyLump*)&lumps.field, sizeof(type))) { \
		result = BSPLoadResult_ErrorFileFormat; \
		goto exit; \
	}
#define INIT_LUMP(name, field) INIT_LUMP_T(name, struct VBSPLump##name, field)
	INIT_LUMP(Model, models);
	INIT_LUMP(Node, nodes);
	INIT_LUMP(Plane, planes);
	INIT_LUMP(Leaf, leaves);
	INIT_LUMP_T(LeafFace, uint16_t, leaffaces);
	INIT_LUMP(Face, faces);
	INIT_LUMP_T(Surfedge, int32_t, surfedges);
	INIT_LUMP(Edge, edges);
	INIT_LUMP(Vertex, vertices);
	INIT_LUMP(TexInfo, texinfos);
	INIT_LUMP(LightMap, lightmaps);

	result = bspLoadModel(context.model, context.pool, context.tmp, &lumps, 0);
	if (result != BSPLoadResult_Success) {
		PRINT("Error: bspLoadModel() => %s", R2S(result));
	}

exit:
	aFileMapClose(&file);
	return result;
}
