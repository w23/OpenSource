#pragma once
#include "atto/math.h"

#if !defined(ATTO_PLATFORM)
#include "atto/platform.h"
#endif

#if !defined(ATTO_GL_HEADERS_INCLUDED)
#ifdef ATTO_PLATFORM_X11
#define GL_GLEXT_PROTOTYPES 1
#include <GL/glx.h>
#include <GL/gl.h>
#include <GL/glext.h>
#define ATTO_GL_DESKTOP
#endif /* ifdef ATTO_PLATFORM_X11 */

#ifdef ATTO_PLATFORM_RPI
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#define ATTO_GL_ES
#endif /* ifdef ATTO_PLATFORM_RPI */

#ifdef ATTO_PLATFORM_WINDOWS
#include "libc.h"
#include <GL/gl.h>
#include <glext.h>
#define ATTO_GL_DESKTOP
#endif /* ifdef ATTO_PLATFORM_WINDOWS */

#ifdef ATTO_PLATFORM_OSX
#include <OpenGL/gl3.h>
#define ATTO_GL_DESKTOP
#endif
#endif /* if !defined(ATTO_GL_HEADERS_INCLUDED) */

int renderInit();
void renderClear();

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

typedef struct RTexture {
	int width, height;
	RTexFormat format;
	GLuint gl_name;
	int type_flags;
} RTexture;

typedef struct {
	RTexType type;
	int width, height;
	RTexFormat format;
	const void *pixels;
	int generate_mipmaps;
} RTextureUploadParams;

#define renderTextureInit(texture_ptr) do { (texture_ptr)->gl_name = (GLuint)-1; } while (0)
void renderTextureUpload(RTexture *texture, RTextureUploadParams params);

typedef struct {
	GLuint gl_name;
} RBuffer;

typedef enum {
	RBufferType_Vertex = GL_ARRAY_BUFFER,
	RBufferType_Index = GL_ELEMENT_ARRAY_BUFFER
} RBufferType;

void renderBufferCreate(RBuffer *buffer, RBufferType type, int size, const void *data);
//void renderBufferUpload(RBuffer *buffer, RBufferUpload upload);

struct BSPModel;

void renderModelDraw(const struct AMat4f *mvp, struct AVec3f camera_position, float lmn, const struct BSPModel *model);
