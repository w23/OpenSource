#pragma once
#include "filemap.h"
#include "mempools.h"
#include <stddef.h>

struct IFile {
	size_t size;
	/* read size bytes into buffer
	 * returns bytes read, or < 0 on error. error codes aren't specified */
	size_t (*read)(struct IFile *file, size_t offset, size_t size, void *buffer);
	/* free any internal resources.
	 * will not free memory associated with this structure itself */
	void (*close)();
};

enum CollectionOpenResult {
	CollectionOpen_Success,
	CollectionOpen_NotFound, /* such item was not found in collection */
	CollectionOpen_TooManyFiles, /* collection limit on open files exceeded (regardless of was the item found or not) */
	CollectionOpen_Internal /* collection data is inconsistent internally, e.g. corrupted archive */
};

enum FileType {
	File_Map,
	File_Material,
	File_Texture,
	File_Model
};

struct ICollection {
	/* free any internal resources, but don't deallocate this structure itself */
	void (*close)();
	enum CollectionOpenResult (*open)(struct ICollection *collection,
			const char *name, enum FileType type, struct IFile **out_file);
	struct ICollection *next;
};

enum CollectionOpenResult collectionChainOpen(struct ICollection *collection,
		const char *name, enum FileType type, struct IFile **out_file);

#define FilesystemCollectionFileSlots 16

struct FilesystemCollectionFile_ {
		struct IFile head;
		struct AFile file;
		int opened;
};

struct FilesystemCollection {
	struct ICollection head;
	char path[256];
	int path_len;
	struct FilesystemCollectionFile_ files[FilesystemCollectionFileSlots];
};

void filesystemCollectionCreate(struct FilesystemCollection *collection, const char *dir);

#define MAX_VPK_ARCHIVES 16

struct VPKFileMetadata;

struct VPKCollection {
	struct ICollection head;
	struct AFileMap directory;
	struct AFile archives[MAX_VPK_ARCHIVES];
	struct VPKFileMetadata *files;
	int files_count;
};

void vpkCollectionCreate(struct VPKCollection *collection, const char *dir_filename, struct Stack *persistent, struct Stack *temp);



