
/*
************************************************************************************************************************
*           Pedalboard minimap: parses the display list sent by mod-ui and draws it on the GLCD.
*
*           mod-ui does not send pixels. It sends a compact ASCII display list describing boxes, port
*           stubs and cables with their pixel geometry in a scene larger than the panel, and we draw it
*           here. That way panning, moving the selection and toggling signal layers stay local instead
*           of a round trip over a serial queue that carries one message at a time.
*
*           The same records are the hit map: a box rect is both what gets drawn and what gets selected,
*           so the two cannot drift apart.
*
*           Scene coordinates are global and int16_t; the panel is 128x64 uint8_t. Everything is clipped
*           here before it reaches the driver -- st7565p_set_pixel() wraps out-of-range coordinates with
*           a modulo instead of dropping them, so an unclipped draw lands in the wrong place rather than
*           doing nothing at all.
************************************************************************************************************************
*/

#ifndef MINIMAP_H
#define MINIMAP_H


/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <stdint.h>
#include "glcd.h"
#include "glcd_clip.h"   // glcd_rect_t, and the clipped primitives the renderer draws with


/*
************************************************************************************************************************
*           DO NOT CHANGE THESE DEFINES
************************************************************************************************************************
*/

// Records are separated by this, sent as a token of its own. Not a newline: the protocol splits
// a message on spaces only (protocol.c: strarr_split(msg->data, ' ')), so a newline would be
// glued onto the neighbouring token. mod-ui keeps it out of labels, and LV2 symbols cannot
// contain it, so it is unambiguous rather than merely unlikely.
#define MINIMAP_RECORD_SEP      ';'

// record types, first character of each record
#define MINIMAP_REC_HEADER      'M'
#define MINIMAP_REC_NODE        'N'
#define MINIMAP_REC_PORT        'P'
#define MINIMAP_REC_EDGE        'E'
#define MINIMAP_REC_ADJACENCY   'A'

// node kinds, as sent on the wire
#define MINIMAP_PLUGIN          'p'
#define MINIMAP_HW_SOURCE       'i'
#define MINIMAP_HW_SINK         'o'

// port directions, as sent on the wire
#define MINIMAP_PORT_IN         'i'
#define MINIMAP_PORT_OUT        'o'

// signal types, kept as a bitmask so a layer can be toggled without refetching
#define MINIMAP_AUDIO           (1 << 0)
#define MINIMAP_MIDI            (1 << 1)
#define MINIMAP_CV              (1 << 2)
#define MINIMAP_ALL_LAYERS      (MINIMAP_AUDIO | MINIMAP_MIDI | MINIMAP_CV)

// navigation directions for minimap_step(). The panel has one encoder for the graph and
// the graph is a sequence, not a compass: mod-ui orders every box left to right by column
// and top to bottom inside a column, and sends each one its neighbours in that order.
#define MINIMAP_PREV            0
#define MINIMAP_NEXT            1

#define MINIMAP_NONE            (-1)

/*
 * Two ways of showing the same scene. The graph is the picture; the list is the same boxes
 * as names in alphabetical order, which is easier to find something in and needs no
 * panning. The scene, the selection and the navigation are shared, so switching between
 * them keeps the cursor where it was.
 */
#define MINIMAP_VIEW_GRAPH      0
#define MINIMAP_VIEW_LIST       1


/*
************************************************************************************************************************
*           CONFIGURATION DEFINES
************************************************************************************************************************
*/

// Sized for one window rather than a whole pedalboard: mod-ui sends the plugins around the selection
// (MOD_MINIMAP_WIN_PLUGINS, 12 by default) plus the hardware nodes their cables reach, so the cost is
// bounded by the window and not by how big the board is.
#define MINIMAP_MAX_NODES       28
#define MINIMAP_MAX_PORTS       72
#define MINIMAP_MAX_EDGES       48
#define MINIMAP_MAX_POINTS      8
#define MINIMAP_LABEL_SIZE      14

// The label inside a box is cut to the width of the box, which in mod-ui's compact
// picture leaves five characters -- not enough to tell COMPRES from COMPR_2. Each node
// therefore carries a second label cut to the width of the panel, which is what the
// builder screen puts in its title bar for the selected node.
#define MINIMAP_TITLE_SIZE      26

// keep the selected box this far from the viewport edge when scrolling to it
#define MINIMAP_SCROLL_MARGIN   6


/*
************************************************************************************************************************
*           DATA TYPES
************************************************************************************************************************
*/

typedef struct MINIMAP_PORT_T {
    int16_t x, y;
    uint8_t node;           // index into minimap_t.nodes
    uint8_t pid;            // port id, unique within its node and direction
    uint8_t direction;      // MINIMAP_PORT_IN / MINIMAP_PORT_OUT
    uint8_t type;           // MINIMAP_AUDIO / MINIMAP_MIDI / MINIMAP_CV
} minimap_port_t;

typedef struct MINIMAP_NODE_T {
    int16_t id;             // node id as sent on the wire, not our array index
    glcd_rect_t rect;       // box in scene coordinates
    uint8_t kind;
    uint8_t bypassed;
    uint8_t layer, row;
    // Wire ids, not array indexes: mod-ui builds the walk over the whole board, so the
    // box before or after this one may well be outside the window we were sent. That is
    // the signal to go and ask for the window around it.
    int16_t prev, next;
    char label[MINIMAP_LABEL_SIZE];
    char title[MINIMAP_TITLE_SIZE];   // for the title bar; falls back to label
} minimap_node_t;

typedef struct MINIMAP_EDGE_T {
    int16_t px[MINIMAP_MAX_POINTS];
    int16_t py[MINIMAP_MAX_POINTS];
    uint8_t n_points;
    uint8_t type;
    // set when the far end is a plugin outside this window; the cable is still drawn, and following it
    // is how the user asks mod-ui for the next window
    uint8_t source_outside, target_outside;
} minimap_edge_t;

typedef struct MINIMAP_T {
    // scene
    int16_t scene_width, scene_height;
    uint32_t version;
    int16_t focus_id;
    uint8_t window_count, total_count;

    minimap_node_t nodes[MINIMAP_MAX_NODES];
    minimap_port_t ports[MINIMAP_MAX_PORTS];
    minimap_edge_t edges[MINIMAP_MAX_EDGES];
    uint8_t n_nodes, n_ports, n_edges;

    // view
    glcd_rect_t view;       // clip rectangle on the panel
    int16_t offset_x, offset_y;     // scene -> panel pan
    int8_t selected;        // index into nodes, or MINIMAP_NONE
    uint8_t layers;         // which signal types to draw
    uint8_t view_mode;
    uint8_t blink_off;      // hold the selection highlight back, for a blink      // MINIMAP_VIEW_GRAPH or MINIMAP_VIEW_LIST

    // the nodes in alphabetical order, for the list view; rebuilt when the mode is set
    uint8_t order[MINIMAP_MAX_NODES];

    /*
     * When a caller has narrowed down what may be picked -- the connection manager offering
     * only the boxes that could take the cable -- these are the ones the cursor stops on.
     * `filtered` off means everything is fair game, which is the ordinary case.
     */
    uint8_t selectable[MINIMAP_MAX_NODES];
    uint8_t filtered;
} minimap_t;


/*
************************************************************************************************************************
*           FUNCTION PROTOTYPES
************************************************************************************************************************
*/

// Resets the scene and points the view at the whole panel. Call once before the first parse.
void minimap_init(minimap_t *map);

// Parses a display list. Returns 1 when a usable scene was read, 0 otherwise. Partial or unknown
// records are skipped rather than failing the whole payload, so a truncated message degrades into a
// smaller picture instead of a blank screen.
uint8_t minimap_parse(minimap_t *map, const char *text);

// Sets the viewport rectangle on the panel; the rest of the screen is left to the caller. The
// scene is panned under this rectangle, and it is the clip region handed to the drawing
// primitives.
void minimap_set_view(minimap_t *map, uint8_t x, uint8_t y, uint8_t width, uint8_t height);

// Array index of the node with this wire id, or MINIMAP_NONE.
/*
 * Underscores back to spaces, in place. The protocol splits on spaces, so mod-ui's
 * sanitize_label() sends a space as an underscore -- and no command is keyed on a string,
 * the device always answers with ids and indexes, so nothing downstream needs the escaped
 * form back. Only for names a person wrote. An LV2 port symbol like `out_l` keeps its
 * underscore, which is part of the symbol and never was a space.
 */
// Holds the selection highlight back so the box flashes rather than sitting inverted.
// The graph screen drives it from its own tick while a plugin is armed for deletion.
void minimap_set_blink(minimap_t *map, uint8_t off);

void minimap_unescape(char *text);

int8_t minimap_index_of(const minimap_t *map, int16_t id);

// Wire id of the box before or after `from` in mod-ui's walk of the whole board, or
// MINIMAP_NONE at the two ends of it. The id may name a box outside this window; resolve it
// with minimap_index_of() and refetch when that returns MINIMAP_NONE.
int16_t minimap_step(const minimap_t *map, int8_t from, uint8_t direction);

// Selects a node and scrolls the view so it is visible.
void minimap_select(minimap_t *map, int8_t node);

// Scrolls by a delta, clamped to the scene.
void minimap_scroll(minimap_t *map, int16_t dx, int16_t dy);

// Draws the scene, as a graph or as a list. Does not clear the screen and does not call
// glcd_update().
void minimap_draw(glcd_t *display, const minimap_t *map);

// Switches between the picture and the list. Rebuilds the alphabetical order.
void minimap_set_view_mode(minimap_t *map, uint8_t mode);

// Restricts what the cursor may land on to these wire ids. A count of zero clears the
// restriction, which is how it starts.
void minimap_set_selectable(minimap_t *map, const int16_t *ids, uint8_t count);

// The selected node, or NULL.
const minimap_node_t *minimap_selected(const minimap_t *map);

// True when the node has a cable leading out of this window, i.e. there is more graph that way and the
// caller should ask mod-ui for a window centred elsewhere.
uint8_t minimap_has_offscreen_link(const minimap_t *map, int8_t node);


/*
************************************************************************************************************************
*           END HEADER
************************************************************************************************************************
*/

#endif
