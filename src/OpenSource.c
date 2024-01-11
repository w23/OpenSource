#include "bsp.h"
#include "cache.h"
#include "collection.h"
#include "mempools.h"
#include "common.h"
#include "log.h"
//#include "profiler.h"
#include "camera.h"
#include "vmfparser.h"

#include "atto/app.h"
#include "atto/math.h"

#ifdef _MSC_VER
#pragma warning(disable:4221)
#endif

static char persistent_data[128*1024*1024];
static char temp_data[256*1024*1024];

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

typedef enum {
	MapFlags_Empty = 0,
	MapFlags_Loaded = 1,
	MapFlags_FixedOffset = 2,
	MapFlags_Broken = 4
} MapFlags;

typedef struct Map {
	char *name;
	char *depend_name;
	int flags;
	struct AVec3f offset;
	struct AVec3f debug_offset;
	struct BSPModel model;
	struct Map *prev, *next;
	const struct Map *parent;
	struct AVec3f parent_offset;
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

static struct {
	const char *steam_basedir;
	int maps_limit;
} g_cfg;

static Map *opensrcAllocMap(StringView name) {
	if (g.maps_count >= g_cfg.maps_limit) {
		PRINTF("Map limit reached, not trying to add map " PRI_SV, PRI_SVV(name));
		return NULL;
	}

	Map *map = g.maps_begin;
	while (map) {
		if (strncmp(map->name, name.str, name.length) == 0)
			return map;
		map = map->next;
	}

	const int total_size = sizeof(Map) + name.length + 1;
	char *buffer = stackAlloc(&stack_persistent, total_size);
	if (!buffer) {
		PRINT("Not enough memory");
		return NULL;
	}

	memset(buffer, 0, total_size);

	map = (void*)buffer;
	map->name = buffer + sizeof(Map);
	map->depend_name = map->name + name.length + 1;

	memcpy(map->name, name.str, name.length);

	if (!g.maps_end)
		g.maps_begin = map;
	else {
		g.maps_end->next = map;
		map->prev = g.maps_end;
	}

	g.maps_end = map;

	++g.maps_count;

	PRINTF("Added new map to the queue: " PRI_SV, PRI_SVV(name));
	return map;
}

void openSourceAddMap(StringView name) {
	opensrcAllocMap(name);
}

static void mapUpdatePosition(Map *map) {
	if (map->parent && !(map->flags & MapFlags_FixedOffset))
		map->offset = aVec3fAdd(map->parent_offset, aVec3fAdd(map->parent->offset, map->parent->debug_offset));

	PRINTF("Map %s global_offset %f %f %f", map->name,
		map->offset.x, map->offset.y, map->offset.z);
}

static enum BSPLoadResult loadMap(Map *map, ICollection *collection) {
	BSPLoadModelContext loadctx = {
		.collection = collection,
		.persistent = &stack_persistent,
		.tmp = &stack_temp,
		.model = &map->model,
		.name = { .str = map->name, .length = (int)strlen(map->name) },
		.prev_map_name = { .str = NULL, .length = 0 },
		.next_map_name = { .str = NULL, .length = 0 },
	};

	if (map->prev) {
		loadctx.prev_map_name.str = map->prev->name;
		loadctx.prev_map_name.length = (int)strlen(map->prev->name);
	}

	if (map->next) {
		loadctx.next_map_name.str = map->next->name;
		loadctx.next_map_name.length = (int)strlen(map->next->name);
	}

	const enum BSPLoadResult result = bspLoadWorldspawn(loadctx);
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
					memmove(lm, lm + 1, sizeof(BSPLandmark) * ((size_t)map->model.landmarks_count - (size_t)i));
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

	if (map != g.maps_begin && map->model.landmarks_count > 0 && !(map->flags & MapFlags_FixedOffset)) {
		for (int k = 0; k < map->model.landmarks_count; ++k) {
			const struct BSPLandmark *m1 = map->model.landmarks + k;

			for (struct Map *map2 = g.maps_begin; map2; map2 = map2->next) {
				if (map2 == map || !(map2->flags & MapFlags_Loaded))
					continue;

				for (int j = 0; j < map2->model.landmarks_count; ++j) {
					const struct BSPLandmark *m2 = map2->model.landmarks + j;
					if (strcmp(m1->name, m2->name) == 0) {
						map->parent = map2;
						map->parent_offset = aVec3fSub(m2->origin, m1->origin);
						PRINTF("Map %s parent: %s", map->name, map2->name); 
						goto loaded;
					} // if landmarks match
				} // for all landmarks of map 2
			} // for all maps (2)
		} // for all landmarks of map 1
	}

loaded:
	map->flags |= MapFlags_Loaded;
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
	float r = aVec3fLength(aVec3fSub(g.maps_begin->model.aabb.max, g.maps_begin->model.aabb.min)) * .5f;

	if (g.R < 10000.f) {
		g.R = r * 30.f;
	}

	aAppDebugPrintf("Center %f, %f, %f, R~=%f", g.center.x, g.center.y, g.center.z, r);

	const float t = 0;
	cameraLookAt(&g.camera,
			aVec3fAdd(g.center, aVec3fMulf(aVec3f(cosf(t*.5f), sinf(t*.5f), .25f), r*.5f)),
			g.center, aVec3f(0.f, 0.f, 1.f));
}

static void opensrcResize(ATimeUs timestamp, unsigned int old_w, unsigned int old_h) {
	(void)(timestamp); (void)(old_w); (void)(old_h);
	renderResize(a_app_state->width, a_app_state->height);

	cameraProjection(&g.camera, 1.f, g.R, 3.1415926f/2.f, (float)a_app_state->width / (float)a_app_state->height);
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
		if (map->flags & MapFlags_Broken)
			continue;

		if (!(map->flags & MapFlags_Loaded)) {
			if (can_load_map) {
				if (BSPLoadResult_Success != loadMap(map, g.collection_chain))
					map->flags |= MapFlags_Broken;
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

	// if (profilerFrame(&stack_temp)) {
	// 	PRINTF("Total triangles: %d", triangles);
	// }
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
			// TODO graceful termination
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

	char *map_name = stackAlloc(mem.persistent, (size_t)map.length + 1);
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
	StringView map_name;
	StringView map_offset;
} Config;

static char *buildSteamPath(const StringView *gamedir, const StringView *path) {
	const int steam_basedir_length = (int)strlen(g_cfg.steam_basedir);

	const int length = steam_basedir_length + gamedir->length + path->length + 4;
	char *value = stackAlloc(&stack_temp, length);
	if (!value)
		return 0;

	int offset = 0;
	memcpy(value + offset, g_cfg.steam_basedir, steam_basedir_length); offset += steam_basedir_length;
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
		opensrcAddLandmarkPatch(cfg->map_name, kv->key, kv->value);
		break;
	case VMFEntryType_SectionClose:
		cfg->map_name.length = 0;
		state->callback = configPatchCallback;
		break;
	default:
		PRINTF("%s: Unexpected entry %d", __FUNCTION__, entry);
		return VMFAction_SemanticError;
	}

	return VMFAction_Continue;
}

static VMFAction configMapCallback(VMFState *state, VMFEntryType entry, const VMFKeyValue *kv) {
	Config *cfg = state->user_data;

	switch (entry) {
	case VMFEntryType_KeyValue:
		if (strncasecmp("name", kv->key.str, kv->key.length) == 0) {
			cfg->map_name = kv->value;
		} else if (strncasecmp("offset", kv->key.str, kv->key.length) == 0) {
			cfg->map_offset = kv->value;
		} else {
			PRINTF("%s: Unexpected key \"" PRI_SV "\"", __FUNCTION__, PRI_SVV(kv->key));
			return VMFAction_SemanticError;
		}
		break;
	case VMFEntryType_SectionClose:
		if (cfg->map_name.length < 1) {
			PRINTF("%s: Invalid map name \"" PRI_SV "\"", __FUNCTION__, PRI_SVV(cfg->map_name));
			return VMFAction_SemanticError;
		}
		Map *m = opensrcAllocMap(cfg->map_name);
		if (m && cfg->map_offset.length >= 5) {
			float x, y, z;
			// FIXME map_offset is not null-terminated
			if (3 == sscanf(cfg->map_offset.str, "%f %f %f", &x, &y, &z)) {
				m->flags |= MapFlags_FixedOffset;
				m->offset = aVec3f(x, y, z);
			} else {
				PRINTF("Cannot read offset " PRI_SV, PRI_SVV(cfg->map_offset));
			}
		}
		state->callback = configReadCallback;
		break;
	default:
		PRINTF("%s: Unexpected entry %d", __FUNCTION__, entry);
		return VMFAction_SemanticError;
	}

	return VMFAction_Continue;
}

static VMFAction configPatchCallback(VMFState *state, VMFEntryType entry, const VMFKeyValue *kv) {
	Config *cfg = state->user_data;

	switch (entry) {
	case VMFEntryType_SectionOpen:
		if (kv->key.length < 1) {
			PRINTF("%s: Unexpected section \"" PRI_SV "\"", __FUNCTION__, PRI_SVV(kv->key));
			return VMFAction_SemanticError;
		}
		cfg->map_name = kv->key;
		state->callback = configLandmarkCallback;
		break;
	case VMFEntryType_SectionClose:
		return VMFAction_Exit;
	default:
		PRINTF("%s: Unexpected entry %d", __FUNCTION__, entry);
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
			g_cfg.maps_limit = atoi(kv->value.str);
		} else if (strncasecmp("map", kv->key.str, kv->key.length) == 0) {
			openSourceAddMap(kv->value);
		} else if (strncasecmp("z_far", kv->key.str, kv->key.length) == 0) {
			// FIXME null-terminate
			g.R = (float)atof(kv->value.str);
		} else {
			PRINTF("%s: Unexpected key \"" PRI_SV "\"", __FUNCTION__, PRI_SVV(kv->key));
			return VMFAction_SemanticError;
		}
		break;
	case VMFEntryType_SectionOpen:
		if (strncasecmp("patch_landmarks", kv->key.str, kv->key.length) == 0)
			state->callback = configPatchCallback;
		else if (strncasecmp("map", kv->key.str, kv->key.length) == 0) {
			cfg->map_name.length = cfg->map_offset.length = 0;
			state->callback = configMapCallback;
		} else {
			PRINTF("%s: Unexpected section \"" PRI_SV "\"", __FUNCTION__, PRI_SVV(kv->key));
			return VMFAction_SemanticError;
		}
		break;
	default:
		PRINTF("%s: Unexpected entry %d", __FUNCTION__, entry);
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
		.data = { .str = buffer, .length = (int)file.size },
		.callback = configReadCallback
	};

	aFileClose(&file);

	int result = VMFResult_Success == vmfParse(&pstate);

	stackFreeUpToPosition(&stack_temp, buffer);
	return result;
}

const char *getDefaultSteamBaseDir() {
#ifdef _WIN32
	const char *base_dir = getenv("programfiles(x86)");
	const char *steam_prefix = "Steam/steamapps/common";
#else // LINUX
	const char *base_dir = getenv("HOME");
	const char *steam_prefix = ".local/share/Steam/steamapps/common";
#endif
	const int base_dir_length = base_dir ? (int)strlen(base_dir) : 0;
	const int steam_basedir_length = (int)strlen(steam_prefix) + base_dir_length + 2;
	char *steam_basedir_w = stackAlloc(mem.persistent, steam_basedir_length);
	sprintf(steam_basedir_w, "%s/%s", base_dir, steam_prefix);
	return steam_basedir_w;
}

typedef int (*ArgFunc)(const char *value, void *user_ptr);

typedef struct {
	const char *arg;
	const char *desc;
	ArgFunc func;
	void *user_ptr;
} Arg;

void argsPrintUsage(const Arg* args, int nargs, char const* prog) {
	PRINTF("Usage: %s", prog);
	const Arg* in = NULL;
	for (int i = 0; i < nargs; ++i) {
		const Arg* arg = args + i;
		if (arg->arg) {
			PRINTF("\t-%s: %s", arg->arg, arg->desc);
		} else {
			if (!in)
				in = arg;
		}
	}

	if (in)
		PRINTF("\tfree arguments: %s", in->desc);
}

int argsParse(const Arg* args, int nargs, int argc, char const* const* argv) {
	const Arg* in = NULL;
	for (int i = 0; i < nargs; ++i) {
		if (!args[i].arg) {
			in = args + i;
			break;
		}
	}

	// TODO:
	// - handle '--' as delimiter
	// - handle boolean flags w/o arguments
	// - long/short args
	// - optional/mandatory args
	for (int i = 1; i < argc; ++i) {
		char const *arg = argv[i];
		const Arg *handler = NULL;
		if (arg[0] == '-') {
			for (int j = 0; j < nargs; ++j) {
				const Arg* parg = args + j;
				if (parg->arg && arg[1] == parg->arg[0]) {
					if (i == argc - 1) {
						PRINTF("Option %s requres an argument", arg);
						return 0;
					}
					handler = parg;
					++i;
					arg = argv[i];
					break;
				}
			}
		} else {
			handler = in;
		}

		if (!handler) {
			PRINTF("Unrecognized option %s", arg);
			return 0;
		}

		if (handler->func(arg, handler->user_ptr) == 0) {
			PRINTF("Error handling %s%s with argument '%s'", handler->arg ? "option -" : "free option", handler->arg ? handler->arg : "", arg);
			return 0;
		}
	}

	return 1;
}

int argStoreString(const char *str, void *user_ptr) {
	const char** pstr = user_ptr;
	*pstr = str;
	return 1;
}

int argStoreInt(const char *str, void *user_ptr) {
	int *pint = user_ptr;
	// FIXME null-terminate
	*pint = atoi(str);
	return 1;
}

int argAddVpkToCollection(const char *str, void *unused) {
	(void)unused;
	ICollection *collection = collectionCreateVPK(&mem, str);
	if (!collection) {
		PRINTF("Cannot open VPK %s", str);
		return 0;
	}
	g.collection_chain = addToCollectionChain(g.collection_chain, collection);
	return 1;
}

int argAddDirToCollection(const char *str, void *unused) {
	(void)unused;
	ICollection *collection = collectionCreateFilesystem(&mem, str);
	if (!collection) {
		PRINTF("Cannot open filesystem collection %s", str);
		return 0;
	}
	g.collection_chain = addToCollectionChain(g.collection_chain, collection);
	return 1;
}

int argReadConfigFile(const char *str, void *unused) {
	(void)unused;
	if (!configReadFile(str)) {
		PRINTF("Cannot read config file %s", str);
		return 0;
	}
	return 1;
}

int argAddMap(const char *str, void *unused) {
	(void)unused;
	const StringView map = { .str = str, .length = (int)strlen(str) };
	openSourceAddMap(map);
	return 1;
}

static Arg g_args[] = {
	{"s", "Override steam basedir", argStoreString, (void*)&g_cfg.steam_basedir},
	{"m", "Add map name to list of maps to load", argAddMap, NULL},
	{"p", "Add VPK file to list of files to load assets from", argAddVpkToCollection, NULL},
	{"d", "Add directory to list of files to load assets from", argAddDirToCollection, NULL},
	{"n", "Specify a limit of number of maps to load", argStoreInt, &g_cfg.maps_limit},
	// TODO -h
	{NULL, "Game configuration file to load", argReadConfigFile, NULL},
};

void attoAppInit(struct AAppProctable *proctable) {
	//profilerInit();

	logOpen("OpenSource.log");

	g.collection_chain = NULL;
	g.patches = NULL;
	g.maps_count = 0;
	g.selected_map = NULL;
	g.R = 0;

	g_cfg.maps_limit = 1;
	g_cfg.steam_basedir = getDefaultSteamBaseDir();
	PRINTF("Default platform steam basedir = %s", g_cfg.steam_basedir);

	if (!argsParse(g_args, COUNTOF(g_args), a_app_state->argc, a_app_state->argv)) {
		PRINT("Error parsing command line arguments");
		goto print_usage_and_exit;
	}

	if (g_cfg.maps_limit < 1)
		g_cfg.maps_limit = 1;

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
	argsPrintUsage(g_args, COUNTOF(g_args), a_app_state->argv[0]);
	aAppTerminate(1);
}
