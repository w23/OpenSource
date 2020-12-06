#pragma once
#include "atto/math.h"
#include <stddef.h>

typedef enum {
	RTexFormat_RGB565,
#ifdef ATTO_PLATFORM_RPI
	RTexFormat_Compressed_ETC1,
#endif
	RTexFormat_Compressed_DXT1,
	RTexFormat_Compressed_DXT1_A1,
	RTexFormat_Compressed_DXT3,
	RTexFormat_Compressed_DXT5,
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

#ifdef ATTO_GL
	int gl_name;
	int type_flags;
#else
	void *vkImage, *vkImageView;
#endif

} RTexture;

typedef struct {
	void *ptr;
	size_t size;
} RStagingMemory;
RStagingMemory renderGetStagingBuffer(size_t size);
void renderFreeStagingBuffer(RStagingMemory mem);

typedef struct {
	int mip_level;
	int width, height;
	size_t offset;
} RTextureUploadMipmapData;

typedef struct {
	RTexType type;
	int width, height;
	RTexFormat format;

	RStagingMemory *staging;
	int mipmaps_count;
	RTextureUploadMipmapData *mipmaps;
} RTextureUploadParams;

RTexture renderTextureCreateAndUpload(RTextureUploadParams params);

typedef struct {
	// FIXME GL
	// int gl_name;
	// int type;
	//void *vkBuf;
	int index, offset;
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
