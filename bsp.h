#pragma once
#include "atto/math.h"
#include "atto/gl.h"

#include <stdlib.h>
#include <stdio.h>

#define STR1_(m) #m
#define STR_(m) STR1_(m)
#define PRINT_(fmt, ...) fprintf(stderr, __FILE__ ":" STR_(__LINE__) ": " fmt "\n", __VA_ARGS__)
#define ASSERT(cond) if (!(cond)){PRINT_("%s failed", #cond); abort();}

struct MemoryPool {
	void *(*alloc)(struct MemoryPool *pool, size_t size);
	void (*free)(struct MemoryPool *pool, void *ptr);
};
#define POOL_ALLOC(p, sz) ((p)->alloc(p, sz))
#define POOL_FREE(p, ptr) ((p)->free(p, ptr))

struct TemporaryPool {
	char *storage;
	size_t size, cursor;
};

static inline void *tmpGetCursor(const struct TemporaryPool *tmp) {
	return tmp->storage + tmp->cursor;
}
static inline size_t tmpGetLeft(const struct TemporaryPool *tmp) {
	return tmp->size - tmp->cursor;
}
static inline void *tmpAdvance(struct TemporaryPool *tmp, size_t size) {
	if (tmp->size - tmp->cursor < size)
		return 0;

	void *ret = tmp->storage + tmp->cursor;
	tmp->cursor += size;
	return ret;
}
static inline void tmpReturn(struct TemporaryPool *tmp, size_t size) {
	ASSERT(size <= tmp->cursor);
	tmp->cursor -= size;
}
static inline void tmpReturnToPosition(struct TemporaryPool *tmp, void *marker) {
	ASSERT((char*)marker >= tmp->storage);
	const size_t to_release = (char*)marker - tmp->storage;
	tmpReturn(tmp, to_release);
}

struct AABB { struct AVec3f min, max; };
struct Plane { struct AVec3f n; float d; };

struct BSPModelVertex {
	struct AVec3f vertex;
	struct AVec3f normal;
	struct AVec2f lightmap_uv;
};

struct BSPDraw {
	AGLBuffer vbo;
	AGLTexture texture;
	unsigned int start, count;
	struct AVec4f model;
};

/* TODO
struct BSPNode {
	struct AABB aabb;
	struct {
		struct BSPDraw *p;
		unsigned n;
	} draw;
	struct Plane plane;
	int left, right;
};
*/

struct BSPModel {
	struct AABB aabb;
	AGLTexture lightmap;
	AGLBuffer ibo;
	unsigned int draws_count;
	struct BSPDraw *draws;
};

struct BSPLoadModelContext {
	const char *filename;
	struct MemoryPool *pool;
	struct TemporaryPool *tmp;

	/* allocated by caller, populated by callee */
	struct BSPModel *model;
};

enum BSPLoadResult {
	BSPLoadResult_Success,
	BSPLoadResult_ErrorFileOpen,
	BSPLoadResult_ErrorFileFormat,
	BSPLoadResult_ErrorMemory,
	BSPLoadResult_ErrorTempMemory,
	BSPLoadResult_ErrorCapabilities
};

enum BSPLoadResult bspLoadWorldspawn(struct BSPLoadModelContext context);
