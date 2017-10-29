#include "bsp.h"
#include "cache.h"
#include "collection.h"
#include "mempools.h"
#include "common.h"
#include "texture.h"
#include "profiler.h"

#include "atto/app.h"
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

struct Map {
	char *name;
	int loaded;
	struct AVec3f offset;
	struct BSPModel model;
	struct Map *next;
};

static struct {
	struct SimpleCamera camera;
	int forward, right, run;
	struct AVec3f center;
	float R;
	float lmn;

	struct Map *maps_begin, *maps_end;
	int maps_count, maps_limit;
} g;

void openSourceAddMap(const char* mapname, int mapname_length) {
	if (g.maps_count >= g.maps_limit) {
		PRINTF("Map limit reached, not trying to add map %.*s",
				mapname_length, mapname);
		return;
	}

	struct Map *map = g.maps_begin;
	while (map) {
		if (strncmp(map->name, mapname, mapname_length) == 0)
			return;
		map = map->next;
	}

	char *mem = stackAlloc(&stack_persistent, sizeof(struct Map) + mapname_length + 1);
	if (!mem) {
		PRINT("Not enough memory");
		return;
	}

	memcpy(mem, mapname, mapname_length);
	mem[mapname_length] = '\0';

	map = (void*)(mem + mapname_length + 1);
	memset(map, 0, sizeof(*map));
	map->name = mem;

	if (!g.maps_end)
		g.maps_begin = map;
	else
		g.maps_end->next = map;

	g.maps_end = map;

	++g.maps_count;

	PRINTF("Added new map to the queue: %.*s", mapname_length, mapname);
}

static enum BSPLoadResult loadMap(struct Map *map, struct ICollection *collection) {
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

	if (map != g.maps_begin && map->model.landmarks_count != 0) {
		for (struct Map *map2 = g.maps_begin; map2; map2 = map2->next) {
			if (map2->loaded != 1)
				continue;

			for (int j = 0; j < map2->model.landmarks_count; ++j) {
				const struct BSPLandmark *m2 = map2->model.landmarks + j;
				for (int k = 0; k < map->model.landmarks_count; ++k) {
					const struct BSPLandmark *m1 = map->model.landmarks + k;
					if (strcmp(m1->name, m2->name) == 0) {
						map->offset = aVec3fAdd(map2->offset, aVec3fSub(m2->origin, m1->origin));
						PRINTF("Map %s offset %f, %f, %f",
							map->name, map->offset.x, map->offset.y, map->offset.z);
						return BSPLoadResult_Success;
					} // if landmarks match
				} // for all landmarks of map 1
			} // for all landmarks of map 2
		} // for all maps (2)
	} // if neet to position map

	return BSPLoadResult_Success;
}

static void opensrcInit(struct ICollection *collection, const char *map, int max_maps) {
	cacheInit(&stack_persistent);

	if (!renderInit()) {
		PRINT("Failed to initialize render");
		aAppTerminate(-1);
	}

	g.maps_limit = max_maps > 0 ? max_maps : 1;
	g.maps_count = 0;
	openSourceAddMap(map, strlen(map));

	for(struct Map *map = g.maps_begin; map; map = map->next)
		if (BSPLoadResult_Success != loadMap(map, collection) && map == g.maps_begin)
			aAppTerminate(-2);

	PRINTF("Maps loaded: %d", g.maps_count);

	g.center = aVec3fMulf(aVec3fAdd(g.maps_begin->model.aabb.min, g.maps_begin->model.aabb.max), .5f);
	g.R = aVec3fLength(aVec3fSub(g.maps_begin->model.aabb.max, g.maps_begin->model.aabb.min)) * .5f;

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

	for (struct Map *map = g.maps_begin; map; map = map->next) {
		if (!map->loaded)
			continue;

		const struct AMat4f mvp = aMat4fMul(g.camera.view_projection, aMat4fTranslation(map->offset));

		renderModelDraw(&mvp, g.lmn, &map->model);

		if (profilerFrame(&stack_temp)) {
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

static struct ICollection *addToCollectionChain(struct ICollection *chain, struct ICollection *next) {
	if (chain) {
		struct ICollection *coll = chain;
		while (coll->next)
			coll = coll->next;
		coll->next = next;
		return chain;
	}

	return next;
}

void attoAppInit(struct AAppProctable *proctable) {
	profilerInit();
	//aGLInit();
	struct ICollection *collection_chain = NULL;
	const char *map = 0;
	int max_maps = 1;

	struct Memories mem = {
		&stack_temp,
		&stack_persistent
	};

	for (int i = 1; i < a_app_state->argc; ++i) {
		const char *argv = a_app_state->argv[i];
		if (strcmp(argv, "-p") == 0) {
			if (i == a_app_state->argc - 1) {
				aAppDebugPrintf("-p requires an argument");
				goto print_usage_and_exit;
			}
			const char *value = a_app_state->argv[++i];

			collection_chain = addToCollectionChain(collection_chain, collectionCreateVPK(&mem, value));
		} else if (strcmp(argv, "-d") == 0) {
			if (i == a_app_state->argc - 1) {
				aAppDebugPrintf("-d requires an argument");
				goto print_usage_and_exit;
			}
			const char *value = a_app_state->argv[++i];

			collection_chain = addToCollectionChain(collection_chain, collectionCreateFilesystem(&mem, value));
		} else if (strcmp(argv, "-n") == 0) {
			if (i == a_app_state->argc - 1) {
				aAppDebugPrintf("-p requires an argument");
				goto print_usage_and_exit;
			}
			const char *value = a_app_state->argv[++i];

			max_maps = atoi(value);
		} else {
			if (map) {
				aAppDebugPrintf("Only one map can be specified");
				goto print_usage_and_exit;
			}
			map = argv;
		}
	}

	if (!map || !collection_chain) {
		aAppDebugPrintf("At least one map and one collection required");
		goto print_usage_and_exit;
	}

	opensrcInit(collection_chain, map, max_maps);

	proctable->resize = opensrcResize;
	proctable->paint = opensrcPaint;
	proctable->key = opensrcKeyPress;
	proctable->pointer = opensrcPointer;
	return;

print_usage_and_exit:
	aAppDebugPrintf("usage: %s <-d path> ... mapname", a_app_state->argv[0]);
	aAppTerminate(1);
}
