#include "protocol.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "screen.h"

void log_info(const char *fmt,...)
{
    char msg[128];
    va_list args;

    va_start(args, fmt);
    int size = vsnprintf(msg, 128, fmt, args);
    va_end(args);
    ui_comm_webgui_set_response_cb(NULL, NULL);
    ui_comm_webgui_send(msg, size);
    ui_comm_webgui_wait_response();
    ui_comm_webgui_clear();
}