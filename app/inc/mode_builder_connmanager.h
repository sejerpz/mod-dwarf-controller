/*
************************************************************************************************************************
*           Connection manager for the pedalboard builder.
*
*           The cables in and out of one box, and the four steps that add one: our own
*           output, the boxes that output could feed, and one of their inputs. Always
*           forward -- a cable runs from an output to an input, and wiring something into a
*           box is done from the box at the other end, which reaches every pair just the
*           same and halves every list.
*
*           It draws nothing and owns no screen: mode_builder routes input here while
*           BM_conn_manager_is_open() and redraws afterwards.
************************************************************************************************************************
*/

#ifndef MODE_BUILDER_CONNMANAGER_H
#define MODE_BUILDER_CONNMANAGER_H


/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <stdint.h>

#include "plugin_map.h"
#include "screen.h"


/*
************************************************************************************************************************
*           FUNCTION PROTOTYPES
************************************************************************************************************************
*/

// call once, before anything else
void BM_conn_manager_init(void);

// Opens on the box currently selected in `map`, keeping the pointer for as long as it is
// open. Does nothing when there is no selection, so check BM_conn_manager_is_open() afterwards.
void BM_conn_manager_open(plugin_map_t *map);
uint8_t BM_conn_manager_is_open(void);

// one step back through the lists, and out of the popup from the first of them
void BM_conn_manager_back(void);

// the encoder: a step through the current list. Stepping back off the first row of the
// first list closes the popup, so check BM_conn_manager_is_open() afterwards.
void BM_conn_manager_turn(int8_t step);
void BM_conn_manager_click(void);

// The third button cycles the signal type. The second is ADD while the list of cables is
// idle, DEL once one of them is armed, and SELECT in the three lists that follow ADD.
void BM_conn_manager_filter(void);
void BM_conn_manager_delete(void);

// Footswitch B and C while a destination is being chosen: the picture, or the same boxes
// as a list. The same pair does the same thing over the board itself.
void BM_conn_manager_view(uint8_t list);

// The third encoder, which walks the cables behind the row under the cursor: a stereo
// pair is one row, and this is how the strip under the list names both of its ends.
void BM_conn_manager_pairs_turn(int8_t step);

// Polled from the displays task. Returns 1 when a cable armed for deletion has just
// changed the half of its blink and the screen owes a redraw.
uint8_t BM_conn_manager_tick(void);

// While the destination is being chosen the picture itself is the menu, so the caller
// draws screen_connection_pick() with these instead of screen_connections().
uint8_t BM_conn_manager_is_picking(void);
const char *BM_conn_manager_pick_title(void);

// fills in what screen_connections() needs; the caller does the drawing
void BM_conn_manager_fill(connections_t *model);


/*
************************************************************************************************************************
*           END HEADER
************************************************************************************************************************
*/

#endif
