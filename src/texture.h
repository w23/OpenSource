#pragma once
#include "render.h"

struct ICollection;
struct Stack;

typedef struct Texture {
	RTexture texture;
	struct AVec3f avg_color;
} Texture;

const Texture *textureGet(const char *name, struct ICollection *collection, struct Stack *tmp);
