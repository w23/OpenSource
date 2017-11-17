#pragma once
#include "atto/math.h"

typedef enum {
	RTexFormat_RGB565
} RTexFormat;

typedef enum {
	RTexType_2D = (1 << 0),
	RTexType_CubePX = (1 << 1),
	RTexType_CubeNX = (1 << 2),
	RTexType_CubePY = (1 << 3),
	RTexType_CubeNY = (1 << 4),
	RTexType_CubePZ = (1 << 5),
	RTexType_CubeNZ = (1 << 6),
} RTexType;

typedef struct {
	int width, height;
	RTexFormat format;
	int gl_name;
	int type_flags;
} RTexture;

typedef struct {
	RTexType type;
	int width, height;
	RTexFormat format;
	const void *pixels;
	int generate_mipmaps;
} RTextureUploadParams;

#define renderTextureInit(texture_ptr) do { (texture_ptr)->gl_name = -1; } while (0)
void renderTextureUpload(RTexture *texture, RTextureUploadParams params);

typedef struct {
	int gl_name;
	int type;
} RBuffer;

typedef enum {
	RBufferType_Vertex,
	RBufferType_Index
} RBufferType;

int renderInit();
void renderResize(int w, int h);

void renderBufferCreate(RBuffer *buffer, RBufferType type, int size, const void *data);

struct BSPModel;

void renderClear();
void renderModelDraw(const struct AMat4f *mvp, struct AVec3f camera_position, float lmn, const struct BSPModel *model);
