#include <stdlib.h>

void log_info(const char *fmt, ...);

#ifdef ENABLE_DEBUG_TRACE
#define trace(fmt, ...) log_info(fmt, ##__VA_ARGS__)
#else
#define trace(fmt, ...)
#endif
