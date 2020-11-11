#pragma once
#include "material.h"
#include "render.h"
#include "atto/math.h"
#include "common.h"

struct AABB { struct AVec3f min, max; };

struct BSPModelVertex {
	struct AVec3f vertex;
	//struct AVec3f normal;
	struct AVec2f lightmap_uv;
	struct AVec2f tex_uv;
	struct { uint8_t r, g, b; } average_color;
	uint8_t padding_;
};

struct BSPDraw {
	const Material *material;
	unsigned int start, count;
	unsigned int vbo_offset;
};

#define BSP_LANDMARK_NAME_LENGTH 64
#define BSP_MAX_LANDMARKS 32

typedef struct BSPLandmark {
	char name[BSP_LANDMARK_NAME_LENGTH];
	struct AVec3f origin;
} BSPLandmark;

struct BSPDrawSet {
	int draws_count;
	struct BSPDraw *draws;
};

typedef enum {
	BSPSkyboxDir_RT,
	BSPSkyboxDir_LF,
	BSPSkyboxDir_FT,
	BSPSkyboxDir_BK,
	BSPSkyboxDir_UP,
	BSPSkyboxDir_DN,
	BSPSkyboxDir_COUNT
} BSPSkyboxDir;

struct BSPModel {
	struct AABB aabb;
	RTexture lightmap;
	RBuffer vbo, ibo;

	const Material *skybox[BSPSkyboxDir_COUNT];

	struct BSPDrawSet detailed;
	struct BSPDrawSet coarse;

	struct BSPLandmark landmarks[BSP_MAX_LANDMARKS];
	int landmarks_count;

	struct AVec3f player_start;
};

struct ICollection;
struct MemoryPool;
struct Stack;

typedef struct BSPLoadModelContext {
	struct ICollection *collection;
	struct Stack *persistent;
	struct Stack *tmp;

	/* allocated by caller, populated by callee */
	struct BSPModel *model;

	StringView name;
	StringView prev_map_name, next_map_name;
} BSPLoadModelContext;

typedef enum BSPLoadResult {
	BSPLoadResult_Success,
	BSPLoadResult_ErrorFileOpen,
	BSPLoadResult_ErrorFileFormat,
	BSPLoadResult_ErrorMemory,
	BSPLoadResult_ErrorTempMemory,
	BSPLoadResult_ErrorCapabilities
} BSPLoadResult;

/* should be called AFTER renderInit() */
void bspInit();

enum BSPLoadResult bspLoadWorldspawn(BSPLoadModelContext context);

void openSourceAddMap(StringView name);
