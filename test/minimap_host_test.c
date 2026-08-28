/*
************************************************************************************************************************
*           Host-side harness for app/src/minimap.c.
*
*           Runs the firmware's minimap parser and renderer on a desktop against a display list read
*           from stdin, and prints the resulting panel as ASCII art. That makes it possible to check
*           the firmware draws the same picture as the reference renderer on the mod-ui side without
*           flashing anything.
*
*           The four driver entry points minimap.c uses are transcribed from drivers/src/st7565p.c so
*           the buffer behaviour matches the real panel exactly, including the way write_data() ORs
*           into two pages when the baseline is not page aligned. Only the panel transfer is left out.
*
*           Two bounds checks below are deliberately NOT in the real driver: st7565p_text() does no
*           clipping and write_data() indexes buffer[y / 8][x] after a y += 8 without checking. Here
*           they shout on stderr instead of running off the buffer, so a clipping mistake in
*           minimap.c shows up as a test failure rather than as silent corruption on the device.
*
*           Build and run:
*               gcc -std=gnu99 -Wall -Wextra -Inxp-lpc -Iapp/inc -Idrivers/inc -Ifreertos/inc \
*                   -Imod-controller-proto -Inxp-lpc/CMSISv2p00_LPC177x_8xLib/inc \
*                   -Inxp-lpc/LPC177x_8xLib/inc \
*                   test/minimap_host_test.c app/src/minimap.c -o /tmp/minimap_test
*               curl -s "http://localhost:8888/pedalboard/minimap" | /tmp/minimap_test
*
*           Optional arguments: view_x view_y view_w view_h [selection] [offset_x offset_y]
************************************************************************************************************************
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minimap.h"
#include "fonts.h"

#define BUFFER_SIZE 8192

// same layout as drivers/src/st7565p.c (no X mirroring, unlike the UC1701 driver)
#define READ_BUFFER(disp, x, y)         disp->buffer[(y) / 8][(x)]
#define WRITE_BUFFER(disp, x, y, data)  disp->buffer[(y) / 8][(x)] = (data)


/*
************************************************************************************************************************
*           DRIVER TRANSCRIPTION
************************************************************************************************************************
*/

void st7565p_set_pixel(st7565p_t *disp, uint8_t x, uint8_t y, uint8_t color)
{
    x %= DISPLAY_WIDTH;
    y %= DISPLAY_HEIGHT;

    uint8_t data = READ_BUFFER(disp, x, y);
    data &= ~(1 << (y % 8));
    data |= ((color & 0x01) << (y % 8));
    WRITE_BUFFER(disp, x, y, data);
}

void st7565p_vline(st7565p_t *disp, uint8_t x, uint8_t y, uint8_t height, uint8_t color)
{
    uint8_t i = 0, tmp = color;

    while (height--)
    {
        if (color == ST7565P_BLACK_WHITE)
            tmp = ((i % 2) == 0) ? ST7565P_BLACK : ST7565P_WHITE;
        else if (color == ST7565P_WHITE_BLACK)
            tmp = ((i % 2) == 0) ? ST7565P_WHITE : ST7565P_BLACK;
        i++;

        st7565p_set_pixel(disp, x, y++, tmp);
    }
}

void st7565p_rect_fill(st7565p_t *disp, uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t color)
{
    uint8_t i = 0, tmp = color;

    while (width--)
    {
        if (color == ST7565P_CHESS)
            tmp = ((i % 2) == 0) ? ST7565P_BLACK_WHITE : ST7565P_WHITE_BLACK;
        i++;

        st7565p_vline(disp, x++, y, height, tmp);
    }
}

void st7565p_rect_invert(st7565p_t *disp, uint8_t x, uint8_t y, uint8_t width, uint8_t height)
{
    uint8_t mask, page_offset, h, i, data, data_tmp, x_tmp;

    page_offset = y % 8;
    mask = 0xFF;
    if (height < (8 - page_offset))
    {
        mask >>= (8 - height);
        h = height;
    }
    else
    {
        h = 8 - page_offset;
    }
    mask <<= page_offset;

    for (i = 0; i < width; i++)
    {
        x_tmp = x + i;
        data = READ_BUFFER(disp, x_tmp, y);
        data_tmp = ~data;
        data = (data_tmp & mask) | (data & ~mask);
        WRITE_BUFFER(disp, x_tmp, y, data);
    }

    while ((h + 8) <= height)
    {
        h += 8;
        y += 8;

        for (i = 0; i < width; i++)
        {
            x_tmp = x + i;
            data = READ_BUFFER(disp, x_tmp, y);
            WRITE_BUFFER(disp, x_tmp, y, ~data);
        }
    }

    if (h < height)
    {
        mask = ~(0xFF << (height - h));
        y += 8;

        for (i = 0; i < width; i++)
        {
            x_tmp = x + i;
            data = READ_BUFFER(disp, x_tmp, y);
            data_tmp = ~data;
            data = (data_tmp & mask) | (data & ~mask);
            WRITE_BUFFER(disp, x_tmp, y, data);
        }
    }
}

static void write_data(st7565p_t *disp, uint8_t x, uint8_t y, uint8_t data)
{
    uint8_t data_tmp, y_offset;

    y_offset = (y % 8);

    if (y_offset != 0)
    {
        data_tmp = READ_BUFFER(disp, x, y);
        data_tmp |= (data << y_offset);
        WRITE_BUFFER(disp, x, y, data_tmp);

        // the real driver writes this page unconditionally; see the note at the top of this file
        y += 8;
        if (y < DISPLAY_HEIGHT)
        {
            data_tmp = READ_BUFFER(disp, x, y);
            data_tmp |= data >> (8 - y_offset);
            WRITE_BUFFER(disp, x, y, data_tmp);
        }
        else
        {
            fprintf(stderr, "HARNESS: text wrote past the bottom of the buffer\n");
        }
    }
    else
    {
        WRITE_BUFFER(disp, x, y, data);
    }
}

void st7565p_text(st7565p_t *disp, uint8_t x, uint8_t y, const char *text, const uint8_t *font, uint8_t color)
{
    uint8_t i, j, x_tmp, y_tmp, c, bytes, data;
    uint16_t char_data_index, page;

    if (!font) font = FONT_DEFAULT;
    uint8_t width = font[FONT_FIXED_WIDTH];
    uint8_t height = font[FONT_HEIGHT];
    uint8_t first_char = font[FONT_FIRST_CHAR];
    uint8_t char_count = font[FONT_CHAR_COUNT];

    while (*text)
    {
        c = *text;
        if (c < first_char || c >= (first_char + char_count))
        {
            text++;
            continue;
        }

        c -= first_char;
        bytes = (height + 7) / 8;

        if (FONT_IS_MONO_SPACED(font))
        {
            char_data_index = (c * width * bytes) + FONT_WIDTH_TABLE;
        }
        else
        {
            width = font[FONT_WIDTH_TABLE + c];
            char_data_index = 0;
            for (i = 0; i < c; i++) char_data_index += font[FONT_WIDTH_TABLE + i];
            char_data_index = (char_data_index * bytes) + FONT_WIDTH_TABLE + char_count;
        }

        y_tmp = y;

        // the real driver has no such guard; see the note at the top of this file
        if ((x + width) > DISPLAY_WIDTH)
        {
            fprintf(stderr, "HARNESS: text ran off the right edge at x=%u\n", x);
            break;
        }

        for (j = 0; j < bytes; j++)
        {
            x_tmp = x;
            page = j * width;

            for (i = 0; i < width; i++)
            {
                data = font[char_data_index + page + i];
                if (color == ST7565P_WHITE) data = ~data;

                if (height > 8 && height < (j + 1) * 8)
                {
                    data >>= ((j + 1) * 8) - height;
                    data = READ_BUFFER(disp, x_tmp, y_tmp) | data;
                }

                write_data(disp, x_tmp, y_tmp, data);
                x_tmp++;
            }

            data = (color == ST7565P_BLACK ? 0x00 : 0xFF);
            if (*(text + 1) != '\0') write_data(disp, x_tmp, y_tmp, data);

            y_tmp += 8;
        }

        x += width + 1;
        text++;
    }
}


/*
************************************************************************************************************************
*           HARNESS
************************************************************************************************************************
*/

static void dump(const st7565p_t *disp)
{
    for (int y = 0; y < DISPLAY_HEIGHT; y++)
    {
        for (int x = 0; x < DISPLAY_WIDTH; x++)
        {
            uint8_t data = disp->buffer[y / 8][x];
            putchar((data & (1 << (y % 8))) ? '#' : '.');
        }
        putchar('\n');
    }
}

int main(int argc, char **argv)
{
    static st7565p_t display;
    static minimap_t map;
    static char text[BUFFER_SIZE];

    size_t length = fread(text, 1, sizeof(text) - 1, stdin);
    text[length] = 0;

    memset(&display, 0, sizeof(display));
    minimap_init(&map);

    // let the caller carve out a title bar / footer the way the builder screen does
    if (argc >= 5)
    {
        minimap_set_view(&map, (uint8_t)atoi(argv[1]), (uint8_t)atoi(argv[2]),
                         (uint8_t)atoi(argv[3]), (uint8_t)atoi(argv[4]));
    }

    if (!minimap_parse(&map, text))
    {
        fprintf(stderr, "minimap_parse failed: no usable header in %lu bytes\n",
                (unsigned long)length);
        return 1;
    }

    fprintf(stderr, "scene %dx%d v%lu  nodes=%u ports=%u edges=%u  window=%u/%u  offset=%d,%d selected=%d\n",
            map.scene_width, map.scene_height, (unsigned long)map.version,
            map.n_nodes, map.n_ports, map.n_edges,
            map.window_count, map.total_count,
            map.offset_x, map.offset_y, map.selected);

    // a 5th argument of 0 drops the selection, so the output can be diffed against the mod-ui
    // reference renderer, which draws no highlight by default
    if (argc >= 6 && atoi(argv[5]) == 0) map.selected = MINIMAP_NONE;

    // explicit pan, so clipping can be exercised at every edge
    if (argc >= 8)
    {
        map.offset_x = 0;
        map.offset_y = 0;
        minimap_scroll(&map, (int16_t)atoi(argv[6]), (int16_t)atoi(argv[7]));
    }

    minimap_draw(&display, &map);
    dump(&display);

    return 0;
}
