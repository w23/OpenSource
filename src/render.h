#pragma once
#include "atto/math.h"

typedef enum {
	RTexFormat_RGB565,
#ifdef ATTO_PLATFORM_RPI
	RTexFormat_Compressed_ETC1,
#endif
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

typedef enum {
	RTexWrap_Repeat,
	RTexWrap_Clamp
} RTexWrap;

typedef struct {
	int width, height;
	RTexFormat format;

#if ATTO_GL
	int gl_name;
	int type_flags;
#else
	void *vkImage, *vkDevMem, *vkImageView;
#endif

} RTexture;

typedef struct {
	RTexType type;
	int width, height;
	RTexFormat format;
	const void *pixels;
	int mip_level; // -1 means generate; -2 means don't need
	RTexWrap wrap;
} RTextureUploadParams;

//#define renderTextureInit(texture_ptr) do { (texture_ptr)->gl_name = -1; } while (0)
#define renderTextureInit(texture_ptr) do { *(texture_ptr) = (RTexture){0}; } while (0)
//void renderTextureInit(RTexture *texture);
void renderTextureUpload(RTexture *texture, RTextureUploadParams params);

typedef struct {
	// FIXME GL
	// int gl_name;
	// int type;
	void *vkBuf;
} RBuffer;

typedef enum {
	RBufferType_Vertex,
	RBufferType_Index
} RBufferType;

int renderInit();


// FIXME if GL
void renderGlResize(int w, int h);
// FIXME elif VK
void renderVkSwapchainCreated(int w, int h);
void renderVkSwapchainDestroy();

void renderBufferCreate(RBuffer *buffer, RBufferType type, int size, const void *data);

struct BSPModel;
struct Camera;

void renderBegin();

typedef struct {
	const struct Camera *camera;
	struct AVec3f translation;
	int selected;
} RDrawParams;

void renderModelDraw(const RDrawParams *params, const struct BSPModel *model);

void renderEnd(const struct Camera *camera);
