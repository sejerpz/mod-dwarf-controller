
/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <string.h>

#include "minimap.h"
#include "fonts.h"


/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/

// cable dash patterns: audio is solid, MIDI dashed, CV dotted. About as much as one bit per pixel
// allows, and it matches what the reference renderer on the mod-ui side draws.
#define DASH_PERIOD_MIDI    4
#define DASH_ON_MIDI        2
#define DASH_PERIOD_CV      3
#define DASH_ON_CV          1

// a cable whose far end is outside the window is cut short and capped with this marker, so it reads as
// "the graph continues that way" instead of as a cable to nowhere
#define STUB_LENGTH         4


/*
************************************************************************************************************************
*           LOCAL FUNCTIONS
************************************************************************************************************************
*/

// Reads a non-negative or negative decimal integer and advances the cursor past it.
static int16_t read_int(const char **cursor)
{
    const char *p = *cursor;
    int16_t value = 0;
    uint8_t negative = 0;

    while (*p == ' ') p++;

    if (*p == '-')
    {
        negative = 1;
        p++;
    }

    while (*p >= '0' && *p <= '9')
    {
        value = (value * 10) + (*p - '0');
        p++;
    }

    *cursor = p;
    return negative ? -value : value;
}

// Reads the next non-space token as a single character and advances past the token.
static char read_char(const char **cursor)
{
    const char *p = *cursor;
    char value;

    while (*p == ' ') p++;
    value = *p;
    while (*p && *p != ' ') p++;

    *cursor = p;
    return value;
}

// Copies the next token into dest, truncating rather than overflowing.
static void read_token(const char **cursor, char *dest, uint8_t size)
{
    const char *p = *cursor;
    uint8_t i = 0;

    while (*p == ' ') p++;

    while (*p && *p != ' ' && *p != '\n' && *p != '\r')
    {
        if (i < (size - 1)) dest[i++] = *p;
        p++;
    }

    dest[i] = 0;
    *cursor = p;
}

static uint8_t type_from_char(char c)
{
    if (c == 'm') return MINIMAP_MIDI;
    if (c == 'c') return MINIMAP_CV;
    return MINIMAP_AUDIO;
}

static int16_t clamp16(int16_t value, int16_t low, int16_t high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}


/*
************************************************************************************************************************
*           CLIPPED DRAWING
*
*           The driver does not clip: st7565p_set_pixel() folds out-of-range coordinates back into the
*           panel with a modulo, so anything drawn without bounds checking reappears somewhere wrong.
*           Every primitive below therefore translates scene coordinates to the panel and drops what
*           falls outside the viewport.
************************************************************************************************************************
*/

// Scene point -> panel point, drawing only if it lands inside the viewport.
static void plot(glcd_t *display, const minimap_t *map, int16_t sx, int16_t sy)
{
    int16_t x = sx - map->offset_x + map->view_x;
    int16_t y = sy - map->offset_y + map->view_y;

    if (x < map->view_x || x >= (map->view_x + map->view_width)) return;
    if (y < map->view_y || y >= (map->view_y + map->view_height)) return;

    glcd_set_pixel(display, (uint8_t)x, (uint8_t)y, GLCD_BLACK);
}

// Bresenham, so a segment is clipped per pixel and dash patterns can be counted along it. The router
// on the mod-ui side only emits axis-aligned segments, but handling the general case costs nothing and
// avoids a silent gap if that ever changes.
static void draw_segment(glcd_t *display, const minimap_t *map,
                         int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                         uint8_t period, uint8_t on, uint16_t *phase)
{
    int16_t dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int16_t dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;

    for (;;)
    {
        if (period == 0 || (*phase % period) < on)
            plot(display, map, x0, y0);
        (*phase)++;

        if (x0 == x1 && y0 == y1) break;

        int16_t e2 = err * 2;
        if (e2 > -dy)
        {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

// Clips a scene rectangle to the viewport and hands the driver panel coordinates. Returns 0 when
// nothing of it is visible.
static uint8_t clip_rect(const minimap_t *map, int16_t sx, int16_t sy, int16_t w, int16_t h,
                         uint8_t *out_x, uint8_t *out_y, uint8_t *out_w, uint8_t *out_h)
{
    int16_t x0 = sx - map->offset_x + map->view_x;
    int16_t y0 = sy - map->offset_y + map->view_y;
    int16_t x1 = x0 + w;
    int16_t y1 = y0 + h;

    int16_t vx0 = map->view_x;
    int16_t vy0 = map->view_y;
    int16_t vx1 = map->view_x + map->view_width;
    int16_t vy1 = map->view_y + map->view_height;

    if (x0 < vx0) x0 = vx0;
    if (y0 < vy0) y0 = vy0;
    if (x1 > vx1) x1 = vx1;
    if (y1 > vy1) y1 = vy1;

    if (x1 <= x0 || y1 <= y0) return 0;

    *out_x = (uint8_t)x0;
    *out_y = (uint8_t)y0;
    *out_w = (uint8_t)(x1 - x0);
    *out_h = (uint8_t)(y1 - y0);
    return 1;
}

// Box outline drawn edge by edge through plot(), so a box hanging off the viewport loses only the part
// that is outside instead of being dropped or wrapping around.
static void draw_box_outline(glcd_t *display, const minimap_t *map, const minimap_node_t *node)
{
    int16_t x0 = node->x;
    int16_t y0 = node->y;
    int16_t x1 = node->x + node->width - 1;
    int16_t y1 = node->y + node->height - 1;
    int16_t i;

    // a bypassed plugin gets a dashed border; there is no colour to spare on a 1-bit panel
    uint8_t step = node->bypassed ? 2 : 1;

    for (i = x0; i <= x1; i += step)
    {
        plot(display, map, i, y0);
        plot(display, map, i, y1);
    }

    for (i = y0; i <= y1; i += step)
    {
        plot(display, map, x0, i);
        plot(display, map, x1, i);
    }
}

// Labels are blitted glyph by glyph through plot() rather than handed to glcd_text().
//
// glcd_text() cannot be used safely here. st7565p_text() does no clipping at all, and the
// write_data() it calls indexes buffer[y / 8][x] again after a y += 8 with no bounds check, so a
// baseline in the last page writes past the end of the frame buffer -- buffer is the final member of
// st7565p_t, so that lands in whatever follows the struct. Going through plot() also means a label
// that is only half inside the viewport draws its visible half instead of vanishing, which is what
// the reference renderer on the mod-ui side does.
static void draw_label(glcd_t *display, const minimap_t *map, const minimap_node_t *node)
{
    const uint8_t *font = Terminal3x5;
    uint8_t width = font[FONT_FIXED_WIDTH];
    uint8_t height = font[FONT_HEIGHT];
    uint8_t first_char = font[FONT_FIRST_CHAR];
    uint8_t char_count = font[FONT_CHAR_COUNT];
    uint8_t pages = (height + 7) / 8;

    const char *p;
    int16_t x, y, text_width;
    uint8_t length, column, row;

    if (node->label[0] == 0 || node->label[0] == '-') return;

    length = (uint8_t)strlen(node->label);
    text_width = (int16_t)(length * (width + FONT_INTERCHAR_SPACE)) - FONT_INTERCHAR_SPACE;

    x = node->x + ((node->width - text_width) / 2);
    y = node->y + ((node->height - height) / 2);

    for (p = node->label; *p; p++)
    {
        uint8_t c = (uint8_t)*p;

        if (c >= first_char && c < (first_char + char_count))
        {
            uint16_t index = (uint16_t)((c - first_char) * width * pages) + FONT_WIDTH_TABLE;

            for (column = 0; column < width; column++)
            {
                for (row = 0; row < height; row++)
                {
                    uint8_t bits = font[index + ((row / 8) * width) + column];

                    if (bits & (1 << (row % 8)))
                        plot(display, map, x + column, y + row);
                }
            }
        }

        x += width + FONT_INTERCHAR_SPACE;
    }
}


/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

void minimap_init(minimap_t *map)
{
    if (!map) return;

    memset(map, 0, sizeof(minimap_t));

    map->selected = MINIMAP_NONE;
    map->focus_id = MINIMAP_NONE;
    map->layers = MINIMAP_ALL_LAYERS;
    map->view_x = 0;
    map->view_y = 0;
    map->view_width = DISPLAY_WIDTH;
    map->view_height = DISPLAY_HEIGHT;
}

void minimap_set_view(minimap_t *map, uint8_t x, uint8_t y, uint8_t width, uint8_t height)
{
    if (!map) return;

    map->view_x = x;
    map->view_y = y;
    map->view_width = width;
    map->view_height = height;
}

int8_t minimap_index_of(const minimap_t *map, int16_t id)
{
    uint8_t i;

    if (!map || id < 0) return MINIMAP_NONE;

    for (i = 0; i < map->n_nodes; i++)
    {
        if (map->nodes[i].id == id) return (int8_t)i;
    }

    return MINIMAP_NONE;
}

uint8_t minimap_parse(minimap_t *map, const char *text)
{
    const char *line = text;
    uint8_t header_seen = 0;

    if (!map || !text) return 0;

    // keep the view where it was: a refetch after an edit should not throw away the user's scroll
    map->n_nodes = 0;
    map->n_ports = 0;
    map->n_edges = 0;

    while (*line)
    {
        const char *cursor = line;
        char record = *cursor;

        // advance past the record letter
        if (*cursor) cursor++;

        switch (record)
        {
            case MINIMAP_REC_HEADER:
            {
                char mask[8];

                map->scene_width = read_int(&cursor);
                map->scene_height = read_int(&cursor);
                map->version = (uint32_t)read_int(&cursor);

                read_token(&cursor, mask, sizeof(mask));
                map->layers = 0;
                if (strchr(mask, 'a')) map->layers |= MINIMAP_AUDIO;
                if (strchr(mask, 'm')) map->layers |= MINIMAP_MIDI;
                if (strchr(mask, 'c')) map->layers |= MINIMAP_CV;

                map->focus_id = read_int(&cursor);
                map->window_count = (uint8_t)read_int(&cursor);
                map->total_count = (uint8_t)read_int(&cursor);

                header_seen = 1;
                break;
            }

            case MINIMAP_REC_NODE:
            {
                if (map->n_nodes >= MINIMAP_MAX_NODES) break;

                minimap_node_t *node = &map->nodes[map->n_nodes];

                node->id = read_int(&cursor);
                node->kind = (uint8_t)read_char(&cursor);
                node->x = read_int(&cursor);
                node->y = read_int(&cursor);
                node->width = (uint8_t)read_int(&cursor);
                node->height = (uint8_t)read_int(&cursor);
                node->bypassed = (uint8_t)read_int(&cursor);
                node->layer = (uint8_t)read_int(&cursor);
                node->row = (uint8_t)read_int(&cursor);
                read_token(&cursor, node->label, MINIMAP_LABEL_SIZE);

                node->up = MINIMAP_NONE;
                node->down = MINIMAP_NONE;
                node->left = MINIMAP_NONE;
                node->right = MINIMAP_NONE;

                map->n_nodes++;
                break;
            }

            case MINIMAP_REC_PORT:
            {
                if (map->n_ports >= MINIMAP_MAX_PORTS) break;

                int16_t node_id = read_int(&cursor);
                int8_t index = minimap_index_of(map, node_id);

                // ports arrive after their node; a port for an unknown node means a truncated payload
                if (index == MINIMAP_NONE) break;

                minimap_port_t *port = &map->ports[map->n_ports];

                port->node = (uint8_t)index;
                port->pid = (uint8_t)read_int(&cursor);
                port->direction = (uint8_t)read_char(&cursor);
                port->type = type_from_char(read_char(&cursor));
                port->x = read_int(&cursor);
                port->y = read_int(&cursor);

                map->n_ports++;
                break;
            }

            case MINIMAP_REC_EDGE:
            {
                if (map->n_edges >= MINIMAP_MAX_EDGES) break;

                minimap_edge_t *edge = &map->edges[map->n_edges];
                char token[16];

                read_int(&cursor);                      // edge id, not needed for drawing
                read_token(&cursor, token, sizeof(token));   // source node:port
                read_token(&cursor, token, sizeof(token));   // target node:port

                edge->type = type_from_char(read_char(&cursor));
                edge->source_outside = (uint8_t)read_int(&cursor);
                edge->target_outside = (uint8_t)read_int(&cursor);
                edge->n_points = 0;

                while (*cursor && *cursor != '\n' && *cursor != '\r')
                {
                    while (*cursor == ' ') cursor++;
                    if (!*cursor || *cursor == '\n' || *cursor == '\r') break;

                    if (edge->n_points >= MINIMAP_MAX_POINTS)
                    {
                        // skip the rest of the polyline; a cable longer than we can hold is drawn
                        // truncated rather than dropped
                        while (*cursor && *cursor != '\n' && *cursor != '\r') cursor++;
                        break;
                    }

                    edge->px[edge->n_points] = read_int(&cursor);
                    if (*cursor == ',') cursor++;
                    edge->py[edge->n_points] = read_int(&cursor);
                    edge->n_points++;
                }

                if (edge->n_points >= 2) map->n_edges++;
                break;
            }

            case MINIMAP_REC_ADJACENCY:
            {
                int16_t node_id = read_int(&cursor);
                int8_t index = minimap_index_of(map, node_id);

                if (index == MINIMAP_NONE) break;

                minimap_node_t *node = &map->nodes[index];

                node->up = minimap_index_of(map, read_int(&cursor));
                node->down = minimap_index_of(map, read_int(&cursor));
                node->left = minimap_index_of(map, read_int(&cursor));
                node->right = minimap_index_of(map, read_int(&cursor));
                break;
            }

            default:
                break;
        }

        // next line
        while (*line && *line != '\n') line++;
        if (*line == '\n') line++;
    }

    if (!header_seen) return 0;

    // keep the selection pointing at the same plugin across a refetch; fall back to the focus mod-ui
    // used, then to the first node
    if (map->selected >= map->n_nodes) map->selected = MINIMAP_NONE;

    if (map->selected == MINIMAP_NONE)
    {
        map->selected = minimap_index_of(map, map->focus_id);

        if (map->selected == MINIMAP_NONE && map->n_nodes > 0)
            map->selected = 0;
    }

    if (map->selected != MINIMAP_NONE)
        minimap_select(map, map->selected);

    return 1;
}

int8_t minimap_navigate(const minimap_t *map, int8_t from, uint8_t direction)
{
    if (!map || from < 0 || from >= map->n_nodes) return MINIMAP_NONE;

    const minimap_node_t *node = &map->nodes[from];

    switch (direction)
    {
        case MINIMAP_UP:    return node->up;
        case MINIMAP_DOWN:  return node->down;
        case MINIMAP_LEFT:  return node->left;
        case MINIMAP_RIGHT: return node->right;
        default:            return MINIMAP_NONE;
    }
}

void minimap_select(minimap_t *map, int8_t node)
{
    if (!map || node < 0 || node >= map->n_nodes) return;

    map->selected = node;

    const minimap_node_t *n = &map->nodes[node];
    int16_t margin = MINIMAP_SCROLL_MARGIN;

    // scroll only as far as needed to bring the box inside, so the picture moves as little as possible
    if ((n->x - margin) < map->offset_x)
        map->offset_x = n->x - margin;
    else if ((n->x + n->width + margin) > (map->offset_x + map->view_width))
        map->offset_x = n->x + n->width + margin - map->view_width;

    if ((n->y - margin) < map->offset_y)
        map->offset_y = n->y - margin;
    else if ((n->y + n->height + margin) > (map->offset_y + map->view_height))
        map->offset_y = n->y + n->height + margin - map->view_height;

    minimap_scroll(map, 0, 0);
}

void minimap_scroll(minimap_t *map, int16_t dx, int16_t dy)
{
    int16_t max_x, max_y;

    if (!map) return;

    max_x = map->scene_width - map->view_width;
    max_y = map->scene_height - map->view_height;
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;

    map->offset_x = clamp16(map->offset_x + dx, 0, max_x);
    map->offset_y = clamp16(map->offset_y + dy, 0, max_y);
}

const minimap_node_t *minimap_selected(const minimap_t *map)
{
    if (!map || map->selected < 0 || map->selected >= map->n_nodes) return 0;
    return &map->nodes[map->selected];
}

uint8_t minimap_has_offscreen_link(const minimap_t *map, int8_t node)
{
    uint8_t i;

    if (!map || node < 0 || node >= map->n_nodes) return 0;

    for (i = 0; i < map->n_edges; i++)
    {
        const minimap_edge_t *edge = &map->edges[i];

        if (!(edge->type & map->layers)) continue;
        if (edge->source_outside || edge->target_outside) return 1;
    }

    return 0;
}

void minimap_draw(glcd_t *display, const minimap_t *map)
{
    uint8_t i, j;
    uint8_t rx, ry, rw, rh;

    if (!display || !map) return;

    // cables first, so the boxes paint over them
    for (i = 0; i < map->n_edges; i++)
    {
        const minimap_edge_t *edge = &map->edges[i];
        uint8_t period = 0, on = 0;
        uint16_t phase = 0;

        if (!(edge->type & map->layers)) continue;

        if (edge->type == MINIMAP_MIDI)
        {
            period = DASH_PERIOD_MIDI;
            on = DASH_ON_MIDI;
        }
        else if (edge->type == MINIMAP_CV)
        {
            period = DASH_PERIOD_CV;
            on = DASH_ON_CV;
        }

        for (j = 0; j + 1 < edge->n_points; j++)
        {
            draw_segment(display, map,
                         edge->px[j], edge->py[j],
                         edge->px[j + 1], edge->py[j + 1],
                         period, on, &phase);
        }

        // mark the end that leaves the window, so a cable running off the picture reads as "there is
        // more graph this way" rather than as a broken connection
        if (edge->source_outside && edge->n_points >= 1)
        {
            int16_t x = edge->px[0];
            int16_t y = edge->py[0];
            for (j = 0; j < STUB_LENGTH; j++) plot(display, map, x, y - 2 + j);
        }

        if (edge->target_outside && edge->n_points >= 1)
        {
            int16_t x = edge->px[edge->n_points - 1];
            int16_t y = edge->py[edge->n_points - 1];
            for (j = 0; j < STUB_LENGTH; j++) plot(display, map, x, y - 2 + j);
        }
    }

    // boxes
    for (i = 0; i < map->n_nodes; i++)
    {
        const minimap_node_t *node = &map->nodes[i];

        // clear the interior so cables do not run through the label
        if (clip_rect(map, node->x, node->y, node->width, node->height, &rx, &ry, &rw, &rh))
            glcd_rect_fill(display, rx, ry, rw, rh, GLCD_WHITE);

        // label before the outline: write_byte() overwrites a whole 8-row page when the baseline is
        // page aligned, which would eat the bottom border if the order were reversed
        draw_label(display, map, node);
        draw_box_outline(display, map, node);
    }

    // port stubs, drawn after the boxes so they sit on the border
    for (i = 0; i < map->n_ports; i++)
    {
        const minimap_port_t *port = &map->ports[i];

        if (!(port->type & map->layers)) continue;

        plot(display, map, port->x, port->y);
    }

    // selection last, as an inversion of the whole box -- the one highlight a 1-bit panel does well
    if (map->selected >= 0 && map->selected < map->n_nodes)
    {
        const minimap_node_t *node = &map->nodes[map->selected];

        if (clip_rect(map, node->x, node->y, node->width, node->height, &rx, &ry, &rw, &rh))
            glcd_rect_invert(display, rx, ry, rw, rh);
    }
}
