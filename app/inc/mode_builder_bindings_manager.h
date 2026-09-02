/*
************************************************************************************************************************
*           Binding manager for the pedalboard builder.
*
*           Two columns and two verbs: a parameter of the selected box on the left, a slot
*           on the panel on the right, the page of the board as a heading over it -- then
*           ADD to bind, or DEL to take off whatever is already there.
*
*           A knob is three slots, not one: the panel turns its knobs over in sub-pages, so
*           the right column lists each knob once per sub-page and the footswitches once
*           each. A page and a row of that list together are one slot, and a slot holds one
*           addressing -- which is why the column marks what is spoken for. It is the one
*           thing about a slot a user cannot work out by looking at the panel.
*
*           It draws nothing and owns no screen: mode_builder routes input here while
*           BM_bindings_manager_is_open() and redraws afterwards.
************************************************************************************************************************
*/

#ifndef MODE_BUILDER_BINDINGS_MANAGER_H
#define MODE_BUILDER_BINDINGS_MANAGER_H


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
void BM_bindings_manager_init(void);

// Opens on the box currently selected in `map`. Does nothing when the selection is not a
// plugin -- the capture and playback boxes have nothing to bind -- so check
// BM_bindings_manager_is_open() afterwards.
void BM_bindings_manager_open(plugin_map_t *map);
uint8_t BM_bindings_manager_is_open(void);
void BM_bindings_manager_close(void);

// the first two encoders take a column each; the third walks the page
void BM_bindings_manager_turn(uint8_t encoder, int8_t step);

// The second button. Binds the parameter under the cursor to the slot under it, on the
// page in the heading, replacing whatever held that slot.
void BM_bindings_manager_add(void);

// The third button, and only where the slot holds something. The first press arms it and
// the row starts flashing; the second is the one that takes the binding off.
void BM_bindings_manager_del(void);

// Polled from the displays task. Returns 1 when an armed binding has just changed the
// half of its blink and the screen owes a redraw.
uint8_t BM_bindings_manager_tick(void);

// fills in what screen_bindings() needs; the caller does the drawing
void BM_bindings_manager_fill(bindings_t *model);


/*
************************************************************************************************************************
*           END HEADER
************************************************************************************************************************
*/

#endif
