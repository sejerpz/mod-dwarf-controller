
/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "config.h"
#include "hardware.h"
#include "protocol.h"
#include "ledz.h"
#include "naveg.h"
#include "screen.h"
#include "ui_comm.h"
#include "mode_builder.h"
#include "minimap.h"
#include "mode_builder_connmanager.h"
#include "mode_builder_plugin_manager.h"
#include "mode_builder_bindings_manager.h"

/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/
// the minimap viewport: inside the outlines print_menu_outlines() draws, above the footer
#define MINIMAP_VIEW_X 2
#define MINIMAP_VIEW_Y 9
#define MINIMAP_VIEW_W 124
#define MINIMAP_VIEW_H 43

#define PAGE_DIR_DOWN 0
#define PAGE_DIR_UP 1
#define PAGE_DIR_INIT 2

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

enum UIStates {
    DEFAULT,
    PLUGIN_SELECT,
    PLUGIN_EDIT,
    CONNECTIONS,
    ADD_PLUGIN,
    BINDINGS
};

/*
 * One line of the connection menu. A line is a box at the far end and a signal type, not a
 * pair of ports: mod-ui groups them that way because the picture does, so a stereo pair is
 * one line here and deleting it drops both cables.
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
static enum UIStates uiState = DEFAULT;
static list_clone_t g_list_clone[ENCODERS_COUNT];
static bool g_list_click = 0;

/*
 * The pedalboard graph, as mod-ui described it. Not a bitmap: the server sends a display
 * list of boxes, port stubs and cables in scene coordinates and minimap.c draws it with
 * the GLCD primitives, because a pannable bitmap would be ~65kB on a part with 96kB of
 * SRAM. The same records are the hit map: the rect of a box is both what gets drawn and
 * what gets selected.
 */
static minimap_t g_minimap;
static uint8_t g_minimap_loaded = 0;
/* the selected plugin's instance id as text, for CMD_DWARF_BUILDER_CONTROLS */
static char g_selected_uid[12];


static plugin_edit_t plugin_edit; // data for the plugin edit screen

/*
 * Unfortunately, the controller architecture is multithreaded.
 *
 * A function may be called either by the actuator task (e.g. when moving
 * encoders or pressing buttons) or by the web protocol task (e.g. when
 * adding/removing bindings or changing pages).
 *
 * Race conditions are not very common, since user interaction with buttons
 * is usually followed by a server response (for example, loading a new page
 * of controls).
 *
 * However, it can sometimes happen that a data structure is deallocated
 * or modified while the screen is being updated.
 *
 */
static SemaphoreHandle_t module_mutex = NULL;
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
static void encoder_control_add(control_t *control);
static void encoder_control_rm(uint8_t hw_id);
static void reset_list_encoders(void);
static void clone_list_encoders(control_t *control);
static void request_minimap(int16_t focus_id, uint8_t initial);
static void request_control_page(control_t *control, uint8_t dir);
static void send_control_set(control_t *control);

// control assigned to display
static void encoder_control_add(control_t *control)
{
    if (control->hw_id >= ENCODERS_COUNT) return;

    xSemaphoreTake(module_mutex, portMAX_DELAY);
    // checks if is already a control assigned in this display and remove it
    control_t *prev_control = plugin_edit.controls[control->hw_id];

    // cleanup previous
    if (prev_control)
    {
        data_free_control(prev_control);
    }

    // assign the new control
    plugin_edit.controls[control->hw_id] = control;

    if (control->properties & (FLAG_CONTROL_REVERSE | FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS))
    {
        control->step = 0;
        uint8_t i;
        for (i = 0; i < control->scale_points_count; i++)
        {
            if (floats_are_equal(control->value, control->scale_points[i]->value))
            {
                control->step = i;
                break;
            }
        }

        clone_list_encoders(control);

    }
    else if (control->properties & FLAG_CONTROL_LOGARITHMIC)
    {
        if (float_is_zero(control->minimum))
            control->minimum = FLT_MIN;

        if (float_is_zero(control->maximum))
            control->maximum = FLT_MIN;

        if (float_is_zero(control->value))
            control->value = FLT_MIN;

        control->step =
            (control->steps - 1) * log(control->value / control->minimum) / log(control->maximum / control->minimum);
    }
    else if (control->properties & FLAG_CONTROL_INTEGER)
    {
        control->steps = (control->maximum - control->minimum) + 1;
        control->step =
            (control->value - control->minimum) / ((control->maximum - control->minimum) / control->steps);
    }
    else if (control->properties & (FLAG_CONTROL_BYPASS | FLAG_CONTROL_TOGGLED))
    {
        control->steps = 1;
        control->step = control->value;
    }
    else
    {
        control->step =
            (control->value - control->minimum) / ((control->maximum - control->minimum) / control->steps);
    }

    /*
     * Only where the encoders are what is on the panel. A control can arrive while the
     * builder is showing something else entirely -- binding a parameter from the bindings
     * screen has the host send one straight back -- and drawing it there would paint the
     * control over a screen the user is still working in.
     */
    if (naveg_get_current_mode() == MODE_BUILDER && uiState == PLUGIN_EDIT)
    {
        //if screen overlay active, update that
        if ((hardware_get_overlay_counter() || !control->scroll_dir) && (plugin_edit.current_overlay_control_index == control->hw_id))
        {
            BM_print_control_overlay(control, ENCODER_LIST_TIMEOUT);
        }
        else
        {
            // update the control screen
            if (plugin_edit.current_overlay_control_index == -1)
                screen_encoder(control, control->hw_id);
        }
    }

    xSemaphoreGive(module_mutex);
}

// control removed from display
static void encoder_control_rm(uint8_t hw_id)
{
    if (hw_id > ENCODERS_COUNT) return;

    if (naveg_get_current_mode() == MODE_BUILDER)
    {
        xSemaphoreTake(module_mutex, portMAX_DELAY);

        control_t *control = plugin_edit.controls[hw_id];

        if (control)
        {
            plugin_edit.controls[hw_id] = NULL;
            data_free_control(control);

        }

        /*
         * Cleared on screen only where the encoders are what is on the panel. Removing a
         * plugin, or binding a parameter, has the host send controls back on its own, and
         * every one of them takes the previous one off first -- which would wipe a strip of
         * whatever screen the builder is actually showing.
         */
        if (uiState == PLUGIN_EDIT && hardware_get_overlay_counter() == 0)
            screen_encoder(NULL, hw_id);

        xSemaphoreGive(module_mutex);
    }
}

static void reset_list_encoders(void)
{
    uint8_t q, i;
    for (q = 0; q < ENCODERS_COUNT; q++) {
        control_t *control = plugin_edit.controls[q];

        //do we even have a control
        if (!control)
            continue;

        //is there even a list control to check?
        if (!(control->properties & (FLAG_CONTROL_REVERSE | FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS)))
            continue;

        //are the values out of sync?
        if (control->scale_point_index == g_list_clone[control->hw_id].scale_point_index)
            continue;

        //clear old list, free memory
        if (control->scale_points)
        {
            for (i = 0; i < control->scale_points_count; i++) {
                if (control->scale_points[i]) {
                    FREE(control->scale_points[i]->label);
                    FREE(control->scale_points[i]);
                }
            }

            FREE(control->scale_points);
        }

        //restore list from the local clone
        control->scale_points_count = g_list_clone[control->hw_id].scale_points_count;

        control->scale_points = (scale_point_t **) MALLOC(sizeof(scale_point_t*) * control->scale_points_count);

        // initializes the scale points pointers
        for (i = 0; i < control->scale_points_count; i++) control->scale_points[i] = NULL;

        for (i = 0; i < control->scale_points_count; i++) {
            control->scale_points[i] = (scale_point_t *) MALLOC(sizeof(scale_point_t));
            control->scale_points[i]->label = str_duplicate(g_list_clone[control->hw_id].scale_points[i]->label);
            control->scale_points[i]->value = g_list_clone[control->hw_id].scale_points[i]->value;
        }

        control->step = g_list_clone[control->hw_id].step;
        control->scale_point_index = g_list_clone[control->hw_id].scale_point_index;
    }
}

static void clone_list_encoders(control_t *control)
{
    uint8_t i;

    //if already a list, free memory
    if (g_list_clone[control->hw_id].hw_id != -1) {
        for (i = 0; i < g_list_clone[control->hw_id].scale_points_count; i++)
        {
            if (g_list_clone[control->hw_id].scale_points[i])
            {
                FREE(g_list_clone[control->hw_id].scale_points[i]->label);
                FREE(g_list_clone[control->hw_id].scale_points[i]);
            }
        }

        FREE(g_list_clone[control->hw_id].scale_points);
    }

    //check if we need to clone this encoder (scalepoints)
    if (control->properties & (FLAG_CONTROL_REVERSE | FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS)) {
        g_list_clone[control->hw_id].scale_points_count = control->scale_points_count;

        g_list_clone[control->hw_id].scale_points = (scale_point_t **) MALLOC(sizeof(scale_point_t*) * control->scale_points_count);

        // initializes the scale points pointers
        for (i = 0; i < control->scale_points_count; i++) g_list_clone[control->hw_id].scale_points[i] = NULL;

        for (i = 0; i < control->scale_points_count; i++) {
            g_list_clone[control->hw_id].scale_points[i] = (scale_point_t *) MALLOC(sizeof(scale_point_t));
            g_list_clone[control->hw_id].scale_points[i]->label = str_duplicate(control->scale_points[i]->label);
            g_list_clone[control->hw_id].scale_points[i]->value = control->scale_points[i]->value;
        }

        g_list_clone[control->hw_id].hw_id = control->hw_id;
        g_list_clone[control->hw_id].step = control->step;
        g_list_clone[control->hw_id].scale_point_index = control->scale_point_index;
    }
}


/*
 * Pedalboard graph navigation
 */

static void parse_minimap(void *data, menu_item_t *item)
{
    (void) item;
    char **list = data;
    uint32_t i;

    g_minimap_loaded = 0;

    //error, dont parse when mod-ui gives error
    if (!list || !list[0] || !list[1] || atoi(list[1]) == -1)
        return;

    if (!list[2]) return;

    /*
     * The display list is one message with spaces inside it, and protocol.c has already
     * split the whole thing into tokens. strarr_split() writes the separators over in
     * place, so the tokens are still contiguous in the rx buffer: putting the spaces back
     * rebuilds the original text without a second 3.5kB buffer, which this device does not
     * have to spare. Labels are sanitised server side, so no token can contain a space or
     * a quotation mark that would have made the split lossy.
     */
    for (i = 2; list[i + 1] != NULL; i++)
        list[i][strlen(list[i])] = ' ';

    g_minimap_loaded = minimap_parse(&g_minimap, list[2]);

    if (g_minimap_loaded)
    {
        /* mod-ui centred the window on the node we asked for, so carry the selection over */
        int8_t focus = minimap_index_of(&g_minimap, g_minimap.focus_id);

        if (focus == MINIMAP_NONE)
        {
            g_minimap.selected = MINIMAP_NONE;
            if (g_minimap.n_nodes > 0)
                minimap_select(&g_minimap, 0);
        }
        else
        {
            minimap_select(&g_minimap, focus);
        }
    }
}

/*
 * ask for the window of the pedalboard graph around <focus_id>
 */
static void request_minimap(int16_t focus_id, uint8_t initial)
{
    uint8_t i;
    char buffer[32];
    memset(buffer, 0, sizeof buffer);

    // sets the response callback
    ui_comm_webgui_set_response_cb(parse_minimap, NULL);
    //clear the buffer
    ui_comm_webgui_clear_tx_buffer();

    // send command minimap
    // response: "r 1 M 392 124 7 amc 3 12 40 ; N 3 p 12 20 30 12 0 0 0 REVERB ; ..."
    i = copy_command((char *)buffer, CMD_DWARF_BUILDER_MINIMAP);

    uint8_t bitmask = 0;
    if (initial)
        bitmask |= FLAG_PAGINATION_INITIAL_REQ;

    // insert the flags on buffer
    i += int_to_str(bitmask, &buffer[i], sizeof(buffer) - i, 0);

    // inserts one space
    buffer[i++] = ' ';

    // insert the node to centre on; ignored by mod-ui on an initial request
    i += int_to_str(focus_id, &buffer[i], sizeof(buffer) - i, 0);

    buffer[i++] = 0;

    // sends the data to GUI
    ui_comm_webgui_send(buffer, i);

    // waits the display list be received
    ui_comm_webgui_wait_response();
}

/*
 * The connection menu of the selected box.
 */

/*
 * ADD: the plugin manager takes the screen until something is added or the user leaves.
 */
/*
 * A plugin is armed before it is removed: the first press starts it flashing, the second
 * is the one that takes it off the board. Losing a plugin and its settings to a mis-hit
 * button is not something an undo would get back, and the graph screen has no undo.
 * hardware_timestamp() counts in units of 500us, so two ticks to the millisecond.
 */
#define BM_DEL_BLINK_TICKS  (400 * 2)

static uint8_t g_del_armed;
static uint8_t g_del_off;
static uint32_t g_del_stamp;

static void del_disarm(void)
{
    g_del_armed = 0;
    g_del_off = 0;
    minimap_set_blink(&g_minimap, 0);
}

static void request_remove(int16_t node_id)
{
    uint8_t i;
    char buffer[24];
    memset(buffer, 0, sizeof buffer);

    ui_comm_webgui_clear_tx_buffer();

    i = copy_command((char *)buffer, CMD_DWARF_BUILDER_REMOVE);
    i += int_to_str(node_id, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = 0;

    ui_comm_webgui_send(buffer, i);
    ui_comm_webgui_wait_response();
}

static void minimap_delete_plugin(void)
{
    const minimap_node_t *node = minimap_selected(&g_minimap);
    int16_t landing;

    // the capture and playback boxes are drawn like the rest but are not on the board
    if (!node || node->kind != MINIMAP_PLUGIN)
    {
        del_disarm();
        return;
    }

    if (!g_del_armed)
    {
        g_del_armed = 1;
        g_del_off = 0;
        g_del_stamp = hardware_timestamp();
        return;
    }

    // somewhere to stand once the box under the cursor is gone
    landing = (node->next != MINIMAP_NONE) ? node->next : node->prev;

    request_remove(node->id);
    del_disarm();

    BM_refresh_graph(landing);
}

static void minimap_add_plugin(void)
{
    const minimap_node_t *node = minimap_selected(&g_minimap);

    del_disarm();

    BM_plugin_manager_open(node ? node->id : MINIMAP_NONE);

    if (BM_plugin_manager_is_open()) uiState = ADD_PLUGIN;

    // the lists are in hand and the state is set, so put them on the panel: the button
    // branches redraw one by one and this one was the only path in that did not
    BM_print_screen();
}

/*
 * Moves the selection one box along mod-ui's walk of the board.
 */
static void minimap_move(uint8_t direction)
{
    int16_t target;
    int8_t index;

    if (!g_minimap_loaded || g_minimap.selected == MINIMAP_NONE) return;

    target = minimap_step(&g_minimap, g_minimap.selected, direction);

    // the two ends of the board; the walk does not wrap
    if (target == MINIMAP_NONE) return;

    index = minimap_index_of(&g_minimap, target);

    if (index == MINIMAP_NONE)
    {
        /*
         * The next box is outside the window mod-ui sent, which is how a board too big
         * for one message is crossed: ask for the window centred on it and carry on. The
         * walk order is built over the whole board, so this always names a real box.
         */
        request_minimap(target, 0);

        if (!g_minimap_loaded) return;

        index = minimap_index_of(&g_minimap, target);
        if (index == MINIMAP_NONE) return;
    }

    minimap_select(&g_minimap, index);
    BM_print_screen();
}


static void parse_plugins_control(void *data, menu_item_t *item)
{
    (void)item;
    // the call returns the number of pages
    char **list = data;

    //error, dont parse when mod-ui gives error
    if (atoi(list[1]) == -1)
        return;

    uint32_t controls_count = atoi(list[2]);

    plugin_edit.controls_count = controls_count;
    plugin_edit.page_count = ceil((float)controls_count / ENCODERS_COUNT);
}

/*
 * ask a list of loaded plugins in the current pedalboard
 */
static void request_plugin_controls(const char* uid, uint8_t start_index, uint8_t count)
{
    uint8_t i;
    char buffer[40];
    memset(buffer, 0, sizeof buffer);

    // sets the response callback
    ui_comm_webgui_set_response_cb(parse_plugins_control, NULL);
    //clear the buffer
    ui_comm_webgui_clear_tx_buffer();

    // send command control list
    i = copy_command((char *)buffer, CMD_DWARF_BUILDER_CONTROLS);

    //buffer[i++] = '"';
    strcpy(&buffer[i], uid);
    i += strlen(uid);
    //buffer[i++] = '"';
    buffer[i++] = ' ';  

    // insert start index on buffer
    i += int_to_str(start_index, &buffer[i], sizeof(buffer) - i, 0);

    // inserts one space
    buffer[i++] = ' ';

    // insert count
    i += int_to_str(count, &buffer[i], sizeof(buffer) - i, 0);

    buffer[i++] = 0;

    // sends the data to GUI
    ui_comm_webgui_send(buffer, i);

    // waits the controls list be received
    ui_comm_webgui_wait_response();
}

/* request the current page controls */
static void request_current_page_controls(void)
{
    if (!plugin_edit.plugin_uid) return;

    uint8_t start_index = plugin_edit.current_page * ENCODERS_COUNT;
    uint8_t count = (plugin_edit.controls_count - start_index) > ENCODERS_COUNT ? ENCODERS_COUNT : (plugin_edit.controls_count - start_index);

    // clear current controls
    for(uint8_t i = 0; i < ENCODERS_COUNT; i++) {
        encoder_control_rm(i);
    }

    if (plugin_edit.controls_count > 0) {
        request_plugin_controls(plugin_edit.plugin_uid, start_index, count);
    }
}


static void parse_control_page(void *data, menu_item_t *item)
{
    (void) item;
    char **list = data;

    control_t *control = data_parse_control(&list[1]);

    if (!control)
        return; // something went wrong with parsing, dont update anything

    // first tries remove the control
    if (control->hw_id < 3) {
        encoder_control_rm(control->hw_id);
    }

    control->scroll_dir = 0;

    if (control->hw_id < ENCODERS_COUNT)
        plugin_edit.controls[control->hw_id] = control;
}

/*
 For enumerated plugin parameter request a page of options
*/
static void request_control_page(control_t *control, uint8_t dir)
{
    // sets the response callback
    ui_comm_webgui_set_response_cb(parse_control_page, NULL);

    char buffer[20];
    memset(buffer, 0, sizeof buffer);
    uint8_t i;
    uint8_t hw_id = control->hw_id;

    i = copy_command(buffer, CMD_DWARF_BUILDER_CONTROL_PAGE);

    // insert the hw_id on buffer
    i += int_to_str(hw_id, &buffer[i], sizeof(buffer) - i, 0);

    // inserts one space
    buffer[i++] = ' ';

    //bitmask for the page direction and wrap around
    uint8_t bitmask = 0;
    if (dir) bitmask |= FLAG_PAGINATION_PAGE_UP;
    if ((control->hw_id >= ENCODERS_COUNT) && (control->scale_points_flag & FLAG_SCALEPOINT_WRAP_AROUND)) bitmask |= FLAG_PAGINATION_WRAP_AROUND;

    // insert the direction on buffer
    i += int_to_str(bitmask, &buffer[i], sizeof(buffer) - i, 0);

    // inserts one space
    buffer[i++] = ' ';

    if (dir) control->scale_point_index++;
    else control->scale_point_index--;

    // insert the index on buffer
    i += int_to_str(control->scale_point_index, &buffer[i], sizeof(buffer) - i, 0);

    uint16_t current_index = control->scale_point_index;
    uint16_t current_step = control->step;

    // sends the data to GUI
    ui_comm_webgui_send(buffer, i);

    // waits the pedalboards list be received
    ui_comm_webgui_wait_response();

    //encoder
    if (hw_id < ENCODERS_COUNT) {
        plugin_edit.controls[hw_id]->scale_point_index = current_index;
        plugin_edit.controls[hw_id]->step = current_step;

        //also set the control if needed
        if (!g_list_click) {
            step_to_value(plugin_edit.controls[hw_id]);
            send_control_set(plugin_edit.controls[hw_id]);

            //in case the user switches modes
            clone_list_encoders(plugin_edit.controls[hw_id]);
        }

        //if screen overlay active, update that
        if ((hardware_get_overlay_counter() || !plugin_edit.controls[hw_id]->scroll_dir) && (plugin_edit.current_overlay_control_index == plugin_edit.controls[hw_id]->hw_id)) {
            BM_print_control_overlay(plugin_edit.controls[hw_id], ENCODER_LIST_TIMEOUT);
            return;
        }

        // update the control screen
        if (plugin_edit.current_overlay_control_index == -1)
            screen_encoder(plugin_edit.controls[hw_id], hw_id);
    }
}

/*
 * open the plugin edit screen for a node of the graph
 */
static void select_plugin_node(const minimap_node_t *node)
{
    if (!node) return;

    /*
     * The wire id of a plugin node is the mapper's instance id, which is exactly what
     * CMD_DWARF_BUILDER_CONTROLS takes, so the graph view needs no second lookup to go
     * from the box on screen to the controls behind it.
     */
    int_to_str(node->id, g_selected_uid, sizeof(g_selected_uid), 0);

    plugin_edit.plugin_name = node->label;
    plugin_edit.plugin_uid = g_selected_uid;
    plugin_edit.current_page = 0;
    plugin_edit.page_count = 0;
    plugin_edit.controls_count = 0;
    // request plugin controls count
    request_plugin_controls(plugin_edit.plugin_uid, 0, 0);
    if (plugin_edit.controls_count > 0) {
        // request the first controls page
        request_current_page_controls();
    }
}

static void send_control_set(control_t *control)
{
    char buffer[128];
    uint8_t i;

    i = copy_command(buffer, CMD_DWARF_BUILDER_CONTROL_SET);

    // insert the hw_id on buffer
    i += int_to_str(control->hw_id, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';

    // insert the value on buffer
    i += float_to_str(control->value, &buffer[i], sizeof(buffer) - i, 3);
    buffer[i] = 0;

    ui_comm_webgui_set_response_cb(NULL, NULL);
    //clear the buffer
    ui_comm_webgui_clear_tx_buffer();

    // sends the data to GUI
    ui_comm_webgui_send(buffer, i);

    //wait for a response from mod-ui
    ui_comm_webgui_wait_response();
}


static void control_set(uint8_t id, control_t *control)
{
    (void) id;

    if ((control->properties & (FLAG_CONTROL_REVERSE | FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS)) && !(control->properties & FLAG_CONTROL_MOMENTARY))
    {
        //encoder (pagination is done in the increment / decrement functions)
        if (control->hw_id < ENCODERS_COUNT)
        {
            // update the screen
            screen_encoder(control, control->hw_id);

            //display overlay
            BM_print_control_overlay(control, ENCODER_LIST_TIMEOUT);
        }
    }
    else if (control->properties & FLAG_CONTROL_TRIGGER)
    {
        if (control->hw_id < ENCODERS_COUNT)
        {
            if (plugin_edit.current_overlay_control_index != -1)
            {
                hardware_force_overlay_off(0);
                BM_print_screen();
            }

            // update the screen
            screen_encoder(control, control->hw_id);
        }
    }
    else if (control->properties & FLAG_CONTROL_MOMENTARY)
    {
        //TODO, BYPASS AND TOGGLES SHOULD BE HANDLED THE SAME
        if (control->properties & FLAG_CONTROL_BYPASS) {
            if (control->properties & FLAG_CONTROL_REVERSE)
                control->value = control->scroll_dir;
            else
                control->value = 1 - control->scroll_dir;
        }
        else {
            if (control->properties & FLAG_CONTROL_REVERSE)
                control->value = 1 - control->scroll_dir;
            else
                control->value = control->scroll_dir;
        }

        // to update the footer and screen
        //foot_control_add(control);
    }
    else if (control->properties & (FLAG_CONTROL_TOGGLED | FLAG_CONTROL_BYPASS))
    {
        if (control->hw_id < ENCODERS_COUNT)
        {
            if (plugin_edit.current_overlay_control_index != -1)
            {
                hardware_force_overlay_off(0);
                BM_print_screen();
            }

            // update the screen
            screen_encoder(control, control->hw_id);
        }
    }
    else
    {
        if (control->hw_id < ENCODERS_COUNT)
        {
            if (plugin_edit.current_overlay_control_index != -1)
            {
                hardware_force_overlay_off(0);
                BM_print_screen();
            }

            // update the screen
            screen_encoder(control, control->hw_id);
        }
    }

    if ((ENCODERS_COUNT <= control->hw_id) && (!control->lock_overlays))
        BM_print_control_overlay(control, FOOT_CONTROLS_TIMEOUT);

    if (g_list_click && (control->properties & (FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS | FLAG_CONTROL_REVERSE)) 
        && (control->hw_id < ENCODERS_COUNT))
        return;

    send_control_set(control);
}

static void BM_inc_control(uint8_t encoder)
{
    control_t *control = plugin_edit.controls[encoder];

    //no control
    if (!control) return;

    //if we already have an overlay, reprint the full screen first
    if ((hardware_get_overlay_counter() != 0) && (hardware_get_overlay_type() == OVERLAY_ATTENTION)) {
        hardware_force_overlay_off(0);
    }

    if (control->properties & (FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS | FLAG_CONTROL_REVERSE)) {
        //prepare display overlay
        BM_print_control_overlay(control, ENCODER_LIST_TIMEOUT);

        if (control->scale_points_flag & FLAG_SCALEPOINT_PAGINATED) {
            // increments the step
            if (control->step < (control->scale_points_count - 3)) {
                if (control->scale_point_index >= control->steps)
                    return;

                control->step++;
                control->scale_point_index++;
            }
            //we are at the end of our list ask for more data
            else {
                if ((control->scale_point_index >= control->steps - 2) ) {

                    if (control->scale_point_index >= control->steps)
                        return;

                    control->step++;
                    control->scale_point_index++;

                    if (!g_list_click) {
                        // converts the step to absolute value
                        step_to_value(control);

                        //make sure to save this value, in case the user switches mode
                        clone_list_encoders(control);
                    }

                    // applies the control value
                    control_set(encoder, control);
                }
                else if (control->scale_point_index < control->steps - 1) {
                    //request new data, a new control we be assigned after
                    request_control_page(control, 1);
                }

                //since a new control is assigned we can return
                return;
            }
        }
        else  {
            // increments the step
            if ((control->step < (control->steps)) && (control->step < (control->scale_points_count))) {
                control->scale_point_index++;
                control->step++;
            }  else {
                return;
            }
        }
    }
    else if (control->properties & FLAG_CONTROL_TRIGGER) {
        control->value = control->maximum;
    }
    else if (control->properties & FLAG_CONTROL_TOGGLED) {
        if (float_is_not_zero(control->value))
            return;
        else 
            control->value = 1;
    }
    else if (control->properties & FLAG_CONTROL_BYPASS) {
        if (float_is_zero(control->value))
            return;
        else 
            control->value = 0;
    }
    else {
        // increments the step
        if (control->step < (control->steps - 1)) {
            if (g_encoders_pressed[encoder])
                control->step+=10;
            else
                control->step++;
        
            if (control->step > (control->steps - 1))
                control->step = (control->steps - 1);
        }
        else
            return;
    }

    if ((!g_list_click) || !(control->properties & (FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS | FLAG_CONTROL_REVERSE)) ) {
        // converts the step to absolute value
        step_to_value(control);
    }

    if (control->properties & (FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS | FLAG_CONTROL_REVERSE)) {
        //make sure to save this value, in case the user switches mode
        clone_list_encoders(control);
    }

    // applies the control value
    control_set(encoder, control);
}

static void BM_dec_control(uint8_t encoder)
{
    control_t *control = plugin_edit.controls[encoder];

    //no control, return
    if (!control) return;


    //if we already have an overlay, reprint the full screen first
    if ((hardware_get_overlay_counter() != 0) && (hardware_get_overlay_type() == OVERLAY_ATTENTION)) {
        hardware_force_overlay_off(0);
    }
    
    if  (control->properties & (FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS | FLAG_CONTROL_REVERSE))  {
        //prepare display overlay
        BM_print_control_overlay(control, ENCODER_LIST_TIMEOUT);

        if (control->scale_points_flag & FLAG_SCALEPOINT_PAGINATED) {
            // decrements the step
            if (control->step > 2) {
                control->step--;
                control->scale_point_index--;
            }
            //we are at the end of our list ask for more data
            else {
                if ((control->scale_point_index <= 2) && (control->scale_point_index > 0)) {
                    control->step--;
                    control->scale_point_index--;

                    if (!g_list_click) {
                        // converts the step to absolute value
                        step_to_value(control);

                        //make sure to save this value, in case the user switches mode
                        clone_list_encoders(control);
                    }

                    // applies the control value
                    control_set(encoder, control);
                }
                else if (control->scale_point_index > 0) {
                    //request new data, a new control we be assigned after
                    request_control_page(control, 0);
                }

                //since a new control is assigned we can return
                return;
            }
        }
        else {
            // decrements the step
            if (control->step > 0) {
                control->scale_point_index--;
                control->step--;
            }
            else
            {
                return;
            }
        }
    }
    else if (control->properties & FLAG_CONTROL_TRIGGER) {
        control->value = control->maximum;
    }
    else if (control->properties & FLAG_CONTROL_TOGGLED) {
        if (float_is_zero(control->value))
            return;
        else 
            control->value = 0;
    }
    else if (control->properties & FLAG_CONTROL_BYPASS) {
        if (float_is_not_zero(control->value))
            return;
        else 
            control->value = 1;
    }
    else {
        // decrements the step
        if (control->step > 0)
        {
            if (g_encoders_pressed[encoder])
                control->step-=10;
            else
                control->step--;

            if (control->step < 0)
                control->step = 0;
        }
        else
            return;
    }

    if ((!g_list_click) || !(control->properties & (FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS | FLAG_CONTROL_REVERSE)) ) {
        // converts the step to absolute value
        step_to_value(control);
    }

    if (control->properties & (FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS | FLAG_CONTROL_REVERSE)) {
        //make sure to save this value, in case the user switches mode
        clone_list_encoders(control);
    }

    // applies the control value
    control_set(encoder, control);
}

/*
 * Toggle control value on encoder click if appliable
 */
static void BM_toggle_control(uint8_t encoder)
{
    control_t *control = plugin_edit.controls[encoder];

    //no control
    if (!control) return;

    //if we already have an overlay, reprint the full screen first
    if ((hardware_get_overlay_counter() != 0) && (hardware_get_overlay_type() == OVERLAY_ATTENTION)) {
        hardware_force_overlay_off(0);
    }

    if (control->properties & FLAG_CONTROL_TRIGGER)
    {
        control->value = control->maximum;
    }
    else if ((control->properties & FLAG_CONTROL_TOGGLED) || (control->properties & FLAG_CONTROL_BYPASS))
    {
        control->value = float_is_zero(control->value) ? 1 : 0;
    }
    else if (control->properties & (FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS | FLAG_CONTROL_REVERSE))
    {
        //no overlay active, toggle
        if (plugin_edit.current_overlay_control_index == -1)
        {
            BM_print_control_overlay(control, ENCODER_LIST_TIMEOUT);
        }
        //list click, change and set value, then close overlay
        else if (g_list_click)
        {
            step_to_value(control);
            send_control_set(control);

            clone_list_encoders(control);

            plugin_edit.current_overlay_control_index = -1;
            BM_print_screen();

            return;
        }
        //overlay already active, close
        else
        {
            plugin_edit.current_overlay_control_index = -1;
            BM_print_screen();
        }

        return;
    }
    else
        return;

    // applies the control value
    control_set(encoder, control);
}


/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/
void BM_init(void)
{
    module_mutex = xSemaphoreCreateMutex();
    plugin_edit.plugin_name = "";
    plugin_edit.plugin_uid = NULL;
    plugin_edit.controls[0] = NULL;
    plugin_edit.controls[1] = NULL;
    plugin_edit.controls[2] = NULL;
    plugin_edit.controls_count = 0;
    plugin_edit.current_page = 0;
    plugin_edit.page_count = 0;
    plugin_edit.current_overlay_control_index = -1;

    /*
     * The viewport sits inside the frame print_menu_outlines() draws and above the footer,
     * so the graph is clipped to it rather than painted over the chrome.
     */
    BM_conn_manager_init();
    BM_bindings_manager_init();
    BM_plugin_manager_init();
    minimap_init(&g_minimap);
    minimap_set_view(&g_minimap, MINIMAP_VIEW_X, MINIMAP_VIEW_Y, MINIMAP_VIEW_W, MINIMAP_VIEW_H);
}

void BM_clear(void)
{
}


/*
 * The two footswitches that are not the page one: B puts the board back on the picture, C
 * reads it as a list. Not a toggle on one switch -- each names a view, so a glance at which
 * one is lit says what is on screen without pressing anything.
 */
void BM_foot_change(uint8_t foot)
{
    uint8_t list;

    if (foot == 0) list = 0;
    else if (foot == 1) list = 1;
    else return;                    // the third foot is not ours

    /*
     * Wherever the board is what is on screen, and only there: over the board itself, and
     * inside the connection picker, where the picture is the menu. The other popups own
     * the panel while they are up.
     */
    if (uiState == PLUGIN_SELECT)
        minimap_set_view_mode(&g_minimap, list ? MINIMAP_VIEW_LIST : MINIMAP_VIEW_GRAPH);
    else if (uiState == CONNECTIONS && BM_conn_manager_is_picking())
        BM_conn_manager_view(list);
    else
        return;

    BM_print_screen();
}

/*
 * Called on builder mode enter
 */
void BM_enter(void)
{
    uiState = PLUGIN_SELECT;
    // an arm does not survive leaving the mode: coming back is not a confirmation either
    del_disarm();
    request_minimap(MINIMAP_NONE, 1);

    /*
     * The first screen looks at the middle of the board, not at the selection. mod-ui
     * centres every column on one axis and puts the hardware pairs across it, so the
     * middle of the canvas is the middle of IN1/IN2 -- centring on IN1 alone would sit it
     * in the centre of the panel and push IN2 below. No guard needed on the pair falling
     * outside: it is centred too, and shorter than the viewport.
     */
    if (g_minimap_loaded)
    {
        g_minimap.offset_x = 0;
        g_minimap.offset_y = (g_minimap.scene_height - g_minimap.view.height) / 2;
        minimap_scroll(&g_minimap, 0, 0);
    }

    BM_set_state();
}


/*
 * Called on builder activate (eg. from menu-shift button)
 */
void BM_refresh_graph(int16_t focus)
{
    request_minimap(focus, 0);
}


void BM_set_state(void)
{
    //CM_set_leds();
    BM_print_screen();
}


void BM_encoder_click(uint8_t encoder)
{
    if (uiState == ADD_PLUGIN) {
        BM_plugin_manager_click(encoder);
        BM_print_screen();
        return;
    }

    if (uiState == CONNECTIONS) {
        if (encoder == 0 || encoder == 1) {
            BM_conn_manager_click();
            BM_print_screen();
        }
        return;
    }

    if (uiState == PLUGIN_SELECT) {
        del_disarm();

        // opening a plugin is the encoder click; there is no button for it
        if (encoder == 0 || encoder == 1) {
            const minimap_node_t *node = minimap_selected(&g_minimap);

            // the hardware in/out boxes are part of the picture but have nothing to edit
            if (node && node->kind == MINIMAP_PLUGIN) {
                uiState = PLUGIN_EDIT;
                select_plugin_node(node);
                BM_print_screen();
            }
        }
    } else {
        BM_toggle_control(encoder);
    }
}


/*
 * One encoder walks the whole board, box by box: left to right by column, top to bottom
 * inside a column. The second opens and closes the connection popup -- right to open, left
 * off its first row to close -- and the third is free while the graph is on screen.
 */
void BM_encoder_hold(uint8_t encoder)
{
    if (uiState != ADD_PLUGIN) return;

    BM_plugin_manager_hold(encoder);
    BM_print_screen();
}


void BM_encoder_released(uint8_t encoder)
{
    if (uiState != ADD_PLUGIN) return;

    // the click that follows is swallowed by the plugin manager, which knows it was a scrub
    BM_plugin_manager_released(encoder);
}


void BM_up(uint8_t encoder)
{
    if (uiState == PLUGIN_SELECT) {
        // an armed DEL is about the box under the cursor, so moving off it disarms
        del_disarm();

        if (encoder == 0)
            minimap_move(MINIMAP_PREV);
        /*
         * The second encoder opens the popup one way and closes it the other: turning it
         * right opens, turning it left off the first row closes. Turning left here, with
         * nothing open, has nothing to undo.
         */
    } else if (uiState == ADD_PLUGIN) {
        // the overlay owns the second encoder while it is up, to walk its description
        if (BM_plugin_manager_info_is_open()) {
            if (encoder == 1) BM_plugin_manager_info_scroll(-1);
        } else {
            BM_plugin_manager_turn(encoder, -1);
        }
        BM_print_screen();
    } else if (uiState == CONNECTIONS) {
        if (encoder == 1) {
            // stepping back off the first row closes the popup, so follow it out
            BM_conn_manager_turn(-1);
            if (!BM_conn_manager_is_open()) uiState = PLUGIN_SELECT;
            BM_print_screen();
        }
        else if (encoder == 2) {
            // the cables behind the hovered row, named one at a time under the list
            BM_conn_manager_pairs_turn(-1);
            BM_print_screen();
        }
    } else if (uiState == BINDINGS) {
        BM_bindings_manager_turn(encoder, -1);
        BM_print_screen();
    } else if (uiState == PLUGIN_EDIT) {
        BM_dec_control(encoder);
    }
}

void BM_down(uint8_t encoder)
{
    if (uiState == PLUGIN_SELECT) {
        del_disarm();

        if (encoder == 0)
            minimap_move(MINIMAP_NEXT);
        else if (encoder == 1) {
            // rightwards only; leftwards is what closes it
            BM_conn_manager_open(&g_minimap);
            if (BM_conn_manager_is_open()) uiState = CONNECTIONS;
            BM_print_screen();
        }
        else if (encoder == 2) {
            // the bindings screen has three columns of its own, so BACK is what leaves it
            BM_bindings_manager_open(&g_minimap);
            if (BM_bindings_manager_is_open()) uiState = BINDINGS;
            BM_print_screen();
        }
    } else if (uiState == ADD_PLUGIN) {
        if (BM_plugin_manager_info_is_open()) {
            if (encoder == 1) BM_plugin_manager_info_scroll(1);
        } else {
            BM_plugin_manager_turn(encoder, 1);
        }
        BM_print_screen();
    } else if (uiState == CONNECTIONS) {
        if (encoder == 1) {
            BM_conn_manager_turn(1);
            BM_print_screen();
        }
        else if (encoder == 2) {
            BM_conn_manager_pairs_turn(1);
            BM_print_screen();
        }
    } else if (uiState == BINDINGS) {
        BM_bindings_manager_turn(encoder, 1);
        BM_print_screen();
    } else if (uiState == PLUGIN_EDIT) {
        BM_inc_control(encoder);
    }
}
/*
 * Buttons under the encoder
 */
void BM_button_pressed(uint8_t button)
{
    // TODO make either list type or button/led leading, its confusing with the function above
    switch(button)
    {
        //enter menu
        case 0:
            if (uiState == ADD_PLUGIN)
            {
                // BACK takes the overlay away first, and the lists only after it
                if (BM_plugin_manager_info_is_open())
                {
                    BM_plugin_manager_info_close();
                    BM_print_screen();
                    break;
                }

                BM_plugin_manager_close();
                uiState = PLUGIN_SELECT;
                BM_print_screen();
            }
            else if (uiState == CONNECTIONS)
            {
                BM_conn_manager_back();
                if (!BM_conn_manager_is_open()) uiState = PLUGIN_SELECT;
                BM_print_screen();
            }
            else if (uiState == BINDINGS)
            {
                BM_bindings_manager_close();
                uiState = PLUGIN_SELECT;
                BM_print_screen();
            }
            else if (uiState == PLUGIN_EDIT) 
            {
                uiState = PLUGIN_SELECT;
                BM_print_screen();
            }
            else
            {
                // this should exit shift mode
                naveg_trigger_mode_change(MODE_CONTROL);
            }
        break;

        case 1:
            if (uiState == ADD_PLUGIN)
            {
                int16_t added = BM_plugin_manager_add();

                if (!BM_plugin_manager_is_open()) {
                    uiState = PLUGIN_SELECT;

                    // land on what was just added, so it is there to wire up
                    if (added != MINIMAP_NONE) request_minimap(added, 0);
                }

                BM_print_screen();
            }
            else if (uiState == CONNECTIONS)
            {
                BM_conn_manager_delete();
                BM_print_screen();
            }
            else if (uiState == BINDINGS)
            {
                BM_bindings_manager_add();
                BM_print_screen();
            }
            else if (uiState == PLUGIN_EDIT) {
                if (plugin_edit.current_page > 0) {
                    plugin_edit.current_page--;
                    BM_print_screen();
                    request_current_page_controls();
                }
            }
            else
            {
                minimap_add_plugin();
            }
        break;
        
        case 2:
            if (uiState == PLUGIN_SELECT)
            {
                minimap_delete_plugin();
                BM_print_screen();
            }
            else if (uiState == ADD_PLUGIN)
            {
                BM_plugin_manager_filter();
                BM_print_screen();
            }
            else if (uiState == CONNECTIONS)
            {
                BM_conn_manager_filter();
                BM_print_screen();
            }
            else if (uiState == BINDINGS)
            {
                BM_bindings_manager_del();
                BM_print_screen();
            }
            else if (uiState == PLUGIN_EDIT) 
            {
                /* next page */
                if (plugin_edit.current_page < plugin_edit.page_count - 1) {
                    plugin_edit.current_page++;
                    request_current_page_controls();
                }
                BM_print_screen();
            }
        break;
    }
}

bool BM_remove_control(uint8_t hw_id)
{
    if (!g_initialized) return false;

    if (hw_id < ENCODERS_COUNT)
    {
        encoder_control_rm(hw_id);
        return true;
    }
    else
        return false;
    //else foot_control_rm(hw_id);
}

bool BM_add_control(control_t *control, uint8_t protocol)
{
    if (!g_initialized) return false;
    if (!control) return false;

    // first tries remove the control
    BM_remove_control(control->hw_id);

    if (protocol) control->scroll_dir = 2;
    else control->scroll_dir = 0;

    if (control->hw_id < ENCODERS_COUNT)
    {
        // this routine remove and deallocate the previous control too if present
        encoder_control_add(control);
        return true;
    }
    else
        return false;
}

void BM_print_control_overlay(control_t *control, uint16_t overlay_time)
{
    plugin_edit.current_overlay_control_index = control->hw_id;

    screen_control_overlay(control);

    hardware_set_overlay_timeout(overlay_time, BM_close_overlay, OVERLAY_CONTROL);
}


void BM_close_overlay(void)
{
    if (g_list_click && (plugin_edit.current_overlay_control_index < ENCODERS_COUNT))
        reset_list_encoders();

    BM_print_screen();
}



void BM_tick(void)
{
    if (naveg_get_current_mode() != MODE_BUILDER) return;

    if (uiState == CONNECTIONS)
    {
        if (BM_conn_manager_tick())
            BM_print_screen();

        return;
    }

    if (uiState == BINDINGS)
    {
        if (BM_bindings_manager_tick())
            BM_print_screen();

        return;
    }

    if (uiState == PLUGIN_SELECT && g_del_armed)
    {
        if ((hardware_timestamp() - g_del_stamp) < BM_DEL_BLINK_TICKS) return;

        g_del_stamp = hardware_timestamp();
        g_del_off = !g_del_off;
        minimap_set_blink(&g_minimap, g_del_off);
        BM_print_screen();
    }
}


void BM_print_screen(void)
{
    xSemaphoreTake(module_mutex, portMAX_DELAY);

    switch (uiState)
    {
        case PLUGIN_SELECT:
            screen_minimap(&g_minimap, g_minimap_loaded, g_del_armed, !g_del_off);
        break;
        case BINDINGS:
        {
            bindings_t model;

            BM_bindings_manager_fill(&model);
            screen_bindings(&model);
        }
        break;
        case ADD_PLUGIN:
        {
            // the info overlay covers the two lists for as long as it is up
            if (BM_plugin_manager_info_is_open())
            {
                plugin_info_t info;

                BM_plugin_manager_fill_info(&info);
                screen_plugin_info(&info);
                break;
            }

            plugin_manager_t model;

            BM_plugin_manager_fill(&model);
            screen_plugin_manager(&model);
        }
        break;
        case CONNECTIONS:
        {
            connections_t model;

            // while a destination is being chosen the picture is the menu
            if (BM_conn_manager_is_picking())
            {
                screen_connection_pick(&g_minimap, BM_conn_manager_pick_title());
                break;
            }

            BM_conn_manager_fill(&model);
            screen_connections(&model);
        }
        break;
        case PLUGIN_EDIT:
            screen_plugin_edit(&plugin_edit);
            break;
        case DEFAULT:
        default:
            break;
    }
    //we are sure we are not in overlay anymore
    plugin_edit.current_overlay_control_index = -1;

    xSemaphoreGive(module_mutex);
}
