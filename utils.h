#pragma once
#include <stdlib.h>
#include <stdio.h>

#define STR1_(m) #m
#define STR_(m) STR1_(m)
#define PRINT_(fmt, ...) fprintf(stderr, __FILE__ ":" STR_(__LINE__) ": " fmt "\n", __VA_ARGS__)
#define ASSERT(cond) if (!(cond)){PRINT_("%s failed", #cond); abort();}
