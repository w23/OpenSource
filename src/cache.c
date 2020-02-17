#include "cache.h"
#include "material.h"
#include "texture.h"
#define AHASH_IMPLEMENT
#include "ahash.h"
#include "mempools.h"

static struct {
	AHash materials;
	AHash textures;
} g;

static void initHash(AHash *hash, struct Stack *pool, long item_size) {
	hash->alloc_param = pool;
	hash->alloc = (AHashAllocFunc)stackAlloc;
	hash->nbuckets = 64;
	hash->key_size = 256;
	hash->value_size = item_size;
	hash->key_hash = aHashStringHash;
	hash->key_compare = (AHashKeyCompareFunc)strcmp;
	aHashInit(hash);
}

void cacheInit(struct Stack *pool) {
	initHash(&g.materials, pool, sizeof(struct Material));
	initHash(&g.textures, pool, sizeof(struct Texture));
}

const struct Material *cacheGetMaterial(const char *name) {
	return aHashGet(&g.materials, name);
}

void cachePutMaterial(const char *name, const struct Material *mat /* copied */) {
	aHashInsert(&g.materials, name, mat);
}

const struct Texture *cacheGetTexture(const char *name) {
	return aHashGet(&g.textures, name);
}

void cachePutTexture(const char *name, const struct Texture *tex /* copied */) {
	aHashInsert(&g.textures, name, tex);
}
