
/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <string.h>

#include "ui_comm.h"
#include "config.h"
#include "serial.h"

#include "FreeRTOS.h"
#include "semphr.h"

/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/

#define WEBGUI_MAX_SEM_COUNT   5


/*
************************************************************************************************************************
*           LOCAL CONSTANTS
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           LOCAL DATA TYPES
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           LOCAL MACROS
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           LOCAL GLOBAL VARIABLES
************************************************************************************************************************
*/

static  void (*g_webgui_response_cb)(void *data, menu_item_t *item) = NULL;
static  menu_item_t *g_current_item;
static volatile uint8_t  g_webgui_blocked;
static volatile xSemaphoreHandle g_webgui_sem = NULL;
static  ringbuff_t *g_webgui_rx_rb;


/*
************************************************************************************************************************
*           LOCAL FUNCTION PROTOTYPES
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           LOCAL CONFIGURATION ERRORS
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           LOCAL FUNCTIONS
************************************************************************************************************************
*/

static void webgui_rx_cb(serial_t *serial)
{
    uint8_t buffer[SERIAL_MAX_RX_BUFF_SIZE] = {};
    uint32_t size = serial_read(serial->uart_id, buffer, sizeof(buffer));
    if (size > 0)
    {
        ringbuff_write(g_webgui_rx_rb, buffer, size);
        // check end of message
        uint32_t i;
        for (i = 0; i < size; i++)
        {
            if (buffer[i] == 0)
            {
                portBASE_TYPE xHigherPriorityTaskWoken;
                xHigherPriorityTaskWoken = pdFALSE;
                xSemaphoreGiveFromISR(g_webgui_sem, &xHigherPriorityTaskWoken);
                portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
                break;
            }
        }
    }
}


/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

void ui_comm_init(void)
{
    g_webgui_sem = xSemaphoreCreateCounting(WEBGUI_MAX_SEM_COUNT, 0);
    g_webgui_rx_rb = ringbuff_create(WEBGUI_COMM_RX_BUFF_SIZE);

    serial_set_callback(WEBGUI_SERIAL, webgui_rx_cb);
}

void ui_comm_webgui_send(const char *data, uint32_t data_size)
{
    // A command, not a response: the wait for its answer starts before the bytes leave, as in
    // sys_comm_send(). Raised after sending, the flag could go up only once the answer had already
    // come and cleared it, and the wait would never end.
    if (strncmp(data, "r ", 2) != 0)
        g_webgui_blocked = 1;

    serial_send(WEBGUI_SERIAL, (const uint8_t*)data, data_size+1);
}

ringbuff_t* ui_comm_webgui_read(void)
{
    if (xSemaphoreTake(g_webgui_sem, portMAX_DELAY) == pdTRUE)
    {
        return g_webgui_rx_rb;
    }

    return NULL;
}

void ui_comm_webgui_set_response_cb(void (*resp_cb)(void *data, menu_item_t *item), menu_item_t *item)
{
    g_current_item = item;
    g_webgui_response_cb = resp_cb;
}

void ui_comm_webgui_response_cb(void *data)
{
    if (g_webgui_response_cb)
    {
        g_webgui_response_cb(data, g_current_item);
        g_webgui_response_cb = NULL;
    }

    g_webgui_blocked = 0;
}

// the flag went up when the command was sent, see ui_comm_webgui_send()
void ui_comm_webgui_wait_response(void)
{
    while (g_webgui_blocked);
}

/*
 * The same wait taken apart, for a caller that wants to do something while it lasts --
 * put a notice on screen when the host is taking its time, say. Call begin exactly where
 * ui_comm_webgui_wait_response() would have been called, then poll pending.
 */
void ui_comm_webgui_wait_begin(void)
{
    // nothing left to do: the flag went up when the command was sent, see ui_comm_webgui_send()
}

uint8_t ui_comm_webgui_wait_pending(void)
{
    return g_webgui_blocked;
}

//clear the ringbuffer
void ui_comm_webgui_clear(void)
{
    ringbuff_flush(g_webgui_rx_rb);
}

//clear the ringbuffer
void ui_comm_webgui_clear_tx_buffer(void)
{
    serial_flush_tx_buffer(WEBGUI_SERIAL);
}