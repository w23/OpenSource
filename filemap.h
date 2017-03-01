#ifndef FILEMAP_H__INCLUDED
#define FILEMAP_H__INCLUDED

#include <stddef.h> /* size_t */

struct AFileMap {
	const void *map;
	size_t size;
	struct {
		int fd;
	} impl_;
};

struct AFileMap aFileMapOpen(const char *filename);
void aFileMapClose(struct AFileMap *file);

#endif /* ifndef FILEMAP_H__INCLUDED */
