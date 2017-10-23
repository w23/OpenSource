#include "collection.h"
#include "common.h"
#include "vpk.h"
#include <alloca.h>

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
	char buffer[512] = {0};
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
		case File_Texture: subdir = "/materials/"; suffix = ".vtf"; break;
		case File_Model: subdir = "/models/"; suffix = ".mdl"; break;
	}

	const int subdir_len = strlen(subdir);
	const int name_len = strlen(name);
	const int suffix_len = strlen(suffix);

	if (fsc->path_len + subdir_len + name_len + suffix_len >= (int)sizeof(buffer)) {
		PRINTF("Resource \"%s\" path is too long", name);
		return CollectionOpen_NotFound;
	}

	int skip_end = 0;
	int max_skip = name_len;
	for (;;) {
		char *c = buffer;
		for (int i = 0; i < fsc->path_len; ++i) *c++ = fsc->path[i];
		for (int i = 0; i < subdir_len; ++i) *c++ = subdir[i];
		char *name_start = c;
		for (int i = 0; i < name_len; ++i) {
			char C = tolower(name[i]);
			*c++ = (C == '\\') ? '/' : C;
		}
		if (strstr(name_start, "maps/") == name_start) {
			char *start = strchr(name_start + 6, '/');
			if (start) {
				memmove(name_start, start + 1, c - start);
				c -= start - name_start;
				max_skip -= start - name_start;
			}
		}
		c -= skip_end;
		for (int i = 0; i < suffix_len; ++i) *c++ = suffix[i];
		*c = '\0';

		if (AFile_Success != filesystemCollectionFile_Open(f, buffer)) {
			//if (type == File_Material)
			{
				skip_end++;
				if (skip_end < max_skip)
					continue;
			}
			return CollectionOpen_NotFound;
		}

		break;
	}

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

struct StringView {
	const char *s;
	int len;
};
#define PRI_SV "%.*s"
#define PASS_SV(sv) sv.len, sv.s
#define PASS_PSV(sv) sv->len, sv->s

static struct StringView readString(const char **c, const char *end) {
	struct StringView ret = { *c, 0 };
	while (*c < end && **c != '\0') ++(*c);
	ret.len = *c - ret.s;
	++(*c);
	return ret;
}

enum { VPKFilePartDir = 0, VPKFilePartArchive = 1, VPKFilePart_MAX };

struct VPKFileMetadata {
	struct StringView filename;
	int archive;
	struct {
		size_t off, size;
	} parts[VPKFilePart_MAX];
};

static void vpkCollectionClose(struct ICollection *collection) {
	(void)(collection);
	/* FIXME close handles */
}

static enum CollectionOpenResult vpkCollectionOpen(struct ICollection *collection,
		const char *name, enum FileType type, struct IFile **out_file) {
	struct VPKCollection *vpkc = (struct VPKCollection*)collection;

	return CollectionOpen_NotFound;
}

void vpkCollectionCreate(struct VPKCollection *collection, const char *dir_filename, struct Stack *persistent, struct Stack *temp) {
	PRINTF("Opening collection %s", dir_filename);

	void *const temp_stack_top = stackGetCursor(temp);

	collection->directory = aFileMapOpen(dir_filename);
	if (!collection->directory.map) {
		PRINTF("Cannot open %s", dir_filename);
		exit(-1);
	}

	const char *dir = collection->directory.map;
	const size_t size = collection->directory.size;

	if (size <= sizeof(struct VPK2Header)) {
		PRINT("VPK header is too small");
		exit(-1);
	}

	const struct VPK2Header *header = collection->directory.map;

	if (header->signature != VPK_SIGNATURE) {
		PRINTF("Wrong VPK signature %08x", header->signature);
		exit(-1);
	}

	if (header->version != 2) {
		PRINTF("VPK version %d is not supported", header->version);
		exit(-1);
	}

	struct VPKFileMetadata *files_begin = stackGetCursor(persistent), *files_end = files_begin;

	int max_archives = -1;
	const char *const end = dir + size;
	const char *c = dir + sizeof(struct VPK2Header);
	for (;;) {
		// read extension
		const struct StringView ext = readString(&c, end);
		if (ext.len == 0)
			break;

		for (;;) {
			// read path
			const struct StringView path = readString(&c, end);
			if (path.len == 0)
				break;

			for (;;) {
				// read filename
				const struct StringView filename = readString(&c, end);
				if (filename.len == 0)
					break;

				if ((unsigned long)(end - c) < sizeof(struct VPKTreeEntry)) {
					PRINT("Incomplete VPKTreeEntry struct");
					exit(-1);
				}

				const struct VPKTreeEntry *const entry = (const struct VPKTreeEntry*)c;
				c += sizeof(struct VPKTreeEntry);

				if (entry->terminator != VPK_TERMINATOR) {
					PRINTF("Wrong terminator: %04x", entry->terminator);
					exit(-1);
				}

				const int filename_len = 3 + path.len + filename.len + ext.len;
				char *filename_temp = stackAlloc(temp, filename_len);
				if (!filename_temp) {
					PRINT("Not enough temp memory");
					exit(-1);
				}

				memcpy(filename_temp, path.s, path.len);
				filename_temp[path.len] = '/';
				memcpy(filename_temp + path.len + 1, filename.s, filename.len);
				filename_temp[path.len + 1 + filename.len] = '.';
				memcpy(filename_temp + path.len + 1 + filename.len + 1, ext.s, ext.len);
				filename_temp[filename_len-1] = '\0';

				PRINTF("%s crc=%08x pre=%d arc=%d(%04x) off=%d len=%d",
					filename_temp,
					entry->crc,
					entry->preloadBytes, entry->archive, entry->archive,
					entry->archiveOffset, entry->archiveLength);

				struct VPKFileMetadata *file = stackAlloc(persistent, sizeof(struct VPKFileMetadata));
				if (!file) {
					PRINT("Not enough persistent memory");
					exit(-1);
				}
				memset(file, 0, sizeof(*file));

				file->filename.s = filename_temp;
				file->filename.len = filename_len - 1;
				if (entry->preloadBytes) {
					file->parts[VPKFilePartDir].off = c - (char*)dir;
					file->parts[VPKFilePartDir].size = entry->preloadBytes;
				}

				if (entry->archiveLength) {
					file->archive = entry->archive != 0x7fff ? entry->archive : -1;
					file->parts[VPKFilePartArchive].off = entry->archiveOffset;
					file->parts[VPKFilePartArchive].size = entry->archiveLength;
				}

				if (file->archive > max_archives)
					max_archives = file->archive;

				files_end = file + 1;

				c += entry->preloadBytes;
			} // for filenames
		} // for paths
	} // for extensions

	// TODO sort
	
	// store filenames in persistent memory
	for (struct VPKFileMetadata *file = files_begin; file != files_end; ++file) {
		char *string = stackAlloc(persistent, file->filename.len + 1);
		if (!string) {
			PRINT("Not enough persistent memory");
			exit(-1);
		}

		memcpy(string, file->filename.s, file->filename.len + 1);
		file->filename.s = string;
	}

	// open archives
	if (max_archives >= MAX_VPK_ARCHIVES) {
		PRINTF("Too many archives: %d", max_archives);
		exit(-1);
	}

	const int dir_filename_len = strlen(dir_filename) + 1;
	char *arcname = alloca(dir_filename_len);
	if (!arcname || dir_filename_len < 8) {
		PRINT("WTF");
		exit(-1);
	}
	memcpy(arcname, dir_filename, dir_filename_len);
	for (int i = 0; i < max_archives; ++i) {
		sprintf(arcname + dir_filename_len - 8, "%03d.vpk", i);
		if (AFile_Success != aFileOpen(collection->archives+i, arcname)) {
			PRINTF("Cannot open archive %s", arcname);
			exit(-1);
		}
	}

	stackFreeUpToPosition(temp, temp_stack_top);
	collection->head.open = vpkCollectionOpen;
	collection->head.close = vpkCollectionClose;
}
