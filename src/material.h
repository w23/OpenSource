#pragma once
#include "atto/math.h"

struct ICollection;
struct Texture;
struct Stack;

enum MaterialShader {
	MaterialShader_LightmappedAverageColor,
	MaterialShader_LightmappedGeneric,
};

struct Material {
	enum MaterialShader shader;
	struct AVec3f average_color;
	const struct Texture *base_texture[2];
	/* TODO:
	 * - bump
	 * - detail
	 * - texture transforms
	 * - special material type (e.g. water, translucent, ...)
	 * ...
	 */
};

const struct Material *materialGet(const char *name, struct ICollection *collection, struct Stack *tmp);
