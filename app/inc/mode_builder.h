
/*
************************************************************************************************************************
*
************************************************************************************************************************
*/

#ifndef MODE_BUILDER_H
#define MODE_BUILDER_H


/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

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

//void tool_mode_trigger_tool(uint8_t tool, uint8_t status);
//uint8_t tool_mode_status(uint8_t tool);
//uint8_t tool_mode_has_tool_enabled(uint8_t display);
void BM_init(void);
void BM_clear(void);
// called when builder mode is activated
void BM_enter(void);
// called when builder node should refresh state (e.g. when returning from shift menu)
void BM_set_state(void);
void BM_encoder_click(uint8_t encoder);
// held and let go: the Add screen scrubs by letter while an encoder is down
void BM_encoder_hold(uint8_t encoder);
void BM_encoder_released(uint8_t encoder);
void BM_up(uint8_t encoder);
void BM_down(uint8_t encoder);
void BM_draw_encoders(void);
void BM_button_pressed(uint8_t button);
void BM_print_screen(void);
// re-fetches the graph around `focus`, after an edit changed it
void BM_refresh_graph(int16_t focus);

// The host says the board has changed under us. Only raises a flag: the refetch is a
// command, and a callback of a received command cannot send one.
void BM_plugin_map_stale(void);

// Drop the board watch without telling the host, for the paths that leave the builder from
// inside a protocol callback where nothing may be sent.
void BM_forget_watch(void);

// Called when the panel leaves the builder, from every path that leaves it.
void BM_exit(void);

// Footswitch B and C over the board: the picture, or the same board as a list. Ignored
// anywhere else in the builder, where the panel belongs to something else.
void BM_foot_change(uint8_t foot);
// polled from the displays task: drives the blink of a cable armed for deletion
void BM_tick(void);

// returns true if control was added
bool BM_add_control(control_t *control, uint8_t protocol);
bool BM_remove_control(uint8_t hw_id);
void BM_print_control_overlay(control_t *control, uint16_t overlay_time);
     
void BM_close_overlay(void);

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
