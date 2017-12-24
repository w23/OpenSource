#pragma once

#include <stdint.h>

typedef struct { int r, g, b; } ETC1Color;

// in4x4 layout is column-major
void etc1PackBlock(const ETC1Color *in4x4, uint8_t *out);
