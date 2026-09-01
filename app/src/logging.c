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
    /*
     * A whole transaction, not a fire and forget. ui_comm_webgui_send() only starts the
     * transmission, and every request_* starts by calling ui_comm_webgui_clear_tx_buffer()
     * -- so a trace left in flight is cut off mid word and the command that follows is
     * spliced into it. mod-ui answers a log, so waiting for that answer drains the line
     * before anyone else touches it.
     *
     * The cost is that a trace cannot be taken from inside a response callback, where the
     * wait would never end. Debug builds only: this file does nothing without
     * ENABLE_DEBUG_TRACE.
     */
    ui_comm_webgui_set_response_cb(NULL, NULL);
    ui_comm_webgui_clear_tx_buffer();
    ui_comm_webgui_send(msg, strlen(msg));
    ui_comm_webgui_wait_response();
}
