#include "log.h"

#include <stdio.h>
#include <stdarg.h>

static struct {
	FILE *logfile;
} glog = {0};

void logOpen(const char *logfile) {
	glog.logfile = fopen(logfile, "w");
}

void logClose(void) {
	fclose(glog.logfile);
}

void logPrintf(const char *format, ...) {
	va_list args;
	va_start(args, format);

	if (glog.logfile) {
		va_list args_copy;
		va_copy(args_copy, args);
		vfprintf(glog.logfile, format, args_copy);
		va_end(args_copy);
	}

	vfprintf(stderr, format, args);
	va_end(args);
}
