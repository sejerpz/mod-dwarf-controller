
/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
static uint8_t g_current_plugin, g_selected_plugin;
static control_t *g_controls[ENCODERS_COUNT];
static list_clone_t g_list_clone[ENCODERS_COUNT];
static int8_t g_current_overlay_actuator = -1;
static bool g_list_click = 0;
static menu_item_t pluginMenuItem;

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

// control assigned to display
static void encoder_control_add(control_t *control)
{
    if (control->hw_id >= ENCODERS_COUNT) return;

    // checks if is already a control assigned in this display and remove it
    if (g_controls[control->hw_id])
        data_free_control(g_controls[control->hw_id]);

    // assign the new control
    g_controls[control->hw_id] = control;

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
        if ((hardware_get_overlay_counter() || !control->scroll_dir) && (g_current_overlay_actuator == control->hw_id))
        {
            BM_print_control_overlay(control, ENCODER_LIST_TIMEOUT);
            return;
        }

        // update the control screen
        if (g_current_overlay_actuator == -1)
            screen_encoder(control, control->hw_id);
    }
}

// control removed from display
static void encoder_control_rm(uint8_t hw_id)
{
    if (hw_id > ENCODERS_COUNT) return;

    if ((!g_controls[hw_id]) && (naveg_get_current_mode() == MODE_BUILDER))
    {
        if (hardware_get_overlay_counter() == 0)
            screen_encoder(NULL, hw_id);
        return;
    }

    control_t *control = g_controls[hw_id];

    if (control)
    {
        data_free_control(control);
        g_controls[hw_id] = NULL;
        if ((naveg_get_current_mode() == MODE_BUILDER) && (hardware_get_overlay_counter() == 0))
            screen_encoder(NULL, hw_id);
    }
}

static void reset_list_encoders(void)
{
    uint8_t q, i;
    for (q = 0; q < ENCODERS_COUNT; q++) {
        control_t *control = g_controls[q];

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
        data_free_snapshots_list(g_plugins);

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
    item->data.selected = g_current_plugin;
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

    i = copy_command((char *)buffer, CMD_BUILDER_EFFECTS);

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

/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/
void BM_init(void)
{
    pluginMenuItem.name = strdup("Plugins");
}

void BM_clear(void)
{
}

/*
 * Called on builder mode enter
 */
void BM_set_state(void)
{
    uiState = PLUGIN_SELECT;
    g_current_plugin = 0;
    g_selected_plugin = 0;
    request_plugins(PAGE_DIR_INIT);
    //CM_set_leds();
    BM_print_screen();
}


void BM_encoder_click(uint8_t encoder)
{
     switch (encoder)
     {
        case 0:
            if (uiState == PLUGIN_SELECT)
            {
                // if there is some plugin
                if (pluginMenuItem.data.list_count > 0) {

                    g_selected_plugin = g_current_plugin;
                    uiState = PLUGIN_EDIT;
                    BM_print_screen();
                }
            }
        break;
        
        default:
        break;
     }
}

void BM_up(uint8_t encoder)
{
    switch(encoder) {
        case 0:
            switch (uiState)
            {
                case PLUGIN_SELECT:
                    if (g_current_plugin > 0)
                        g_current_plugin--;
                    else
                        g_current_plugin = pluginMenuItem.data.list_count - 1;

                    pluginMenuItem.data.hover = g_current_plugin;
                    BM_print_screen();

                break;
                case DEFAULT:
                case PLUGIN_EDIT:
                default:
                    break;
            }
        break;
    }
}

void BM_down(uint8_t encoder)
{
    switch(encoder) {
        case 0:
            switch (uiState)
            {
                case PLUGIN_SELECT:
                    if (g_current_plugin >= pluginMenuItem.data.list_count - 1)
                        g_current_plugin = 0;
                    else
                        g_current_plugin++;

                    pluginMenuItem.data.hover = g_current_plugin;
                    BM_print_screen();
                break;
                case DEFAULT:
                case PLUGIN_EDIT:
                default:
                    break;
            }
        break;
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
                request_plugins(PAGE_DIR_INIT);
            }
            else
            {
                uiState = PLUGIN_EDIT;
            }
        break;

        case 1:
        break;
        
        case 2:
            if (uiState == PLUGIN_SELECT) 
            {
                if (pluginMenuItem.data.list_count > 0) {
                    uiState = PLUGIN_EDIT;
                    g_selected_plugin = g_current_plugin;
                }
            }
            else
            {
                naveg_trigger_mode_change(MODE_CONTROL);
            }
        break;
    }

    // if the user didn't close the builder mode, update the screen
    if (naveg_get_current_mode() == MODE_BUILDER)
    {
        BM_print_screen();
    }
}


void BM_add_control(control_t *control, uint8_t protocol)
{
    if (!g_initialized) return;
    if (!control) return;

    // first tries remove the control
    BM_remove_control(control->hw_id);

    if (protocol) control->scroll_dir = 2;
    else control->scroll_dir = 0;

    if (control->hw_id < ENCODERS_COUNT)
    {
        encoder_control_add(control);
    }
    else
    {
        //foot_control_add(control);     
    }
}

void BM_remove_control(uint8_t hw_id)
{
    if (!g_initialized) return;

    if (hw_id < 3) encoder_control_rm(hw_id);
    //else foot_control_rm(hw_id);
}

void BM_print_control_overlay(control_t *control, uint16_t overlay_time)
{
    g_current_overlay_actuator = control->hw_id;

    screen_control_overlay(control);

    hardware_set_overlay_timeout(overlay_time, BM_close_overlay, OVERLAY_CONTROL);
}


//function that draws the 3 encoders
void BM_draw_encoders(void)
{

   
}

void BM_close_overlay(void)
{
    if (g_list_click && (g_current_overlay_actuator < ENCODERS_COUNT))
        reset_list_encoders();

    BM_print_screen();
}



void BM_print_screen(void)
{
    switch (uiState)
    {
        case PLUGIN_SELECT:
            screen_plugins_list(&pluginMenuItem);
        break;
        case PLUGIN_EDIT:
            screen_plugin_edit(g_controls);
            break;
        case DEFAULT:
        default:
            break;
    }
    //we are sure we are not in overlay anymore
    g_current_overlay_actuator = -1;

    // char str[20];
    // sprintf(str, "%d", (int)g_current_plugin);
    // screen_text_box(10, 20, str);

    // sprintf(str, "%d", (int)g_selected_plugin);
    // screen_text_box(10, 40, str);

    // sprintf(str, "%d", (int)pluginMenuItem.data.list_count);
    // screen_text_box(10, 30, str);
}
