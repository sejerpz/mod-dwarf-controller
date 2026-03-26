
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
#include "logging.h"

/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/
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
    PLUGIN_EDIT
};


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
static bp_list_t *g_plugins; /* list of pedalboard plugins effects */
static uint8_t g_plugins_loaded = 0;
static uint8_t g_current_plugin = 0;
static uint8_t g_selected_plugin = 0;
static list_clone_t g_list_clone[ENCODERS_COUNT];
static bool g_list_click = 0;
static menu_item_t pluginMenuItem;

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
static void request_plugins(uint8_t dir);
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

    if (naveg_get_current_mode() == MODE_BUILDER)
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
    else
    {
        trace("not builder mode");
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
        if (hardware_get_overlay_counter() == 0)
            screen_encoder(NULL, hw_id);

        xSemaphoreGive(module_mutex);
    }
    else
    {
        trace("not builder mode");
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
 * Plugin effect navigation
 */

 static void parse_plugins_list(void *data, menu_item_t *item)
{
    (void) item;
    char **list = data;

    //error, dont parse when mod-ui gives error
    if (atoi(list[1]) == -1)
        return;

    uint32_t count = strarr_length(&list[5]);

    // free the navigation pedalboads list
    if (g_plugins)
        data_free_plugins_list(g_plugins);

    // parses the list
    g_plugins = data_parse_plugins_list(&list[5], count);

    if (g_plugins) {
        g_plugins->menu_max = (atoi(list[2]));
        g_plugins->page_min = (atoi(list[3]));
        g_plugins->page_max = (atoi(list[4])); 
    
        g_plugins_loaded = 1;
    }
    else
        g_plugins_loaded = 0;

    item->data.list = g_plugins->names;
    item->data.list_count = count / 2; // why count is two time the effective # elements?
    item->data.selected = item->data.hover = g_current_plugin;
}

/*
 * ask a list of loaded plugins in the current pedalboard
 */
static void request_plugins(uint8_t dir)
{
    uint8_t i;
    char buffer[40];
    memset(buffer, 0, sizeof buffer);

    // sets the response callback
    ui_comm_webgui_set_response_cb(parse_plugins_list, &pluginMenuItem);
    //clear the buffer
    ui_comm_webgui_clear_tx_buffer();

    // send command plugin list
    // response:  "r 1 4 0 4 "My Autopanner" "dynamic_1" "Distortion" "ojd_1"...
    i = copy_command((char *)buffer, CMD_DWARF_BUILDER_PLUGINS);

    uint8_t bitmask = 0;
    if (dir == 1)
        bitmask |= FLAG_PAGINATION_PAGE_UP;
    else if (dir == 2)
        bitmask |= FLAG_PAGINATION_INITIAL_REQ;

    // insert the direction on buffer
    i += int_to_str(bitmask, &buffer[i], sizeof(buffer) - i, 0);

    // inserts one space
    buffer[i++] = ' ';

    // insert the current hover on buffer
    if ((dir == PAGE_DIR_INIT)) {
        if (g_plugins && g_plugins->selected == -1)
            i += int_to_str(0, &buffer[i], sizeof(buffer) - i, 0);
        else
            i += int_to_str(g_current_plugin, &buffer[i], sizeof(buffer) - i, 0);
    }
    else
        i += int_to_str(g_plugins->hover, &buffer[i], sizeof(buffer) - i, 0);

    buffer[i++] = 0;

    int32_t prev_hover = g_current_plugin;
    int32_t prev_selected = g_current_plugin;

    if (g_plugins) {
        prev_hover = g_plugins->hover;
        prev_selected = g_plugins->selected;
    }

    // sends the data to GUI
    ui_comm_webgui_send(buffer, i);

    // waits the pedalboards list be received
    ui_comm_webgui_wait_response();

    if (g_plugins) {
        g_plugins->hover = prev_hover;
        g_plugins->selected = prev_selected;
    }
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
 * select a plugin from the list
 */
static void list_select_plugin(uint8_t index)
{
    if (!g_plugins) return;

    g_selected_plugin = index;
    plugin_edit.plugin_name = g_plugins->names[index];
    plugin_edit.plugin_uid = g_plugins->uids[index];
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
    pluginMenuItem.name = strdup("Plugins");
}

void BM_clear(void)
{
}


/*
 * Called on builder mode enter
 */
void BM_enter(void)
{
    uiState = PLUGIN_SELECT;
    g_current_plugin = 0;
    g_selected_plugin = 0;
    request_plugins(PAGE_DIR_INIT);
    BM_set_state();
}


/*
 * Called on builder activate (eg. from menu-shift button)
 */
void BM_set_state(void)
{
    //CM_set_leds();
    BM_print_screen();
}


void BM_encoder_click(uint8_t encoder)
{
    if (uiState == PLUGIN_SELECT) {
        if (encoder == 0) {
            // if there is some plugin
            if (pluginMenuItem.data.list_count > 0) {

                uiState = PLUGIN_EDIT;
                list_select_plugin(g_current_plugin);
                BM_print_screen();
            }
        }
    } else {
        BM_toggle_control(encoder);
    }
}


void BM_up(uint8_t encoder)
{
    if (uiState == PLUGIN_SELECT) {
        if (encoder == 0) {
            if (g_current_plugin > 0)
                g_current_plugin--;
            else
                g_current_plugin = pluginMenuItem.data.list_count - 1;

            pluginMenuItem.data.selected = g_plugins->selected = g_current_plugin;
            pluginMenuItem.data.hover = g_plugins->hover = g_current_plugin;
            BM_print_screen();
        }
    } else if (uiState == PLUGIN_EDIT) {
        BM_dec_control(encoder);
    }
}

void BM_down(uint8_t encoder)
{
    if (uiState == PLUGIN_SELECT) {
        if (encoder == 0) {
            if (g_current_plugin >= pluginMenuItem.data.list_count - 1)
                g_current_plugin = 0;
            else
                g_current_plugin++;

            pluginMenuItem.data.selected = g_plugins->selected = g_current_plugin;
            pluginMenuItem.data.hover = g_plugins->hover = g_current_plugin;
            BM_print_screen();
        }
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
            if (uiState == PLUGIN_EDIT) 
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
            if (uiState == PLUGIN_EDIT) {
                if (plugin_edit.current_page > 0) {
                    plugin_edit.current_page--;
                    BM_print_screen();
                    request_current_page_controls();
                }
            }
        break;
        
        case 2:
           if (uiState == PLUGIN_EDIT) 
            {
                /* next page */
                if (plugin_edit.current_page < plugin_edit.page_count - 1) {
                    plugin_edit.current_page++;
                    request_current_page_controls();
                }
            }
            else
            {
                if (pluginMenuItem.data.list_count > 0) {
                    uiState = PLUGIN_EDIT;
                    list_select_plugin(g_current_plugin);
                }
            }
            BM_print_screen();
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



void BM_print_screen(void)
{
    xSemaphoreTake(module_mutex, portMAX_DELAY);

    switch (uiState)
    {
        case PLUGIN_SELECT:
            screen_plugins_list(&pluginMenuItem);
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

/*
    Update the display control value

    Actually in builder only encoder are used to display
    plugin parameters values
*/
void BM_set_control(uint8_t hw_id, float value)
{
    if (!g_initialized) return;

    control_t *control = NULL;
    uint8_t i = 0;

    //encoder
    if (hw_id < ENCODERS_COUNT)
    {
        control = plugin_edit.controls[hw_id];
    }

    if (control)
    {
        control->value = value;
        if (value < control->minimum)
            control->value = control->minimum;
        if (value > control->maximum)
            control->value = control->maximum;

        // updates the step value
        //for enumerations, this will ONLY be called for non paginated lists
        if (control->properties & (FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS)) {
            // locates the current value
            control->step = 0;
            for (i = 0; i < control->scale_points_count; i++)
            {
                if (floats_are_equal(control->value, control->scale_points[i]->value))
                {
                    control->step = i;
                    control->scale_point_index = i;
                    break;
                }
            }
        }
        else {
            control->step =
                (control->value - control->minimum) / ((control->maximum - control->minimum) / control->steps);
        }

        if ((naveg_get_current_mode() != MODE_BUILDER) || (hardware_get_overlay_counter() != 0)){
            return;
        }

        //encoder
        if (hw_id < ENCODERS_COUNT)
        {
            if (control->properties & (FLAG_CONTROL_SCALE_POINTS | FLAG_CONTROL_REVERSE | FLAG_CONTROL_ENUMERATION))  {
                clone_list_encoders(control);
            }
            screen_encoder(control, control->hw_id);
        }
    }
}
