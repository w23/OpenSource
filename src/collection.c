#include "collection.h"
#include "common.h"
#include "vpk.h"
#include "zip.h"
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

struct FilesystemCollectionFile {
	struct IFile head;
	struct AFile file;
	struct Stack *temp;
};

struct FilesystemCollection {
	struct ICollection head;
	struct Memories mem;
	char *prefix;
};

static size_t filesystemCollectionFile_Read(struct IFile *file, size_t offset, size_t size, void *buffer) {
	struct FilesystemCollectionFile *f = (void*)file;
	const size_t result = aFileReadAtOffset(&f->file, offset, size, buffer);
	return result != AFileError ? result : 0;
}

static void filesystemCollectionFile_Close(struct IFile *file) {
	struct FilesystemCollectionFile *f = (void*)file;
	aFileClose(&f->file);
	stackFreeUpToPosition(f->temp, f);
}

static void filesystemCollectionClose(struct ICollection *collection) {
	(void)(collection);
	/* TODO ensure all files are closed */
	/* TODO free memory */
}

static char *makeResourceFilename(struct Stack *temp, const char *prefix, const char *name, enum FileType type) {
	const char *subdir = NULL;
	const char *suffix = NULL;

	switch (type) {
		case File_Map: subdir = "maps/"; suffix = ".bsp"; break;
		case File_Material: subdir = "materials/"; suffix = ".vmt"; break;
		case File_Texture: subdir = "materials/"; suffix = ".vtf"; break;
		case File_Model: subdir = "models/"; suffix = ".mdl"; break;
	}

	const int prefix_len = prefix ? strlen(prefix) : 0;
	const int subdir_len = strlen(subdir);
	const int name_len = strlen(name);
	const int suffix_len = strlen(suffix);
	const int name_length = prefix_len + subdir_len + name_len + suffix_len + 1;

	char *output = stackAlloc(temp, name_length);
	if (!output) return NULL;

	char *c = output;
	if (prefix)
		for (int i = 0; i < prefix_len; ++i)
			*c++ = prefix[i];

	for (int i = 0; i < subdir_len; ++i)
		*c++ = subdir[i];

	for (int i = 0; i < name_len; ++i) {
		char C = tolower(name[i]);
		*c++ = (C == '\\') ? '/' : C;
	}

	for (int i = 0; i < suffix_len; ++i)
		*c++ = suffix[i];

	*c = '\0';

	return output;
}

static enum CollectionOpenResult filesystemCollectionOpen(struct ICollection *collection,
			const char *name, enum FileType type, struct IFile **out_file) {
	struct FilesystemCollection *fsc = (struct FilesystemCollection*)collection;

	*out_file = NULL;

	struct FilesystemCollectionFile *file = stackAlloc(fsc->mem.temp, sizeof(*file));
	char *filename = makeResourceFilename(fsc->mem.temp, fsc->prefix, name, type);

	if (!file || !filename) {
		PRINTF("Not enough memory for file %s", name);
		return CollectionOpen_NotEnoughMemory;
	}

	if (aFileOpen(&file->file, filename) != AFile_Success) {
		stackFreeUpToPosition(fsc->mem.temp, file);
		return CollectionOpen_NotFound;
		
	}

	file->head.size = file->file.size;
	file->head.read = filesystemCollectionFile_Read;
	file->head.close = filesystemCollectionFile_Close;
	file->temp = fsc->mem.temp;
	*out_file = &file->head;

	stackFreeUpToPosition(fsc->mem.temp, filename);
	return CollectionOpen_Success;
}

struct ICollection *collectionCreateFilesystem(struct Memories *mem, const char *dir) {
	int dir_len = strlen(dir);
	struct FilesystemCollection *collection = stackAlloc(mem->persistent, sizeof(*collection) + dir_len + 2);

	if (!collection)
		return NULL;

	memset(collection, 0, sizeof *collection);
	collection->mem = *mem;
	collection->prefix = (char*)(collection + 1);

	memcpy(collection->prefix, dir, dir_len);
	collection->prefix[dir_len] = '/';
	collection->prefix[dir_len+1] = '\0';

	collection->head.open = filesystemCollectionOpen;
	collection->head.close = filesystemCollectionClose;
	return &collection->head;
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

struct VPKFileMetadata {
	struct StringView filename;
	int archive;
	struct {
		size_t off, size;
	} dir, arc;
};

#define MAX_VPK_ARCHIVES 16

struct VPKCollection {
	struct ICollection head;
	struct Memories mem;
	struct AFileMap directory;
	struct AFile archives[MAX_VPK_ARCHIVES];
	struct VPKFileMetadata *files;
	int files_count;
};

struct VPKCollectionFile {
	struct IFile head;
	const struct VPKFileMetadata *metadata;
	struct VPKCollection *collection;
};

static void vpkCollectionClose(struct ICollection *collection) {
	(void)(collection);
	/* FIXME close handles and free memory */
}

static size_t vpkCollectionFileRead(struct IFile *file, size_t offset, size_t size, void *buffer) {
	struct VPKCollectionFile *f = (struct VPKCollectionFile*)file;
	const struct VPKFileMetadata *meta = f->metadata;

	size_t size_read = 0;
	if (offset < meta->dir.size) {
		const void *begin = ((char*)f->collection->directory.map) + offset + meta->dir.off;
		const size_t dir_size_left = meta->dir.size - offset;
		if (size < dir_size_left) {
			memcpy(buffer, begin, size);
			return size;
		}

		const size_t size_to_read = size - dir_size_left;
		memcpy(buffer, begin, size_to_read);

		buffer = ((char*)buffer) + size_to_read;
		offset += size_to_read;
		size -= size_to_read;
		size_read += size_to_read;
	}

	offset -= meta->dir.size;

	if (offset < meta->arc.size)
		size_read += aFileReadAtOffset(&f->collection->archives[meta->archive], meta->arc.off + offset, size, buffer);

	return size_read;
}

static void vpkCollectionFileClose(struct IFile *file) {
	struct VPKCollectionFile *f = (void*)file;
	stackFreeUpToPosition(f->collection->mem.temp, f);
}

static enum CollectionOpenResult vpkCollectionFileOpen(struct ICollection *collection,
		const char *name, enum FileType type, struct IFile **out_file) {
	struct VPKCollection *vpkc = (struct VPKCollection*)collection;

	*out_file = NULL;

	struct VPKCollectionFile *file = stackAlloc(vpkc->mem.temp, sizeof(*file));
	char *filename = makeResourceFilename(vpkc->mem.temp, NULL, name, type);

	if (!file || !filename) {
		PRINTF("Not enough memory for file %s", name);
		return CollectionOpen_NotEnoughMemory;
	}

	// binary search
	{
		const struct VPKFileMetadata *begin = vpkc->files;
		int count = vpkc->files_count;

		while (count > 0) {
			int item = count / 2;

			const struct VPKFileMetadata *meta = begin + item;
			const int comparison = strcmp(filename, meta->filename.s);
			if (comparison == 0) {
				file->metadata = meta;
				file->collection = vpkc;
				file->head.size = meta->arc.size + meta->dir.size;
				file->head.read = vpkCollectionFileRead;
				file->head.close = vpkCollectionFileClose;
				*out_file = &file->head;
				stackFreeUpToPosition(vpkc->mem.temp, filename);
				return CollectionOpen_Success;
			}

			if (comparison < 0) {
				count = item;
			} else {
				count = count - item - 1;
				begin += item + 1;
			}
		}
	}

	stackFreeUpToPosition(vpkc->mem.temp, file);
	return CollectionOpen_NotFound;
}

static int vpkMetadataCompare(const void *a, const void *b) {
	const struct VPKFileMetadata *am = a, *bm = b;
	return strcmp(am->filename.s, bm->filename.s);
}

struct ICollection *collectionCreateVPK(struct Memories *mem, const char *dirfile) {
	PRINTF("Opening collection %s", dirfile);
	struct VPKCollection *collection = stackAlloc(mem->persistent, sizeof(*collection));

	if (!collection)
		return NULL;

	memset(collection, 0, sizeof *collection);
	collection->mem = *mem;

	void *const temp_stack_top = stackGetCursor(mem->temp);

	collection->directory = aFileMapOpen(dirfile);
	if (!collection->directory.map) {
		PRINTF("Cannot open %s", dirfile);
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

	struct VPKFileMetadata *files_begin = stackGetCursor(mem->persistent), *files_end = files_begin;

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
				char *filename_temp = stackAlloc(mem->temp, filename_len);
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

				/*
				PRINTF("%s crc=%08x pre=%d arc=%d(%04x) off=%d len=%d",
					filename_temp,
					entry->crc,
					entry->preloadBytes, entry->archive, entry->archive,
					entry->archiveOffset, entry->archiveLength);
				*/

				struct VPKFileMetadata *file = stackAlloc(mem->persistent, sizeof(struct VPKFileMetadata));
				if (!file) {
					PRINT("Not enough persistent memory");
					exit(-1);
				}
				memset(file, 0, sizeof(*file));

				file->filename.s = filename_temp;
				file->filename.len = filename_len - 1;
				if (entry->preloadBytes) {
					file->dir.off = c - (char*)dir;
					file->dir.size = entry->preloadBytes;
				}

				if (entry->archiveLength) {
					file->archive = entry->archive != 0x7fff ? entry->archive : -1;
					file->arc.off = entry->archiveOffset;
					file->arc.size = entry->archiveLength;
				}

				if (file->archive > max_archives)
					max_archives = file->archive;

				files_end = file + 1;
				++collection->files_count;

				c += entry->preloadBytes;
			} // for filenames
		} // for paths
	} // for extensions

	// sort
	qsort(files_begin, files_end - files_begin, sizeof(*files_begin), vpkMetadataCompare);
	
	// store filenames in persistent memory
	for (struct VPKFileMetadata *file = files_begin; file != files_end; ++file) {
		char *string = stackAlloc(mem->persistent, file->filename.len + 1);
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

	const int dirfile_len = strlen(dirfile) + 1;
	char *arcname = alloca(dirfile_len);
	if (!arcname || dirfile_len < 8) {
		PRINT("WTF");
		exit(-1);
	}
	memcpy(arcname, dirfile, dirfile_len);
	for (int i = 0; i <= max_archives; ++i) {
		sprintf(arcname + dirfile_len - 8, "%03d.vpk", i);
		if (AFile_Success != aFileOpen(collection->archives+i, arcname)) {
			PRINTF("Cannot open archive %s", arcname);
			exit(-1);
		}
	}

	stackFreeUpToPosition(mem->temp, temp_stack_top);
	collection->head.open = vpkCollectionFileOpen;
	collection->head.close = vpkCollectionClose;
	collection->files = files_begin;

	return &collection->head;
}

struct PakfileFileMetadata {
	struct StringView filename;
	const void *data;
	uint32_t size;
};

struct PakfileCollection {
	struct ICollection head;
	struct Memories mem;
	struct PakfileFileMetadata *files;
	int files_count;
};

struct PakfileCollectionFile {
	struct IFile head;
	const struct PakfileFileMetadata *metadata;
	struct Stack *stack;
};

static void pakfileCollectionClose(struct ICollection *collection) {
	struct PakfileCollection *pc = (void*)collection;
	stackFreeUpToPosition(pc->mem.persistent, pc->files);
}

static size_t pakfileCollectionFileRead(struct IFile *file, size_t offset, size_t size, void *buffer) {
	struct PakfileCollectionFile *f = (struct PakfileCollectionFile*)file;
	const struct PakfileFileMetadata *meta = f->metadata;

	if (offset > meta->size)
		return 0;
	if (offset + size > meta->size)
		size = meta->size - offset;
	memcpy(buffer, (const char*)meta->data + offset, size);
	return size;
}

static void pakfileCollectionFileClose(struct IFile *file) {
	struct PakfileCollectionFile *f = (void*)file;
	stackFreeUpToPosition(f->stack, f);
}

static enum CollectionOpenResult pakfileCollectionFileOpen(struct ICollection *collection,
		const char *name, enum FileType type, struct IFile **out_file) {
	struct PakfileCollection *pakfilec = (struct PakfileCollection*)collection;

	*out_file = NULL;

	struct PakfileCollectionFile *file = stackAlloc(pakfilec->mem.temp, sizeof(*file));
	char *filename = makeResourceFilename(pakfilec->mem.temp, NULL, name, type);

	if (!file || !filename) {
		PRINTF("Not enough memory for file %s", name);
		return CollectionOpen_NotEnoughMemory;
	}

	// binary search
	{
		const struct PakfileFileMetadata *begin = pakfilec->files;
		int count = pakfilec->files_count;

		while (count > 0) {
			int item = count / 2;

			const struct PakfileFileMetadata *meta = begin + item;
			const int comparison = strncmp(filename, meta->filename.s, meta->filename.len);
			if (comparison == 0) {
				file->metadata = meta;
				file->stack = pakfilec->mem.temp;
				file->head.size = meta->size;
				file->head.read = pakfileCollectionFileRead;
				file->head.close = pakfileCollectionFileClose;
				*out_file = &file->head;
				stackFreeUpToPosition(pakfilec->mem.temp, filename);
				return CollectionOpen_Success;
			}

			if (comparison < 0) {
				count = item;
			} else {
				count = count - item - 1;
				begin += item + 1;
			}
		}
	}

	stackFreeUpToPosition(pakfilec->mem.temp, file);
	return CollectionOpen_NotFound;
}

static int pakfileMetadataCompare(const void *a, const void *b) {
	const struct PakfileFileMetadata *am = a, *bm = b;
	return strncmp(am->filename.s, bm->filename.s,
			am->filename.len < bm->filename.len ?
			am->filename.len : bm->filename.len);
}

struct ICollection *collectionCreatePakfile(struct Memories *mem, const void *pakfile, uint32_t size) {

	(void)mem;

	// 1. need to find zip end of directory 
	if (size < (int)sizeof(struct ZipEndOfDirectory)) {
		PRINT("Invalid pakfile size");
		return NULL;
	}

	int eod_offset = size - sizeof(struct ZipEndOfDirectory);
	const struct ZipEndOfDirectory *eod;
	for (;;) {
		eod = (void*)((const char*)pakfile + eod_offset);
		// TODO what if comment contain signature?
		if (eod->signature == ZipEndOfDirectory_Signature)
			break;

		--eod_offset;
	}

	if (!eod) {
		PRINT("End-of-directory not found");
		return NULL;
	}

	if (eod->dir_offset > size || eod->dir_size > size || eod->dir_size + eod->dir_offset > size) {
		PRINTF("Wrong pakfile directory sizes; size=%d, dir_offset=%d, dir_size=%d",
				size, eod->dir_offset, eod->dir_size);
		return NULL;
	}

	struct PakfileFileMetadata *metadata_start = stackGetCursor(mem->persistent);
	int files_count = 0;

	const char *dir = (const char*)pakfile + eod->dir_offset, *dir_end = dir + eod->dir_size;
	for (;;) {
		if (dir == dir_end)
			break;

		if (dir_end - dir < (long)sizeof(struct ZipFileHeader)) {
			PRINT("Corrupted directory");
			break;
		}

		const struct ZipFileHeader *fileheader = (void*)dir;
		if (fileheader->signature != ZipFileHeader_Signature) {
			PRINTF("Wrong file header signature: %08x", fileheader->signature);
			break;
		}

		// TODO overflow
		const char *next_dir = dir + sizeof(struct ZipFileHeader) + fileheader->filename_length + fileheader->extra_field_length + fileheader->file_comment_length;
		if (dir > dir_end) {
			PRINT("Corrupted directory");
			break;
		}

		if (fileheader->compression != 0 || fileheader->uncompressed_size != fileheader->compressed_size) {
			PRINTF("Compression method %d is not supported", fileheader->compression);
		} else {
			const char *filename = dir + sizeof(struct ZipFileHeader);

			const char *local = (const char*)pakfile + fileheader->local_offset;
			if (size - (local - (const char*)pakfile) < (long)sizeof(struct ZipLocalFileHeader)) {
				PRINT("Local file header OOB");
				break;
			}

			const struct ZipLocalFileHeader *localheader = (void*)local;
			if (localheader->signature != ZipLocalFileHeader_Signature) {
				PRINTF("Invalid local file header signature %08x", localheader->signature);
				break;
			}

			// TODO overflow
			local += sizeof(*localheader) + localheader->filename_length + localheader->extra_field_length;

			if ((local - (const char*)pakfile) + fileheader->compressed_size > size) {
				PRINT("File data OOB");
				break;
			}

			struct PakfileFileMetadata *metadata = stackAlloc(mem->persistent, sizeof(*metadata));
			if (!metadata) {
				PRINT("Not enough memory");
				exit(-1);
			}

			metadata->data = local;
			metadata->size = fileheader->uncompressed_size;
			metadata->filename.s = filename;
			metadata->filename.len = fileheader->filename_length;
			++files_count;

			/*
			PRINTF("FILE \""PRI_SV"\" size=%d offset=%d",
				PASS_SV(metadata->filename), fileheader->uncompressed_size, fileheader->local_offset);
			*/
		}

		dir = next_dir;
	}

	if (dir != dir_end) {
		PRINT("Something went wrong");
		exit(-1);
	}

	// sort
	qsort(metadata_start, files_count, sizeof(*metadata_start), pakfileMetadataCompare);

	struct PakfileCollection *collection = stackAlloc(mem->persistent, sizeof(*collection));
	collection->mem = *mem;
	collection->files = metadata_start;
	collection->files_count = files_count;
	collection->head.open = pakfileCollectionFileOpen;
	collection->head.close = pakfileCollectionClose;

	return &collection->head;
}

