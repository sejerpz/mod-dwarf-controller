
#include <stdlib.h>

#ifdef DEBUG
    void log_info(const char *fmt, ...);
#else
#define log_info(fmt, ...)
#endif
