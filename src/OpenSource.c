#include "bsp.h"
#include "cache.h"
#include "collection.h"
#include "mempools.h"
#include "common.h"
#include "texture.h"
#include "profiler.h"
#include "camera.h"
#include "vmfparser.h"

#include "atto/app.h"
#include "atto/math.h"

static char persistent_data[128*1024*1024];
static char temp_data[128*1024*1024];

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

static struct Memories mem = {
	&stack_temp,
	&stack_persistent
};

typedef struct Map {
	char *name;
	int loaded;
	struct AVec3f offset;
	struct AVec3f debug_offset;
	struct BSPModel model;
	struct Map *next;
} Map;

typedef struct Patch {
	const char *map_name;
	int delete;
	BSPLandmark landmark;
	struct Patch *next;
} Patch;

static struct {
	struct Camera camera;
	int forward, right, run;
	struct AVec3f center;
	float R;

	struct ICollection *collection_chain;

	Patch *patches;

	Map *maps_begin, *maps_end;
	int maps_count, maps_limit;
	Map *selected_map;
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

	char *buffer = stackAlloc(&stack_persistent, sizeof(struct Map) + mapname_length + 1);
	if (!buffer) {
		PRINT("Not enough memory");
		return;
	}

	memcpy(buffer, mapname, mapname_length);
	buffer[mapname_length] = '\0';

	map = (void*)(buffer + mapname_length + 1);
	memset(map, 0, sizeof(*map));
	map->name = buffer;

	if (!g.maps_end)
		g.maps_begin = map;
	else
		g.maps_end->next = map;

	g.maps_end = map;

	++g.maps_count;

	PRINTF("Added new map to the queue: %.*s", mapname_length, mapname);
}

static void mapUpdatePosition(Map *map) {
	if (map == g.maps_begin && map->model.landmarks_count == 0)
		return;

	for (struct Map *map2 = g.maps_begin; map2; map2 = map2->next) {
		if (map2 == map || map2->loaded != 1)
			continue;

		for (int j = 0; j < map2->model.landmarks_count; ++j) {
			const struct BSPLandmark *m2 = map2->model.landmarks + j;
			for (int k = 0; k < map->model.landmarks_count; ++k) {
				const struct BSPLandmark *m1 = map->model.landmarks + k;
				if (strcmp(m1->name, m2->name) == 0) {
					map->offset = aVec3fAdd(aVec3fAdd(map2->offset, map2->debug_offset), aVec3fSub(m2->origin, m1->origin));
					PRINTF("Map %s landmark %s parent map %s offset %f, %f, %f",
						map->name, m1->name, map2->name, map->offset.x, map->offset.y, map->offset.z);
					return;
				} // if landmarks match
			} // for all landmarks of map 1
		} // for all landmarks of map 2
	} // for all maps (2)
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

	aAppDebugPrintf("Loaded %s to %u draw calls", map->name, map->model.detailed.draws_count);
	aAppDebugPrintf("AABB (%f, %f, %f) - (%f, %f, %f)",
			map->model.aabb.min.x,
			map->model.aabb.min.y,
			map->model.aabb.min.z,
			map->model.aabb.max.x,
			map->model.aabb.max.y,
			map->model.aabb.max.z);

	for (const Patch *p = g.patches; p; p = p->next) {
		if (p->delete || strcasecmp(p->map_name, map->name) != 0)
			continue;

		int found = 0;
		for (int i = 0; i < map->model.landmarks_count; ++i) {
			struct BSPLandmark *lm = map->model.landmarks + i;
			if (strcasecmp(p->landmark.name, lm->name) == 0) {
				found = 1;
				break;
			}
		}

		if (found)
			continue;

		if (map->model.landmarks_count == BSP_MAX_LANDMARKS) {
			PRINTF("Too many landmarks for map %s", map->name);
			break;
		}

		PRINTF("Injecting landmark %s %f %f %f to map %s",
			p->landmark.name,
			p->landmark.origin.x,
			p->landmark.origin.y,
			p->landmark.origin.z,
			map->name);
		memmove(map->model.landmarks + 1, map->model.landmarks, sizeof(BSPLandmark) * map->model.landmarks_count);
		map->model.landmarks[0] = p->landmark;
		++map->model.landmarks_count;
	}

	PRINTF("Landmarks: %d", map->model.landmarks_count);
	for (int i = 0; i < map->model.landmarks_count; ++i) {
		struct BSPLandmark *lm = map->model.landmarks + i;

		int deleted = 0;
		for (const Patch *p = g.patches; p; p = p->next) {
			if (strcasecmp(p->map_name, map->name) == 0 && strcasecmp(p->landmark.name, lm->name) == 0) {
				if (p->delete) {
					PRINTF("Deleting landmark %s", p->landmark.name);
					--map->model.landmarks_count;
					memmove(lm, lm + 1, sizeof(BSPLandmark) * (map->model.landmarks_count - i));
					deleted = 1;
				} else {
					PRINTF("Modifying landmark %s %f %f %f -> %f %f %f of map %s",
						p->landmark.name,
						p->landmark.origin.x,
						p->landmark.origin.y,
						p->landmark.origin.z,
						lm->origin.x,
						lm->origin.y,
						lm->origin.z,
						map->name);
					lm->origin = p->landmark.origin;
				}

				continue;
			}
		}

		if (deleted) {
			--i;
			continue;
		}

		PRINTF("\t%d: %s -> (%f, %f, %f)", i + 1, lm->name,
			lm->origin.x, lm->origin.y, lm->origin.z);
	}

	map->offset = map->debug_offset = aVec3ff(0);
	map->loaded = 1;
	mapUpdatePosition(map);

	return BSPLoadResult_Success;
}

static void opensrcInit() {
	cacheInit(&stack_persistent);

	if (!renderInit()) {
		PRINT("Failed to initialize render");
		aAppTerminate(-1);
	}

	bspInit();

	if (BSPLoadResult_Success != loadMap(g.maps_begin, g.collection_chain))
		aAppTerminate(-2);

	g.center = aVec3fMulf(aVec3fAdd(g.maps_begin->model.aabb.min, g.maps_begin->model.aabb.max), .5f);
	g.R = aVec3fLength(aVec3fSub(g.maps_begin->model.aabb.max, g.maps_begin->model.aabb.min)) * .5f;

	aAppDebugPrintf("Center %f, %f, %f, R~=%f", g.center.x, g.center.y, g.center.z, g.R);

	const float t = 0;
	cameraLookAt(&g.camera,
			aVec3fAdd(g.center, aVec3fMulf(aVec3f(cosf(t*.5f), sinf(t*.5f), .25f), g.R*.5f)),
			g.center, aVec3f(0.f, 0.f, 1.f));
}

static void opensrcResize(ATimeUs timestamp, unsigned int old_w, unsigned int old_h) {
	(void)(timestamp); (void)(old_w); (void)(old_h);
	renderResize(a_app_state->width, a_app_state->height);

	cameraProjection(&g.camera, 1.f, g.R * 20.f, 3.1415926f/2.f, (float)a_app_state->width / (float)a_app_state->height);
	cameraRecompute(&g.camera);
}

static void opensrcPaint(ATimeUs timestamp, float dt) {
	(void)(timestamp); (void)(dt);

	float move = dt * (g.run?3000.f:300.f);
	cameraMove(&g.camera, aVec3f(g.right * move, 0.f, -g.forward * move));
	cameraRecompute(&g.camera);

	renderBegin();

	int triangles = 0;
	int can_load_map = 1;
	for (struct Map *map = g.maps_begin; map; map = map->next) {
		if (map->loaded < 0)
			continue;

		if (map->loaded == 0) {
			if (can_load_map) {
				if (BSPLoadResult_Success != loadMap(map, g.collection_chain))
					map->loaded = -1;
			}

			can_load_map = 0;
			continue;
		}

		const RDrawParams params = {
			.camera = &g.camera,
			.translation = aVec3fAdd(map->offset, map->debug_offset),
			.selected = map == g.selected_map
		};

		renderModelDraw(&params, &map->model);

		for (int i = 0; i < map->model.detailed.draws_count; ++i)
			triangles += map->model.detailed.draws[i].count / 3;
	}

	renderEnd(&g.camera);

	if (profilerFrame(&stack_temp)) {
		PRINTF("Total triangles: %d", triangles);
	}
}

static void opensrcKeyPress(ATimeUs timestamp, AKey key, int pressed) {
	(void)(timestamp); (void)(key); (void)(pressed);
	//printf("KEY %u %d %d\n", timestamp, key, pressed);

	struct AVec3f map_offset = aVec3ff(0);
	int moved_map = 0;

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

	default: break;
	}

	if (pressed) {
		switch(key) {
		case AK_Up: map_offset.x += 1.f; moved_map = 1; break;
		case AK_Down: map_offset.x -= 1.f; moved_map = 1; break;
		case AK_Left: map_offset.y -= 1.f; moved_map = 1; break;
		case AK_Right: map_offset.y += 1.f; moved_map = 1; break;
		case AK_PageUp: map_offset.z += 1.f; moved_map = 1; break;
		case AK_PageDown: map_offset.z -= 1.f; moved_map = 1; break;
		case AK_Tab:
			g.selected_map = g.selected_map ? g.selected_map->next : g.maps_begin;
			if (g.selected_map)
				PRINTF("Selected map %s", g.selected_map->name);
			break;
		case AK_Q:
			g.selected_map = NULL;
			break;
		default: break;
		}

		if (moved_map && g.selected_map) {
			Map *map = g.selected_map;
			if (g.run) {
				map_offset.x *= 100.f;
				map_offset.y *= 100.f;
				map_offset.z *= 100.f;
			}
			map->debug_offset = aVec3fAdd(map->debug_offset, map_offset);
			PRINTF("Map %s offset: %f %f %f", map->name, map->debug_offset.x, map->debug_offset.y, map->debug_offset.z);

			for (Map *m = map; m; m = m->next)
				mapUpdatePosition(m);
		}
	}
}

static void opensrcPointer(ATimeUs timestamp, int dx, int dy, unsigned int btndiff) {
	(void)(timestamp); (void)(dx); (void)(dy); (void)(btndiff);
	//printf("PTR %u %d %d %x\n", timestamp, dx, dy, btndiff);
	if (a_app_state->grabbed) {
		cameraRotatePitch(&g.camera, dy * -4e-3f);
		cameraRotateYaw(&g.camera, dx * -4e-3f);
		cameraRecompute(&g.camera);
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

static void opensrcAddLandmarkPatch(StringView map, StringView key, StringView value) {
	if (key.length >= BSP_LANDMARK_NAME_LENGTH) {
		PRINTF(PRI_SV " is too long", PRI_SVV(key));
		return;
	}

	struct AVec3f origin = aVec3ff(0);
	// FIXME sscanf limit by value.length
	if (value.length >= 5 && 3 != sscanf(value.str, "%f %f %f", &origin.x, &origin.y, &origin.z)) {
		PRINTF(PRI_SV " format is wrong", PRI_SVV(value));
		return;
	}

	Patch *new_patch = stackAlloc(mem.persistent, sizeof(Patch));
	new_patch->next = g.patches;
	g.patches = new_patch;

	char *map_name = stackAlloc(mem.persistent, map.length + 1);
	memcpy(map_name, map.str, map.length);
	map_name[map.length] = '\0';
	new_patch->map_name = map_name;

	new_patch->delete = value.length < 5;

	memcpy(new_patch->landmark.name, key.str, key.length);
	new_patch->landmark.name[key.length] = '\0';
	new_patch->landmark.origin = origin;
}

typedef struct {
	StringView gamedir;
	StringView current_patched_map;
} Config;

static char *buildSteamPath(const StringView *gamedir, const StringView *path) {
	// FIXME windows and macos paths?
	const char *home_dir = getenv("HOME");
	const int home_dir_length = strlen(home_dir);

	const char steam_basedir[] = ".local/share/Steam/steamapps/common";
	const int steam_basedir_length = sizeof(steam_basedir) - 1;

	const int length = home_dir_length + steam_basedir_length + gamedir->length + path->length + 4;
	char *value = stackAlloc(&stack_temp, length);
	if (!value)
		return 0;

	int offset = 0;
	memcpy(value + offset, home_dir, home_dir_length); offset += home_dir_length;
	value[offset++] = '/';
	memcpy(value + offset, steam_basedir, steam_basedir_length); offset += steam_basedir_length;
	value[offset++] = '/';
	memcpy(value + offset, gamedir->str, gamedir->length); offset += gamedir->length; 
	value[offset++] = '/';
	memcpy(value + offset, path->str, path->length); offset += path->length;
	value[offset] = '\0';

	return value;
}

static VMFAction configPatchCallback(VMFState *state, VMFEntryType entry, const VMFKeyValue *kv);
static VMFAction configReadCallback(VMFState *state, VMFEntryType entry, const VMFKeyValue *kv);

static VMFAction configLandmarkCallback(VMFState *state, VMFEntryType entry, const VMFKeyValue *kv) {
	Config *cfg = state->user_data;

	switch (entry) {
	case VMFEntryType_KeyValue:
		opensrcAddLandmarkPatch(cfg->current_patched_map, kv->key, kv->value);
		break;
	case VMFEntryType_SectionClose:
		cfg->current_patched_map.length = 0;
		state->callback = configPatchCallback;
		break;
	default:
		return VMFAction_SemanticError;
	}

	return VMFAction_Continue;
}

static VMFAction configPatchCallback(VMFState *state, VMFEntryType entry, const VMFKeyValue *kv) {
	Config *cfg = state->user_data;

	switch (entry) {
	case VMFEntryType_SectionOpen:
		if (kv->key.length < 1)
			return VMFAction_SemanticError;
		cfg->current_patched_map = kv->key;
		state->callback = configLandmarkCallback;
		break;
	case VMFEntryType_SectionClose:
		return VMFAction_Exit;
	default:
		return VMFAction_SemanticError;
	}

	return VMFAction_Continue;
}

static VMFAction configReadCallback(VMFState *state, VMFEntryType entry, const VMFKeyValue *kv) {
	Config *cfg = state->user_data;

	switch (entry) {
	case VMFEntryType_KeyValue:
		if (strncasecmp("gamedir", kv->key.str, kv->key.length) == 0) {
			cfg->gamedir = kv->value;
		} else if (strncasecmp("vpk", kv->key.str, kv->key.length) == 0) {
			char *value = buildSteamPath(&cfg->gamedir, &kv->value);
			g.collection_chain = addToCollectionChain(g.collection_chain, collectionCreateVPK(&mem, value));
			stackFreeUpToPosition(&stack_temp, value);
		} else if (strncasecmp("dir", kv->key.str, kv->key.length) == 0) {
			char *value = buildSteamPath(&cfg->gamedir, &kv->value);
			g.collection_chain = addToCollectionChain(g.collection_chain, collectionCreateFilesystem(&mem, value));
			stackFreeUpToPosition(&stack_temp, value);
		} else if (strncasecmp("max_maps", kv->key.str, kv->key.length) == 0) {
			// FIXME null-terminate
			g.maps_limit = atoi(kv->value.str);
		} else if (strncasecmp("map", kv->key.str, kv->key.length) == 0) {
			openSourceAddMap(kv->value.str, kv->value.length);
		} 
		break;
	case VMFEntryType_SectionOpen:
		if (strncasecmp("patch_landmarks", kv->key.str, kv->key.length) != 0)
			return VMFAction_SemanticError;
		state->callback = configPatchCallback;
		break;
	default:
		return VMFAction_SemanticError;
	}

	return VMFAction_Continue;
}

static int configReadFile(const char *cfgfile) {
	AFile file;
	aFileReset(&file);
	if (AFile_Success != aFileOpen(&file, cfgfile))
		return 0;

	char *buffer = stackAlloc(&stack_temp, file.size); 
	if (!buffer)
		return 0;

	if (file.size != aFileReadAtOffset(&file, 0, file.size, buffer))
		return 0;

	Config config = {
		.gamedir = { .str = NULL, .length = 0 }
	};

	VMFState pstate = {
		.user_data = &config,
		.data = { .str = buffer, .length = file.size },
		.callback = configReadCallback
	};

	aFileClose(&file);

	int result = VMFResult_Success == vmfParse(&pstate);

	stackFreeUpToPosition(&stack_temp, buffer);
	return result;
}

void attoAppInit(struct AAppProctable *proctable) {
	profilerInit();
	//aGLInit();
	g.collection_chain = NULL;
	g.patches = NULL;
	g.maps_limit = 1;
	g.maps_count = 0;
	g.selected_map = NULL;

	for (int i = 1; i < a_app_state->argc; ++i) {
		const char *argv = a_app_state->argv[i];
		if (strcmp(argv, "-c") == 0) {
			if (i == a_app_state->argc - 1) {
				aAppDebugPrintf("-c requires an argument");
				goto print_usage_and_exit;
			}
			const char *value = a_app_state->argv[++i];

			PRINTF("Reading config file %s", value);
			if (!configReadFile(value)) {
				PRINTF("Cannot read config file %s", value);
				goto print_usage_and_exit;
			}
		} else if (strcmp(argv, "-p") == 0) {
			if (i == a_app_state->argc - 1) {
				aAppDebugPrintf("-p requires an argument");
				goto print_usage_and_exit;
			}
			const char *value = a_app_state->argv[++i];

			PRINTF("Adding vpk collection at %s", value);
			g.collection_chain = addToCollectionChain(g.collection_chain, collectionCreateVPK(&mem, value));
		} else if (strcmp(argv, "-d") == 0) {
			if (i == a_app_state->argc - 1) {
				aAppDebugPrintf("-d requires an argument");
				goto print_usage_and_exit;
			}
			const char *value = a_app_state->argv[++i];

			PRINTF("Adding dir collection at %s", value);
			g.collection_chain = addToCollectionChain(g.collection_chain, collectionCreateFilesystem(&mem, value));
		} else if (strcmp(argv, "-n") == 0) {
			if (i == a_app_state->argc - 1) {
				aAppDebugPrintf("-p requires an argument");
				goto print_usage_and_exit;
			}
			const char *value = a_app_state->argv[++i];

			g.maps_limit = atoi(value);
			if (g.maps_limit < 1)
				g.maps_limit = 1;
		} else {
			openSourceAddMap(argv, strlen(argv));
		}
	}

	if (!g.maps_count || !g.collection_chain) {
		aAppDebugPrintf("At least one map and one collection required");
		goto print_usage_and_exit;
	}

	opensrcInit();

	proctable->resize = opensrcResize;
	proctable->paint = opensrcPaint;
	proctable->key = opensrcKeyPress;
	proctable->pointer = opensrcPointer;
	return;

print_usage_and_exit:
	aAppDebugPrintf("usage: %s <-c config> <-p vpk> <-d path> ... <mapname0> <mapname1> ...", a_app_state->argv[0]);
	aAppTerminate(1);
}
