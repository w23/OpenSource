#pragma once

struct MemoryPool;
void cacheInit(struct MemoryPool *pool);

struct Material;
struct Texture;

const struct Material *cacheGetMaterial(const char *name);
void cachePutMaterial(const char *name, const struct Material *mat /* copied */);

const struct Texture *cacheGetTexture(const char *name);
void cachePutTexture(const char *name, const struct Texture *tex /* copied */);
