#pragma once
#include "filemap.h"
#include "mempools.h"
#include <stddef.h>

typedef struct IFile {
	size_t size;
	/* read size bytes into buffer
	 * returns bytes read, or < 0 on error. error codes aren't specified */
	size_t (*read)(struct IFile *file, size_t offset, size_t size, void *buffer);
	/* free any internal resources.
	 * will not free memory associated with this structure itself */
	void (*close)(struct IFile *file);
} IFile;

enum CollectionOpenResult {
	CollectionOpen_Success,
	CollectionOpen_NotFound, /* such item was not found in collection */
	CollectionOpen_NotEnoughMemory,
	CollectionOpen_Internal /* collection data is inconsistent internally, e.g. corrupted archive */
};

enum FileType {
	File_Map,
	File_Material,
	File_Texture,
	File_Model
};

typedef struct ICollection {
	/* free any internal resources, but don't deallocate this structure itself */
	void (*close)(struct ICollection *collection);
	enum CollectionOpenResult (*open)(struct ICollection *collection,
			const char *name, enum FileType type, struct IFile **out_file);
	struct ICollection *next;
} ICollection;

enum CollectionOpenResult collectionChainOpen(struct ICollection *collection,
		const char *name, enum FileType type, struct IFile **out_file);

struct ICollection *collectionCreateFilesystem(struct Memories *mem, const char *dir);
struct ICollection *collectionCreateVPK(struct Memories *mem, const char *dir_filename);
struct ICollection *collectionCreatePakfile(struct Memories *mem, const void *pakfile, uint32_t size);

