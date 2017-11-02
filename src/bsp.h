#pragma once
#include "material.h"
#include "render.h"
#include "atto/math.h"

struct Material;

struct AABB { struct AVec3f min, max; };
struct Plane { struct AVec3f n; float d; };

struct BSPModelVertex {
	struct AVec3f vertex;
	struct AVec3f normal;
	struct AVec2f lightmap_uv;
	struct AVec2f tex_uv;
};

struct BSPDraw {
	const struct Material *material;
	unsigned int start, count;
	unsigned int vbo_offset;
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

#define BSP_LANDMARK_NAME_LENGTH 64
#define BSP_MAX_LANDMARKS 32

struct BSPLandmark {
	char name[BSP_LANDMARK_NAME_LENGTH];
	struct AVec3f origin;
};

struct BSPModel {
	struct AABB aabb;
	RTexture lightmap;
	RBuffer vbo, ibo;
	int draws_count;
	struct BSPDraw *draws;

	struct BSPLandmark landmarks[BSP_MAX_LANDMARKS];
	int landmarks_count;

	struct AVec3f player_start;
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

void openSourceAddMap(const char* mapname, int mapname_length);
