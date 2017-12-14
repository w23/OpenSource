#pragma once
#include "atto/math.h"

struct ICollection;
struct Texture;
struct Stack;

typedef struct AVec2f AVec2f;

typedef enum {
	MShader_Unknown,
	MShader_LightmappedOnly,
	MShader_LightmappedGeneric,
	MShader_UnlitGeneric,

	MShader_COUNT
} MShader;

typedef struct {
	const struct Texture *texture;
	struct {
		AVec2f translate;
		AVec2f scale;
	} transform;
} MTexture;

typedef struct Material {
	MShader shader;
	struct AVec3f average_color;
	MTexture base_texture;
} Material;

const Material *materialGet(const char *name, struct ICollection *collection, struct Stack *tmp);
