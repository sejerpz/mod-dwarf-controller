/*
************************************************************************************************************************
*           plugin_list_test.c
*
*           Host-side checks for the list view: the windowed plugin_list_t the device walks a row
*           at a time, and the window order plugin_map.c falls back on, which has to follow every
*           parse rather than only a change of mode.
*
*           Build and run:
*               gcc -std=gnu99 -Wall -Wextra -Inxp-lpc -Iapp/inc -Idrivers/inc -Ifreertos/inc \
*                   -Imod-controller-proto -Inxp-lpc/CMSISv2p00_LPC177x_8xLib/inc \
*                   -Inxp-lpc/LPC177x_8xLib/inc \
*                   test/plugin_list_test.c app/src/plugin_map.c app/src/glcd_clip.c \
*                   -o /tmp/plugin_list_test && /tmp/plugin_list_test
************************************************************************************************************************
*/

#include <stdio.h>
#include <string.h>

#include "plugin_map.h"


/*
************************************************************************************************************************
*           DRIVER STUBS
*
*           The only thing drawn that matters here is the cursor row, which is the one inversion
*           plugin_list_draw() makes: its y is recorded.
************************************************************************************************************************
*/

static int g_inverted_y = -1;
static int g_inversions = 0;

void st7565p_set_pixel(st7565p_t *disp, uint8_t x, uint8_t y, uint8_t color)
{
    (void)disp; (void)x; (void)y; (void)color;
}

void st7565p_rect_fill(st7565p_t *disp, uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t color)
{
    (void)disp; (void)x; (void)y; (void)width; (void)height; (void)color;
}

void st7565p_rect_invert(st7565p_t *disp, uint8_t x, uint8_t y, uint8_t width, uint8_t height)
{
    (void)disp; (void)x; (void)width; (void)height;
    g_inverted_y = y;
    g_inversions++;
}


/*
************************************************************************************************************************
*           CHECKS
************************************************************************************************************************
*/

static int g_failed = 0, g_passed = 0;

static void check(int ok, const char *what)
{
    if (ok) g_passed++;
    else { g_failed++; printf("FAIL: %s\n", what); }
}

/* a window of a list `total` long starting at `first`, as parse_plugin_list() would fill it */
static void fill(plugin_list_t *list, uint16_t total, uint16_t first)
{
    uint8_t i;

    list->count = 0;
    list->total = total;
    list->first = first;
    list->loaded = 1;

    for (i = 0; i < PLUGIN_LIST_MAX_ROWS && first + i < total; i++)
    {
        list->rows[i].id = (int16_t)(first + i);
        list->rows[i].kind = BM_PLUGIN;
        snprintf(list->rows[i].title, PLUGIN_MAP_TITLE_SIZE, "FX%03d", first + i);
        list->count++;
    }
}

static void check_walk(void)
{
    plugin_list_t list;
    uint16_t fetches = 0;
    int16_t seen = 0;

    plugin_list_clear(&list);
    check(!plugin_list_step(&list, 1), "an empty list does not move");
    check(!plugin_list_needs_window(&list), "an empty list asks for nothing");

    // the walk the device makes over a board of 130 boxes, down and back up again
    fill(&list, 130, 0);
    list.hover = 0;

    for (;;)
    {
        const plugin_list_row_t *row = plugin_list_hovered(&list);

        if (!row || row->id != list.hover) break;
        seen++;

        if (!plugin_list_step(&list, 1)) break;
        if (plugin_list_needs_window(&list))
        {
            fill(&list, 130, plugin_list_window_for(&list));
            fetches++;
        }
    }

    check(seen == 130, "walking down reaches every row");
    check(list.hover == 129, "and stops on the last one");
    check(fetches > 0 && fetches < 10, "a window at a time, not a row at a time");

    seen = 0;
    for (;;)
    {
        const plugin_list_row_t *row = plugin_list_hovered(&list);

        if (!row || row->id != list.hover) break;
        seen++;

        if (!plugin_list_step(&list, -1)) break;
        if (plugin_list_needs_window(&list))
            fill(&list, 130, plugin_list_window_for(&list));

        check(list.first == 0 || list.hover - list.first >= PLUGIN_LIST_MARGIN,
              "walking up keeps a margin above the cursor");
    }

    check(seen == 130, "walking up reaches every row");
    check(list.hover == 0, "and stops on the first one");
}

static void check_draw(void)
{
    plugin_list_t list;
    glcd_rect_t view = { 2, 9, 124, 43 };
    glcd_t display;

    memset(&display, 0, sizeof display);

    // the cursor walks down the panel, then stays on the bottom row
    fill(&list, 100, 0);
    list.hover = 0;
    g_inversions = 0;
    plugin_list_draw(&display, &view, &list);
    check(g_inversions == 1 && g_inverted_y == view.y, "the first row is the cursor");

    list.hover = 20;
    g_inversions = 0;
    plugin_list_draw(&display, &view, &list);
    check(g_inversions == 1 && g_inverted_y == view.y + (43 / 7 - 1) * 7,
          "further down the cursor sits on the bottom row");

    // a cursor that outran the rows held draws no highlight rather than a wrong one
    list.hover = 70;
    g_inversions = 0;
    plugin_list_draw(&display, &view, &list);
    check(g_inversions == 0, "a cursor outside the window is not drawn");
}

/*
 * The fallback list: the window's order must follow a refetch made while the list is on
 * screen, or it points at the boxes of the window before.
 */
static void check_order_follows_parse(void)
{
    static plugin_map_t map;
    char big[] = "M 400 100 1 amc 1 3 3 ; "
                 "N 1 p 0 0 30 12 0 0 0 ZETA = ; "
                 "N 2 p 40 0 30 12 0 1 0 ALPHA = ; "
                 "N 3 p 80 0 30 12 0 2 0 MID = ;";
    char small[] = "M 400 100 2 amc 5 1 1 ; "
                   "N 5 p 0 0 30 12 0 0 0 ONLY = ;";
    uint8_t i, ok;

    plugin_map_init(&map);
    plugin_map_set_view(&map, 2, 9, 124, 43);

    plugin_map_parse(&map, big);
    plugin_map_set_view_mode(&map, PLUGIN_MAP_VIEW_LIST);
    check(strcmp(map.nodes[map.order[0]].title, "ALPHA") == 0 &&
          strcmp(map.nodes[map.order[2]].title, "ZETA") == 0, "the window sorts by name");

    // a smaller window arrives with the list still up
    plugin_map_parse(&map, small);
    check(map.n_nodes == 1 && map.order[0] == 0, "the order is rebuilt on a parse");

    // and back to a bigger one: every slot names a box that is really there
    plugin_map_parse(&map, big);
    ok = 1;
    for (i = 0; i < map.n_nodes; i++)
        if (map.order[i] >= map.n_nodes) ok = 0;
    check(ok && strcmp(map.nodes[map.order[1]].title, "MID") == 0,
          "every slot of the order is a box of this window");
}

int main(void)
{
    check_walk();
    check_draw();
    check_order_follows_parse();

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
