#pragma once
#include "utils.h"
#include <stddef.h>

struct MemoryPool {
	void *(*alloc)(struct MemoryPool *pool, size_t size);
	void (*free)(struct MemoryPool *pool, void *ptr);
};
#define POOL_ALLOC(p, sz) ((p)->alloc(p, sz))
#define POOL_FREE(p, ptr) ((p)->free(p, ptr))

struct TemporaryPool {
	char *storage;
	size_t size, cursor;
};

static inline void *tmpGetCursor(const struct TemporaryPool *tmp) {
	return tmp->storage + tmp->cursor;
}
static inline size_t tmpGetLeft(const struct TemporaryPool *tmp) {
	return tmp->size - tmp->cursor;
}
static inline void *tmpAdvance(struct TemporaryPool *tmp, size_t size) {
	size = 4 * ((size + 3) / 4); // alignment
	if (tmp->size - tmp->cursor < size)
		return 0;

	void *ret = tmp->storage + tmp->cursor;
	tmp->cursor += size;
	return ret;
}
static inline void tmpReturn(struct TemporaryPool *tmp, size_t size) {
	ASSERT(size <= tmp->cursor);
	tmp->cursor -= size;
}
static inline void tmpReturnToPosition(struct TemporaryPool *tmp, void *marker) {
	ASSERT((char*)marker >= tmp->storage);
	const size_t to_release = tmp->cursor - ((char*)marker - tmp->storage);
	tmpReturn(tmp, to_release);
}
