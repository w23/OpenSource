#pragma once
#include "libc.h"

#define STR1(m) #m
#define STR(m) STR1(m)
#define PRINTF(fmt, ...) fprintf(stderr, __FILE__ ":" STR(__LINE__) ": " fmt "\n", __VA_ARGS__)
#define PRINT(msg) fprintf(stderr, __FILE__ ":" STR(__LINE__) ": %s\n", msg)

#define ASSERT(cond) if (!(cond)){PRINTF("%s failed", #cond); abort();}

#define COUNTOF(a) (sizeof(a) / sizeof(*(a)))

typedef struct {
	const char *str;
	int length;
} StringView;

#define PRI_SV "%.*s"
#define PRI_SVV(s) ((s).length), ((s).str)
