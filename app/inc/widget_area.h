
/*
************************************************************************************************************************
*           Display areas given to plugin widgets.
*
*           mod-ui decides, for each area of the control screen, whether it shows the default drawing of the
*           actuator below it or a widget drawn by a plugin. A widget area belongs to a page and a subpage (or to
*           every subpage of a page) and covers whatever the firmware would draw there: knob boxes, footers,
*           even the title bar when it spans the whole display. The actuators below keep working as usual.
*
*           Only the areas of the screen in view hold pixels: the plugin redraws an area every time it comes
*           into view, so nothing is kept for the pages that are not shown. Each visible area keeps its own
*           backing buffer, in the same layout the plugin sends (1bpp, rows top to bottom, most significant bit
*           leftmost), so that the default drawing can be painted over again whenever the screen is redrawn.
*
*           Pixels only reach the display in control mode and with no overlay open; otherwise they are kept in
*           the buffer and shown the next time the control screen is printed.
************************************************************************************************************************
*/

#ifndef WIDGET_AREA_H
#define WIDGET_AREA_H


/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <stdint.h>
#include "glcd_clip.h"


/*
************************************************************************************************************************
*           DO NOT CHANGE THESE DEFINES
************************************************************************************************************************
*/

// an area added with this subpage is shown on every subpage of its page
#define WIDGET_AREA_ALL_SUBPAGES    -1

// removes every area
#define WIDGET_AREA_ALL             -1


/*
************************************************************************************************************************
*           FUNCTION PROTOTYPES
************************************************************************************************************************
*/

// Adds an area, in display coordinates, replacing any area with the same id.
// Returns 0 when the rectangle does not fit the display or memory is short.
// Adding or removing does not touch the display: the caller prints the control screen again.
uint8_t WA_add(int16_t area_id, int8_t page, int8_t subpage, const glcd_rect_t *rect);

// Removes an area, or all of them with WIDGET_AREA_ALL.
void WA_remove(int16_t area_id);

// The page and subpage now in view. Areas coming into view start blank, areas leaving it drop their pixels.
// `redrawn` is the part of the display the caller is about to draw again, NULL for all of it. Returns 1 when
// an area coming or going reaches outside it -- a full screen one over the title and footer, say -- so the
// caller has to print the whole control screen, or it would leave pieces of the old one behind.
uint8_t WA_set_screen(int8_t page, int8_t subpage, const glcd_rect_t *redrawn);

// Replaces the pixels of a rectangle of a visible area, in area coordinates, clipped to the area.
// `rows` is the hex string of the protocol, `height` rows of (`width` + 7) / 8 bytes; NULL clears the rectangle.
// Returns 0 when the area is not in view or `rows` is too short.
uint8_t WA_draw(int16_t area_id, int16_t x, int16_t y, int16_t width, int16_t height, const char *rows);

// Whether some area in view covers part of `rect`, whatever the mode or overlay.
uint8_t WA_covers(const glcd_rect_t *rect);

// Paints the visible areas over what the firmware drew in `rect`, or over the whole display when NULL.
// Returns 1 when some visible area touches `rect`, so the caller knows its own drawing was covered.
uint8_t WA_paint(const glcd_rect_t *rect);


/*
************************************************************************************************************************
*           END HEADER
************************************************************************************************************************
*/

#endif
