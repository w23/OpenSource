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

typedef struct RTexture {
	int width, height;
	RTexFormat format;
	GLuint gl_name;
} RTexture;

typedef struct {
	int width, height;
	RTexFormat format;
	const void *pixels;
} RTextureCreateParams;

void renderTextureCreate(RTexture *texture, RTextureCreateParams params);

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

void renderModelOptimize(struct BSPModel *model);
void renderModelDraw(const struct AMat4f *mvp, float lmn, const struct BSPModel *model);
