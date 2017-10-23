#include "bsp.h"
#include "cache.h"
#include "collection.h"
#include "mempools.h"
#include "common.h"
#include "texture.h"

#include "atto/app.h"

void profileEvent(const char *msg, ATimeUs delta);

//#define ATTO_GL_PROFILE_FUNC profileEvent
//#define ATTO_GL_TRACE
//#define ATTO_GL_DEBUG
//#define ATTO_GL_H_IMPLEMENT
//#include "atto/gl.h"
#include "atto/math.h"

static char persistent_data[32*1024*1024];
static char temp_data[32*1024*1024];

static struct Stack stack_temp = {
	.storage = temp_data,
	.size = sizeof(temp_data),
	.cursor = 0
};

static struct Stack stack_persistent = {
	.storage = persistent_data,
	.size = sizeof(persistent_data),
	.cursor = 0
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

static struct {
	int cursor;
	struct {
		const char *msg;
		ATimeUs delta;
	} event[65536];
	int frame;
	ATimeUs last_print_time;
	ATimeUs profiler_time;
	ATimeUs frame_deltas, last_frame;
	int counted_frame;
} profiler;

static void profilerInit() {
	profiler.cursor = 0;
	profiler.frame = 0;
	profiler.last_print_time = 0;
	profiler.profiler_time = 0;
	profiler.frame_deltas = profiler.last_frame = 0;
	profiler.counted_frame = 0;
}

void profileEvent(const char *msg, ATimeUs delta) {
	ATTO_ASSERT(profiler.cursor < 65536);
	profiler.event[profiler.cursor].msg = msg;
	profiler.event[profiler.cursor].delta = delta;
	++profiler.cursor;
}

typedef struct {
	const char *name;
	int count;
	ATimeUs total_time;
	ATimeUs min_time;
	ATimeUs max_time;
} ProfilerLocation;

static int profilerFrame() {
	int retval = 0;
	const ATimeUs start = aAppTime();
	profiler.frame_deltas += start - profiler.last_frame;
	
	void *tmp_cursor = stackGetCursor(&stack_temp);
	const int max_array_size = stackGetFree(&stack_temp) / sizeof(ProfilerLocation);
	int array_size = 0;
	ProfilerLocation *array = tmp_cursor;
	int total_time = 0;
	for (int i = 0; i < profiler.cursor; ++i) {
		ProfilerLocation *loc = 0;
		for (int j = 0; j < array_size; ++j)
			if (array[j].name == profiler.event[i].msg) {
				loc = array + j;
				break;
			}
		if (!loc) {
			ATTO_ASSERT(array_size< max_array_size);
			loc = array + array_size++;
			loc->name = profiler.event[i].msg;
			loc->count = 0;
			loc->total_time = 0;
			loc->min_time = 0x7fffffffu;
			loc->max_time = 0;
		}

		++loc->count;
		const ATimeUs delta = profiler.event[i].delta;
		loc->total_time += delta;
		total_time += delta;
		if (delta < loc->min_time) loc->min_time = delta;
		if (delta > loc->max_time) loc->max_time = delta;
	}

	++profiler.counted_frame;
	++profiler.frame;

	if (start - profiler.last_print_time > 60000000) {
		PRINT("=================================================");
		const ATimeUs dt = profiler.frame_deltas / profiler.counted_frame;
		PRINTF("avg frame = %dus (fps = %f)", dt, 1000000. / dt);
		PRINTF("PROF: frame=%d, total_frame_time=%d total_prof_time=%d, avg_prof_time=%d events=%d unique=%d",
			profiler.frame, total_time, profiler.profiler_time, profiler.profiler_time / profiler.frame,
			profiler.cursor, array_size);

	for (int i = 0; i < array_size; ++i) {
		const ProfilerLocation *loc = array + i;
		PRINTF("T%d: total=%d count=%d min=%d max=%d, avg=%d %s",
				i, loc->total_time, loc->count, loc->min_time, loc->max_time,
				loc->total_time / loc->count, loc->name);
	}

#if 0
#define TOP_N 10
		int max_time[TOP_N] = {0};
		int max_count[TOP_N] = {0};
		for (int i = 0; i < array_size; ++i) {
			const ProfilerLocation *loc = array + i;
			for (int j = 0; j < TOP_N; ++j)
				if (array[max_time[j]].max_time < loc->max_time) {
					for (int k = j + 1; k < TOP_N; ++k) max_time[k] = max_time[k - 1];
					max_time[j] = i;
					break;
				}
			for (int j = 0; j < TOP_N; ++j)
				if (array[max_count[j]].count < loc->count) {
					for (int k = j + 1; k < TOP_N; ++k) max_count[k] = max_count[k - 1];
					max_count[j] = i;
					break;
				}
		}
		if (array_size > TOP_N) {
			for (int i = 0; i < TOP_N; ++i) {
				const ProfilerLocation *loc = array + max_time[i];
				PRINTF("T%d %d: total=%d count=%d min=%d max=%d, avg=%d %s",
						i, max_time[i], loc->total_time, loc->count, loc->min_time, loc->max_time,
						loc->total_time / loc->count, loc->name);
			}
			for (int i = 0; i < TOP_N; ++i) {
				const ProfilerLocation *loc = array + max_count[i];
				PRINTF("C%d %d: total=%d count=%d min=%d max=%d, avg=%d %s",
						i, max_count[i], loc->total_time, loc->count, loc->min_time, loc->max_time,
						loc->total_time / loc->count, loc->name);
			}
		}
#endif

		profiler.last_print_time = start;
		profiler.counted_frame = 0;
		profiler.frame_deltas = 0;
		retval = 1;
	}

	profiler.last_frame = start;
	profiler.profiler_time += aAppTime() - start;
	profiler.cursor = 0;
	profileEvent("PROFILER", aAppTime() - start);
	return retval;
}

#define MAX_MAP_NAME_LENGTH 32
struct Map {
	char name[MAX_MAP_NAME_LENGTH];
	int loaded;
	struct AVec3f offset;
	struct BSPModel model;
};

#define MAX_MAP_COUNT 32

static struct {
	struct SimpleCamera camera;
	int forward, right, run;
	struct AVec3f center;
	float R;
	float lmn;

	struct Map maps[MAX_MAP_COUNT];
	int maps_count;
} g;

void openSourceAddMap(const char* mapname, int mapname_length) {
	for (int i = 0; i < g.maps_count; ++i)
		if (strncmp(g.maps[i].name, mapname, mapname_length) == 0)
			return;

	if (g.maps_count >= MAX_MAP_COUNT) {
		PRINTF("No slots left for map %.*s", mapname_length, mapname);
		return;
	}

	if (mapname_length >= MAX_MAP_NAME_LENGTH) {
		PRINTF("Map name \"%.*s\" is too long", mapname_length, mapname);
		return;
	}

	memcpy(g.maps[g.maps_count].name, mapname, mapname_length);

	++g.maps_count;

	PRINTF("Added new map to the queue: %.*s", mapname_length, mapname);
}

static enum BSPLoadResult loadMap(int index, struct ICollection *collection) {
	struct Map *map = g.maps + index;
	struct BSPLoadModelContext loadctx = {
		.collection = collection,
		.persistent = &stack_persistent,
		.tmp = &stack_temp,
		.model = &map->model
	};

	const enum BSPLoadResult result = bspLoadWorldspawn(loadctx, map->name);
	if (result != BSPLoadResult_Success) {
		PRINTF("Cannot load map \"%s\": %d", map->name, result);
		return result;
	}

	aAppDebugPrintf("Loaded %s to %u draw calls", map->name, map->model.draws_count);
	aAppDebugPrintf("AABB (%f, %f, %f) - (%f, %f, %f)",
			map->model.aabb.min.x,
			map->model.aabb.min.y,
			map->model.aabb.min.z,
			map->model.aabb.max.x,
			map->model.aabb.max.y,
			map->model.aabb.max.z);

	PRINTF("Landmarks: %d", map->model.landmarks_count);
	for (int i = 0; i < map->model.landmarks_count; ++i) {
		struct BSPLandmark *lm = map->model.landmarks + i;
		PRINTF("\t%d: %s -> (%f, %f, %f)", i + 1, lm->name,
			lm->origin.x, lm->origin.y, lm->origin.z);
	}

	map->loaded = 1;

	if (index != 0 && map->model.landmarks_count != 0) {
		for (int i = 0; i < index; ++i) {
			const struct Map *map2 = g.maps + i;
			if (map2->loaded == 1) {
				for (int j = 0; j < map2->model.landmarks_count; ++j) {
					const struct BSPLandmark *m2 = map2->model.landmarks + j;
					for (int k = 0; k < map->model.landmarks_count; ++k) {
						const struct BSPLandmark *m1 = map->model.landmarks + k;
						if (strcmp(m1->name, m2->name) == 0) {
							map->offset = aVec3fAdd(map2->offset, aVec3fSub(m2->origin, m1->origin));
							PRINTF("Map %s offset %f, %f, %f", 
								map->name, map->offset.x, map->offset.y, map->offset.z);
							return BSPLoadResult_Success;
						}
					}
				}
			}
		}
	}

	return BSPLoadResult_Success;
}

static void opensrcInit(struct ICollection *collection, const char *map) {
	cacheInit(&stack_persistent);

	if (!renderInit()) {
		PRINT("Failed to initialize render");
		aAppTerminate(-1);
	}

	g.maps_count = 1;
	memset(g.maps, 0, sizeof(g.maps));
	strncpy(g.maps[0].name, map, sizeof(g.maps[0].name)-1);

	for (int i = 0; i < g.maps_count; ++i)
		if (BSPLoadResult_Success != loadMap(i, collection) && i == 0)
			aAppTerminate(-2);

	PRINTF("Maps loaded: %d", g.maps_count);

	g.center = aVec3fMulf(aVec3fAdd(g.maps[0].model.aabb.min, g.maps[0].model.aabb.max), .5f);
	g.R = aVec3fLength(aVec3fSub(g.maps[0].model.aabb.max, g.maps[0].model.aabb.min)) * .5f;

	aAppDebugPrintf("Center %f, %f, %f, R~=%f", g.center.x, g.center.y, g.center.z, g.R);

	const float t = 0;
	simplecamLookAt(&g.camera,
			aVec3fAdd(g.center, aVec3fMulf(aVec3f(cosf(t*.5), sinf(t*.5), .25f), g.R*.5f)),
			g.center, aVec3f(0.f, 0.f, 1.f));
}

static void opensrcResize(ATimeUs timestamp, unsigned int old_w, unsigned int old_h) {
	(void)(timestamp); (void)(old_w); (void)(old_h);
	glViewport(0, 0, a_app_state->width, a_app_state->height);

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

	renderClear();

	for (int i = 0; i < g.maps_count; ++i) {
		struct Map *map = g.maps + i;
		if (!map->loaded)
			continue;

		const struct AMat4f mvp = aMat4fMul(g.camera.view_projection, aMat4fTranslation(map->offset));

		renderModelDraw(&mvp, g.lmn, &map->model);

		if (profilerFrame()) {
			int triangles = 0;
			for (int i = 0; i < map->model.draws_count; ++i) {
				triangles += map->model.draws[i].count / 3;
			}
			PRINTF("Total triangles: %d", triangles);
		}
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
	profilerInit();
	//aGLInit();
	const int max_collections = 16;
	struct FilesystemCollection collections[max_collections];
	int free_collection = 0;
	const char *map = 0;

	for (int i = 1; i < a_app_state->argc; ++i) {
		const char *argv = a_app_state->argv[i];
		if (strcmp(argv, "-p") == 0) {
			if (i == a_app_state->argc - 1) {
				aAppDebugPrintf("-p requires an argument");
				goto print_usage_and_exit;
			}
			const char *value = a_app_state->argv[++i];

			if (free_collection >= max_collections) {
				aAppDebugPrintf("Too many collections specified: %s", value);
				goto print_usage_and_exit;
			}

			struct VPKCollection vc;
			vpkCollectionCreate(&vc, value, &stack_persistent, &stack_temp);
		} else if (strcmp(argv, "-d") == 0) {
			if (i == a_app_state->argc - 1) {
				aAppDebugPrintf("-d requires an argument");
				goto print_usage_and_exit;
			}
			const char *value = a_app_state->argv[++i];

			if (free_collection >= max_collections) {
				aAppDebugPrintf("Too many collections specified: %s", value);
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
