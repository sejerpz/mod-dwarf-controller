/*
************************************************************************************************************************
*           Binding manager for the pedalboard builder. See the header for what it is.
************************************************************************************************************************
*/

/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "hardware.h"
#include "mod-protocol.h"
#include "ui_comm.h"
#include "utils.h"

#include "mode_builder.h"
#include "mode_builder_bindings_manager.h"


/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/

// mod-ui cuts a label to a column's width, 13 characters, plus the terminator
#define BB_LABEL_SIZE       16

// a plugin with more control ports than this exists, and is windowed like the catalogue
#define BB_MAX_PARAMS       24
/*
 * Every slot on the panel: each knob once per sub-page it turns over in, then the
 * footswitches, which do not. Three knobs over three sub-pages, two footswitches and the
 * pair of them together is twelve; the room is for whatever else a descriptor declares.
 */
#define BB_MAX_ACTUATORS    24
// the pages of the board, drawn one-based
#define BB_MAX_PAGES        8

// hardware_timestamp() counts in units of 500us, so two ticks to the millisecond
#define BB_BLINK_TICKS      (400 * 2)


/*
************************************************************************************************************************
*           LOCAL GLOBAL VARIABLES
************************************************************************************************************************
*/

static uint8_t g_open;
static int16_t g_node = BM_NONE;
static char g_title[PLUGIN_MAP_TITLE_SIZE];

/* column one: the box's control ports, a window at a time over a longer list */
static char g_param_text[BB_MAX_PARAMS][BB_LABEL_SIZE];
static char *g_param_rows[BB_MAX_PARAMS];
static uint8_t g_param_count;           /* in the window */
static uint16_t g_param_total;
static uint16_t g_param_first;
static int16_t g_param_hover;           /* absolute, not window relative */

/*
 * The page of the board, zero based here and drawn one based over the second column. Not
 * a column of its own: it is one number, and a heading is what a single number wants.
 */
static uint8_t g_page;
static char g_page_title[12];

/* column two: every slot on the panel, as the host lists them */
static char g_act_text[BB_MAX_ACTUATORS][BB_LABEL_SIZE];
static char *g_act_rows[BB_MAX_ACTUATORS];
static uint8_t g_act_count;
static int16_t g_act_hover;

/* which of those already carry an addressing on the page in the heading */
static uint8_t g_act_taken[BB_MAX_ACTUATORS];

/* the slot a bind took the parameter off, since a parameter holds one addressing */
static int16_t g_freed = BM_NONE;

/* DEL is armed before it acts, and this is which half of the flash we are on */
static uint8_t g_armed;
static uint8_t g_blink_off;
static uint32_t g_blink_stamp;


/*
************************************************************************************************************************
*           LOCAL FUNCTIONS
************************************************************************************************************************
*/

static void disarm(void)
{
    g_armed = 0;
    g_blink_off = 0;
}

static void parse_actuators(void *data, menu_item_t *item)
{
    (void) item;
    char **list = data;
    uint32_t i, count;

    g_act_count = 0;

    if (!list || !list[0] || !list[1] || atoi(list[1]) == -1) return;
    if (!list[2]) return;

    count = (uint32_t) atoi(list[2]);

    /*
     * One token an entry: its name. Which actuator and which sub-page a row is, is the
     * host's business -- a knob appears three times over and reads as three places to put
     * a parameter, which is what it is.
     */
    for (i = 0; i < count && g_act_count < BB_MAX_ACTUATORS; i++)
    {
        if (!list[3 + i]) break;

        strncpy(g_act_text[g_act_count], list[3 + i], BB_LABEL_SIZE - 1);
        g_act_text[g_act_count][BB_LABEL_SIZE - 1] = 0;
        plugin_map_unescape(g_act_text[g_act_count]);

        g_act_rows[g_act_count] = g_act_text[g_act_count];
        g_act_count++;
    }
}

static void parse_params(void *data, menu_item_t *item)
{
    (void) item;
    char **list = data;
    uint32_t i, count;

    g_param_count = 0;
    g_param_total = 0;
    g_param_first = 0;

    if (!list || !list[0] || !list[1] || atoi(list[1]) == -1) return;
    if (!list[2] || !list[3] || !list[4]) return;

    g_param_total = (uint16_t) atoi(list[2]);
    g_param_first = (uint16_t) atoi(list[3]);
    count = (uint32_t) atoi(list[4]);

    for (i = 0; i < count && g_param_count < BB_MAX_PARAMS; i++)
    {
        if (!list[5 + i]) break;

        strncpy(g_param_text[g_param_count], list[5 + i], BB_LABEL_SIZE - 1);
        g_param_text[g_param_count][BB_LABEL_SIZE - 1] = 0;
        plugin_map_unescape(g_param_text[g_param_count]);

        g_param_rows[g_param_count] = g_param_text[g_param_count];
        g_param_count++;
    }
}

static void parse_bindings(void *data, menu_item_t *item)
{
    (void) item;
    char **list = data;
    uint32_t i, count;

    memset(g_act_taken, 0, sizeof g_act_taken);

    if (!list || !list[0] || !list[1] || atoi(list[1]) == -1) return;
    if (!list[2]) return;

    count = (uint32_t) atoi(list[2]);

    // two tokens an entry: which actuator is spoken for, and by what
    for (i = 0; i < count; i++)
    {
        char **entry = &list[3 + i * 2];
        int16_t at;

        if (!entry[0] || !entry[1]) break;

        at = (int16_t) atoi(entry[0]);
        if (at >= 0 && at < BB_MAX_ACTUATORS) g_act_taken[at] = 1;
    }
}

static void request_actuators(void)
{
    uint8_t i;
    char buffer[16];
    memset(buffer, 0, sizeof buffer);

    ui_comm_webgui_set_response_cb(parse_actuators, NULL);
    ui_comm_webgui_clear_tx_buffer();

    i = copy_command((char *)buffer, CMD_BUILDER_BINDING_ACTUATORS);
    buffer[i++] = 0;

    ui_comm_webgui_send(buffer, i);
    ui_comm_webgui_wait_response();
}

static void request_params(uint16_t first)
{
    uint8_t i;
    char buffer[32];
    memset(buffer, 0, sizeof buffer);

    ui_comm_webgui_set_response_cb(parse_params, NULL);
    ui_comm_webgui_clear_tx_buffer();

    i = copy_command((char *)buffer, CMD_BUILDER_BINDING_PARAMS);
    i += int_to_str(g_node, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str(first, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str(BB_MAX_PARAMS, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = 0;

    ui_comm_webgui_send(buffer, i);
    ui_comm_webgui_wait_response();
}

static void request_bindings(void)
{
    uint8_t i;
    char buffer[24];
    memset(buffer, 0, sizeof buffer);

    ui_comm_webgui_set_response_cb(parse_bindings, NULL);
    ui_comm_webgui_clear_tx_buffer();

    i = copy_command((char *)buffer, CMD_BUILDER_BINDING_LIST);
    i += int_to_str(g_page, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = 0;

    ui_comm_webgui_send(buffer, i);
    ui_comm_webgui_wait_response();
}

/*
 * A bind answers with whether it was accepted and which slot the parameter came off. It
 * holds one addressing, so putting it somewhere else takes it off wherever it was -- and
 * the mark there has to go with it. -1 when it was on nothing, or on another page, which
 * the device reads afresh when it goes there.
 */
static void parse_bind(void *data, menu_item_t *item)
{
    (void) item;
    char **list = data;

    g_freed = BM_NONE;

    if (!list || !list[0] || !list[1] || atoi(list[1]) == -1) return;
    if (!list[2] || !list[3]) return;
    if (atoi(list[2]) == 0) return;

    g_freed = (int16_t) atoi(list[3]);
}

static void request_bind(int16_t param, int16_t actuator)
{
    uint8_t i;
    char buffer[40];
    memset(buffer, 0, sizeof buffer);

    ui_comm_webgui_set_response_cb(parse_bind, NULL);
    ui_comm_webgui_clear_tx_buffer();

    i = copy_command((char *)buffer, CMD_BUILDER_BINDING_ADD);
    i += int_to_str(g_node, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str(param, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str(g_page, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str(actuator, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = 0;

    ui_comm_webgui_send(buffer, i);
    ui_comm_webgui_wait_response();
}

static void request_unbind(int16_t actuator)
{
    uint8_t i;
    char buffer[24];
    memset(buffer, 0, sizeof buffer);

    ui_comm_webgui_set_response_cb(NULL, NULL);
    ui_comm_webgui_clear_tx_buffer();

    i = copy_command((char *)buffer, CMD_BUILDER_BINDING_DELETE);
    i += int_to_str(g_page, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str(actuator, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = 0;

    ui_comm_webgui_send(buffer, i);
    ui_comm_webgui_wait_response();
}

/* "PAGE 3", over the second column */
static void build_page_title(void)
{
    uint8_t i = 0;

    g_page_title[i++] = 'P';
    g_page_title[i++] = 'A';
    g_page_title[i++] = 'G';
    g_page_title[i++] = 'E';
    g_page_title[i++] = ' ';
    g_page_title[i++] = (char)('1' + g_page);
    g_page_title[i] = 0;
}

/* slides the parameter window when the hover has walked off either end of it */
static void follow_hover(void)
{
    if (g_param_total == 0) return;

    if (g_param_hover < 0) g_param_hover = 0;
    if (g_param_hover >= (int16_t) g_param_total) g_param_hover = g_param_total - 1;

    if (g_param_hover < (int16_t) g_param_first ||
        g_param_hover >= (int16_t)(g_param_first + g_param_count))
    {
        uint16_t first = 0;

        if (g_param_hover >= (int16_t) BB_MAX_PARAMS)
            first = (uint16_t)(g_param_hover - BB_MAX_PARAMS / 2);

        request_params(first);
    }
}


/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

void BM_bindings_manager_init(void)
{
    g_page = 0;
    build_page_title();
}

void BM_bindings_manager_open(plugin_map_t *map)
{
    const plugin_map_node_t *node = map ? plugin_map_selected(map) : NULL;

    // the capture and playback boxes are part of the picture and have nothing to bind
    if (!node || node->kind != BM_PLUGIN) return;

    g_open = 1;
    g_node = node->id;

    strncpy(g_title, node->title, PLUGIN_MAP_TITLE_SIZE - 1);
    g_title[PLUGIN_MAP_TITLE_SIZE - 1] = 0;

    g_param_hover = 0;
    g_page = 0;
    g_act_hover = 0;
    disarm();

    build_page_title();

    request_params(0);
    request_actuators();
    request_bindings();
}

uint8_t BM_bindings_manager_is_open(void)
{
    return g_open;
}

void BM_bindings_manager_close(void)
{
    g_open = 0;
    disarm();
}

void BM_bindings_manager_turn(uint8_t encoder, int8_t step)
{
    if (!g_open) return;

    // moving the cursor is not a confirmation, whichever column it moves in
    disarm();

    if (encoder == 0)
    {
        g_param_hover += step;
        follow_hover();
        return;
    }

    if (encoder == 1)
    {
        int16_t next = g_act_hover + step;

        if (next < 0 || next >= (int16_t) g_act_count) return;

        g_act_hover = next;
        return;
    }

    if (encoder == 2)
    {
        int16_t next = (int16_t) g_page + step;

        if (next < 0 || next >= BB_MAX_PAGES) return;

        g_page = (uint8_t) next;
        build_page_title();

        // a different page is a different set of slots
        request_bindings();
    }
}

void BM_bindings_manager_add(void)
{
    if (!g_open) return;
    if (g_param_total == 0 || g_act_count == 0) return;

    disarm();

    request_bind(g_param_hover, g_act_hover);

    /*
     * Marked here rather than read back. The host answers before it starts, because making
     * an addressing sends this device commands of its own and it cannot answer one while it
     * is spinning on a reply -- so by the time the reply arrives there is nothing yet to
     * read back.
     *
     * Emptied before filled, and in that order: rebinding a parameter to the slot it is
     * already on frees and fills the same one.
     */
    if (g_freed >= 0 && g_freed < (int16_t) g_act_count)
        g_act_taken[g_freed] = 0;

    if (g_act_hover >= 0 && g_act_hover < (int16_t) g_act_count)
        g_act_taken[g_act_hover] = 1;
}

void BM_bindings_manager_del(void)
{
    if (!g_open) return;
    if (g_act_count == 0) return;
    if (g_act_hover < 0 || g_act_hover >= (int16_t) g_act_count) return;

    // nothing there to take off, so nothing to arm either
    if (!g_act_taken[g_act_hover])
    {
        disarm();
        return;
    }

    if (!g_armed)
    {
        // the first press arms it and the row starts flashing; the second is the one that
        // drops the binding. An addressing is not something to lose to a mis-hit button.
        g_armed = 1;
        g_blink_off = 0;
        g_blink_stamp = hardware_timestamp();
        return;
    }

    request_unbind(g_act_hover);
    disarm();

    // emptied, for the same reason the bind marks rather than reads back
    g_act_taken[g_act_hover] = 0;
}

uint8_t BM_bindings_manager_tick(void)
{
    if (!g_open || !g_armed) return 0;

    if ((hardware_timestamp() - g_blink_stamp) < BB_BLINK_TICKS) return 0;

    g_blink_stamp = hardware_timestamp();
    g_blink_off = !g_blink_off;
    return 1;
}

void BM_bindings_manager_fill(bindings_t *model)
{
    model->title = g_title[0] ? g_title : "BINDINGS";

    model->params = g_param_rows;
    model->param_count = g_param_count;
    // the column widget works in the rows it holds, so the hover is window relative
    model->param_hover = g_param_hover - (int16_t) g_param_first;

    model->page = g_page_title;

    model->actuators = g_act_rows;
    model->actuator_count = g_act_count;
    model->actuator_hover = g_act_hover;
    model->actuator_taken = g_act_taken;

    model->can_delete = (g_act_count > 0 && g_act_hover >= 0 &&
                         g_act_hover < (int16_t) g_act_count && g_act_taken[g_act_hover]);
    model->blink_reverse = !g_blink_off;
    model->armed = g_armed;
}
