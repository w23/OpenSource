#include "filemap.h"

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

