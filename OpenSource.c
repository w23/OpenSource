#include "bsp.h"
#include "collection.h"
#include "mempools.h"

#include "atto/app.h"
#define ATTO_GL_DEBUG
#define ATTO_GL_H_IMPLEMENT
#include "atto/gl.h"
#include "atto/math.h"

#include <string.h>
#include <stdlib.h> /* malloc */
#include <stddef.h> /* offsetof */

static char temp_data[64*1024*1024];

static struct TemporaryPool temp = {
	.storage = temp_data,
	.size = sizeof(temp_data),
	.cursor = 0
};

static void* stdpool_alloc(struct MemoryPool *pool, size_t size) {
	(void)(pool);
	return malloc(size);
}
static void stdpool_free(struct MemoryPool *pool, void *ptr) {
	(void)(pool);
	free(ptr);
}

static struct MemoryPool stdpool = {
	.alloc = stdpool_alloc,
	.free = stdpool_free
};

struct SimpleCamera {
	struct AVec3f pos, dir, up;
	struct AMat3f axes;
	struct AMat4f projection;
	struct AMat4f view_projection;
};

static void simplecamRecalc(struct SimpleCamera *cam) {
	cam->dir = aVec3fNormalize(cam->dir);
	const struct AVec3f
			z = aVec3fNeg(cam->dir),
			x = aVec3fNormalize(aVec3fCross(cam->up, z)),
			y = aVec3fCross(z, x);
	cam->axes = aMat3fv(x, y, z);
	const struct AMat3f axes_inv = aMat3fTranspose(cam->axes);
	cam->view_projection = aMat4fMul(cam->projection,
		aMat4f3(axes_inv, aVec3fMulMat(axes_inv, aVec3fNeg(cam->pos))));
}

static void simplecamProjection(struct SimpleCamera *cam, float near, float far, float horizfov, float aspect) {
	const float w = 2.f * near * tanf(horizfov / 2.f), h = w / aspect;
	//aAppDebugPrintf("%f %f %f %f -> %f %f", near, far, horizfov, aspect, w, h);
	cam->projection = aMat4fPerspective(near, far, w, h);
}

static void simplecamLookAt(struct SimpleCamera *cam, struct AVec3f pos, struct AVec3f at, struct AVec3f up) {
	cam->pos = pos;
	cam->dir = aVec3fNormalize(aVec3fSub(at, pos));
	cam->up = up;
	simplecamRecalc(cam);
}

static void simplecamMove(struct SimpleCamera *cam, struct AVec3f v) {
	cam->pos = aVec3fAdd(cam->pos, aVec3fMulf(cam->axes.X, v.x));
	cam->pos = aVec3fAdd(cam->pos, aVec3fMulf(cam->axes.Y, v.y));
	cam->pos = aVec3fAdd(cam->pos, aVec3fMulf(cam->axes.Z, v.z));
}

static void simplecamRotateYaw(struct SimpleCamera *cam, float yaw) {
	const struct AMat3f rot = aMat3fRotateAxis(cam->up, yaw);
	cam->dir = aVec3fMulMat(rot, cam->dir);
}

static void simplecamRotatePitch(struct SimpleCamera *cam, float pitch) {
	/* TODO limit pitch */
	const struct AMat3f rot = aMat3fRotateAxis(cam->axes.X, pitch);
	cam->dir = aVec3fMulMat(rot, cam->dir);
}

#define COUNTOF(v) (sizeof(v) / sizeof(*(v)))

static const float fsquad_vertices[] = {
	-1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f, -1.f
};

static const char fsquad_vertex_src[] =
	"attribute vec2 av2_pos;\n"
	"varying vec2 vv2_pos;\n"
	"void main() {\n"
		"vv2_pos = av2_pos;\n"
		"gl_Position = vec4(vv2_pos, 0., 1.);\n"
	"}\n";

static const char fsquad_fragment_src[] =
	"uniform sampler2D us2_tex;\n"
	"varying vec2 vv2_pos;\n"
	"void main() {\n"
		"gl_FragColor = texture2D(us2_tex, vv2_pos * .5 + .5);\n"
	"}\n";

static struct {
	AGLDrawSource source;
	AGLDrawMerge merge;
	AGLProgramUniform uniforms[2];
	AGLAttribute attrib_pos;
} fsquad;

static void fsquadInit() {
	fsquad.merge.depth.mode = AGLDM_Disabled;
	fsquad.merge.blend.enable = 0;

	fsquad.source.program = aGLProgramCreateSimple(fsquad_vertex_src, fsquad_fragment_src);
	if (fsquad.source.program < 1) {
		aAppDebugPrintf("Shader compilation error: %s", a_gl_error);
		aAppTerminate(-1);
	}

	fsquad.source.primitive.mode = GL_TRIANGLE_STRIP;
	fsquad.source.primitive.cull_mode = AGLCM_Disable;
	fsquad.source.primitive.front_face = AGLFF_CounterClockwise;
	fsquad.source.primitive.first = 0;
	fsquad.source.primitive.index.buffer = 0;
	fsquad.source.primitive.index.type = 0;
	fsquad.source.primitive.index.data.ptr = 0;
	fsquad.source.primitive.count = 4;

	fsquad.source.uniforms.p = fsquad.uniforms;
	fsquad.source.uniforms.n = COUNTOF(fsquad.uniforms);
	fsquad.source.attribs.p = &fsquad.attrib_pos;
	fsquad.source.attribs.n = 1;

	fsquad.attrib_pos.name = "av2_pos";
	fsquad.attrib_pos.size = 2;
	fsquad.attrib_pos.type = GL_FLOAT;
	fsquad.attrib_pos.normalized = GL_FALSE;
	fsquad.attrib_pos.stride = 2 * sizeof(float);
	fsquad.attrib_pos.ptr = fsquad_vertices;
	fsquad.attrib_pos.buffer = 0;

	fsquad.uniforms[0].name = "uf_aspect";
	fsquad.uniforms[0].type = AGLAT_Float;
	fsquad.uniforms[0].count = 1;

	fsquad.uniforms[1].name = "us2_tex";
	fsquad.uniforms[1].type = AGLAT_Texture;
	fsquad.uniforms[1].count = 1;
}

static void fsquadDraw(float aspect, const AGLTexture *tex, const AGLDrawTarget *target) {
	fsquad.uniforms[0].value.pf = &aspect;
	fsquad.uniforms[1].value.texture = tex;
	aGLDraw(&fsquad.source, &fsquad.merge, target);
}

static const float aabb_draw_vertices[] = {
	0.f, 0.f, 0.f,
	0.f, 0.f, 1.f,
	0.f, 1.f, 1.f,
	0.f, 1.f, 0.f,
	1.f, 0.f, 0.f,
	1.f, 0.f, 1.f,
	1.f, 1.f, 1.f,
	1.f, 1.f, 0.f
};

static const uint16_t aabb_draw_indices[] = {
	0, 1, 1, 2, 2, 3, 3, 0,
	4, 5, 5, 6, 6, 7, 7, 4,
	0, 4, 1, 5, 2, 6, 3, 7
};

static const char aabb_draw_vertex_src[] =
	"attribute vec3 av3_pos;\n"
	"uniform mat4 um4_MVP;\n"
	"uniform vec3 uv3_mul, uv3_add;\n"
	"varying vec3 vv3_pos;\n"
	"void main() {\n"
		"vv3_pos = av3_pos;\n"
		"gl_Position = um4_MVP * vec4(av3_pos * uv3_mul + uv3_add, 1.);\n"
	"}\n";

static const char aabb_draw_fragment_src[] =
	"uniform vec3 uv3_color;\n"
	"varying vec3 vv3_pos;\n"
	"void main() {\n"
		"gl_FragColor = vec4(vv3_pos * uv3_color, 1.);\n"
	"}\n";

enum { AABBDUniformMVP, AABBDUniformMul, AABBDUniformAdd, AABBDUniformColor, AABBDUniform_COUNT };

struct AABBDraw {
	struct AVec3f min, max, color;
	const struct AMat4f *mvp;
	const AGLDrawTarget *target;
};

static struct {
	AGLProgram program;
} aabb_draw;

static void aabbDraw(const struct AABBDraw *draw) {
	AGLDrawSource source;
	AGLDrawMerge merge;
	AGLProgramUniform uniforms[AABBDUniform_COUNT];
	AGLAttribute attrib_pos;
	const struct AVec3f mul = aVec3fSub(draw->max, draw->min);

	merge.depth.mode = AGLDM_Disabled;
	merge.blend.enable = 0;

	if (aabb_draw.program < 1) {
		aabb_draw.program = aGLProgramCreateSimple(aabb_draw_vertex_src, aabb_draw_fragment_src);
		if (aabb_draw.program < 1) {
			aAppDebugPrintf("Shader compilation error: %s", a_gl_error);
			aAppTerminate(-1);
		}
	}
	source.program = aabb_draw.program;

	source.primitive.mode = GL_LINES;
	source.primitive.cull_mode = AGLCM_Back;//AGLCM_Disable;
	source.primitive.front_face = AGLFF_CounterClockwise;
	source.primitive.first = 0;
	source.primitive.index.buffer = 0;
#if 1
	source.primitive.index.type = GL_UNSIGNED_SHORT;
	source.primitive.index.data.ptr = aabb_draw_indices;
	source.primitive.count = COUNTOF(aabb_draw_indices);
#else
	source.primitive.index.type = 0;//GL_UNSIGNED_SHORT;
	source.primitive.index.data.ptr = 0;//aabb_draw_indices;
	source.primitive.count = 8;(void)(aabb_draw_indices);//COUNTOF(aabb_draw_indices);
#endif

	source.uniforms.p = uniforms;
	source.uniforms.n = AABBDUniform_COUNT;
	source.attribs.p = &attrib_pos;
	source.attribs.n = 1;

	attrib_pos.name = "av3_pos";
	attrib_pos.size = 3;
	attrib_pos.type = GL_FLOAT;
	attrib_pos.normalized = GL_FALSE;
	attrib_pos.stride = 3 * sizeof(float);
	attrib_pos.ptr = aabb_draw_vertices;
	attrib_pos.buffer = 0;

	uniforms[AABBDUniformMVP].name = "um4_MVP";
	uniforms[AABBDUniformMVP].type = AGLAT_Mat4;
	uniforms[AABBDUniformMVP].count = 1;
	uniforms[AABBDUniformMVP].value.pf = &draw->mvp->X.x;

	uniforms[AABBDUniformMul].name = "uv3_mul";
	uniforms[AABBDUniformMul].type = AGLAT_Vec3;
	uniforms[AABBDUniformMul].count = 1;
	uniforms[AABBDUniformMul].value.pf = &mul.x;

	uniforms[AABBDUniformAdd].name = "uv3_add";
	uniforms[AABBDUniformAdd].type = AGLAT_Vec3;
	uniforms[AABBDUniformAdd].count = 1;
	uniforms[AABBDUniformAdd].value.pf = &draw->min.x;

	uniforms[AABBDUniformColor].name = "uv3_color";
	uniforms[AABBDUniformColor].type = AGLAT_Vec3;
	uniforms[AABBDUniformColor].count = 1;
	uniforms[AABBDUniformColor].value.pf = &draw->color.x;

	aGLDraw(&source, &merge, draw->target);
}

static const char vertex_src[] =
	"attribute vec3 av3_pos, av3_normal;\n"
	"attribute vec2 av2_lightmap;\n"
	"uniform mat4 um4_VP;\n"
	"varying vec2 vv2_lightmap;\n"
	"varying vec3 vv3_normal;\n"
	"void main() {\n"
		"vv2_lightmap = av2_lightmap;\n"
		"vv3_normal = av3_normal;\n"
		"gl_Position = um4_VP * vec4(av3_pos, 1.);\n"
	"}\n";

static const char fragment_src[] =
	"uniform sampler2D us2_lightmap;\n"
	"uniform vec2 uv2_lightmap_size;\n"
	"varying vec2 vv2_lightmap;\n"
	"varying vec3 vv3_normal;\n"
	"uniform float uf_lmn;\n"
	"void main() {\n"
		"gl_FragColor = mix(texture2D(us2_lightmap, vv2_lightmap), vec4(vv3_normal, 0.), uf_lmn);\n"
		//"gl_FragColor = texture2D(us2_lightmap, vv2_lightmap);\n"
	"}\n";

enum { UniformM, UniformVP, UniformLightmap, UniformLightmapSize, UniformLMN, Uniform_COUNT };
enum { AttribPos, AttribNormal, AttribLightmapUV, Attrib_COUNT };

static struct {
	struct SimpleCamera camera;
	int forward, right, run;
	struct AVec3f center;
	float R;
	float lmn;

	AGLDrawTarget screen;
	AGLDrawSource source;
	AGLDrawMerge merge;
	AGLAttribute attribs[Attrib_COUNT];
	AGLProgramUniform uniforms[Uniform_COUNT];
	struct BSPModel worldspawn;
	struct AMat4f model;
} g;

static void opensrcInit(struct ICollection *collection, const char *map) {
	g.source.program = aGLProgramCreateSimple(vertex_src, fragment_src);
	if (g.source.program < 1) {
		aAppDebugPrintf("Shader compilation error: %s", a_gl_error);
		aAppTerminate(-1);
	}
	fsquadInit();

	struct BSPLoadModelContext loadctx = {
		.collection = collection,
		.pool = &stdpool,
		.tmp = &temp,
		.model = &g.worldspawn
	};
	const enum BSPLoadResult result = bspLoadWorldspawn(loadctx, map);
	if (result != BSPLoadResult_Success) {
		aAppDebugPrintf("Cannot load map \"%s\": %d", map, result);
		aAppTerminate(-2);
	}

	aAppDebugPrintf("Loaded %s to %u draw calls", map, g.worldspawn.draws_count);
	aAppDebugPrintf("AABB (%f, %f, %f) - (%f, %f, %f)",
			g.worldspawn.aabb.min.x,
			g.worldspawn.aabb.min.y,
			g.worldspawn.aabb.min.z,
			g.worldspawn.aabb.max.x,
			g.worldspawn.aabb.max.y,
			g.worldspawn.aabb.max.z);

	g.center = aVec3fMulf(aVec3fAdd(g.worldspawn.aabb.min, g.worldspawn.aabb.max), .5f);
	g.R = aVec3fLength(aVec3fSub(g.worldspawn.aabb.max, g.worldspawn.aabb.min)) * .5f;

	aAppDebugPrintf("Center %f, %f, %f, R~=%f", g.center.x, g.center.y, g.center.z, g.R);

	g.model = aMat4fIdentity();

	g.source.primitive.mode = GL_TRIANGLES;
	g.source.primitive.first = 0;
	g.source.primitive.cull_mode = AGLCM_Back;
	g.source.primitive.front_face = AGLFF_CounterClockwise;
	g.source.primitive.index.type = GL_UNSIGNED_SHORT;

	g.source.uniforms.p = g.uniforms;
	g.source.uniforms.n = Uniform_COUNT;
	g.source.attribs.p = g.attribs;
	g.source.attribs.n = Attrib_COUNT;

	g.attribs[AttribPos].name = "av3_pos";
	g.attribs[AttribPos].size = 3;
	g.attribs[AttribPos].type = GL_FLOAT;
	g.attribs[AttribPos].normalized = GL_FALSE;
	g.attribs[AttribPos].stride = sizeof(struct BSPModelVertex);
	g.attribs[AttribPos].ptr = 0;

	g.attribs[AttribNormal].name = "av3_normal";
	g.attribs[AttribNormal].size = 3;
	g.attribs[AttribNormal].type = GL_FLOAT;
	g.attribs[AttribNormal].normalized = GL_FALSE;
	g.attribs[AttribNormal].stride = sizeof(struct BSPModelVertex);
	g.attribs[AttribNormal].ptr = (void*)offsetof(struct BSPModelVertex, normal);

	g.attribs[AttribLightmapUV].name = "av2_lightmap";
	g.attribs[AttribLightmapUV].size = 2;
	g.attribs[AttribLightmapUV].type = GL_FLOAT;
	g.attribs[AttribLightmapUV].normalized = GL_FALSE;
	g.attribs[AttribLightmapUV].stride = sizeof(struct BSPModelVertex);
	g.attribs[AttribLightmapUV].ptr = (void*)offsetof(struct BSPModelVertex, lightmap_uv);

	g.uniforms[UniformM].name = "um4_M";
	g.uniforms[UniformM].type = AGLAT_Mat4;
	g.uniforms[UniformM].count = 1;
	g.uniforms[UniformM].value.pf = &g.model.X.x;

	g.uniforms[UniformVP].name = "um4_VP";
	g.uniforms[UniformVP].type = AGLAT_Mat4;
	g.uniforms[UniformVP].count = 1;
	g.uniforms[UniformVP].value.pf = &g.camera.view_projection.X.x;

	g.uniforms[UniformLightmap].name = "us2_lightmap";
	g.uniforms[UniformLightmap].type = AGLAT_Texture;
	g.uniforms[UniformLightmap].count = 1;

	g.uniforms[UniformLightmapSize].name = "uv2_lightmap_size";
	g.uniforms[UniformLightmapSize].type = AGLAT_Vec2;
	g.uniforms[UniformLightmapSize].count = 1;

	g.uniforms[UniformLMN].name = "uf_lmn";
	g.uniforms[UniformLMN].type = AGLAT_Float;
	g.uniforms[UniformLMN].count = 1;
	g.uniforms[UniformLMN].value.pf = &g.lmn;
	g.lmn = 0;

	g.merge.blend.enable = 0;
	g.merge.depth.mode = AGLDM_TestAndWrite;
	g.merge.depth.func = AGLDF_Less;

	g.screen.framebuffer = 0;

	const float t = 0;
	simplecamLookAt(&g.camera,
			aVec3fAdd(g.center, aVec3fMulf(aVec3f(cosf(t*.5), sinf(t*.5), .25f), g.R*.5f)),
			g.center, aVec3f(0.f, 0.f, 1.f));
}

static void drawBSPDraw(const struct BSPDraw *draw) {
	g.source.primitive.index.data.offset = draw->start;
	g.source.primitive.count = draw->count;
	g.attribs[AttribPos].buffer =
		g.attribs[AttribNormal].buffer =
		g.attribs[AttribLightmapUV].buffer = &draw->vbo;

	aGLDraw(&g.source, &g.merge, &g.screen);
}

static void drawModel(const struct BSPModel *model) {
	const struct AVec2f lm_size = aVec2f(model->lightmap.width, model->lightmap.height);
	g.uniforms[UniformLightmap].value.texture = &model->lightmap;
	g.uniforms[UniformLightmapSize].value.pf = &lm_size.x;
	g.source.primitive.index.buffer = &model->ibo;

	for (unsigned int i = 0; i < model->draws_count; ++i)
		drawBSPDraw(model->draws + i);
}

static void opensrcResize(ATimeUs timestamp, unsigned int old_w, unsigned int old_h) {
	(void)(timestamp); (void)(old_w); (void)(old_h);
	g.screen.viewport.x = g.screen.viewport.y = 0;
	g.screen.viewport.w = a_app_state->width;
	g.screen.viewport.h = a_app_state->height;

	simplecamProjection(&g.camera, 1.f, g.R * 10.f, 3.1415926f/2.f, (float)a_app_state->width / (float)a_app_state->height);
	simplecamRecalc(&g.camera);
}

static void opensrcPaint(ATimeUs timestamp, float dt) {
	(void)(timestamp); (void)(dt);

	if (0) {
		const float t = timestamp * 1e-6f;
		simplecamLookAt(&g.camera,
				aVec3fAdd(g.center, aVec3fMulf(aVec3f(cosf(t*.5), sinf(t*.5), .25f), g.R*.5f)),
				g.center, aVec3f(0.f, 0.f, 1.f));
	} else {
		float move = dt * (g.run?3000.f:300.f);
		simplecamMove(&g.camera, aVec3f(g.right * move, 0.f, -g.forward * move));
		simplecamRecalc(&g.camera);
	}

	AGLClearParams clear;
	clear.r = clear.g = clear.b = 0;
	clear.depth = 1.f;
	clear.bits = AGLCB_ColorAndDepth;
	aGLClear(&clear, &g.screen);

	if (0)
		fsquadDraw(1., &g.worldspawn.lightmap, &g.screen);

	drawModel(&g.worldspawn);

	if (0) {
		const struct AABBDraw aabb = {
			.min = g.worldspawn.aabb.min,
			.max = g.worldspawn.aabb.max,
			.color = aVec3ff(1),
			.mvp = &g.camera.view_projection,
			.target = &g.screen
		};
		aabbDraw(&aabb);
	}
}

static void opensrcKeyPress(ATimeUs timestamp, AKey key, int pressed) {
	(void)(timestamp); (void)(key); (void)(pressed);
	//printf("KEY %u %d %d\n", timestamp, key, pressed);
	switch (key) {
	case AK_Esc:
		if (!pressed) break;
		if (a_app_state->grabbed)
			aAppGrabInput(0);
		else
			aAppTerminate(0);
		break;
	case AK_W: g.forward += pressed?1:-1; break;
	case AK_S: g.forward += pressed?-1:1; break;
	case AK_A: g.right += pressed?-1:1; break;
	case AK_D: g.right += pressed?1:-1; break;
	case AK_LeftShift: g.run = pressed; break;
	case AK_E: g.lmn = pressed; break;
	default: break;
	}
}

static void opensrcPointer(ATimeUs timestamp, int dx, int dy, unsigned int btndiff) {
	(void)(timestamp); (void)(dx); (void)(dy); (void)(btndiff);
	//printf("PTR %u %d %d %x\n", timestamp, dx, dy, btndiff);
	if (a_app_state->grabbed) {
		simplecamRotatePitch(&g.camera, dy * -4e-3f);
		simplecamRotateYaw(&g.camera, dx * -4e-3f);
		simplecamRecalc(&g.camera);
	} else if (btndiff)
		aAppGrabInput(1);
}

void attoAppInit(struct AAppProctable *proctable) {
	aGLInit();
	const int max_collections = 16;
	struct FilesystemCollection collections[max_collections];
	int free_collection = 0;
	const char *map = 0;

	for (int i = 1; i < a_app_state->argc; ++i) {
		const char *argv = a_app_state->argv[i];
		if (strcmp(argv, "-d") == 0) {
			if (i == a_app_state->argc - 1) {
				aAppDebugPrintf("-d requires an argument");
				goto print_usage_and_exit;
			}
			const char *value = a_app_state->argv[++i];

			if (free_collection >= max_collections) {
				aAppDebugPrintf("Too many fs collections specified: %s", value); 
				goto print_usage_and_exit;
			}

			filesystemCollectionCreate(collections + (free_collection++), value);
		} else {
			if (map) {
				aAppDebugPrintf("Only one map can be specified");
				goto print_usage_and_exit;
			}
			map = argv;
		}
	}

	if (!map || !free_collection) {
		aAppDebugPrintf("At least one map and one collection required");
		goto print_usage_and_exit;
	}

	for (int i = 0; i < free_collection - 1; ++i)
		collections[i].head.next = &collections[i+1].head;

	opensrcInit(&collections[0].head, map);

	proctable->resize = opensrcResize;
	proctable->paint = opensrcPaint;
	proctable->key = opensrcKeyPress;
	proctable->pointer = opensrcPointer;
	return;

print_usage_and_exit:
	aAppDebugPrintf("usage: %s <-d path> ... mapname", a_app_state->argv[0]);
	aAppTerminate(1);
}
