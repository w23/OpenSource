#include "collection.h"
#include "common.h"

enum CollectionOpenResult collectionChainOpen(struct ICollection *collection,
		const char *name, enum FileType type, struct IFile **out_file) {
	while (collection) {
		enum CollectionOpenResult result = collection->open(collection, name, type, out_file);
		if (result == CollectionOpen_Success) return result;
		if (result == CollectionOpen_NotFound) {
			collection = collection->next;
		} else {
			/* TODO print error */
			return result;
		}
	}

	return CollectionOpen_NotFound;
}

static enum AFileResult filesystemCollectionFile_Open(struct FilesystemCollectionFile_ *f, const char *filename) {
	const enum AFileResult result = aFileOpen(&f->file, filename);
	f->opened = result == AFile_Success;
	f->head.size = f->file.size;
	return result;
}

static size_t filesystemCollectionFile_Read(struct IFile *file, size_t offset, size_t size, void *buffer) {
	struct FilesystemCollectionFile_ *f = (void*)file;
	const size_t result = aFileReadAtOffset(&f->file, offset, size, buffer);
	return result != AFileError ? result : 0;
}

static void filesystemCollectionFile_Close(struct IFile *file) {
	struct FilesystemCollectionFile_ *f = (void*)file;
	aFileClose(&f->file);
	f->head.size = 0;
	f->opened = 0;
}

static void filesystemCollectionClose(struct ICollection *collection) {
	(void)(collection);
	/* TODO ensure all files are closed */
}

static enum CollectionOpenResult filesystemCollectionOpen(struct ICollection *collection,
			const char *name, enum FileType type, struct IFile **out_file) {
	struct FilesystemCollection *fsc = (struct FilesystemCollection*)collection;
	char buffer[512];
	const char *subdir = "/";
	const char *suffix = "";

	*out_file = 0;

	int fileslot = 0;
	for(; fileslot < FilesystemCollectionFileSlots; ++fileslot)
		if (fsc->files[fileslot].head.read) break;
	if (fileslot >= FilesystemCollectionFileSlots)
		return CollectionOpen_TooManyFiles;
	struct FilesystemCollectionFile_ *f = fsc->files + fileslot;

	switch (type) {
		case File_Map: subdir = "/maps/"; suffix = ".bsp"; break;
		case File_Material: subdir = "/materials/"; suffix = ".vmt"; break;
		case File_Texture: subdir = "/textures/"; suffix = ".vtf"; break;
		case File_Model: subdir = "/models/"; suffix = ".mdl"; break;
	}

	const int subdir_len = strlen(subdir);
	const int name_len = strlen(name);
	const int suffix_len = strlen(suffix);

	if (fsc->path_len + subdir_len + name_len + suffix_len >= (int)sizeof(buffer)) {
		PRINT("Resource \"%s\" path is too long", name);
		return CollectionOpen_NotFound;
	}

	char *c = buffer;
	for (int i = 0; i < fsc->path_len; ++i) *c++ = fsc->path[i];
	for (int i = 0; i < subdir_len; ++i) *c++ = subdir[i];
	for (int i = 0; i < name_len; ++i) *c++ = tolower(name[i]);
	for (int i = 0; i < suffix_len; ++i) *c++ = suffix[i];
	*c = '\0';

	if (AFile_Success != filesystemCollectionFile_Open(f, buffer))
		return CollectionOpen_NotFound;
	*out_file = &f->head;
	return CollectionOpen_Success;
}

void filesystemCollectionCreate(struct FilesystemCollection *collection, const char *dir) {
	memset(collection, 0, sizeof *collection);
	/* TODO length check? dir exists check? */
	collection->path_len = strlen(dir);
	strncpy(collection->path, dir, sizeof(collection->path) - 1);

	for (int i = 0; i < FilesystemCollectionFileSlots; ++i) {
		struct FilesystemCollectionFile_ *f = collection->files + i;
		aFileReset(&f->file);
		f->head.read = filesystemCollectionFile_Read;
		f->head.close = filesystemCollectionFile_Close;
	}

	collection->head.open = filesystemCollectionOpen;
	collection->head.close = filesystemCollectionClose;
}
