#pragma once

#include "libc.h"

typedef struct AFile {
	size_t size;
	struct {
#ifndef _WIN32
		int fd;
#else
		HANDLE handle;
#endif
	} impl_;
} AFile;

#define AFileError ((size_t)-1)

enum AFileResult {
	AFile_Success,
	AFile_Fail
};

/* reset file to default state, useful for initialization */
void aFileReset(struct AFile *file);
enum AFileResult aFileOpen(struct AFile *file, const char *filename);
size_t aFileReadAtOffset(struct AFile *file, size_t off, size_t size, void *buffer);
void aFileClose(struct AFile *file);
