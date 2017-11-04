#include "filemap.h"
#include "common.h"

#ifndef _WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h> /* open */
#include <sys/mman.h> /* mmap */
#include <unistd.h> /* close */
#include <stdio.h> /* perror */

struct AFileMap aFileMapOpen(const char *filename) {
	struct AFileMap ret;
	ret.map = 0;
	ret.size = 0;

	ret.impl_.fd = open(filename, O_RDONLY);
	if (ret.impl_.fd < 0) { perror("cannot open file"); goto exit; }

	struct stat stat;
	if (fstat(ret.impl_.fd, &stat) < 0) { perror("cannot fstat file"); goto exit; }

	ret.size = stat.st_size;

	ret.map = mmap(0, ret.size, PROT_READ, MAP_PRIVATE, ret.impl_.fd, 0);
	if (!ret.map) perror("cannot mmap file");

exit:
	if (!ret.map && ret.impl_.fd > 0)
		close(ret.impl_.fd);
	return ret;
}

void aFileMapClose(struct AFileMap *file) {
	if (file->map && file->impl_.fd > 0) {
		munmap((void*)file->map, file->size);
		close(file->impl_.fd);
	}
}

void aFileReset(struct AFile *file) {
	file->size = 0;
	file->impl_.fd = -1;
}

enum AFileResult aFileOpen(struct AFile *file, const char *filename) {
	file->impl_.fd = open(filename, O_RDONLY);
	if (file->impl_.fd < 0)
		return AFile_Fail;

	struct stat stat;
	fstat(file->impl_.fd, &stat);
	file->size = stat.st_size;

	return AFile_Success;
}

size_t aFileReadAtOffset(struct AFile *file, size_t off, size_t size, void *buffer) {
	ssize_t rd = pread(file->impl_.fd, buffer, size, off);
	if (rd < 0)
		perror("pread(fd)");
	return rd;
}

void aFileClose(struct AFile *file) {
	if (file->impl_.fd > 0) {
		close(file->impl_.fd);
		aFileReset(file);
	}
}
#else
struct AFileMap aFileMapOpen(const char *filename) {
	(void)filename;
	struct AFileMap map = { 0 };
	return map;
}

void aFileMapClose(struct AFileMap *file) {
	(void)file;
}

void aFileReset(struct AFile *file) {
	file->size = 0;
	file->impl_.handle = INVALID_HANDLE_VALUE;
}

enum AFileResult aFileOpen(struct AFile *file, const char *filename) {
	const int filename_len = strlen(filename);
	char *slashes = _alloca(filename_len + 1);
	for (int i = 0; filename[i] != '\0'; ++i)
		slashes[i] = filename[i] != '/' ? filename[i] : '\\';
	slashes[filename_len] = '\0';

	wchar_t *filename_w;
	const int buf_length = MultiByteToWideChar(CP_UTF8, 0, slashes, -1, NULL, 0);
	filename_w = _alloca(buf_length);
	MultiByteToWideChar(CP_UTF8, 0, slashes, -1, filename_w, buf_length);

	file->impl_.handle = CreateFileW(filename_w, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file->impl_.handle == INVALID_HANDLE_VALUE)
		return AFile_Fail;

	LARGE_INTEGER splurge_integer = { 0 };
	if (!GetFileSizeEx(file->impl_.handle, &splurge_integer)) {
		CloseHandle(file->impl_.handle);
		file->impl_.handle = INVALID_HANDLE_VALUE;
		return AFile_Fail;
	}

	file->size = (size_t)splurge_integer.QuadPart;
	return AFile_Success;
}

size_t aFileReadAtOffset(struct AFile *file, size_t off, size_t size, void *buffer) {
	OVERLAPPED overlapped;
	memset(&overlapped, 0, sizeof(overlapped));
	overlapped.Offset = off;

	DWORD read = 0;
	if (!ReadFile(file->impl_.handle, buffer, size, &read, &overlapped))
		PRINTF("Failed to read from file %p", file->impl_.handle);

	return read;
}

void aFileClose(struct AFile *file) {
	CloseHandle(file->impl_.handle);
}

#endif