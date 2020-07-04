#include "material.h"
#include "texture.h"
#include "cache.h"
#include "collection.h"
#include "vmfparser.h"
#include "common.h"

#ifdef _MSC_VER
#pragma warning(disable:4221)
#endif

typedef struct {
	ICollection *collection;
	Stack *temp;
	StringView shader;
	Material *mat;
	int depth;
} MaterialContext;

static VMFAction materialReadKeyValue(MaterialContext *ctx, const VMFKeyValue *kv) {
	if (kv->value.length > 127)
		return VMFAction_SemanticError;

	char value[128];
	memset(value, 0, sizeof value);
	memcpy(value, kv->value.str, kv->value.length);

	if (strncasecmp("$basetexture", kv->key.str, kv->key.length) == 0) {
		ctx->mat->base_texture.texture = textureGet(value, ctx->collection, ctx->temp);
	} else if (strncasecmp("$basetexturetransform", kv->key.str, kv->key.length) == 0) {
		AVec2f center, scale, translate;
		float rotate;
		if (7 != sscanf(value, "center %f %f scale %f %f rotate %f translate %f %f",
					&center.x, &center.y, &scale.x, &scale.y, &rotate, &translate.x, &translate.y)) {
			PRINTF("ERR: transform has wrong format: \"%s\"", value);
		} else {
			ctx->mat->base_texture.transform.scale = scale;
			ctx->mat->base_texture.transform.translate = translate;
			// TODO support rotation
		}
	} else if (strncasecmp("include", kv->key.str, kv->key.length) == 0) {
		char *vmt = strstr(value, ".vmt");
		if (vmt)
			*vmt = '\0';
		if (strstr(value, "materials/") == value)
			*ctx->mat = *materialGet(value + 10, ctx->collection, ctx->temp);
	}

	return VMFAction_Continue;
}

static VMFAction materialParserCallback(VMFState *state, VMFEntryType entry, const VMFKeyValue *kv);
static VMFAction materialShaderCallback(VMFState *state, VMFEntryType entry, const VMFKeyValue *kv);
static VMFAction materialPatchCallback(VMFState *state, VMFEntryType entry, const VMFKeyValue *kv);

static VMFAction materialPatchCallback(VMFState *state, VMFEntryType entry, const VMFKeyValue *kv) {
	//PRINTF("Entry %d (%.*s -> %.*s)", entry, PRI_SVV(kv->key), PRI_SVV(kv->value));
	MaterialContext *ctx = state->user_data;

	VMFAction retval = VMFAction_SemanticError;

	switch (entry) {
		case VMFEntryType_KeyValue:
			retval = materialReadKeyValue(ctx, kv);
			break;
		case VMFEntryType_SectionOpen:
			++ctx->depth;
			retval = VMFAction_Continue;
			break;
		case VMFEntryType_SectionClose:
			--ctx->depth;
			retval = ctx->depth == 0 ? VMFAction_Exit : VMFAction_Continue;
			break;
	}

	return retval;
}

static VMFAction materialShaderCallback(VMFState *state, VMFEntryType entry, const VMFKeyValue *kv) {
	//PRINTF("Entry %d (%.*s -> %.*s)", entry, PRI_SVV(kv->key), PRI_SVV(kv->value));
	MaterialContext *ctx = state->user_data;

	VMFAction retval = VMFAction_SemanticError;

	switch (entry) {
		case VMFEntryType_KeyValue:
			// Ignore any nested settings for no
			retval = (ctx->depth == 1) ? materialReadKeyValue(ctx, kv) : VMFAction_Continue;
			break;
		case VMFEntryType_SectionOpen:
			++ctx->depth;
			retval = VMFAction_Continue;
			break;
		case VMFEntryType_SectionClose:
			--ctx->depth;
			retval = ctx->depth == 0 ? VMFAction_Exit : VMFAction_Continue;
			break;
	}

	return retval;
}

static void mtextureInit(MTexture *t) {
	memset(t, 0, sizeof(*t));
	t->transform.scale = aVec2f(1.f, 1.f);
}

static VMFAction materialParserCallback(VMFState *state, VMFEntryType entry, const VMFKeyValue *kv) {
	//PRINTF("Entry %d (%.*s -> %.*s)", entry, PRI_SVV(kv->key), PRI_SVV(kv->value));
	MaterialContext *ctx = state->user_data;

	ctx->mat->average_color = aVec3f(0,1,1);
	ctx->mat->shader = MShader_Unknown;
	mtextureInit(&ctx->mat->base_texture);

	VMFAction retval = VMFAction_SemanticError;

	switch (entry) {
		case VMFEntryType_KeyValue:
			break;
		case VMFEntryType_SectionOpen:
			++ctx->depth;
			if (strncasecmp("patch", kv->key.str, kv->key.length) == 0) {
				state->callback = materialPatchCallback;
			} else {
				ctx->shader = kv->key;
				if (strncasecmp("unlitgeneric", kv->key.str, kv->key.length) == 0
					|| strncasecmp("sky", kv->key.str, kv->key.length) == 0)
					ctx->mat->shader = MShader_UnlitGeneric;
				else if (strncasecmp("lightmappedgeneric", kv->key.str, kv->key.length) == 0
					|| strncasecmp("worldvertextransition", kv->key.str, kv->key.length) == 0)
					ctx->mat->shader = MShader_LightmappedGeneric;
				else
					PRINTF("Unknown material shader " PRI_SV, PRI_SVV(kv->key));
				state->callback = materialShaderCallback;
			}
			retval = VMFAction_Continue;
			break;
		case VMFEntryType_SectionClose:
			break;
	}

	return retval;
}

static int materialLoad(struct IFile *file, struct ICollection *coll, Material *output, struct Stack *tmp) {
	char *buffer = stackAlloc(tmp, file->size);

	if (!buffer) {
		PRINT("Out of temp memory");
		return 0;
	}

	const int read_size = (int)file->read(file, 0, file->size, buffer);
	if ((int)file->size != read_size) {
		PRINTF("Cannot read material file: %d != %d", (int)file->size, read_size);
		return 0;
	}

	MaterialContext ctx = {
		.collection = coll,
		.temp = tmp,
		.mat = output,
		.depth = 0,
	};

	VMFState parser_state = {
		.user_data = &ctx,
		.data = { .str = buffer, .length = (int)file->size },
		.callback = materialParserCallback
	};

	const int success = VMFResult_Success == vmfParse(&parser_state);

	if (!success)
		PRINTF("Failed to read material with contents:\n" PRI_SV, PRI_SVV(parser_state.data));

	if (success && ctx.mat->base_texture.texture)
		ctx.mat->average_color = ctx.mat->base_texture.texture->avg_color;

	stackFreeUpToPosition(tmp, buffer);

	return success;
}

const Material *materialGet(const char *name, struct ICollection *collection, struct Stack *tmp) {
	const Material *mat = cacheGetMaterial(name);
	if (mat) return mat;

	struct IFile *matfile;
	if (CollectionOpen_Success != collectionChainOpen(collection, name, File_Material, &matfile)) {
		PRINTF("Material \"%s\" not found", name);
		return cacheGetMaterial("opensource/placeholder");
	}

	Material localmat;
	memset(&localmat, 0, sizeof localmat);
	if (materialLoad(matfile, collection, &localmat, tmp) == 0) {
		PRINTF("Material \"%s\" found, but could not be loaded", name);
	} else {
		cachePutMaterial(name, &localmat);
		mat = cacheGetMaterial(name);
	}

	matfile->close(matfile);
	return mat ? mat : cacheGetMaterial("opensource/placeholder");
}

