#pragma once

struct ICollection;
struct Texture;
struct Stack;

struct Material {
	const struct Texture *base_texture[2];
	const struct Texture *bump;
	const struct Texture *detail;
	const struct Texture *envmap;
	/* TODO:
	 * - bump
	 * - detail
	 * - texture transforms
	 * - special material type (e.g. water, translucent, ...)
	 * ...
	 */
};

const struct Material *materialGet(const char *name, struct ICollection *collection, struct Stack *tmp);
