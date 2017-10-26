#pragma once
#include "render.h"

struct ICollection;
struct Stack;

typedef struct Texture {
	RTexture texture;
} Texture;

const Texture *textureGet(const char *name, struct ICollection *collection, struct Stack *tmp);
