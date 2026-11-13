
/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include "widget_area.h"
#include "config.h"
#include "hardware.h"
#include "naveg.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>


/*
************************************************************************************************************************
*           LOCAL DATA TYPES
************************************************************************************************************************
*/

typedef struct WIDGET_AREA_T {
    int16_t id;
    int8_t page, subpage;
    glcd_rect_t rect;
    // backing buffer while the area is in view, NULL otherwise
    uint8_t *pixels;
    struct WIDGET_AREA_T *next;
} widget_area_t;


/*
************************************************************************************************************************
*           LOCAL GLOBAL VARIABLES
************************************************************************************************************************
*/

// The list is changed from the tasks that talk to mod-ui and read from the higher priority system task,
// which never changes it: every change runs with the scheduler suspended so a bitmap arriving in the
// middle never sees a half linked or freed area.
static widget_area_t *g_areas = NULL;
static int8_t g_page = 0, g_subpage = 0;


/*
************************************************************************************************************************
*           LOCAL FUNCTIONS
************************************************************************************************************************
*/

static inline uint16_t row_bytes(int16_t width)
{
    return (uint16_t)((width + 7) / 8);
}

static uint8_t is_in_view(const widget_area_t *area)
{
    return area->page == g_page &&
           (area->subpage == WIDGET_AREA_ALL_SUBPAGES || area->subpage == g_subpage);
}

// pixels only reach the display on the control screen, never under an overlay
static uint8_t can_paint(void)
{
    // not before boot: the logo stays until the control view is printed
    return g_device_booted && naveg_get_current_mode() == MODE_CONTROL && hardware_get_overlay_counter() == 0;
}

static void update_buffer(widget_area_t *area)
{
    if (is_in_view(area))
    {
        if (area->pixels) return;

        const uint16_t size = row_bytes(area->rect.width) * area->rect.height;
        area->pixels = MALLOC(size);

        // out of memory: the area stays reserved, just blank
        if (area->pixels) memset(area->pixels, 0, size);
    }
    else if (area->pixels)
    {
        FREE(area->pixels);
        area->pixels = NULL;
    }
}

static void free_area(widget_area_t *area)
{
    if (area->pixels) FREE(area->pixels);
    FREE(area);
}

static widget_area_t *find_area(int16_t area_id)
{
    widget_area_t *area;
    for (area = g_areas; area; area = area->next)
    {
        if (area->id == area_id) return area;
    }
    return NULL;
}

// a digit that is not hex counts as 0, unlit pixels
static uint8_t hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c |= 0x20;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

// byte `index` of a row of hex digits `bytes` long, 0 outside of it
static uint8_t hex_byte(const char *hex, int16_t index, uint16_t bytes)
{
    if (!hex || index < 0 || index >= bytes) return 0;
    return (hex_nibble(hex[index * 2]) << 4) | hex_nibble(hex[index * 2 + 1]);
}

// The display buffer is in pages: one byte holds 8 rows of a column, the top one in the lowest bit.
// Turns 8 rows of a column byte of an area into the 8 page bytes of those columns (Hacker's Delight,
// transpose8), leftmost column first. Rows are given bottom first, so that the top row lands in bit 0.
static void rows_to_columns(const uint8_t rows[8], uint8_t columns[8])
{
    uint32_t x = ((uint32_t)rows[7] << 24) | ((uint32_t)rows[6] << 16) | ((uint32_t)rows[5] << 8) | rows[4];
    uint32_t y = ((uint32_t)rows[3] << 24) | ((uint32_t)rows[2] << 16) | ((uint32_t)rows[1] << 8) | rows[0];
    uint32_t t;

    t = (x ^ (x >> 7)) & 0x00AA00AA;  x = x ^ t ^ (t << 7);
    t = (y ^ (y >> 7)) & 0x00AA00AA;  y = y ^ t ^ (t << 7);
    t = (x ^ (x >> 14)) & 0x0000CCCC; x = x ^ t ^ (t << 14);
    t = (y ^ (y >> 14)) & 0x0000CCCC; y = y ^ t ^ (t << 14);
    t = (x & 0xF0F0F0F0) | ((y >> 4) & 0x0F0F0F0F);
    y = ((x << 4) & 0xF0F0F0F0) | (y & 0x0F0F0F0F);
    x = t;

    columns[0] = x >> 24; columns[1] = x >> 16; columns[2] = x >> 8; columns[3] = x;
    columns[4] = y >> 24; columns[5] = y >> 16; columns[6] = y >> 8; columns[7] = y;
}

// the byte of the display buffer holding column `x` of `page`
static inline uint8_t *display_byte(glcd_t *display, int16_t page, int16_t x)
{
#if GLCD_DRIVER == UC1701
    // this controller is mounted mirrored
    return &display->buffer[page][(DISPLAY_WIDTH - 1) - x];
#else
    return &display->buffer[page][x];
#endif
}

// Paints the part of `area` inside `clip`, both lit and unlit pixels, straight into the display buffer:
// 8 rows by 8 columns at a time, with masks for the pages and column bytes only partly inside.
static void paint_area(glcd_t *display, const widget_area_t *area, const glcd_rect_t *clip)
{
    glcd_rect_t r;
    if (!glcd_rect_intersect(&area->rect, clip, &r)) return;

    const uint16_t stride = row_bytes(area->rect.width);
    // the columns to paint, in area coordinates
    const int16_t ax0 = r.x - area->rect.x, ax1 = ax0 + r.width;
    int16_t page, bx, k;

    for (page = r.y / 8; page <= (r.y + r.height - 1) / 8; page++)
    {
        const int16_t top = page * 8;
        const int16_t y0 = r.y > top ? r.y : top;
        const int16_t y1 = r.y + r.height < top + 8 ? r.y + r.height : top + 8;
        // the rows of the page inside `r`
        const uint8_t mask = (uint8_t)((0xFF << (y0 - top)) & (0xFF >> (top + 8 - y1)));

        const uint8_t *rows[8];
        for (k = 0; k < 8; k++)
        {
            const int16_t y = top + k;
            rows[k] = (area->pixels && y >= y0 && y < y1) ? &area->pixels[(y - area->rect.y) * stride] : NULL;
        }

        for (bx = ax0 >> 3; bx <= (ax1 - 1) >> 3; bx++)
        {
            uint8_t bits[8], columns[8];
            for (k = 0; k < 8; k++)
                bits[k] = rows[k] ? rows[k][bx] : 0;

            rows_to_columns(bits, columns);

            int16_t j0 = ax0 - bx * 8, j1 = ax1 - bx * 8, j;
            if (j0 < 0) j0 = 0;
            if (j1 > 8) j1 = 8;

            for (j = j0; j < j1; j++)
            {
                uint8_t *dst = display_byte(display, page, area->rect.x + bx * 8 + j);
                *dst = (*dst & ~mask) | (columns[j] & mask);
            }
        }
    }
}


/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

uint8_t WA_add(int16_t area_id, int8_t page, int8_t subpage, const glcd_rect_t *rect)
{
    if (rect->x < 0 || rect->y < 0 || rect->width <= 0 || rect->height <= 0 ||
        rect->x + rect->width > DISPLAY_WIDTH || rect->y + rect->height > DISPLAY_HEIGHT)
        return 0;

    WA_remove(area_id);

    widget_area_t *area = MALLOC(sizeof(widget_area_t));
    if (!area) return 0;

    area->id = area_id;
    area->page = page;
    area->subpage = subpage;
    area->rect = *rect;
    area->pixels = NULL;

    vTaskSuspendAll();
    update_buffer(area);
    area->next = g_areas;
    g_areas = area;
    xTaskResumeAll();

    return 1;
}

void WA_remove(int16_t area_id)
{
    vTaskSuspendAll();

    widget_area_t **link = &g_areas;

    while (*link)
    {
        widget_area_t *area = *link;

        if (area_id == WIDGET_AREA_ALL || area->id == area_id)
        {
            *link = area->next;
            free_area(area);
        }
        else
        {
            link = &area->next;
        }
    }

    xTaskResumeAll();
}

static uint8_t rect_contains(const glcd_rect_t *outer, const glcd_rect_t *inner)
{
    return inner->x >= outer->x && inner->y >= outer->y &&
           inner->x + inner->width <= outer->x + outer->width &&
           inner->y + inner->height <= outer->y + outer->height;
}

uint8_t WA_set_screen(int8_t page, int8_t subpage, const glcd_rect_t *redrawn)
{
    uint8_t full_redraw = 0;
    widget_area_t *area;

    vTaskSuspendAll();

    // which areas change visibility, measured before the screen in view changes
    for (area = g_areas; area; area = area->next)
    {
        const uint8_t was_in_view = is_in_view(area);
        const uint8_t will_be_in_view = area->page == page &&
                                        (area->subpage == WIDGET_AREA_ALL_SUBPAGES || area->subpage == subpage);

        if (was_in_view != will_be_in_view && redrawn && !rect_contains(redrawn, &area->rect))
            full_redraw = 1;
    }

    g_page = page;
    g_subpage = subpage;

    for (area = g_areas; area; area = area->next)
        update_buffer(area);

    xTaskResumeAll();

    return full_redraw;
}

uint8_t WA_draw(int16_t area_id, int16_t x, int16_t y, int16_t width, int16_t height, const char *rows)
{
    widget_area_t *area = find_area(area_id);

    if (!area || !area->pixels || width <= 0 || height <= 0) return 0;

    const uint16_t src_stride = row_bytes(width);

    if (rows && strlen(rows) < (size_t)src_stride * height * 2) return 0;

    const uint16_t dst_stride = row_bytes(area->rect.width);

    // the columns to replace, in area coordinates, clipped to the area
    const int16_t ax0 = x > 0 ? x : 0;
    const int16_t ax1 = x + width < area->rect.width ? x + width : area->rect.width;
    if (ax0 >= ax1) return 1;

    // a byte of the area takes its bits from two bytes of the source when x is not a multiple of 8:
    // the source byte where the area byte starts, shifted left, and the next one, shifted right
    const int16_t first = ax0 >> 3, last = (ax1 - 1) >> 3;
    // bit offset of the first area byte in the source, plus 8 so that it never goes below 0
    const int16_t offset = first * 8 - x + 8;
    const uint8_t shift = offset & 7;
    int16_t row, i;

    for (row = 0; row < height; row++)
    {
        const int16_t ay = y + row;
        if (ay < 0 || ay >= area->rect.height) continue;

        uint8_t *dst = &area->pixels[ay * dst_stride];
        const char *src = rows ? &rows[row * src_stride * 2] : NULL;
        int16_t index = (offset >> 3) - 1;
        uint8_t next = hex_byte(src, index, src_stride);

        for (i = first; i <= last; i++, index++)
        {
            const uint8_t cur = next;
            next = hex_byte(src, index + 1, src_stride);
            const uint8_t bits = shift ? (uint8_t)((cur << shift) | (next >> (8 - shift))) : cur;

            // the bits of this byte inside [ax0, ax1)
            uint8_t mask = 0xFF;
            if (i == first) mask &= 0xFF >> (ax0 & 7);
            if (i == last)  mask &= 0xFF << (7 - ((ax1 - 1) & 7));

            dst[i] = (dst[i] & ~mask) | (bits & mask);
        }
    }

    if (can_paint())
    {
        const glcd_rect_t dirty = { area->rect.x + x, area->rect.y + y, width, height };
        paint_area(hardware_glcds(0), area, &dirty);
    }

    return 1;
}

uint8_t WA_covers(const glcd_rect_t *rect)
{
    widget_area_t *area;
    glcd_rect_t r;

    for (area = g_areas; area; area = area->next)
    {
        if (is_in_view(area) && glcd_rect_intersect(&area->rect, rect, &r))
            return 1;
    }

    return 0;
}

uint8_t WA_paint(const glcd_rect_t *rect)
{
    static const glcd_rect_t whole = { 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT };

    if (!can_paint()) return 0;
    if (!rect) rect = &whole;

    glcd_t *display = hardware_glcds(0);
    uint8_t touched = 0;
    widget_area_t *area;
    glcd_rect_t r;

    for (area = g_areas; area; area = area->next)
    {
        if (!is_in_view(area) || !glcd_rect_intersect(&area->rect, rect, &r)) continue;

        paint_area(display, area, rect);
        touched = 1;
    }

    return touched;
}
