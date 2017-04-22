#pragma once
#include "atto/gl.h"

struct ICollection;
struct Stack;

typedef struct Texture {
	AGLTexture gltex;
} Texture;

const Texture *textureGet(const char *name, struct ICollection *collection, struct Stack *tmp);
