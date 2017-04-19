#pragma once
#include "common.h"
#include <stddef.h>

struct Stack {
	char *storage;
	size_t size, cursor;
};

static inline void *stackGetCursor(const struct Stack *stack) {
	return stack->storage + stack->cursor;
}
static inline size_t stackGetFree(const struct Stack *stack) {
	return stack->size - stack->cursor;
}
static inline void *stackAlloc(struct Stack *stack, size_t size) {
	size = 4 * ((size + 3) / 4); // alignment
	if (stack->size - stack->cursor < size)
		return 0;

	void *ret = stack->storage + stack->cursor;
	stack->cursor += size;
	return ret;
}
static inline void stackFree(struct Stack *stack, size_t size) {
	ASSERT(size <= stack->cursor);
	stack->cursor -= size;
}
static inline void stackFreeUpToPosition(struct Stack *stack, void *marker) {
	ASSERT((char*)marker >= stack->storage);
	const size_t marker_pos = ((char*)marker - stack->storage);
	ASSERT(marker_pos <= stack->cursor);
	stackFree(stack, stack->cursor - marker_pos);
}
