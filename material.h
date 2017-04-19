#pragma once

struct ICollection;
struct Texture;
struct Stack;

struct Material {
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
