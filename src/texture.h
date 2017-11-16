#pragma once
#include "render.h"

typedef struct ICollection ICollection;
typedef struct Stack Stack;

typedef struct Texture {
	RTexture texture;
	struct AVec3f avg_color;
} Texture;

const Texture *textureGet(const char *name, struct ICollection *collection, struct Stack *tmp);

typedef struct {
	const char *str;
	int length;
} StringView;

const Texture *textureGetSkybox(StringView name, ICollection *coll, Stack *tmp);
