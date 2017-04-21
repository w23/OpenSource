#include "material.h"
#include "texture.h"
#include "cache.h"
#include "collection.h"
#include "common.h"

enum TokenType {
	Token_Skip,
	Token_String,
	Token_CurlyOpen,
	Token_CurlyClose,
	Token_Error,
	Token_End
};
struct TokenContext {
	const char *start;
	int length;
	const char *cursor;
};

static enum TokenType getNextToken(struct TokenContext *tok) {
	enum TokenType type = Token_Skip;
	const char *c = tok->cursor;

	while (type == Token_Skip) {
		while(*c != '\0' && isspace(*c)) ++c;
		if (*c == '\0') return Token_End;

		tok->start = c;
		tok->length = 0;
		switch(*c) {
			case '\"':
				tok->start = ++c;
				while(*c != '\0' && *c != '\"') ++c;
				type = (*c == '\"') ? Token_String : Token_Error;
				break;
			case '{': type = Token_CurlyOpen; break;
			case '}': type = Token_CurlyClose; break;
			case '/':
				if (*++c == '/') {
					while(*c != '\0' && *c != '\n') ++c;
					type = Token_Skip;
				} else
					type = Token_Error;
				break;
			default:
				while (*c != '\0' && isgraph(*c)) ++c;
				type = (c == tok->start) ? Token_Error : Token_String;
		} /* switch(*c) */
	} /* while skip */

	tok->length = c - tok->start;
	tok->cursor = c + 1;
	return type;
}

struct MaterialContext {
	struct Material *mat;
	struct TokenContext tok;
	const char *key;
	int key_length;
	char value[128];
};

enum KeyValueResult {KeyValue_Read, KeyValue_Error, KeyValue_End};
static enum KeyValueResult getNextKeyValue(struct MaterialContext *ctx) {
	for (;;) {
		enum TokenType type = getNextToken(&ctx->tok);
		if (type == Token_End || type == Token_CurlyClose) return KeyValue_End;
		if (type != Token_String) return KeyValue_Error;

		ctx->key = ctx->tok.start;
		ctx->key_length = ctx->tok.length;

		type = getNextToken(&ctx->tok);
		if (type == Token_CurlyOpen) {
			/* skip unsupported proxies and DX-level specifics */
			PRINTF("Skipping section %.*s", ctx->key_length, ctx->key);

			int depth = 1;
			for (;;) {
				type = getNextToken(&ctx->tok);
				if (type == Token_CurlyClose) {
					if (--depth == 0)
						break;
				} else if (type == Token_CurlyOpen) {
					++depth;
				} else if (type != Token_String)
					return KeyValue_Error;
			}
		} else if (type != Token_String) {
			return KeyValue_Error;
		} else {
			if (ctx->tok.length > (int)sizeof(ctx->value) - 1) {
				PRINTF("Value is too long: %d", ctx->tok.length);
				return KeyValue_Error;
			}
			memcpy(ctx->value, ctx->tok.start, ctx->tok.length);
			ctx->value[ctx->tok.length] = '\0';
			return KeyValue_Read;
		}
	} /* loop until key value pair found */
} /* getNextKeyValue */

static const char * const ignore_params[] = {
	"$surfaceprop", "$surfaceprop2", "$tooltexture",
	"%tooltexture", "%keywords", "%compilewater", "%detailtype",
	"%compilenolight", "%compilepassbullets",
	0
};

static int materialLoad(struct IFile *file, struct ICollection *coll, struct Material *output, struct Stack *tmp) {
	char buffer[8192]; /* most vmts are < 300, a few are almost 1000, max seen ~3200 */
	if (file->size > sizeof(buffer) - 1) {
		PRINTF("VMT is too large: %zu", file->size);
		return 0;
	}

	if (file->size != file->read(file, 0, file->size, buffer)) return 0;

	buffer[file->size] = '\0';

	struct MaterialContext ctx;
	ctx.mat = output;
	ctx.tok.cursor = buffer;

#define EXPECT_TOKEN(type) \
	if (getNextToken(&ctx.tok) != type) { \
		PRINTF("Unexpected token at position %zd, expecting %d; left: %s", ctx.tok.cursor - buffer, type, ctx.tok.cursor); \
		return 0; \
	}

	EXPECT_TOKEN(Token_String);
	const char *shader = ctx.tok.start;
	const int shader_length = ctx.tok.length;

	EXPECT_TOKEN(Token_CurlyOpen);

	for (;;) {
		const enum KeyValueResult result = getNextKeyValue(&ctx);
		if (result == KeyValue_End) break;
		if (result != KeyValue_Read) goto error;

		int skip = 0;
		for (const char *const *ignore = ignore_params; ignore[0] != 0; ++ignore)
			if (strncasecmp(ignore[0], ctx.key, ctx.key_length) == 0) {
				skip = 1;
				break;
			}
		if (skip) continue;

		if (strncasecmp("$basetexture", ctx.key, ctx.key_length) == 0) {
			output->base_texture[0] = textureGet(ctx.value, coll, tmp);
		} else if (strncasecmp("$basetexture2", ctx.key, ctx.key_length) == 0) {
			output->base_texture[1] = textureGet(ctx.value, coll, tmp);
		} else if (strncasecmp("$basetexturetransform", ctx.key, ctx.key_length) == 0) {
		} else if (strncasecmp("$basetexturetransform2", ctx.key, ctx.key_length) == 0) {
		} else if (strncasecmp("$detail", ctx.key, ctx.key_length) == 0) {
		} else if (strncasecmp("$detailscale", ctx.key, ctx.key_length) == 0) {
		} else if (strncasecmp("$detailblendfactor", ctx.key, ctx.key_length) == 0) {
		} else if (strncasecmp("$detailblendmode", ctx.key, ctx.key_length) == 0) {
		} else if (strncasecmp("$parallaxmap", ctx.key, ctx.key_length) == 0) {
		} else if (strncasecmp("$parallaxmapscale", ctx.key, ctx.key_length) == 0) {
		} else if (strncasecmp("$bumpmap", ctx.key, ctx.key_length) == 0) {
		} else if (strncasecmp("$envmap", ctx.key, ctx.key_length) == 0) {
		} else if (strncasecmp("$fogenable", ctx.key, ctx.key_length) == 0) {
		} else if (strncasecmp("$fogcolor", ctx.key, ctx.key_length) == 0) {
		} else if (strncasecmp("$alphatest", ctx.key, ctx.key_length) == 0) {
		} else if (strncasecmp("$translucent", ctx.key, ctx.key_length) == 0) {
		} else {
			PRINTF("Material shader:%.*s, unknown param %.*s = %s",
					shader_length, shader, ctx.key_length, ctx.key, ctx.value);
		}
	} /* for all properties */

	return 1;

error:
	PRINTF("Error parsing material with shader %.*s: %s", shader_length, shader, ctx.tok.cursor);
	return 0;
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
	if (materialLoad(matfile, collection, &localmat, tmp) == 0) {
		PRINTF("Material \"%s\" found, but could not be loaded", name);
	} else {
		mat = &localmat;
		cachePutMaterial(name, mat);
	}

	matfile->close(matfile);
	return mat ? mat : cacheGetMaterial("opensource/placeholder");
}

