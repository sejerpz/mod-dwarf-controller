
/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <string.h>

#include "minimap.h"
#include "glcd_clip.h"
#include "fonts.h"


/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/

// Cable dash patterns: audio solid, MIDI dashed, CV dotted -- about as much as one bit per pixel
// allows, and matching what the reference renderer on the mod-ui side draws.
//
// Spelled out here rather than taken from the driver's ST7565P_BLACK_WHITE mode, which alternates
// BLACK and WHITE and so erases every other pixel instead of leaving it alone, and which offers only
// one pattern where three signal types need to stay apart.
static const glcd_dash_t DASH_MIDI = { 4, 2 };
static const glcd_dash_t DASH_CV   = { 3, 1 };

// dashed border for a bypassed plugin: every other pixel
static const glcd_dash_t DASH_BYPASS = { 2, 1 };

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

    while (*p && *p != ' ' && *p != MINIMAP_RECORD_SEP && *p != '\n' && *p != '\r')
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
    map->view.x = 0;
    map->view.y = 0;
    map->view.width = DISPLAY_WIDTH;
    map->view.height = DISPLAY_HEIGHT;
}

void minimap_set_view(minimap_t *map, uint8_t x, uint8_t y, uint8_t width, uint8_t height)
{
    if (!map) return;

    map->view.x = x;
    map->view.y = y;
    map->view.width = width;
    map->view.height = height;
}

int8_t minimap_index_of(const minimap_t *map, int16_t id)
{
    uint8_t i;

    // negative ids are real: hardware nodes live in the negative half of the id space. Only -1 is
    // reserved, as the "no node" sentinel, and no node ever carries it.
    if (!map) return MINIMAP_NONE;

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
        char record;

        // records may be padded with whitespace on either side of the separator
        while (*cursor == ' ' || *cursor == '\n' || *cursor == '\r') cursor++;
        record = *cursor;

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
                node->rect.x = read_int(&cursor);
                node->rect.y = read_int(&cursor);
                node->rect.width = read_int(&cursor);
                node->rect.height = read_int(&cursor);
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

                while (*cursor && *cursor != MINIMAP_RECORD_SEP)
                {
                    while (*cursor == ' ') cursor++;
                    if (!*cursor || *cursor == MINIMAP_RECORD_SEP) break;

                    if (edge->n_points >= MINIMAP_MAX_POINTS)
                    {
                        // skip the rest of the polyline; a cable longer than we can hold is drawn
                        // truncated rather than dropped
                        while (*cursor && *cursor != MINIMAP_RECORD_SEP) cursor++;
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

        // next record
        while (*line && *line != MINIMAP_RECORD_SEP) line++;
        if (*line == MINIMAP_RECORD_SEP) line++;
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
    if ((n->rect.x - margin) < map->offset_x)
        map->offset_x = n->rect.x - margin;
    else if ((n->rect.x + n->rect.width + margin) > (map->offset_x + map->view.width))
        map->offset_x = n->rect.x + n->rect.width + margin - map->view.width;

    if ((n->rect.y - margin) < map->offset_y)
        map->offset_y = n->rect.y - margin;
    else if ((n->rect.y + n->rect.height + margin) > (map->offset_y + map->view.height))
        map->offset_y = n->rect.y + n->rect.height + margin - map->view.height;

    minimap_scroll(map, 0, 0);
}

void minimap_scroll(minimap_t *map, int16_t dx, int16_t dy)
{
    int16_t max_x, max_y;

    if (!map) return;

    max_x = map->scene_width - map->view.width;
    max_y = map->scene_height - map->view.height;
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
    int16_t dx, dy;
    glcd_rect_t clip, rect;

    if (!display || !map) return;

    clip = map->view;

    // The one place the scene is mapped onto the panel. Adding this to a scene coordinate gives a
    // panel coordinate, which is all the glcd_*_clip primitives ever see -- they know nothing about
    // scenes or panning, which is what lets them be used by anything else that needs clipped drawing.
    dx = map->view.x - map->offset_x;
    dy = map->view.y - map->offset_y;

    // cables first, so the boxes paint over them
    for (i = 0; i < map->n_edges; i++)
    {
        const minimap_edge_t *edge = &map->edges[i];
        const glcd_dash_t *dash = 0;
        int16_t px[MINIMAP_MAX_POINTS], py[MINIMAP_MAX_POINTS];

        if (!(edge->type & map->layers)) continue;

        if (edge->type == MINIMAP_MIDI) dash = &DASH_MIDI;
        else if (edge->type == MINIMAP_CV) dash = &DASH_CV;

        // translate the polyline onto the panel; the primitives never see scene coordinates
        for (j = 0; j < edge->n_points; j++)
        {
            px[j] = edge->px[j] + dx;
            py[j] = edge->py[j] + dy;
        }

        glcd_polyline_clip(display, &clip, px, py, edge->n_points, dash, GLCD_BLACK);

        // Mark the end that leaves the window, so a cable running off the picture reads as "there is
        // more graph this way" rather than as a broken connection.
        if (edge->source_outside && edge->n_points >= 1)
        {
            int16_t x = edge->px[0] + dx;
            int16_t y = edge->py[0] + dy;
            for (j = 0; j < STUB_LENGTH; j++) glcd_set_pixel_clip(display, &clip, x, y - 2 + j, GLCD_BLACK);
        }

        if (edge->target_outside && edge->n_points >= 1)
        {
            int16_t x = edge->px[edge->n_points - 1] + dx;
            int16_t y = edge->py[edge->n_points - 1] + dy;
            for (j = 0; j < STUB_LENGTH; j++) glcd_set_pixel_clip(display, &clip, x, y - 2 + j, GLCD_BLACK);
        }
    }

    // boxes
    for (i = 0; i < map->n_nodes; i++)
    {
        const minimap_node_t *node = &map->nodes[i];

        rect.x = node->rect.x + dx;
        rect.y = node->rect.y + dy;
        rect.width = node->rect.width;
        rect.height = node->rect.height;

        // clear the interior so cables do not run through the label
        glcd_rect_fill_clip(display, &clip, &rect, GLCD_WHITE);

        // Centring the label belongs here now that glcd_text_clip() takes coordinates like
        // glcd_text() does, rather than a node.
        if (node->label[0] != 0 && node->label[0] != '-')
        {
            int16_t text_width = glcd_text_width(Terminal3x5, node->label);

            glcd_text_clip(display, &clip,
                           rect.x + ((rect.width - text_width) / 2),
                           rect.y + ((rect.height - Terminal3x5_HEIGHT) / 2),
                           node->label, Terminal3x5, GLCD_BLACK);
        }

        // a bypassed plugin gets a dashed border; there is no colour to spare on a 1-bit panel
        glcd_rect_clip(display, &clip, &rect, node->bypassed ? &DASH_BYPASS : 0, GLCD_BLACK);
    }

    // port stubs, drawn after the boxes so they sit on the border
    for (i = 0; i < map->n_ports; i++)
    {
        const minimap_port_t *port = &map->ports[i];

        if (!(port->type & map->layers)) continue;

        glcd_set_pixel_clip(display, &clip, port->x + dx, port->y + dy, GLCD_BLACK);
    }

    // Selection last, as an inversion of the whole box -- the one highlight a 1-bit panel does well,
    // and another filled area, so again straight to the driver.
    if (map->selected >= 0 && map->selected < map->n_nodes)
    {
        const minimap_node_t *node = &map->nodes[map->selected];

        rect.x = node->rect.x + dx;
        rect.y = node->rect.y + dy;
        rect.width = node->rect.width;
        rect.height = node->rect.height;

        glcd_rect_invert_clip(display, &clip, &rect);
    }
}
