#include "protocol.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

void log_info(const char *fmt,...)
{
    char msg[128];
    va_list args;

    va_start(args, fmt);
    int size = vsnprintf(msg, 128, fmt, args);
    ui_comm_webgui_set_response_cb(NULL, NULL);
    ui_comm_webgui_send(msg, size);
    ui_comm_webgui_wait_response();
    ui_comm_webgui_clear();
    va_end(args);
}