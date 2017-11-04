#pragma once

#include <stdint.h>
#include <stdio.h> /* printf */
#include <stdlib.h> /* malloc */
#include <stddef.h> /* offsetof, size_t */
#include <string.h> /* memset */
#ifndef _WIN32
#include <strings.h> /* strncasecmp */
#else
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define strncasecmp _strnicmp
#endif
#include <ctype.h> /* isspace */
