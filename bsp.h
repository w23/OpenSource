#pragma once
#include "material.h"
#include "atto/gl.h"
#include "atto/math.h"

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
	int draws_count;
	struct BSPDraw *draws;
};

struct ICollection;
struct MemoryPool;
struct Stack;

struct BSPLoadModelContext {
	struct ICollection *collection;
	struct Stack *persistent;
	struct Stack *tmp;

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

enum BSPLoadResult bspLoadWorldspawn(struct BSPLoadModelContext context, const char *mapname);
