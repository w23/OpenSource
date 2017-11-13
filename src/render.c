#include "render.h"
#include "texture.h"
#include "bsp.h"
#include "cache.h"
#include "common.h"
#include "profiler.h"
#include "atto/app.h"

#define RENDER_ERRORCHECK
//#define RENDER_GL_TRACE

#define RENDER_GL_PROFILE_FUNC profileEvent

#ifndef RENDER_ASSERT
#define RENDER_ASSERT(cond) \
	if (!(cond)) { \
		aAppDebugPrintf("ERROR @ %s:%d: (%s) failed", __FILE__, __LINE__, #cond); \
		aAppTerminate(-1); \
	}
#endif /* ifndef RENDER_ASSERT */

#ifdef RENDER_GL_PROFILE_FUNC
#define RENDER_GL_PROFILE_PREAMBLE const ATimeUs profile_time_start__ = aAppTime();
#define RENDER_GL_PROFILE_START const ATimeUs agl_profile_start_ = aAppTime();
#define RENDER_GL_PROFILE_END RENDER_GL_PROFILE_FUNC(__FUNCTION__, aAppTime() - agl_profile_start_);
#define RENDER_GL_PROFILE_END_NAME(name) RENDER_GL_PROFILE_FUNC(name, aAppTime() - agl_profile_start_);
#else
#define RENDER_GL_PROFILE_PREAMBLE
#define RENDER_GL_PROFILE_FUNC(...)
#endif

#if 0 //ndef RENDER_GL_DEBUG
#define GL_CALL(f) (f)
#else
#if 0
#include <stdlib.h> /* abort() */
static void a__GlPrintError(const char *message, int error) {
	const char *errstr = "UNKNOWN";
	switch (error) {
		case GL_INVALID_ENUM: errstr = "GL_INVALID_ENUM"; break;
		case GL_INVALID_VALUE: errstr = "GL_INVALID_VALUE"; break;
		case GL_INVALID_OPERATION: errstr = "GL_INVALID_OPERATION"; break;
#ifdef GL_STACK_OVERFLOW
		case GL_STACK_OVERFLOW: errstr = "GL_STACK_OVERFLOW"; break;
#endif
#ifdef GL_STACK_UNDERFLOW
		case GL_STACK_UNDERFLOW: errstr = "GL_STACK_UNDERFLOW"; break;
#endif
		case GL_OUT_OF_MEMORY: errstr = "GL_OUT_OF_MEMORY"; break;
#ifdef GL_TABLE_TOO_LARGE
		case GL_TABLE_TOO_LARGE: errstr = "GL_TABLE_TOO_LARGE"; break;
#endif
	};
	PRINTF("%s %s (%#x)", message, errstr, error);
}
#define RENDER_GL_GETERROR(f) \
		const int glerror = glGetError(); \
		if (glerror != GL_NO_ERROR) { \
			a__GlPrintError(__FILE__ ":" RENDER__GL_STR(__LINE__) ": " #f " returned ", glerror); \
			abort(); \
		}
#else
#define RENDER_GL_GETERROR(f)
#endif
#define RENDER__GL_STR__(s) #s
#define RENDER__GL_STR(s) RENDER__GL_STR__(s)
#ifdef RENDER_GL_TRACE
#define RENDER_GL_TRACE_PRINT PRINTF
#else
#define RENDER_GL_TRACE_PRINT(...)
#endif
#define GL_CALL(f) do{\
		RENDER_GL_TRACE_PRINT("%s", #f); \
		RENDER_GL_PROFILE_PREAMBLE \
		f; \
		RENDER_GL_PROFILE_FUNC(#f, aAppTime() - profile_time_start__); \
		RENDER_GL_GETERROR(f) \
	} while(0)
#endif /* RENDER_GL_DEBUG */

#ifdef _WIN32
#define WGL__FUNCLIST \
	WGL__FUNCLIST_DO(PFNGLGENBUFFERSPROC, GenBuffers) \
	WGL__FUNCLIST_DO(PFNGLBINDBUFFERPROC, BindBuffer) \
	WGL__FUNCLIST_DO(PFNGLBUFFERDATAPROC, BufferData) \
	WGL__FUNCLIST_DO(PFNGLGETATTRIBLOCATIONPROC, GetAttribLocation) \
	WGL__FUNCLIST_DO(PFNGLACTIVETEXTUREPROC, ActiveTexture) \
	WGL__FUNCLIST_DO(PFNGLCREATESHADERPROC, CreateShader) \
	WGL__FUNCLIST_DO(PFNGLSHADERSOURCEPROC, ShaderSource) \
	WGL__FUNCLIST_DO(PFNGLCOMPILESHADERPROC, CompileShader) \
	WGL__FUNCLIST_DO(PFNGLATTACHSHADERPROC, AttachShader) \
	WGL__FUNCLIST_DO(PFNGLDELETESHADERPROC, DeleteShader) \
	WGL__FUNCLIST_DO(PFNGLGETSHADERIVPROC, GetShaderiv) \
	WGL__FUNCLIST_DO(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog) \
	WGL__FUNCLIST_DO(PFNGLCREATEPROGRAMPROC, CreateProgram) \
	WGL__FUNCLIST_DO(PFNGLLINKPROGRAMPROC, LinkProgram) \
	WGL__FUNCLIST_DO(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog) \
	WGL__FUNCLIST_DO(PFNGLDELETEPROGRAMPROC, DeleteProgram) \
	WGL__FUNCLIST_DO(PFNGLGETPROGRAMIVPROC, GetProgramiv) \
	WGL__FUNCLIST_DO(PFNGLUSEPROGRAMPROC, UseProgram) \
	WGL__FUNCLIST_DO(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) \
	WGL__FUNCLIST_DO(PFNGLUNIFORM1FPROC, Uniform1f) \
	WGL__FUNCLIST_DO(PFNGLUNIFORM2FPROC, Uniform2f) \
	WGL__FUNCLIST_DO(PFNGLUNIFORM1IPROC, Uniform1i) \
	WGL__FUNCLIST_DO(PFNGLUNIFORMMATRIX4FVPROC, UniformMatrix4fv) \
	WGL__FUNCLIST_DO(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray) \
	WGL__FUNCLIST_DO(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer) \
	WGL__FUNCLIST_DO(PFNGLGENERATEMIPMAPPROC, GenerateMipmap) \

#define WGL__FUNCLIST_DO(T,N) T gl##N = 0;
WGL__FUNCLIST
#undef WGL__FUNCLIST_DO
#endif /* ifdef _WIN32 */

static GLint render_ShaderCreate(GLenum type, const char *sources[]) {
	int n;
	GLuint shader = glCreateShader(type);

	for (n = 0; sources[n]; ++n);

	GL_CALL(glShaderSource(shader, n, (const GLchar **)sources, 0));
	GL_CALL(glCompileShader(shader));

#ifdef RENDER_ERRORCHECK
	{
		GLint status;
		GL_CALL(glGetShaderiv(shader, GL_COMPILE_STATUS, &status));
		if (status != GL_TRUE) {
			char buffer[1024];
			GL_CALL(glGetShaderInfoLog(shader, sizeof(buffer), 0, buffer));
			PRINTF("Shader compilation error: %s", buffer);
			GL_CALL(glDeleteShader(shader));
			shader = 0;
		}
	}
#endif

	return shader;
}

void renderTextureCreate(RTexture *texture, RTextureCreateParams params) {
	GLenum internal, format, type;
	GL_CALL(glGenTextures(1, &texture->gl_name));

	switch (params.format) {
		case RTexFormat_RGB565:
			internal = format = GL_RGB; type = GL_UNSIGNED_SHORT_5_6_5; break;
	}
	GL_CALL(glBindTexture(GL_TEXTURE_2D, texture->gl_name));

	GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, internal, params.width, params.height, 0,
			format, type, params.pixels));

	GL_CALL(glGenerateMipmap(GL_TEXTURE_2D));

	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR));
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));

	texture->width = params.width;
	texture->height = params.height;
	texture->format = params.format;
}

void renderBufferCreate(RBuffer *buffer, RBufferType type, int size, const void *data) {
	GL_CALL(glGenBuffers(1, &buffer->gl_name));
	GL_CALL(glBindBuffer(type, buffer->gl_name));
	GL_CALL(glBufferData(type, size, data, GL_STATIC_DRAW));
}

typedef struct {
	const char *name;
	int components;
	GLenum type;
	GLint normalize;
	int stride;
	const void *ptr;
} RAttrib;

typedef struct {
	const char *name;
} RUniform;

#define RENDER_LIST_ATTRIBS \
	RENDER_DECLARE_ATTRIB(vertex, 3, GL_FLOAT, GL_FALSE) \
	RENDER_DECLARE_ATTRIB(lightmap_uv, 2, GL_FLOAT, GL_FALSE) \
	RENDER_DECLARE_ATTRIB(tex_uv, 2, GL_FLOAT, GL_FALSE) \
	RENDER_DECLARE_ATTRIB(average_color, 3, GL_UNSIGNED_BYTE, GL_TRUE) \

//	RENDER_DECLARE_ATTRIB(normal, 3, GL_FLOAT)

static const RAttrib attribs[] = {
#define RENDER_DECLARE_ATTRIB(n,c,t,N) \
	{"a_" # n, c, t, N, sizeof(struct BSPModelVertex), (void*)offsetof(struct BSPModelVertex, n)},
RENDER_LIST_ATTRIBS
#undef RENDER_DECLARE_ATTRIB
};

enum RAttribKinds {
#define RENDER_DECLARE_ATTRIB(n,c,t,N) \
	RAttribKind_ ## n,
RENDER_LIST_ATTRIBS
#undef RENDER_DECLARE_ATTRIB
	RAttribKind_COUNT
};

#define RENDER_LIST_UNIFORMS \
	RENDER_DECLARE_UNIFORM(mvp) \
	RENDER_DECLARE_UNIFORM(lmn) \
	RENDER_DECLARE_UNIFORM(lightmap) \
	RENDER_DECLARE_UNIFORM(tex0) \
	RENDER_DECLARE_UNIFORM(tex1) \
	RENDER_DECLARE_UNIFORM(tex0_size) \
	RENDER_DECLARE_UNIFORM(tex1_size) \

static const RUniform uniforms[] = {
#define RENDER_DECLARE_UNIFORM(n) {"u_" # n},
	RENDER_LIST_UNIFORMS
#undef RENDER_DECLARE_UNIFORM
};

enum RUniformKinds {
#define RENDER_DECLARE_UNIFORM(n) RUniformKind_ ## n,
	RENDER_LIST_UNIFORMS
#undef RENDER_DECLARE_UNIFORM
	RUniformKind_COUNT
};

typedef struct RProgram {
	GLint name;
	struct {
		const char *common, *vertex, *fragment;
	} shader_sources;
	int attrib_locations[RAttribKind_COUNT];
	int uniform_locations[RUniformKind_COUNT];
} RProgram;

enum {
	Program_LightmapColor,
	Program_LightmapTexture,
	Program_COUNT
};

static RProgram programs[Program_COUNT] = {
	/*LightmapColor*/
	{-1, {
			/*common*/
			"varying vec2 v_lightmap_uv;\n"
			"varying vec3 v_color;\n",
			/*vertex*/
			"attribute vec3 a_vertex, a_average_color;\n"
			"attribute vec2 a_lightmap_uv;\n"
			"uniform mat4 u_mvp;\n"
			"void main() {\n"
				"v_lightmap_uv = a_lightmap_uv;\n"
				"v_color = a_average_color;\n"
				"gl_Position = u_mvp * vec4(a_vertex, 1.);\n"
			"}\n",
			/*fragment*/
			"uniform sampler2D u_lightmap;\n"
			"void main() {\n"
				"gl_FragColor = vec4(v_color * (vec3(.1) + texture2D(u_lightmap, v_lightmap_uv).xyz), 1.);\n"
			"}\n"
			},
		{ -1 }, { -1 }
	},
	/*LightmapTexture*/
	{-1, {
			/*common*/
	"varying vec2 v_lightmap_uv, v_tex_uv;\n",
			/*vertex*/
	"attribute vec3 a_vertex;\n"
	"attribute vec2 a_lightmap_uv, a_tex_uv;\n"
	"uniform mat4 u_mvp;\n"
	"void main() {\n"
		"v_lightmap_uv = a_lightmap_uv;\n"
		"v_tex_uv = a_tex_uv;\n"
		"gl_Position = u_mvp * vec4(a_vertex, 1.);\n"
	"}\n",
			/*fragment*/
	"uniform sampler2D u_lightmap, u_tex0, u_tex1;\n"
	"uniform vec2 u_lightmap_size, u_tex0_size, u_tex1_size;\n"
	"uniform float u_lmn;\n"
	"void main() {\n"
		"vec3 tc = vec3(fract(v_tex_uv/u_tex0_size), 0.);\n"
		"vec4 albedo = texture2D(u_tex0, v_tex_uv/u_tex0_size);\n"
		"albedo = mix(albedo, texture2D(u_tex1, v_tex_uv/u_tex1_size), .0);\n"
		"vec3 lm = texture2D(u_lightmap, v_lightmap_uv).xyz;\n"
		"vec3 color = albedo.xyz * (vec3(.1) + lm);\n"
		"gl_FragColor = vec4(mix(color, tc, u_lmn), 1.);\n"
	"}\n"
			},
		{ -1 }, { -1 }
	},

};

static struct {
	const RTexture *current_tex0;

	const RProgram *current_program;
	struct {
		const float *mvp;
		float lmn;
	} uniforms;
} r;

static void renderApplyAttribs(const RAttrib *attribs, const RBuffer *buffer, unsigned int vbo_offset) {
	for(int i = 0; i < RAttribKind_COUNT; ++i) {
		const RAttrib *a = attribs + i;
		const int loc = r.current_program->attrib_locations[i];
		if (loc < 0) continue;
		GL_CALL(glEnableVertexAttribArray(loc));
		GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, buffer->gl_name));
		GL_CALL(glVertexAttribPointer(loc, a->components, a->type, a->normalize, a->stride, (const char*)a->ptr + vbo_offset * sizeof(struct BSPModelVertex)));
	}
}

static int render_ProgramUse(RProgram *prog) {
	if (r.current_program == prog)
		return 0;

	if (r.current_program) {
		for (int i = 0; i < RAttribKind_COUNT; ++i) {
			const int loc = r.current_program->attrib_locations[i];
			if (loc >= 0)
				GL_CALL(glDisableVertexAttribArray(loc));
		}
	}

	GL_CALL(glUseProgram(prog->name));
	GL_CALL(glUniform1i(prog->uniform_locations[RUniformKind_lightmap], 0));
	GL_CALL(glUniform1i(prog->uniform_locations[RUniformKind_tex0], 1));
	GL_CALL(glUniform1i(prog->uniform_locations[RUniformKind_tex1], 2));

	GL_CALL(glUniformMatrix4fv(prog->uniform_locations[RUniformKind_mvp], 1, GL_FALSE, r.uniforms.mvp));
	GL_CALL(glUniform1f(prog->uniform_locations[RUniformKind_lmn], r.uniforms.lmn));

	r.current_program = prog;
	r.current_tex0 = NULL;

	return 1;
}

static int render_ProgramInit(RProgram *prog) {
	GLuint program;
	GLuint vertex_shader, fragment_shader;
	const char *sources[] = {
		prog->shader_sources.common, prog->shader_sources.fragment, 0
	};
	fragment_shader = render_ShaderCreate(GL_FRAGMENT_SHADER, sources);
	if (fragment_shader == 0)
		return -1;

	sources[1] = prog->shader_sources.vertex;
	vertex_shader = render_ShaderCreate(GL_VERTEX_SHADER, sources);
	if (vertex_shader == 0) {
		GL_CALL(glDeleteShader(fragment_shader));
		return -2;
	}

	program = glCreateProgram();
	GL_CALL(glAttachShader(program, fragment_shader));
	GL_CALL(glAttachShader(program, vertex_shader));
	GL_CALL(glLinkProgram(program));

	GL_CALL(glDeleteShader(fragment_shader));
	GL_CALL(glDeleteShader(vertex_shader));

#ifdef RENDER_ERRORCHECK
	{
		GLint status;
		GL_CALL(glGetProgramiv(program, GL_LINK_STATUS, &status));
		if (status != GL_TRUE) {
			char buffer[1024];
			GL_CALL(glGetProgramInfoLog(program, sizeof(buffer), 0, buffer));
			PRINTF("Program linking error: %s", buffer);
			GL_CALL(glDeleteProgram(program));
			return -3;
		}
	}
#endif

	prog->name = program;

	for(int i = 0; i < RAttribKind_COUNT; ++i) {
		prog->attrib_locations[i] = glGetAttribLocation(prog->name, attribs[i].name);
		if (prog->attrib_locations[i] < 0)
			PRINTF("Cannot locate attribute %s", attribs[i].name);
	}

	for(int i = 0; i < RUniformKind_COUNT; ++i) {
		prog->uniform_locations[i] = glGetUniformLocation(prog->name, uniforms[i].name);
		if (prog->uniform_locations[i] < 0)
			PRINTF("Cannot locate uniform %s", uniforms[i].name);
	}

	return 0;
}

int renderInit() {
#ifdef _WIN32
#define WGL__FUNCLIST_DO(T, N) \
	gl##N = (T)wglGetProcAddress("gl" #N); \
	ASSERT(gl##N);

	WGL__FUNCLIST
#undef WGL__FUNCLIST_DO
#endif

	r.current_program = NULL;
	r.current_tex0 = NULL;
	r.uniforms.mvp = NULL;
	r.uniforms.lmn = 0;

	for (int i = 0; i < Program_COUNT; ++i) {
		if (render_ProgramInit(programs + i) != 0) {
			PRINTF("Cannot create program %d", i);
			return 0;
		}
	}

	struct Texture default_texture;
	RTextureCreateParams params;
	params.format = RTexFormat_RGB565;
	params.width = 2;
	params.height = 2;
	params.pixels = (uint16_t[]){0xffffu, 0, 0, 0xffffu};
	renderTextureCreate(&default_texture.texture, params);
	cachePutTexture("opensource/placeholder", &default_texture);

	{
		struct Material default_material;
		memset(&default_material, 0, sizeof default_material);
		default_material.average_color = aVec3f(0.f, 1.f, 0.f);
		default_material.shader = MaterialShader_LightmappedAverageColor;
		cachePutMaterial("opensource/placeholder", &default_material);
	} 

	{
		struct Material lightmap_color_material;
		memset(&lightmap_color_material, 0, sizeof lightmap_color_material);
		lightmap_color_material.average_color = aVec3f(0.f, 1.f, 0.f);
		lightmap_color_material.shader = MaterialShader_LightmappedAverageColor;
		cachePutMaterial("opensource/coarse", &lightmap_color_material);
	}

	GL_CALL(glEnable(GL_DEPTH_TEST));
	GL_CALL(glEnable(GL_CULL_FACE));
	r.current_tex0 = 0;
	return 1;
}

static int renderPrepareProgram(const struct BSPDraw *draw) {
	const struct Material *m = draw->material;

	int program_changed = 0;

	switch (m->shader) {
		case MaterialShader_LightmappedAverageColor:
			program_changed = render_ProgramUse(programs + Program_LightmapColor);
			break;
		case MaterialShader_LightmappedGeneric:
			program_changed = render_ProgramUse(programs + Program_LightmapTexture);
			break;
		default:
			ATTO_ASSERT(!"Impossible");
	}

	if (m->base_texture[0]) {
		const RTexture *t = &m->base_texture[0]->texture;
		if (t != r.current_tex0) {
			GL_CALL(glBindTexture(GL_TEXTURE_2D, t->gl_name));
			GL_CALL(glUniform2f(r.current_program->uniform_locations[RUniformKind_tex0_size], (float)t->width, (float)t->height));
			r.current_tex0 = t;
		}
	}

	return program_changed;
}

static void renderDrawSet(const struct BSPModel *model, const struct BSPDrawSet *drawset) {
	unsigned int vbo_offset = 0;
	for (int i = 0; i < drawset->draws_count; ++i) {
		const struct BSPDraw *draw = drawset->draws + i;

		if (renderPrepareProgram(draw) || i == 0 || draw->vbo_offset != vbo_offset) {
			vbo_offset = draw->vbo_offset;
			renderApplyAttribs(attribs, &model->vbo, draw->vbo_offset);
		}

		GL_CALL(glDrawElements(GL_TRIANGLES, draw->count, GL_UNSIGNED_SHORT, (void*)(sizeof(uint16_t) * draw->start)));
	}
}

static float aMaxf(float a, float b) { return a > b ? a : b; }
//static float aMinf(float a, float b) { return a < b ? a : b; }

void renderModelDraw(const struct AMat4f *mvp, struct AVec3f camera_position, float lmn, const struct BSPModel *model) {
	if (!model->detailed.draws_count) return;

	GL_CALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model->ibo.gl_name));
	GL_CALL(glActiveTexture(GL_TEXTURE0));
	GL_CALL(glBindTexture(GL_TEXTURE_2D, model->lightmap.gl_name));
	GL_CALL(glActiveTexture(GL_TEXTURE0 + 1));

	r.current_program = NULL;
	r.uniforms.mvp = &mvp->X.x;
	r.uniforms.lmn = lmn;

	const float distance =
		aMaxf(aMaxf(
			aMaxf(camera_position.x - model->aabb.max.x, model->aabb.min.x - camera_position.x),
			aMaxf(camera_position.y - model->aabb.max.y, model->aabb.min.y - camera_position.y)),
			aMaxf(camera_position.z - model->aabb.max.z, model->aabb.min.z - camera_position.z));

	/*
	PRINTF("%f %f %f -> %f",
			camera_position.x, camera_position.y, camera_position.z, distance);
	*/

	if (distance < 5000.f)
		renderDrawSet(model, &model->detailed);
	else
		renderDrawSet(model, &model->coarse);
}

void renderClear() {
	glClearColor(.5f,.4f,.2f,0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
