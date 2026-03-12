#include "protocol.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "screen.h"

void log_info(const char *fmt,...)
{
    static char msg[128] = "log ";
    va_list args;

    va_start(args, fmt);
    vsnprintf(msg + 4, 124, fmt, args);
    va_end(args);
    ui_comm_webgui_set_response_cb(NULL, NULL);
    ui_comm_webgui_send(msg, strlen(msg));
}
