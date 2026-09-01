/*
************************************************************************************************************************
*           Plugin manager for the pedalboard builder.
*
*           The Add screen: categories on the left, what is in one of them on the right, an
*           encoder each. Picking a plugin instantiates it and hands back the box it became,
*           which the builder then selects.
*
*           The device carries no URIs. It names a plugin by its position in the two lists
*           it was just shown, and the host resolves the pick against the same listing --
*           sixty characters of URI would be no use on a 128 pixel panel.
*
*           It draws nothing and owns no screen: mode_builder routes input here while
*           BM_plugin_manager_is_open() and redraws afterwards.
************************************************************************************************************************
*/

#ifndef MODE_BUILDER_PLUGIN_MANAGER_H
#define MODE_BUILDER_PLUGIN_MANAGER_H


/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <stdint.h>

#include "screen.h"


/*
************************************************************************************************************************
*           FUNCTION PROTOTYPES
************************************************************************************************************************
*/

// The info overlay, on the third button: what the host knows about the row under the
// cursor, over the two lists until BACK takes it away again. Fetched on the press rather
// than as the cursor moves, which would be a request per row.
void BM_plugin_manager_info(void);
uint8_t BM_plugin_manager_info_is_open(void);
void BM_plugin_manager_info_close(void);
// the first encoder while the overlay is up: a line of the description at a time
void BM_plugin_manager_info_scroll(int8_t step);
void BM_plugin_manager_fill_info(plugin_info_t *model);

// call once, before anything else
void BM_plugin_manager_init(void);

// `anchor` is the box the cursor is on, or BM_NONE. The server may put the new
// plugin inside the cable leaving it, so it has to be taken before the list covers it up.
void BM_plugin_manager_open(int16_t anchor);
uint8_t BM_plugin_manager_is_open(void);
void BM_plugin_manager_close(void);

// encoder 0 walks the categories, encoder 1 the plugins of the one it is on
void BM_plugin_manager_turn(uint8_t encoder, int8_t step);

// Clicking the plugin encoder adds it to the pedalboard. Returns the node id of the new
// box, or BM_NONE when nothing was added -- the caller selects it on the graph.
// The second encoder's click: the info overlay, and the same click closes it again. Not
// the add -- that is a button now, so a press cannot instantiate a plugin by accident
// while the finger is still on the encoder that was scrolling the list.
void BM_plugin_manager_click(uint8_t encoder);

// The second button. Returns the box the plugin became, or BM_NONE; the screen has
// closed itself either way, so check BM_plugin_manager_is_open() afterwards.
int16_t BM_plugin_manager_add(void);

// Holding the plugin encoder down scrubs by first letter instead of by row, which turns a
// list of hundreds into one of twenty-odd. Releasing lands on that letter and adds nothing.
void BM_plugin_manager_hold(uint8_t encoder);
void BM_plugin_manager_released(uint8_t encoder);
uint8_t BM_plugin_manager_is_scrubbing(void);

// the second button: cycles all -> audio -> midi -> cv
void BM_plugin_manager_filter(void);

// fills in what screen_plugin_manager() needs; the caller does the drawing
void BM_plugin_manager_fill(plugin_manager_t *model);


/*
************************************************************************************************************************
*           END HEADER
************************************************************************************************************************
*/

#endif
