#pragma once

struct Stack;

void cacheInit(struct Stack* persistent);

struct Material;
struct Texture;

const struct Material *cacheGetMaterial(const char *name);
void cachePutMaterial(const char *name, const struct Material *mat /* copied */);

const struct Texture *cacheGetTexture(const char *name);
void cachePutTexture(const char *name, const struct Texture *tex /* copied */);
