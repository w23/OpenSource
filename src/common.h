#pragma once
#include "libc.h"

#define STR1(m) #m
#define STR(m) STR1(m)

#ifdef _MSC_VER
// MSVC compiler
#define PRINTF_ARG(fmt) _Printf_format_string_ fmt
#define PRINTF_ATTR(fmt_index, vargs_index)
#else
// gcc-compatible
#define PRINTF_ARG(fmt) fmt
#define PRINTF_ATTR(fmt_index, vargs_index) __attribute__((format(printf, fmt_index, vargs_index)))
#endif

#define COUNTOF(a) (sizeof(a) / sizeof(*(a)))

typedef struct {
	const char *str;
	int length;
} StringView;

#define PRI_SV "%.*s"
#define PRI_SVV(s) ((s).length), ((s).str)
