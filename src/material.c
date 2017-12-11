#include "material.h"
#include "texture.h"
#include "cache.h"
#include "collection.h"
#include "vmfparser.h"
#include "common.h"

typedef struct {
	ICollection *collection;
	Stack *temp;
	StringView shader;
	struct Material *mat;
	int depth;
} MaterialContext;

static VMFAction materialReadKeyValue(MaterialContext *ctx, const VMFKeyValue *kv) {
	if (kv->value.length > 127)
		return VMFAction_SemanticError;

	char value[128];
	memcpy(value, kv->value.str, kv->value.length);
	value[kv->value.length] = '\0';

	if (strncasecmp("$basetexture", kv->key.str, kv->key.length) == 0) {
		ctx->mat->base_texture[0] = textureGet(value, ctx->collection, ctx->temp);
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

static VMFAction materialParserCallback(VMFState *state, VMFEntryType entry, const VMFKeyValue *kv) {
	//PRINTF("Entry %d (%.*s -> %.*s)", entry, PRI_SVV(kv->key), PRI_SVV(kv->value));
	MaterialContext *ctx = state->user_data;

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
				state->callback = materialShaderCallback;
			}
			retval = VMFAction_Continue;
			break;
		case VMFEntryType_SectionClose:
			break;
	}

	return retval;
}

static int materialLoad(struct IFile *file, struct ICollection *coll, struct Material *output, struct Stack *tmp) {
	char *buffer = stackAlloc(tmp, file->size);

	if (!buffer) {
		PRINT("Out of temp memory");
		return 0;
	}

	if (file->size != file->read(file, 0, file->size, buffer))
		return 0;

	MaterialContext ctx = {
		.collection = coll,
		.temp = tmp,
		.mat = output,
		.depth = 0,
	};

	VMFState parser_state = {
		.user_data = &ctx,
		.data = { .str = buffer, .length = file->size },
		.callback = materialParserCallback
	};

	const int success = VMFResult_Success == vmfParse(&parser_state);

	if (success) {
		if (!ctx.mat->base_texture[0]) {
			PRINTF("Material with ctx.shader %.*s doesn't have base texture", ctx.shader.length, ctx.shader.str);
			ctx.mat->shader = MaterialShader_LightmappedAverageColor;
			// HACK to notice these materials
			ctx.mat->average_color = aVec3f(1.f, 0.f, 1.f);
		} else {
			ctx.mat->shader = MaterialShader_LightmappedGeneric;
			ctx.mat->average_color = ctx.mat->base_texture[0]->avg_color;
		}
	}


	stackFreeUpToPosition(tmp, buffer);

	return success;
}

const struct Material *materialGet(const char *name, struct ICollection *collection, struct Stack *tmp) {
	const struct Material *mat = cacheGetMaterial(name);
	if (mat) return mat;

	struct IFile *matfile;
	if (CollectionOpen_Success != collectionChainOpen(collection, name, File_Material, &matfile)) {
		PRINTF("Material \"%s\" not found", name);
		return cacheGetMaterial("opensource/placeholder");
	}

	struct Material localmat;
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

