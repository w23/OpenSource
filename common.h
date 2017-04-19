#pragma once
#include "libc.h"

#define STR1(m) #m
#define STR(m) STR1(m)
#define PRINTF(fmt, ...) fprintf(stderr, __FILE__ ":" STR(__LINE__) ": " fmt "\n", __VA_ARGS__)
#define PRINT(msg) fprintf(stderr, __FILE__ ":" STR(__LINE__) ": %s\n", msg)
