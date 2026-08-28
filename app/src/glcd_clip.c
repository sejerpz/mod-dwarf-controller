
/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include "glcd_clip.h"
#include "fonts.h"


/*
************************************************************************************************************************
*           LOCAL FUNCTIONS
************************************************************************************************************************
*/

// Bresenham for one segment, clipped per pixel, with the dash phase passed in and advanced so a run
// of segments can share it. Private: the phase is an implementation detail of the polyline, and no
// caller should have to carry one.
static void line_dashed(glcd_t *display, const glcd_rect_t *clip,
                        int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                        const glcd_dash_t *dash, uint8_t color, uint16_t *phase)
{
    int16_t dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
    int16_t dy = (y2 > y1) ? (y2 - y1) : (y1 - y2);
    int16_t sx = (x1 < x2) ? 1 : -1;
    int16_t sy = (y1 < y2) ? 1 : -1;
    int16_t err = dx - dy;

    for (;;)
    {
        // solid, or inside the lit part of the repeat. A gap skips the pixel, it does not paint it.
        if (!dash || dash->period == 0 || (*phase % dash->period) < dash->on)
            glcd_set_pixel_clip(display, clip, x1, y1, color);
        (*phase)++;

        if (x1 == x2 && y1 == y2) break;

        int16_t e2 = err * 2;
        if (e2 > -dy)
        {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            y1 += sy;
        }
    }
}


/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

uint8_t glcd_rect_intersect(const glcd_rect_t *a, const glcd_rect_t *b, glcd_rect_t *out)
{
    int16_t x0 = (a->x > b->x) ? a->x : b->x;
    int16_t y0 = (a->y > b->y) ? a->y : b->y;
    int16_t x1 = ((a->x + a->width) < (b->x + b->width)) ? (a->x + a->width) : (b->x + b->width);
    int16_t y1 = ((a->y + a->height) < (b->y + b->height)) ? (a->y + a->height) : (b->y + b->height);

    if (x1 <= x0 || y1 <= y0) return 0;

    out->x = x0;
    out->y = y0;
    out->width = x1 - x0;
    out->height = y1 - y0;
    return 1;
}

void glcd_set_pixel_clip(glcd_t *display, const glcd_rect_t *clip,
                         int16_t x, int16_t y, uint8_t color)
{
    if (x < clip->x || x >= (clip->x + clip->width)) return;
    if (y < clip->y || y >= (clip->y + clip->height)) return;

    // the driver still does the write; this only decides whether it happens
    glcd_set_pixel(display, (uint8_t)x, (uint8_t)y, color);
}

void glcd_hline_clip(glcd_t *display, const glcd_rect_t *clip,
                     int16_t x, int16_t y, int16_t width, const glcd_dash_t *dash, uint8_t color)
{
    uint16_t phase = 0;

    if (width <= 0) return;

    line_dashed(display, clip, x, y, x + width - 1, y, dash, color, &phase);
}

void glcd_vline_clip(glcd_t *display, const glcd_rect_t *clip,
                     int16_t x, int16_t y, int16_t height, const glcd_dash_t *dash, uint8_t color)
{
    uint16_t phase = 0;

    if (height <= 0) return;

    line_dashed(display, clip, x, y, x, y + height - 1, dash, color, &phase);
}

void glcd_line_clip(glcd_t *display, const glcd_rect_t *clip,
                    int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                    const glcd_dash_t *dash, uint8_t color)
{
    uint16_t phase = 0;

    line_dashed(display, clip, x1, y1, x2, y2, dash, color, &phase);
}

void glcd_polyline_clip(glcd_t *display, const glcd_rect_t *clip,
                        const int16_t *px, const int16_t *py, uint8_t count,
                        const glcd_dash_t *dash, uint8_t color)
{
    uint16_t phase = 0;
    uint8_t i;

    // one phase for the whole run: drawn as separate lines, each segment would restart the pattern
    // and a bend would show a dash of the wrong length
    for (i = 0; i + 1 < count; i++)
        line_dashed(display, clip, px[i], py[i], px[i + 1], py[i + 1], dash, color, &phase);
}

void glcd_rect_clip(glcd_t *display, const glcd_rect_t *clip,
                    const glcd_rect_t *rect, const glcd_dash_t *dash, uint8_t color)
{
    if (rect->width <= 0 || rect->height <= 0) return;

    glcd_hline_clip(display, clip, rect->x, rect->y, rect->width, dash, color);
    glcd_hline_clip(display, clip, rect->x, rect->y + rect->height - 1, rect->width, dash, color);
    glcd_vline_clip(display, clip, rect->x, rect->y, rect->height, dash, color);
    glcd_vline_clip(display, clip, rect->x + rect->width - 1, rect->y, rect->height, dash, color);
}

void glcd_rect_fill_clip(glcd_t *display, const glcd_rect_t *clip,
                         const glcd_rect_t *rect, uint8_t color)
{
    glcd_rect_t visible;

    if (!glcd_rect_intersect(rect, clip, &visible)) return;

    glcd_rect_fill(display, (uint8_t)visible.x, (uint8_t)visible.y,
                   (uint8_t)visible.width, (uint8_t)visible.height, color);
}

void glcd_rect_invert_clip(glcd_t *display, const glcd_rect_t *clip, const glcd_rect_t *rect)
{
    glcd_rect_t visible;

    if (!glcd_rect_intersect(rect, clip, &visible)) return;

    glcd_rect_invert(display, (uint8_t)visible.x, (uint8_t)visible.y,
                     (uint8_t)visible.width, (uint8_t)visible.height);
}

int16_t glcd_text_width(const uint8_t *font, const char *text)
{
    uint8_t first_char = font[FONT_FIRST_CHAR];
    uint8_t char_count = font[FONT_CHAR_COUNT];
    int16_t total = 0;
    const char *p;

    for (p = text; *p; p++)
    {
        uint8_t c = (uint8_t)*p;
        uint8_t width;

        if (c < first_char || c >= (first_char + char_count)) continue;

        width = FONT_IS_MONO_SPACED(font) ? font[FONT_FIXED_WIDTH]
                                          : font[FONT_WIDTH_TABLE + (c - first_char)];
        total += width + FONT_INTERCHAR_SPACE;
    }

    return total ? (total - FONT_INTERCHAR_SPACE) : 0;
}

void glcd_text_clip(glcd_t *display, const glcd_rect_t *clip,
                    int16_t x, int16_t y, const char *text, const uint8_t *font, uint8_t color)
{
    uint8_t height = font[FONT_HEIGHT];
    uint8_t first_char = font[FONT_FIRST_CHAR];
    uint8_t char_count = font[FONT_CHAR_COUNT];
    uint8_t pages = (height + 7) / 8;
    const char *p;

    for (p = text; *p; p++)
    {
        uint8_t c = (uint8_t)*p;
        uint8_t width, column, row, i;
        uint16_t index;

        if (c < first_char || c >= (first_char + char_count)) continue;

        c -= first_char;

        // glyph indexing mirrors st7565p_text(), so both font layouts render the same
        if (FONT_IS_MONO_SPACED(font))
        {
            width = font[FONT_FIXED_WIDTH];
            index = (uint16_t)(c * width * pages) + FONT_WIDTH_TABLE;
        }
        else
        {
            width = font[FONT_WIDTH_TABLE + c];
            index = 0;
            for (i = 0; i < c; i++) index += font[FONT_WIDTH_TABLE + i];
            index = (uint16_t)(index * pages) + FONT_WIDTH_TABLE + char_count;
        }

        for (column = 0; column < width; column++)
        {
            for (row = 0; row < height; row++)
            {
                uint8_t bits = font[index + ((row / 8) * width) + column];

                if (bits & (1 << (row % 8)))
                    glcd_set_pixel_clip(display, clip, x + column, y + row, color);
            }
        }

        x += width + FONT_INTERCHAR_SPACE;
    }
}
