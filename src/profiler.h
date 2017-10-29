#pragma once
#include "mempools.h"
#include "atto/app.h"

void profilerInit();
void profileEvent(const char *msg, ATimeUs delta);
int profilerFrame(struct Stack *stack_temp);
