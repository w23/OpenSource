#include "common.h"

void logOpen(const char *logfile);
void logClose(void);

void logPrintf(PRINTF_ARG(const char *format), ...) PRINTF_ATTR(1, 2);

#define PRINTF(fmt, ...) logPrintf(__FILE__ ":" STR(__LINE__) ": " fmt "\n", __VA_ARGS__)
#define PRINT(msg) logPrintf(__FILE__ ":" STR(__LINE__) ": %s\n", msg)

#define ASSERT(cond) if (!(cond)){PRINTF("%s failed", #cond); abort();}
