
/*
************************************************************************************************************************
*
************************************************************************************************************************
*/

#ifndef SCREEN_H
#define SCREEN_H


/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <stdint.h>
#include "config.h"
#include "data.h"
#include "node.h"
#include "glcd_widget.h"
#include "minimap.h"
#include "mode_popup.h"

/*
************************************************************************************************************************
*           DO NOT CHANGE THESE DEFINES
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           CONFIGURATION DEFINES
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           DATA TYPES
************************************************************************************************************************
*/

typedef struct {
     /* current select plugin name */
    const char* plugin_name; 
	/* current selected plugin uri */
	const char* plugin_uid;
     /* controls of the plugin */
    control_t *controls[ENCODERS_COUNT];
    /* number of controls */
    uint8_t controls_count; 
    /* current page: controls are paginated 3 per page*/
    uint8_t current_page;
    /* number of pages: g_controls_count / 3 */
    uint8_t page_count;    // current page of the plugin controls (dwarf: 3 controls per page)
    /* current overlay actuator */
    int8_t current_overlay_control_index;
} plugin_edit_t;

typedef struct {
    /* name of the box whose connections these are */
    const char *title;
    /* one row per cable, or per port in the lists that follow "add" */
    char **rows;
    uint8_t count;
    int16_t hover;
    /* which signal type the list is filtered to, as a word for the footer */
    const char *filter;
    /* the second button: DEL once a cable is armed, ADD while none is, and SELECT in the
       three lists that follow ADD */
    uint8_t can_delete;
    uint8_t can_add;
    uint8_t can_select;
    /* The strip under the list: one of the cables the hovered row stands for, feeding end
       first. NULL where the row is a port rather than a cable. `pair_count` above one is
       what puts the arrows on, since the third encoder can then walk them. */
    const char *pair_source;
    const char *pair_sink;
    uint8_t pair_count;
    uint8_t pair_index;
    /* which half of the armed cable's blink this is: the hovered row and DEL are drawn
       in reverse together, so the two read as one thing about to happen */
    uint8_t blink_reverse;
} connections_t;

typedef struct {
    /* the two columns: categories on the left, what is in one of them on the right */
    char **categories;
    uint8_t category_count;
    int16_t category_hover;

    char **plugins;
    uint8_t plugin_count;
    int16_t plugin_hover;

    /* the signal type the lists are filtered to, as a word for the footer */
    const char *filter;

    /* the letter being scrubbed to, drawn over the lists; NULL when not scrubbing */
    const char *scrub;
} plugin_manager_t;

/* how much of a description the overlay shows at once, and the widest line it wraps to */
#define SCREEN_INFO_LINES   5
#define SCREEN_INFO_ROOM    31

typedef struct {
    const char *name;
    const char *brand;
    const char *category;
    /* audio in, audio out, MIDI in, MIDI out, CV in, CV out */
    const uint8_t *ports;
    /* the plugin's own description, one long line; the screen wraps it */
    const char *comment;
    /* the line of it to start at, which the first encoder walks */
    uint8_t first_line;
} plugin_info_t;

typedef struct {
    /* the box whose parameters these are */
    const char *title;

    /* column one: what there is to bind */
    char **params;
    uint8_t param_count;
    int16_t param_hover;

    /* which page of the board, as a heading over the second column */
    const char *page;

    /* column two: every slot on the panel, as the host lists them */
    char **actuators;
    uint8_t actuator_count;
    int16_t actuator_hover;
    /* one per row above: the slot on this page already holds an addressing */
    const uint8_t *actuator_taken;

    /* the hovered slot holds something, so the third button offers to take it off */
    uint8_t can_delete;
    uint8_t armed;
    /* which half of the armed row's blink this is */
    uint8_t blink_reverse;
} bindings_t;

/*
************************************************************************************************************************
*           GLOBAL VARIABLES
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           MACRO'S
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           FUNCTION PROTOTYPES
************************************************************************************************************************
*/

void screen_clear(void);
void screen_force_update(void);
void screen_set_hide_non_assigned_actuators(uint8_t hide);
void screen_set_control_mode_header(uint8_t toggle);
void screen_group_foots(uint8_t toggle);
void screen_encoder(const control_t *control, uint8_t encoder);
void screen_encoder_container(uint8_t current_encoder_page);
void screen_page_index(uint8_t current, uint8_t available);
void screen_tittle(int8_t pb_ss);
void screen_footer(uint8_t foot_id, const char *name, const char *value, int16_t property);
void screen_tool(uint8_t tool, uint8_t display_id);
void screen_bank_list(bp_list_t *list, const char *name);
void screen_pbss_list(const char *title, bp_list_t *list, uint8_t pb_ss_toggle, int8_t hold_item_index, 
					  const char *hold_item_label);
void screen_system_menu(menu_item_t *item);
void screen_tool_control_page(node_t *node);
void screen_toggle_tuner(float frequency, const char *note, int16_t cents);
void screen_image(uint8_t display, const uint8_t *image);
void screen_shift_overlay(int8_t prev_mode, int16_t *item_ids, uint8_t ui_connection);
void screen_menu_page(node_t *node);
void screen_control_overlay(control_t *control);
void screen_msg_overlay(const char *message);
void screen_widget_overlay(int8_t style, char *header, char *text);
void screen_popup(system_popup_t *popup_data);
void screen_keyboard(system_popup_t *popup_data, uint8_t keyboard_index);
void screen_update_tuner(float frequency, char *note, int16_t cents);
void screen_update_tuner_input(uint8_t input);
void screen_update_tuner_ref_freq(int8_t ref_freq);
void print_tripple_menu_items(menu_item_t *item_child, uint8_t knob, uint8_t tool_mode);
void screen_text_box(uint8_t x, uint8_t y, const char *text);
void screen_plugins_list(menu_item_t *item);
// `armed` puts DEL on the third button, and `blink_reverse` is which half of its flash
// this is: the button and the box it would remove flash together, as one thing about to
// happen.
void screen_minimap(minimap_t *map, uint8_t loaded, uint8_t armed, uint8_t blink_reverse);
void screen_connections(connections_t *model);
// the minimap used as a chooser: same picture, a title saying what for
void screen_connection_pick(minimap_t *map, const char *title);
void screen_plugin_manager(plugin_manager_t *model);
void screen_plugin_info(plugin_info_t *model);
void screen_bindings(bindings_t *model);
// how many lines screen_plugin_info() wraps a description into, for clamping the scroll
uint8_t screen_plugin_info_lines(const char *text);
// a two-line box over whatever is on screen, while the host takes its time
void screen_notice(const char *first, const char *second);
void screen_plugin_edit(plugin_edit_t *model);
void screen_plugin_edit_page_info(plugin_edit_t *model);

/*
************************************************************************************************************************
*           CONFIGURATION ERRORS
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           END HEADER
************************************************************************************************************************
*/

#endif
