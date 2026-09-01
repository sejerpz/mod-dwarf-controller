/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "hardware.h"
#include "protocol.h"
#include "ui_comm.h"
#include "utils.h"
#include "minimap.h"
#include "screen.h"
#include "mode_builder.h"
#include "mode_builder_connmanager.h"


/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/

#define BM_MAX_CONNECTIONS  48
// mod-ui caps a label at 25 characters; the rest is the direction arrow and the terminator
#define BM_CONN_LABEL_SIZE  30
// hardware_timestamp() counts in units of 500us, so two ticks to the millisecond
#define BM_BLINK_TICKS      (400 * 2)

/*
 * A menu row stands for every cable between the same two boxes, so a stereo pair is one
 * row. The strip under the list names the ports behind it, one cable at a time, and these
 * bound what mod-ui sends along: a pool shared by the whole menu, and a ceiling per row so
 * that one improbably wired box cannot eat it. Mirrored in mod-ui's settings.py.
 */
#define BM_MAX_PAIRS        48
#define BM_MAX_ROW_PAIRS    4
#define BM_PAIR_LABEL_SIZE  14


/*
************************************************************************************************************************
*           LOCAL DATA TYPES
************************************************************************************************************************
*/

typedef struct BM_CONNECTION_T {
    int16_t node;                   // wire id of the box at the far end
    uint8_t direction;              // MINIMAP_PORT_IN when the cable comes into ours
    uint8_t type;                   // MINIMAP_AUDIO / MINIMAP_MIDI / MINIMAP_CV
    char label[BM_CONN_LABEL_SIZE];
    uint8_t pair_first;             // where this row's cables start in g_pairs
    uint8_t pair_total;             // how many of them there are
} bm_connection_t;

/* one cable, feeding end first whichever side of it we are on */
typedef struct BM_PAIR_T {
    char source[BM_PAIR_LABEL_SIZE];
    char sink[BM_PAIR_LABEL_SIZE];
} bm_pair_t;


/*
 * "New connection..." walks three lists in the same screen: the cables this box has, the
 * boxes it could be wired to, and then that box's ports. All three arrive in the same wire
 * format, so one parser and one row builder serve them; in a port list the `node` field of
 * an entry carries the port index instead of a node id.
 */
/*
 * "New connection..." walks four lists in the same screen, always forward: our own output,
 * then the boxes that output could feed, then that box's inputs. A list with one entry is
 * not worth asking about, so it is taken and skipped.
 */
enum MenuModes {
    MENU_CABLES,
    MENU_OWN_PORT,
    MENU_TARGETS,
    MENU_TARGET_PORT
};

static enum MenuModes g_menu_mode = MENU_CABLES;

/*
 * Our own port comes first, and it settles everything after it: an input of ours wants
 * somebody's output, an output wants somebody's input, and the type has to match too.
 */
static int16_t g_own_port;
static uint8_t g_own_bits;
static uint8_t g_own_is_output;
static char g_own_label[BM_CONN_LABEL_SIZE];

static bm_connection_t g_target;
static char g_title[34];            /* "SRC -> PORT" and then "-> DST", for the title bar */

/* the destination is chosen on the picture itself; this is which of its two modes */
static uint8_t g_pick_list;


/*
************************************************************************************************************************
*           LOCAL GLOBAL VARIABLES
************************************************************************************************************************
*/

/* the graph the popup sits on; borrowed from mode_builder for as long as it is open */
static minimap_t *g_map;
static uint8_t g_open;

/*
 * The box all of this is about, taken once when the popup opens. Not read back off the
 * cursor: choosing a destination moves the cursor onto it, and everything after would
 * then treat the destination as the source and wire the box to itself.
 */
static int16_t g_ours;
static char g_ours_label[BM_CONN_LABEL_SIZE];

/* the connection menu of the selected box */
static bm_connection_t g_connections[BM_MAX_CONNECTIONS];
static uint8_t g_connection_count;

/* the cables of every row, end to end; each row owns a slice. The third encoder walks
   the slice of the row under the cursor, which is what the strip shows. */
static bm_pair_t g_pairs[BM_MAX_PAIRS];
static uint8_t g_pair_count;
static uint8_t g_pair_hover;

static char *g_menu_rows[BM_MAX_CONNECTIONS + 1];
static int8_t g_menu_link[BM_MAX_CONNECTIONS + 1];
static uint8_t g_menu_count;
static int16_t g_menu_hover;

/* one type at a time, cycled by the second button; there is no "all" yet */
static uint8_t g_filter = MINIMAP_AUDIO;

/* the cable armed for deletion, and which half of its blink we are on */
static int8_t g_armed = -1;
static uint8_t g_blink_off;
static uint32_t g_blink_stamp;


/*
************************************************************************************************************************
*           LOCAL FUNCTIONS
************************************************************************************************************************
*/

static void build_connection_menu(void)
{
    uint8_t i, pass;

    g_menu_count = 0;

    // What feeds this box first, then what it feeds. No headings: the arrow already sits
    // on the side the signal comes from, so "CHORUS >" reads into us and "> CABINET" out.
    for (pass = 0; pass < 2; pass++)
    {
        for (i = 0; i < g_connection_count; i++)
        {
            uint8_t incoming = (g_connections[i].direction == MINIMAP_PORT_IN);

            if ((pass == 0) != incoming) continue;

            g_menu_rows[g_menu_count] = g_connections[i].label;
            g_menu_link[g_menu_count] = (int8_t) i;
            g_menu_count++;
        }
    }

    if (g_menu_hover >= g_menu_count) g_menu_hover = 0;

    g_pair_hover = 0;
}

static void parse_connections(void *data, menu_item_t *item)
{
    (void) item;
    char **list = data;
    uint32_t i, count, at;

    g_connection_count = 0;
    g_pair_count = 0;

    //error, dont parse when mod-ui gives error
    if (!list || !list[0] || !list[1] || atoi(list[1]) == -1) return;
    if (!list[2]) return;

    count = (uint32_t) atoi(list[2]);

    /*
     * Five tokens per entry -- direction, far node id, type bitmask, label, and how many
     * cables the row stands for -- then two port names for each of those cables. The
     * stride is therefore not fixed, so the entries are walked with a cursor.
     */
    for (i = 0, at = 3; i < count && g_connection_count < BM_MAX_CONNECTIONS; i++)
    {
        char **entry = &list[at];
        bm_connection_t *connection;
        uint32_t pairs, p;

        if (!entry[0] || !entry[1] || !entry[2] || !entry[3] || !entry[4]) break;

        connection = &g_connections[g_connection_count];
        connection->direction = (uint8_t) entry[0][0];
        connection->node = (int16_t) atoi(entry[1]);
        connection->type = (uint8_t) atoi(entry[2]);

        pairs = (uint32_t) atoi(entry[4]);
        at += 5;

        connection->pair_first = g_pair_count;
        connection->pair_total = 0;

        for (p = 0; p < pairs; p++, at += 2)
        {
            if (!list[at] || !list[at + 1]) { pairs = p; break; }
            if (g_pair_count >= BM_MAX_PAIRS) continue;

            strncpy(g_pairs[g_pair_count].source, list[at], BM_PAIR_LABEL_SIZE - 1);
            g_pairs[g_pair_count].source[BM_PAIR_LABEL_SIZE - 1] = 0;
            strncpy(g_pairs[g_pair_count].sink, list[at + 1], BM_PAIR_LABEL_SIZE - 1);
            g_pairs[g_pair_count].sink[BM_PAIR_LABEL_SIZE - 1] = 0;

            g_pair_count++;
            connection->pair_total++;
        }

        // the arrow goes on at once, so the row is ready to draw and the menu needs no
        // second copy of every name
        {
            uint8_t room = BM_CONN_LABEL_SIZE - 3;

            /*
             * A row is the name of a box in the two lists of boxes and an LV2 port symbol
             * in the two lists of ports. Only the first was ever a name with spaces in
             * it; `out_l` keeps its underscore, which is part of the symbol. The pairs
             * under the list are symbols too, and are left alone for the same reason.
             */
            if (g_menu_mode == MENU_CABLES || g_menu_mode == MENU_TARGETS)
                minimap_unescape(entry[3]);

            if (connection->direction == MINIMAP_PORT_IN)
            {
                strncpy(connection->label, entry[3], room);
                connection->label[room] = 0;
                strcat(connection->label, " >");
            }
            else
            {
                connection->label[0] = '>';
                connection->label[1] = ' ';
                strncpy(connection->label + 2, entry[3], room);
                connection->label[2 + room] = 0;
            }
        }

        g_connection_count++;
    }
}

/*
 * Both lists come back in the same shape, so one request and one parser serve the cables
 * this box has and the boxes it could be wired to.
 */
static void request_menu(int16_t node_id)
{
    uint8_t i;
    char buffer[32];
    memset(buffer, 0, sizeof buffer);

    ui_comm_webgui_set_response_cb(parse_connections, NULL);
    ui_comm_webgui_clear_tx_buffer();

    // response: "r 1 2 i 4 1 LEFTCHAIN o -4 1 OUT1"
    uint8_t ports_list = (g_menu_mode == MENU_OWN_PORT || g_menu_mode == MENU_TARGET_PORT);

    if (ports_list)
        i = copy_command((char *)buffer, CMD_DWARF_BUILDER_PORTS);
    else if (g_menu_mode == MENU_TARGETS)
        i = copy_command((char *)buffer, CMD_DWARF_BUILDER_TARGETS);
    else
        i = copy_command((char *)buffer, CMD_DWARF_BUILDER_CONNECTIONS);

    i += int_to_str(node_id, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';

    /*
     * The button's filter picks the type until one of our ports has been chosen; from then
     * on that port decides, so an audio output is only ever offered audio inputs.
     */
    i += int_to_str((g_menu_mode == MENU_CABLES || g_menu_mode == MENU_OWN_PORT)
                        ? g_filter : g_own_bits,
                    &buffer[i], sizeof(buffer) - i, 0);

    if (g_menu_mode == MENU_OWN_PORT)
    {
        // both sides of our own box, inputs first: which one is picked is the direction
        buffer[i++] = ' ';
        i += int_to_str(2, &buffer[i], sizeof(buffer) - i, 0);
    }
    else if (g_menu_mode == MENU_TARGETS || g_menu_mode == MENU_TARGET_PORT)
    {
        // the far end is always the mirror of ours: a cable never joins two of a kind
        buffer[i++] = ' ';
        i += int_to_str(g_own_is_output ? 0 : 1, &buffer[i], sizeof(buffer) - i, 0);
    }

    buffer[i++] = 0;

    ui_comm_webgui_send(buffer, i);
    ui_comm_webgui_wait_response();

    build_connection_menu();
}

static void send_and_wait(const char *buffer, uint8_t length)
{
    ui_comm_webgui_set_response_cb(NULL, NULL);
    ui_comm_webgui_clear_tx_buffer();
    ui_comm_webgui_send(buffer, length);
    ui_comm_webgui_wait_response();
}

static void request_disconnect(const bm_connection_t *connection, int16_t ours)
{
    uint8_t i;
    char buffer[40];
    memset(buffer, 0, sizeof buffer);

    // the command reads from -> to, so our own box goes on whichever end we are
    int16_t from = (connection->direction == MINIMAP_PORT_IN) ? connection->node : ours;
    int16_t to = (connection->direction == MINIMAP_PORT_IN) ? ours : connection->node;

    i = copy_command((char *)buffer, CMD_DWARF_BUILDER_DISCONNECT);
    i += int_to_str(from, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str(to, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str(connection->type, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = 0;

    send_and_wait(buffer, i);
}

/*
 * Both ends named, from -> to, with our own box on whichever end the direction puts it.
 */
static void request_connect(int16_t ours, int16_t their_port)
{
    uint8_t i;
    char buffer[56];
    memset(buffer, 0, sizeof buffer);

    i = copy_command((char *)buffer, CMD_DWARF_BUILDER_CONNECT);

    // from -> to, with our box on whichever end our own port put it
    i += int_to_str(g_own_is_output ? ours : g_target.node, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str(g_own_is_output ? g_own_port : their_port, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str(g_own_is_output ? g_target.node : ours, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str(g_own_is_output ? their_port : g_own_port, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str(g_own_bits, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = 0;

    send_and_wait(buffer, i);
}

static int16_t selected_node_id(void)
{
    const minimap_node_t *node = g_map ? minimap_selected(g_map) : NULL;
    return node ? node->id : MINIMAP_NONE;
}

/*
 * The blink is driven from the displays task, not from the overlay timeout. The overlay is
 * a device-wide state: while one is up, naveg swallows the next button or encoder press to
 * dismiss it, so DEL never reached us -- and when it expired of its own accord the screen
 * went back to the main one. Nothing about a blinking row is an overlay.
 */
static void connection_disarm(void)
{
    g_armed = -1;
    g_blink_off = 0;
}

static void connection_arm(int8_t index)
{
    g_armed = index;
    g_blink_off = 0;
    g_blink_stamp = hardware_timestamp();
}

static void open_connections(void)
{
    const minimap_node_t *node = g_map ? minimap_selected(g_map) : NULL;

    g_ours = selected_node_id();

    g_ours_label[0] = 0;
    if (node)
    {
        strncpy(g_ours_label, node->title, sizeof g_ours_label - 1);
        g_ours_label[sizeof g_ours_label - 1] = 0;
    }

    g_menu_hover = 0;
    g_menu_mode = MENU_CABLES;
    connection_disarm();
    request_menu(g_ours);
}

/* the steps of "New connection...", which BACK unwinds from here */
static void build_title(const char *destination);
static void enter_own_ports(void);
static void enter_targets(void);
static void release_picker(void);

static void close_connections(void)
{
    connection_disarm();

    // three lists nested in one screen, so BACK unwinds them one at a time
    if (g_menu_mode == MENU_TARGET_PORT)
    {
        build_title(NULL);
        enter_targets();
    }
    else if (g_menu_mode == MENU_TARGETS)
    {
        release_picker();
        enter_own_ports();
    }
    else if (g_menu_mode == MENU_OWN_PORT)
    {
        g_menu_mode = MENU_CABLES;
        g_menu_hover = 0;
        request_menu(g_ours);
    }
    else
    {
        g_open = 0;
    }

}

/*
 * The title grows as the choice narrows: "SRC -> PORT" once our own port is picked, and
 * "SRC -> PORT -> DST" once the far box is. Thirty-two characters is all the bar holds, so
 * the three names are cut to nine, six and nine -- the ports are short anyway.
 */
static uint8_t copy_cut(char *dst, uint8_t at, const char *text, uint8_t room)
{
    uint8_t length;

    // "> IN" and "OUT >" both carry an arrow; the name is what is left between them
    if (text[0] == '>' && text[1] == ' ') text += 2;

    length = (uint8_t) strlen(text);
    if (length >= 2 && text[length - 2] == ' ' && text[length - 1] == '>') length -= 2;
    if (length > room) length = room;

    memcpy(&dst[at], text, length);
    return at + length;
}

static void build_title(const char *destination)
{
    uint8_t i = 0;

    // our own name, kept from when the popup opened: the cursor wanders off during a pick
    if (g_ours_label[0]) i = copy_cut(g_title, i, g_ours_label, 9);

    g_title[i++] = ' ';
    g_title[i++] = '-';
    g_title[i++] = '>';
    g_title[i++] = ' ';
    i = copy_cut(g_title, i, g_own_label, 6);

    if (destination)
    {
        g_title[i++] = ' ';
        g_title[i++] = '-';
        g_title[i++] = '>';
        g_title[i++] = ' ';
        i = copy_cut(g_title, i, destination, 9);
    }

    g_title[i] = 0;
}

static void do_connect(int16_t their_port)
{
    int16_t ours = g_ours;

    if (ours == MINIMAP_NONE) return;

    request_connect(ours, their_port);

    // the graph gained a cable, so the picture and the list are both stale
    g_menu_mode = MENU_CABLES;
    g_menu_hover = 0;
    BM_refresh_graph(ours);
    request_menu(ours);
}

static void enter_target_ports(void)
{
    g_menu_mode = MENU_TARGET_PORT;
    g_menu_hover = 0;
    request_menu(g_target.node);

    if (g_connection_count == 0)
    {
        // nothing on their side after all, so there is nothing to stand in
        enter_targets();
        build_title(NULL);
    }
}

/*
 * Hands the picture back the way it was before it was borrowed as a chooser: the whole
 * board pickable again, the graph mode, and the window around the box we started from.
 */
static void release_picker(void)
{
    int8_t index;

    minimap_set_selectable(g_map, NULL, 0);
    minimap_set_view_mode(g_map, MINIMAP_VIEW_GRAPH);

    // back to the box the popup is about, cursor and window both
    BM_refresh_graph(g_ours);

    index = minimap_index_of(g_map, g_ours);
    if (index != MINIMAP_NONE) minimap_select(g_map, index);
}

/*
 * Borrows the picture as the chooser: only the boxes that could take the cable can be
 * landed on, and the cursor starts on the first of them.
 */
static void enter_targets(void)
{
    int16_t ids[BM_MAX_CONNECTIONS];
    uint8_t i;

    g_menu_mode = MENU_TARGETS;
    g_menu_hover = 0;


    request_menu(g_ours);


    for (i = 0; i < g_connection_count; i++)
        ids[i] = g_connections[i].node;

    minimap_set_selectable(g_map, ids, g_connection_count);
    minimap_set_view_mode(g_map, g_pick_list ? MINIMAP_VIEW_LIST : MINIMAP_VIEW_GRAPH);


    // land on something pickable; a candidate outside this window is fetched to reach it
    if (g_connection_count > 0)
    {
        int8_t index = minimap_index_of(g_map, ids[0]);


        if (index == MINIMAP_NONE)
        {
            BM_refresh_graph(ids[0]);
            minimap_set_selectable(g_map, ids, g_connection_count);
            minimap_set_view_mode(g_map, g_pick_list ? MINIMAP_VIEW_LIST
                                                     : MINIMAP_VIEW_GRAPH);
            index = minimap_index_of(g_map, ids[0]);
        }


        if (index != MINIMAP_NONE) minimap_select(g_map, index);
    }
}

/*
 * The cursor on the picture, one pickable box at a time. A candidate outside the window is
 * fetched the same way ordinary navigation fetches one.
 */
static void picker_move(int8_t step)
{
    const minimap_node_t *node = minimap_selected(g_map);
    int16_t target;
    int8_t index;

    if (!node) return;

    index = minimap_index_of(g_map, node->id);
    if (index == MINIMAP_NONE) return;

    target = minimap_step(g_map, index, (step < 0) ? MINIMAP_PREV : MINIMAP_NEXT);
    if (target == MINIMAP_NONE) return;

    index = minimap_index_of(g_map, target);

    if (index == MINIMAP_NONE)
    {
        int16_t ids[BM_MAX_CONNECTIONS];
        uint8_t i;

        for (i = 0; i < g_connection_count; i++) ids[i] = g_connections[i].node;

        BM_refresh_graph(target);
        minimap_set_selectable(g_map, ids, g_connection_count);
        minimap_set_view_mode(g_map, g_pick_list ? MINIMAP_VIEW_LIST : MINIMAP_VIEW_GRAPH);

        index = minimap_index_of(g_map, target);
        if (index == MINIMAP_NONE) return;
    }

    minimap_select(g_map, index);
}

/*
 * Whatever the cursor is on becomes the destination.
 */
static void picker_click(void)
{
    const minimap_node_t *node = minimap_selected(g_map);

    if (!node) return;

    g_target.node = node->id;
    g_target.type = g_own_bits;
    g_target.direction = g_own_is_output ? MINIMAP_PORT_OUT : MINIMAP_PORT_IN;
    strncpy(g_target.label, node->title, BM_CONN_LABEL_SIZE - 1);
    g_target.label[BM_CONN_LABEL_SIZE - 1] = 0;

    release_picker();

    build_title(g_target.label);
    enter_target_ports();
}

static void pick_own_port(int8_t index)
{

    g_own_port = g_connections[index].node;
    g_own_bits = g_connections[index].type;

    // the list marks a port that feeds with MINIMAP_PORT_IN, which is our output
    g_own_is_output = (g_connections[index].direction == MINIMAP_PORT_IN);

    strncpy(g_own_label, g_connections[index].label, BM_CONN_LABEL_SIZE - 1);
    g_own_label[BM_CONN_LABEL_SIZE - 1] = 0;


    build_title(NULL);


    enter_targets();

}

static void enter_own_ports(void)
{
    g_menu_mode = MENU_OWN_PORT;
    g_menu_hover = 0;
    request_menu(g_ours);

    if (g_connection_count == 0)
    {
        // nothing to wire from or to, so there is no point standing in an empty list
        g_menu_mode = MENU_CABLES;
        request_menu(g_ours);
    }
}

static void connection_move(int8_t step)
{
    if (g_menu_mode == MENU_TARGETS)
    {
        picker_move(step);
        return;
    }

    int16_t next = g_menu_hover + step;

    if (next < 0)
    {
        /*
         * Insisting backwards past the first row closes the popup: the encoder that opened
         * it closes it too, so leaving does not mean reaching for a button. Only from the
         * list of cables -- inside the three lists that follow ADD, backwards is just the
         * top of the list, and BACK is what steps out of them.
         */
        if (g_menu_mode == MENU_CABLES) close_connections();
        return;
    }

    if (next >= g_menu_count) return;

    connection_disarm();
    g_menu_hover = next;
    g_pair_hover = 0;
}

/* the row under the cursor, or NULL where the list holds ports rather than cables */
static const bm_connection_t *hovered_connection(void)
{
    int8_t link;

    if (g_menu_mode != MENU_CABLES) return NULL;
    if (g_menu_hover < 0 || g_menu_hover >= g_menu_count) return NULL;

    link = g_menu_link[g_menu_hover];
    if (link < 0 || link >= g_connection_count) return NULL;

    return &g_connections[link];
}

/*
 * Taking what is under the cursor: a port, a box on the picture, a port at the far end.
 * On the second button rather than on the encoder click, so that every step of building a
 * connection is the same gesture -- and so the encoder is only ever for moving.
 */
static void connection_select(void)
{
    int8_t link;

    if (g_menu_mode == MENU_TARGETS)
    {
        picker_click();
        return;
    }

    if (g_menu_hover < 0 || g_menu_hover >= g_menu_count) return;

    link = g_menu_link[g_menu_hover];
    if (link < 0 || link >= g_connection_count) return;

    if (g_menu_mode == MENU_OWN_PORT)
        // in a port list an entry's `node` is the port index the host handed out
        pick_own_port((int8_t) link);
    else if (g_menu_mode == MENU_TARGET_PORT)
        do_connect(g_connections[link].node);
}

/*
 * The encoder click, which now only arms a cable for deletion. The lists that follow ADD
 * are taken with the second button instead.
 */
static void connection_click(void)
{
    int8_t link;

    if (g_menu_mode != MENU_CABLES) return;
    if (g_menu_hover < 0 || g_menu_hover >= g_menu_count) return;

    link = g_menu_link[g_menu_hover];
    if (link < 0) return;

    if (g_armed == link)
        connection_disarm();
    else
        connection_arm(link);

}

static void connection_cycle_filter(void)
{

    // audio -> midi -> cv -> audio; there is no "all" yet
    if (g_filter == MINIMAP_AUDIO) g_filter = MINIMAP_MIDI;
    else if (g_filter == MINIMAP_MIDI) g_filter = MINIMAP_CV;
    else g_filter = MINIMAP_AUDIO;

    connection_disarm();
    g_menu_hover = 0;
    request_menu(g_ours);
}

static void connection_delete(void)
{
    int16_t ours = g_ours;
    bm_connection_t dropped;

    if (g_menu_mode != MENU_CABLES) return;
    if (g_armed < 0 || g_armed >= g_connection_count || ours == MINIMAP_NONE) return;

    // keep a copy: the refetch below rebuilds the list under us
    dropped = g_connections[g_armed];
    connection_disarm();

    request_disconnect(&dropped, ours);

    // the graph lost a cable, so both the picture and the menu are stale
    BM_refresh_graph(ours);
    request_menu(ours);

}



/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

void BM_conn_manager_init(void)
{
    g_map = NULL;
    g_open = 0;
    g_filter = MINIMAP_AUDIO;
    connection_disarm();
}

void BM_conn_manager_open(minimap_t *map)
{
    if (!map) return;

    g_map = map;

    if (selected_node_id() == MINIMAP_NONE) return;

    g_open = 1;
    open_connections();
}

uint8_t BM_conn_manager_is_open(void)
{
    return g_open;
}

void BM_conn_manager_back(void)
{
    if (!g_open) return;
    close_connections();
}

void BM_conn_manager_turn(int8_t step)
{
    if (!g_open) return;
    connection_move(step);
}

void BM_conn_manager_click(void)
{
    if (!g_open) return;
    connection_click();
}

void BM_conn_manager_filter(void)
{
    if (!g_open) return;
    connection_cycle_filter();
}

void BM_conn_manager_delete(void)
{
    if (!g_open) return;

    /*
     * One button, three jobs. In the list of cables it is ADD with nothing armed and DEL
     * with something armed; in the three lists that follow ADD it is SELECT, which is the
     * one gesture that walks the whole of building a connection.
     */
    if (g_menu_mode != MENU_CABLES)
    {
        connection_select();
        return;
    }

    if (g_armed < 0)
    {
        enter_own_ports();
        return;
    }

    connection_delete();
}

void BM_conn_manager_view(uint8_t list)
{
    // only while choosing a destination; elsewhere the picture is not what is on screen
    if (!g_open || g_menu_mode != MENU_TARGETS) return;

    g_pick_list = list;
    minimap_set_view_mode(g_map, list ? MINIMAP_VIEW_LIST : MINIMAP_VIEW_GRAPH);
}

void BM_conn_manager_pairs_turn(int8_t step)
{
    const bm_connection_t *connection = hovered_connection();

    if (!g_open || !connection || connection->pair_total < 2) return;

    // wraps: with two or three cables behind a row, either way round is a short trip
    if (step < 0)
        g_pair_hover = (g_pair_hover == 0) ? (uint8_t)(connection->pair_total - 1)
                                           : (uint8_t)(g_pair_hover - 1);
    else
        g_pair_hover = (uint8_t)((g_pair_hover + 1) % connection->pair_total);
}

uint8_t BM_conn_manager_tick(void)
{
    if (!g_open || g_armed < 0) return 0;

    if ((hardware_timestamp() - g_blink_stamp) < BM_BLINK_TICKS) return 0;

    g_blink_stamp = hardware_timestamp();
    g_blink_off = !g_blink_off;
    return 1;
}

uint8_t BM_conn_manager_is_picking(void)
{
    return g_open && g_menu_mode == MENU_TARGETS;
}

const char *BM_conn_manager_pick_title(void)
{
    return g_title;
}

void BM_conn_manager_fill(connections_t *model)
{
    const bm_connection_t *connection = hovered_connection();

    model->title = (g_menu_mode == MENU_TARGETS || g_menu_mode == MENU_TARGET_PORT)
                        ? g_title
                 : g_ours_label[0] ? g_ours_label : "CONNECTIONS";
    model->rows = g_menu_rows;
    model->count = g_menu_count;
    model->hover = g_menu_hover;
    model->filter = (g_filter == MINIMAP_MIDI) ? "MIDI"
                  : (g_filter == MINIMAP_CV) ? "CV" : "AUDIO";
    model->can_delete = (g_armed >= 0) && (g_menu_mode == MENU_CABLES);
    model->can_add = (g_armed < 0) && (g_menu_mode == MENU_CABLES);
    model->can_select = (g_menu_mode == MENU_OWN_PORT || g_menu_mode == MENU_TARGET_PORT);
    model->blink_reverse = !g_blink_off;

    model->pair_source = NULL;
    model->pair_sink = NULL;
    model->pair_count = 0;
    model->pair_index = 0;

    if (connection && connection->pair_total)
    {
        uint8_t at = connection->pair_first + g_pair_hover;

        if (g_pair_hover >= connection->pair_total) at = connection->pair_first;

        model->pair_source = g_pairs[at].source;
        model->pair_sink = g_pairs[at].sink;
        model->pair_count = connection->pair_total;
        model->pair_index = (uint8_t)(at - connection->pair_first);
    }
}
