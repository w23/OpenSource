#pragma once
#include <string.h> /* memcpy */
#include <stddef.h> /* size_t */

/* simplest possible insert-only single-linked hash table */

typedef void *(*AHashAllocFunc)(void *alloc_param, size_t size);
typedef unsigned long (*AHashKeyHashFunc)(const void *key);
typedef int (*AHashKeyCompareFunc)(const void *left, const void *right);

struct AHashBucket_;

typedef struct {
	/* must be set before aHashInit */
	long nbuckets;
	/* note-to-self: it is actually trivially possible to have variable-size keys and items */
	long key_size;
	long value_size;
	/* must return properly aligned (e.g. 16) regions */
	void *alloc_param;
	AHashAllocFunc alloc;
	AHashKeyHashFunc key_hash;
	AHashKeyCompareFunc key_compare;

	struct {
		long item_size;
		long value_offset;
		long next_ptr_offset;
		struct AHashBucket_ *buckets;
	} impl_;
	struct {
		long items;
		long empty_buckets;
		long worst_bucket_items;
	} stat;
} AHash;

void aHashInit(AHash *hash);
void *aHashInsert(AHash *hash, const void *key, const void *value);
void *aHashGet(const AHash *hash, const void *key);

/* helper for hashing memory contents, e.g. strings and binary data
 * DON'T use it to hash structs, as those might have holes due to alignment,
 * and actual memory contents of these holes is not specified (UB?) */
unsigned long aHashBytesHash(const void *data, size_t size);

/* can be directly used if key is a zero-terminated string */
unsigned long aHashStringHash(const void *string);

#define AHASH_IMPLEMENT
#ifdef AHASH_IMPLEMENT
#ifndef AHASH_VALUE_ALIGNMENT
#define AHASH_VALUE_ALIGNMENT 16 /* worst case for SSE & friends */
#endif

struct AHashBucket_ {
	void *items;
	long count;
};

struct AHashItemData_ {
	void *key;
	void *value;
	void **next;
};

static struct AHashItemData_ a__hashGetItemData(const AHash *hash, void *item) {
	char *bytes = item;
	return (struct AHashItemData_){
		.key = bytes,
		.value = bytes + hash->impl_.value_offset,
		.next = (void**)(bytes + hash->impl_.next_ptr_offset)
	};
}

void aHashInit(AHash *hash) {
#define AHASH_ALIGNED_SIZE(S,A) (((S+A-1)/A)*A)
	hash->impl_.value_offset = AHASH_ALIGNED_SIZE(hash->key_size, AHASH_VALUE_ALIGNMENT);
	hash->impl_.next_ptr_offset = hash->impl_.value_offset + AHASH_ALIGNED_SIZE(hash->value_size, sizeof(void*));
	hash->impl_.item_size = hash->impl_.next_ptr_offset + sizeof(void*);
	hash->impl_.buckets = hash->alloc(hash->alloc_param, sizeof(struct AHashBucket_) * hash->nbuckets);
	for (long i = 0; i < hash->nbuckets; ++i)
		hash->impl_.buckets[i] = (struct AHashBucket_){
			.items = 0,
			.count = 0
		};
	hash->stat.items = 0;
	hash->stat.empty_buckets = hash->nbuckets;
	hash->stat.worst_bucket_items = 0;
}

void *aHashInsert(AHash *hash, const void *key, const void *value) {
	const long index = hash->key_hash(key) % hash->nbuckets;
	struct AHashBucket_ *const bucket = hash->impl_.buckets + index;

	void *item = bucket->items;
	struct AHashItemData_ prev_item_data;
	prev_item_data.next = &bucket->items;
	while(item != 0) {
		prev_item_data = a__hashGetItemData(hash, item);
		item = *prev_item_data.next;
	}

	void *const new_item = hash->alloc(hash->alloc_param, hash->impl_.item_size);
	const struct AHashItemData_ item_data = a__hashGetItemData(hash, new_item);
	memcpy(item_data.key, key, hash->key_size);
	memcpy(item_data.value, value, hash->value_size);
	item_data.next[0] = 0;
	*prev_item_data.next = new_item;

	if (!bucket->count) hash->stat.empty_buckets--;
	++bucket->count;
	if (bucket->count > hash->stat.worst_bucket_items) hash->stat.worst_bucket_items = bucket->count;
	++hash->stat.items;
	return new_item;
}

void *aHashGet(const AHash *hash, const void *key) {
	const long index = hash->key_hash(key) % hash->nbuckets;
	const struct AHashBucket_ *const bucket = hash->impl_.buckets + index;

	void *item = bucket->items;
	while (item) {
		const struct AHashItemData_ data = a__hashGetItemData(hash, item);
		if (0 == hash->key_compare(key, data.key))
			return data.value;
		item = *data.next;
	}
	return 0;
}

/* FNV-1a */
unsigned long aHashBytesHash(const void *data, size_t size) {
#if __LP64__
	unsigned long hash = 0xcbf29ce484222325ul;
	const unsigned long mul = 1099511628211ul;
#else
	unsigned long hash = 0x811c9dc5ul;
	const unsigned long mul = 16777619ul;
#endif
	for (size_t i = 0; i < size; ++i) {
		hash ^= ((const unsigned char*)data)[i];
		hash *= mul;
	}
	return hash;
}

unsigned long aHashStringHash(const void *string) {
	return aHashBytesHash(string, strlen(string));
}
#endif
