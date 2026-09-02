
/*
************************************************************************************************************************
*           Clipped counterparts to the GLCD drawing primitives.
*
*           The driver does not clip. st7565p_set_pixel() folds an out-of-range coordinate back onto
*           the panel with a modulo, so it does not drop the pixel, it draws it on the opposite edge;
*           st7565p_text() checks nothing at all and the write_data() behind it can index past the
*           frame buffer. Anything that draws a scene larger than the panel, or that pans, produces
*           such coordinates constantly and cannot call the driver directly.
*
*           Each function here mirrors one driver primitive, takes a clip rectangle, and draws only
*           what falls inside it. Coordinates are int16_t and may be negative: that is the point.
*
*           Two deliberate differences from the driver:
*
*             - Dashes skip, they do not erase. The driver's ST7565P_BLACK_WHITE alternates BLACK and
*               WHITE, and WHITE clears the bit, so a dashed line punches holes through whatever it
*               crosses. Here a gap simply leaves the pixel alone.
*
*             - glcd_text_clip() applies `color` to the glyph pixels rather than reproducing the
*               driver's inverse-video trick, and blits pixel by pixel so a partially visible string
*               draws its visible part.
*
*           These live under app/ rather than in drivers/ for now, while the pedalboard plugin_map that
*           drives their design is still being built. Nothing here depends on the plugin_map; when the
*           set settles it can move next to the driver unchanged.
************************************************************************************************************************
*/

#ifndef GLCD_CLIP_H
#define GLCD_CLIP_H


/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <stdint.h>
#include "glcd.h"


/*
************************************************************************************************************************
*           DATA TYPES
************************************************************************************************************************
*/

// A rectangle. int16_t rather than uint8_t because the same type describes panel coordinates, which
// fit in a byte, and the coordinates of whatever is being drawn, which may be larger than the panel
// and may go negative once the view is panned.
typedef struct GLCD_RECT_T {
    int16_t x, y;
    int16_t width, height;
} glcd_rect_t;

// A dash pattern: `on` lit pixels at the start of every `period`, the rest skipped. A period of 0, or
// a NULL pattern, draws solid.
typedef struct GLCD_DASH_T {
    uint8_t period;
    uint8_t on;
} glcd_dash_t;


/*
************************************************************************************************************************
*           FUNCTION PROTOTYPES
************************************************************************************************************************
*/

// Intersection of two rectangles. Returns 0 when they do not overlap, leaving `out` untouched.
uint8_t glcd_rect_intersect(const glcd_rect_t *a, const glcd_rect_t *b, glcd_rect_t *out);

// glcd_set_pixel()
void glcd_set_pixel_clip(glcd_t *display, const glcd_rect_t *clip,
                         int16_t x, int16_t y, uint8_t color);

// glcd_hline() / glcd_vline()
void glcd_hline_clip(glcd_t *display, const glcd_rect_t *clip,
                     int16_t x, int16_t y, int16_t width, const glcd_dash_t *dash, uint8_t color);
void glcd_vline_clip(glcd_t *display, const glcd_rect_t *clip,
                     int16_t x, int16_t y, int16_t height, const glcd_dash_t *dash, uint8_t color);

// glcd_line()
void glcd_line_clip(glcd_t *display, const glcd_rect_t *clip,
                    int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                    const glcd_dash_t *dash, uint8_t color);

// A connected run of segments, `count` points in two parallel arrays. No driver counterpart: it
// exists because the dash phase has to stay continuous around a corner, and owning it here keeps it
// out of every caller.
void glcd_polyline_clip(glcd_t *display, const glcd_rect_t *clip,
                        const int16_t *px, const int16_t *py, uint8_t count,
                        const glcd_dash_t *dash, uint8_t color);

// glcd_rect(). Note this cannot be done by clipping the rectangle and calling glcd_rect(): that would
// close the outline on the cut bounds and draw a border along the clip edge where the shape actually
// continues. The four sides are clipped independently.
void glcd_rect_clip(glcd_t *display, const glcd_rect_t *clip,
                    const glcd_rect_t *rect, const glcd_dash_t *dash, uint8_t color);

// glcd_rect_fill() / glcd_rect_invert(). Filled areas, where clipping the rectangle and clipping the
// result are the same thing, so these do delegate to the driver.
void glcd_rect_fill_clip(glcd_t *display, const glcd_rect_t *clip,
                         const glcd_rect_t *rect, uint8_t color);
void glcd_rect_invert_clip(glcd_t *display, const glcd_rect_t *clip, const glcd_rect_t *rect);

// glcd_text()
void glcd_text_clip(glcd_t *display, const glcd_rect_t *clip,
                    int16_t x, int16_t y, const char *text, const uint8_t *font, uint8_t color);

// Pixel width of a string in this font, the gap between glyphs included but not the one after the
// last glyph. No driver counterpart, but needed by anything that centres or right-aligns text.
int16_t glcd_text_width(const uint8_t *font, const char *text);


/*
************************************************************************************************************************
*           END HEADER
************************************************************************************************************************
*/

#endif
