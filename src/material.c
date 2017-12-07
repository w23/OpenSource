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
	StringView key;
} MaterialContext;

#if 0
static const char * const ignore_params[] = {
	"$surfaceprop", "$surfaceprop2", "$tooltexture",
	"%tooltexture", "%keywords", "%compilewater", "%detailtype",
	"%compilenolight", "%compilepassbullets",
	"replace", /* TODO implement */
	0
};
#endif

static ParserCallbackResult materialReadShader(ParserState *state, StringView s);
static ParserCallbackResult materialReadKeyOrSection(ParserState *state, StringView s);
static ParserCallbackResult materialReadValue(ParserState *state, StringView s);
static ParserCallbackResult materialEnd(ParserState *state, StringView s);

static ParserCallbackResult materialOpenShader(ParserState *state, StringView s) {
	MaterialContext *ctx = state->user_data;
	ctx->shader = s;

	state->callbacks.string = materialReadKeyOrSection;
	state->callbacks.curlyOpen = parserError;

	return Parser_Continue;
}

static ParserCallbackResult materialReadShader(ParserState *state, StringView s) {
	MaterialContext *ctx = state->user_data;
	ctx->shader = s;

	PRINTF("Material shader %.*s", PRI_SVV(ctx->shader));

	state->callbacks.string = parserError;
	state->callbacks.curlyOpen = materialOpenShader;
	return Parser_Continue;
}

static ParserCallbackResult materialReadKeyOrSection(ParserState *state, StringView s) {
	MaterialContext *ctx = state->user_data;
	ctx->key = s;
	state->callbacks.string = materialReadValue;
	state->callbacks.curlyClose = state->callbacks.curlyOpen = parserError;
	return Parser_Continue;
}

static ParserCallbackResult materialReadValue(ParserState *state, StringView s) {
	MaterialContext *ctx = state->user_data;

	if (s.length > 127)
		return Parser_Error;

	char value[128];
	memcpy(value, s.str, s.length);
	value[s.length] = '\0';

	if (strncasecmp("$basetexture", ctx->key.str, ctx->key.length) == 0) {
		ctx->mat->base_texture[0] = textureGet(value, ctx->collection, ctx->temp);
	} else if (strncasecmp("$basetexture2", ctx->key.str, ctx->key.length) == 0) {
		ctx->mat->base_texture[1] = textureGet(value, ctx->collection, ctx->temp);
	} else if (strncasecmp("$basetexturetransform", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$basetexturetransform2", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$detail", ctx->key.str, ctx->key.length) == 0) {
		//output->detail = textureGet(ctx.value, ctx->collection, ctx->temp);
	} else if (strncasecmp("$detailscale", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$detailblendfactor", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$detailblendmode", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$parallaxmap", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$parallaxmapscale", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$bumpmap", ctx->key.str, ctx->key.length) == 0) {
		/* output->bump = textureGet(ctx.value, ctx->collection, ctx->temp); */
	} else if (strncasecmp("$envmap", ctx->key.str, ctx->key.length) == 0) {
		/* output->envmap = textureGet(ctx.value, ctx->collection, ctx->temp); */
	} else if (strncasecmp("$fogenable", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$fogcolor", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$alphatest", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$translucent", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$envmapcontrast", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$envmapsaturation", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$envmaptint", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$normalmapalphaenvmapmask", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$envmapmask", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$nodiffusebumplighting", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$AlphaTestReference", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$basealphaenvmapmask", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$selfillum", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("$reflectivity", ctx->key.str, ctx->key.length) == 0) {
	} else if (strncasecmp("include", ctx->key.str, ctx->key.length) == 0) {
		char *vmt = strstr(value, ".vmt");
		if (vmt)
			*vmt = '\0';
		if (strstr(value, "materials/") == value)
			*ctx->mat = *materialGet(value + 10, ctx->collection, ctx->temp);
	} else {
		PRINTF("Material shader:%.*s, unknown param %.*s = %s",
				ctx->shader.length, ctx->shader.str, ctx->key.length, ctx->key.str, value);
	}

	state->callbacks.string = materialReadKeyOrSection;
	state->callbacks.curlyClose = materialEnd;
	state->callbacks.curlyOpen = parserError;
	return Parser_Continue;
}

static ParserCallbackResult materialEnd(ParserState *state, StringView s) {
	(void)(s);
	MaterialContext *ctx = state->user_data;

	if (!ctx->mat->base_texture[0]) {
		PRINTF("Material with ctx->shader %.*s doesn't have base texture", ctx->shader.length, ctx->shader.str);
		ctx->mat->shader = MaterialShader_LightmappedAverageColor;
		// HACK to notice these materials
		ctx->mat->average_color = aVec3f(1.f, 0.f, 1.f);
	} else {
		ctx->mat->shader = MaterialShader_LightmappedGeneric;
		ctx->mat->average_color = ctx->mat->base_texture[0]->avg_color;
	}

	return Parser_Exit;
}

static int materialLoad(struct IFile *file, struct ICollection *coll, struct Material *output, struct Stack *tmp) {
	char *buffer = stackAlloc(tmp, file->size);

	if (!buffer) {
		PRINT("Out of temp memory");
		return 0;
	}

	if (file->size != file->read(file, 0, file->size, buffer)) return 0;

	MaterialContext ctx = {
		.collection = coll,
		.temp = tmp,
		.mat = output
	};

	ParserState parser = {
		.user_data = &ctx,
		.callbacks = {
			.curlyOpen = parserError,
			.curlyClose = parserError,
			.string = materialReadShader
		}
	};

	StringView buf_sv = { .str = buffer, .length = file->size };

	int success = ParseResult_Success == parserParse(&parser, buf_sv);

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

