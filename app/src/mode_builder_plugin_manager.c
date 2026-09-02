/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "config.h"
#include "hardware.h"
#include "protocol.h"
#include "ui_comm.h"
#include "utils.h"
#include "plugin_map.h"
#include "screen.h"
#include "mode_builder.h"
#include "mode_builder_plugin_manager.h"


/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/

// as many rows as the host is willing to send in one window
#define PM_MAX_ROWS         48
// mod-ui cuts a label to a column's width, 14 characters, plus the terminator
#define PM_LABEL_SIZE       16
// categories are few: Favorites, All and whatever LV2 declares
#define PM_MAX_CATEGORIES   24
// A to Z, the ten digits and a little room for whatever else a name starts with
#define PM_MAX_INITIALS     40

/*
 * The info overlay. A field is a name, a brand or a category, none of which is worth more
 * than a line; the description is what is left of the panel, and mod-ui already caps it at
 * what three lines of Terminal3x5 can hold.
 */
#define PM_INFO_FIELD       26
#define PM_INFO_TEXT        400

/*
 * The host reads every installed plugin the first time this screen is opened, which takes
 * long enough to look broken. After this long with no answer, say what is going on --
 * short enough that it appears while the wait still feels like a wait, long enough that a
 * quick answer never flashes it. hardware_timestamp() counts in units of 500us.
 */
#define PM_NOTICE_TICKS     (400 * 2)


/*
************************************************************************************************************************
*           LOCAL GLOBAL VARIABLES
************************************************************************************************************************
*/

static uint8_t g_open;

/* what the notice says while the host is scanning; one edit changes both lines */
/* Terminal3x5 is marked all caps in the font table, so this reads uppercase on the panel
   whatever case it is written in here. The longer line is 111px, which is what the box
   screen_notice() draws is sized to hold. */
static const char PM_NOTICE_FIRST[] = "reading available plugins...";
static const char PM_NOTICE_SECOND[] = "(first time only)";

/*
 * The info overlay, which covers the two lists while it is up. Fetched on the button
 * rather than as the cursor moves: it is one request per press instead of one per row.
 */
static uint8_t g_info_open;
static char g_info_name[PM_INFO_FIELD];
static char g_info_brand[PM_INFO_FIELD];
static char g_info_category[PM_INFO_FIELD];
static uint8_t g_info_ports[6];         /* audio in/out, MIDI in/out, CV in/out */
static char g_info_text[PM_INFO_TEXT];
static uint8_t g_info_first;            /* the line of the description the panel starts at */

static char g_category_text[PM_MAX_CATEGORIES][PM_LABEL_SIZE];
static char *g_category_rows[PM_MAX_CATEGORIES];
static uint8_t g_category_count;
static int16_t g_category_hover;

/*
 * A category can hold hundreds of plugins and this panel holds a few dozen rows, so the
 * host sends a window and the hover slides it. g_plugin_first is where the window starts
 * and g_plugin_total how long the whole list is; the hover is an absolute position.
 */
static char g_plugin_text[PM_MAX_ROWS][PM_LABEL_SIZE];
static char *g_plugin_rows[PM_MAX_ROWS];
static uint8_t g_plugin_count;
static uint16_t g_plugin_first;
static uint16_t g_plugin_total;
static int16_t g_plugin_hover;

/* all -> audio -> midi -> cv, cycled by the second button */
static uint8_t g_filter;

/* the box mod-ui made of the plugin we just added */
static int16_t g_added;

/*
 * Holding the plugin encoder down scrubs by first letter instead of by row. The letters
 * and where each one starts come from the host on the first hold and are kept for as long
 * as the list they belong to.
 */
static char g_initial[PM_MAX_INITIALS];
static uint16_t g_initial_at[PM_MAX_INITIALS];
static uint8_t g_initial_count;
static uint8_t g_initials_loaded;

static uint8_t g_held;          /* the plugin encoder is down */
static uint8_t g_scrubbing;     /* ... and has been turned since, so this is a scrub */
static int16_t g_scrub_at;
static char g_scrub_text[2];


/*
************************************************************************************************************************
*           LOCAL FUNCTIONS
************************************************************************************************************************
*/

/*
 * Waits like ui_comm_webgui_wait_response(), but puts a notice up when the host is slow.
 *
 * The wait is a spin, so nobody else is awake to draw it: this has to. Once the notice is
 * up the poll yields a tick at a time, or the display task never gets the processor and
 * the notice never reaches the panel.
 */
static void wait_for_host(void)
{
    uint32_t start = hardware_timestamp();
    uint8_t noticed = 0;

    while (ui_comm_webgui_wait_pending())
    {
        if (noticed)
        {
            vTaskDelay(1);
            continue;
        }

        if ((hardware_timestamp() - start) < PM_NOTICE_TICKS) continue;

        screen_notice(PM_NOTICE_FIRST, PM_NOTICE_SECOND);
        noticed = 1;
    }
}

static void parse_categories(void *data, menu_item_t *item)
{
    (void) item;
    char **list = data;
    uint32_t i, count;

    g_category_count = 0;

    if (!list || !list[0] || !list[1] || atoi(list[1]) == -1) return;
    if (!list[2]) return;

    count = (uint32_t) atoi(list[2]);

    // two tokens an entry: the index, then the label
    for (i = 0; i < count && g_category_count < PM_MAX_CATEGORIES; i++)
    {
        char **entry = &list[3 + i * 2];

        if (!entry[0] || !entry[1]) break;

        // a category is a name, not an id: the wire's underscores were spaces
        strncpy(g_category_text[g_category_count], entry[1], PM_LABEL_SIZE - 1);
        g_category_text[g_category_count][PM_LABEL_SIZE - 1] = 0;
        plugin_map_unescape(g_category_text[g_category_count]);
        g_category_rows[g_category_count] = g_category_text[g_category_count];

        g_category_count++;
    }
}

static void parse_catalog(void *data, menu_item_t *item)
{
    (void) item;
    char **list = data;
    uint32_t i, count;

    g_plugin_count = 0;
    g_plugin_total = 0;
    g_plugin_first = 0;

    if (!list || !list[0] || !list[1] || atoi(list[1]) == -1) return;
    if (!list[2] || !list[3] || !list[4]) return;

    g_plugin_total = (uint16_t) atoi(list[2]);
    g_plugin_first = (uint16_t) atoi(list[3]);
    count = (uint32_t) atoi(list[4]);

    for (i = 0; i < count && g_plugin_count < PM_MAX_ROWS; i++)
    {
        char **entry = &list[5 + i * 2];

        if (!entry[0] || !entry[1]) break;

        strncpy(g_plugin_text[g_plugin_count], entry[1], PM_LABEL_SIZE - 1);
        g_plugin_text[g_plugin_count][PM_LABEL_SIZE - 1] = 0;
        plugin_map_unescape(g_plugin_text[g_plugin_count]);
        g_plugin_rows[g_plugin_count] = g_plugin_text[g_plugin_count];

        g_plugin_count++;
    }
}

static void parse_initials(void *data, menu_item_t *item)
{
    (void) item;
    char **list = data;
    uint32_t i, count;

    g_initial_count = 0;

    if (!list || !list[0] || !list[1] || atoi(list[1]) == -1) return;
    if (!list[2]) return;

    count = (uint32_t) atoi(list[2]);

    // two tokens an entry: where the letter starts, then the letter
    for (i = 0; i < count && g_initial_count < PM_MAX_INITIALS; i++)
    {
        char **entry = &list[3 + i * 2];

        if (!entry[0] || !entry[1]) break;

        g_initial_at[g_initial_count] = (uint16_t) atoi(entry[0]);
        g_initial[g_initial_count] = entry[1][0];

        g_initial_count++;
    }
}

static void parse_info(void *data, menu_item_t *item)
{
    (void) item;
    char **list = data;
    uint32_t i, words, at;

    g_info_name[0] = 0;
    g_info_brand[0] = 0;
    g_info_category[0] = 0;
    g_info_text[0] = 0;
    memset(g_info_ports, 0, sizeof g_info_ports);

    if (!list || !list[0] || !list[1] || atoi(list[1]) == -1) return;

    // ten fields before the description: name, brand, category and the six port counts
    for (i = 2; i < 12; i++)
        if (!list[i]) return;

    strncpy(g_info_name, list[2], PM_INFO_FIELD - 1);
    g_info_name[PM_INFO_FIELD - 1] = 0;
    plugin_map_unescape(g_info_name);

    strncpy(g_info_brand, list[3], PM_INFO_FIELD - 1);
    g_info_brand[PM_INFO_FIELD - 1] = 0;
    plugin_map_unescape(g_info_brand);

    strncpy(g_info_category, list[4], PM_INFO_FIELD - 1);
    g_info_category[PM_INFO_FIELD - 1] = 0;
    plugin_map_unescape(g_info_category);

    for (i = 0; i < 6; i++)
        g_info_ports[i] = (uint8_t) atoi(list[5 + i]);

    /*
     * One word to a token, so the description crosses the space-delimited protocol with
     * nothing escaped. Joined back with the spaces it was split on; the panel wraps it.
     */
    words = (uint32_t) atoi(list[11]);
    at = 0;

    for (i = 0; i < words; i++)
    {
        const char *word = list[12 + i];
        uint32_t length;

        if (!word) break;

        length = (uint32_t) strlen(word);
        if (at + length + 2 > PM_INFO_TEXT) break;

        if (at) g_info_text[at++] = ' ';
        memcpy(&g_info_text[at], word, length);
        at += length;
    }

    g_info_text[at] = 0;
}

static void request_info(void)
{
    uint8_t i;
    char buffer[32];
    memset(buffer, 0, sizeof buffer);

    ui_comm_webgui_set_response_cb(parse_info, NULL);
    ui_comm_webgui_clear_tx_buffer();

    i = copy_command((char *)buffer, CMD_BUILDER_CATALOG_INFO);
    i += int_to_str(g_category_hover, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str(g_plugin_hover, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str(g_filter, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = 0;

    ui_comm_webgui_send(buffer, i);
    ui_comm_webgui_wait_begin();
    wait_for_host();
}

static void request_initials(void)
{
    uint8_t i;
    char buffer[24];
    memset(buffer, 0, sizeof buffer);

    ui_comm_webgui_set_response_cb(parse_initials, NULL);
    ui_comm_webgui_clear_tx_buffer();

    i = copy_command((char *)buffer, CMD_BUILDER_CATALOG_INITIALS);
    i += int_to_str(g_category_hover, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str(g_filter, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = 0;

    ui_comm_webgui_send(buffer, i);
    ui_comm_webgui_wait_begin();
    wait_for_host();

    g_initials_loaded = 1;
}

static void request_categories(void)
{
    uint8_t i;
    char buffer[24];
    memset(buffer, 0, sizeof buffer);

    ui_comm_webgui_set_response_cb(parse_categories, NULL);
    ui_comm_webgui_clear_tx_buffer();

    i = copy_command((char *)buffer, CMD_BUILDER_CATALOG_CATEGORIES);
    i += int_to_str(g_filter, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = 0;

    ui_comm_webgui_send(buffer, i);
    ui_comm_webgui_wait_begin();
    wait_for_host();
}

static void request_catalog(uint16_t first)
{
    uint8_t i;
    char buffer[32];
    memset(buffer, 0, sizeof buffer);

    ui_comm_webgui_set_response_cb(parse_catalog, NULL);
    ui_comm_webgui_clear_tx_buffer();

    i = copy_command((char *)buffer, CMD_BUILDER_CATALOG_LIST);
    i += int_to_str(g_category_hover, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str(g_filter, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str((int32_t) first, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = 0;

    ui_comm_webgui_send(buffer, i);
    ui_comm_webgui_wait_begin();
    wait_for_host();
}

static void parse_added(void *data, menu_item_t *item)
{
    (void) item;
    char **list = data;

    g_added = BM_NONE;

    if (!list || !list[0] || !list[1] || atoi(list[1]) == -1) return;
    if (!list[2]) return;

    g_added = (int16_t) atoi(list[2]);
}

/*
 * The box the cursor was on when the list was opened. The server takes it as a hint: when
 * that box feeds exactly one other and the channels line up, the new plugin goes in
 * between the two and takes their cable over. BM_NONE asks for nothing of the sort,
 * and so does a case the server judges too tangled to guess at.
 */
static int16_t g_anchor = BM_NONE;

static int16_t request_add(void)
{
    uint8_t i;
    char buffer[40];
    memset(buffer, 0, sizeof buffer);

    g_added = BM_NONE;

    ui_comm_webgui_set_response_cb(parse_added, NULL);
    ui_comm_webgui_clear_tx_buffer();

    i = copy_command((char *)buffer, CMD_BUILDER_PLUGIN_ADD);
    i += int_to_str(g_category_hover, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str(g_plugin_hover, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str(g_filter, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';
    i += int_to_str(g_anchor, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = 0;

    ui_comm_webgui_send(buffer, i);
    ui_comm_webgui_wait_begin();
    wait_for_host();

    return g_added;
}

/*
 * Slides the window when the hover has walked off either end of it.
 */
static void follow_hover(void)
{
    if (g_plugin_total == 0) return;

    if (g_plugin_hover < (int16_t) g_plugin_first ||
        g_plugin_hover >= (int16_t) (g_plugin_first + g_plugin_count))
    {
        // land the hover in the middle of the new window, so a step either way stays in it
        int16_t first = g_plugin_hover - (PM_MAX_ROWS / 2);
        if (first < 0) first = 0;

        request_catalog((uint16_t) first);
    }
}

static void load_plugins(void)
{
    g_plugin_hover = 0;
    g_initials_loaded = 0;      // a different list has different letters
    request_catalog(0);
}


/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

void BM_plugin_manager_init(void)
{
    g_open = 0;
    g_filter = 0;
    g_category_count = 0;
    g_category_hover = 0;
    g_plugin_count = 0;
    g_plugin_hover = 0;
    g_initial_count = 0;
    g_initials_loaded = 0;
    g_held = 0;
    g_scrubbing = 0;
}

void BM_plugin_manager_open(int16_t anchor)
{
    g_open = 1;
    g_anchor = anchor;
    g_category_hover = 0;

    request_categories();
    load_plugins();
}

uint8_t BM_plugin_manager_is_open(void)
{
    return g_open;
}

void BM_plugin_manager_close(void)
{
    g_open = 0;
    g_info_open = 0;
    g_held = 0;
    g_scrubbing = 0;
}

/*
 * Holding alone is not a scrub, it is a press that has not been let go of yet. Scrubbing
 * starts on the first turn while down, so a long press with no turn is still a click and
 * still adds the plugin -- which is what most presses are.
 */
static void start_scrub(void)
{
    uint8_t i;

    if (!g_initials_loaded)
        request_initials();

    if (g_initial_count == 0) return;

    // start from the letter the current row is already under
    g_scrub_at = 0;
    for (i = 0; i < g_initial_count; i++)
    {
        if ((int16_t) g_initial_at[i] <= g_plugin_hover)
            g_scrub_at = i;
    }

    g_scrubbing = 1;
}

void BM_plugin_manager_hold(uint8_t encoder)
{
    if (g_info_open) return;

    if (!g_open || encoder != 1) return;

    g_held = 1;
}

void BM_plugin_manager_released(uint8_t encoder)
{
    (void) encoder;
    g_held = 0;
    g_scrubbing = 0;
}

uint8_t BM_plugin_manager_is_scrubbing(void)
{
    return g_scrubbing;
}

void BM_plugin_manager_turn(uint8_t encoder, int8_t step)
{
    // the lists are behind the overlay: nothing that would move them should reach them
    if (g_info_open) return;

    if (!g_open) return;

    if (encoder == 1 && g_held)
    {
        int16_t next;

        // the first turn while down is what turns a long press into a scrub
        if (!g_scrubbing)
        {
            start_scrub();
            if (!g_scrubbing) return;
        }

        next = g_scrub_at + step;
        if (next < 0 || next >= (int16_t) g_initial_count) return;

        g_scrub_at = next;
        return;
    }

    if (encoder == 0)
    {
        int16_t next = g_category_hover + step;

        if (next < 0 || next >= g_category_count) return;

        g_category_hover = next;
        load_plugins();
        return;
    }

    if (encoder == 1)
    {
        int16_t next = g_plugin_hover + step;

        if (next < 0 || next >= (int16_t) g_plugin_total) return;

        g_plugin_hover = next;
        follow_hover();
    }
}

void BM_plugin_manager_click(uint8_t encoder)
{
    if (!g_open || encoder != 1) return;

    // the overlay is on the same click, and the same click takes it away again
    if (g_info_open)
    {
        g_info_open = 0;
        return;
    }

    /*
     * A click arrives on release, and the driver cancels it for a held button but not for a
     * held encoder -- so the end of a scrub looks exactly like a click. Landing on the
     * letter is what the user asked for, not opening the info of whatever row happened to
     * be under it.
     */
    if (g_scrubbing)
    {
        g_scrubbing = 0;
        g_held = 0;
        g_plugin_hover = (int16_t) g_initial_at[g_scrub_at];
        follow_hover();
        return;
    }

    g_held = 0;

    BM_plugin_manager_info();
}

int16_t BM_plugin_manager_add(void)
{
    int16_t added;

    if (!g_open || g_info_open) return BM_NONE;
    if (g_plugin_total == 0) return BM_NONE;

    added = request_add();

    // the screen has done its job either way; a failed add just leaves the graph as it was
    g_open = 0;
    return added;
}

void BM_plugin_manager_filter(void)
{
    if (g_info_open) return;

    if (!g_open) return;

    // all -> audio -> midi -> cv
    if (g_filter == 0) g_filter = BM_AUDIO;
    else if (g_filter == BM_AUDIO) g_filter = BM_MIDI;
    else if (g_filter == BM_MIDI) g_filter = BM_CV;
    else g_filter = 0;

    g_category_hover = 0;
    request_categories();
    load_plugins();
}

void BM_plugin_manager_info(void)
{
    if (!g_open || g_info_open) return;
    if (g_plugin_count == 0) return;

    g_info_first = 0;
    request_info();

    // an answer that named nothing is nothing to put on the panel
    if (g_info_name[0]) g_info_open = 1;
}

uint8_t BM_plugin_manager_info_is_open(void)
{
    return g_info_open;
}

void BM_plugin_manager_info_close(void)
{
    g_info_open = 0;
}

void BM_plugin_manager_info_scroll(int8_t step)
{
    uint8_t total, last;

    if (!g_info_open) return;

    total = screen_plugin_info_lines(g_info_text);
    if (total <= SCREEN_INFO_LINES) return;

    // as far down as the last screenful, no further: a blank panel is not a scroll
    last = (uint8_t)(total - SCREEN_INFO_LINES);

    if (step < 0)
    {
        if (g_info_first) g_info_first--;
    }
    else if (g_info_first < last)
    {
        g_info_first++;
    }
}

void BM_plugin_manager_fill_info(plugin_info_t *model)
{
    model->name = g_info_name;
    model->brand = g_info_brand;
    model->category = g_info_category;
    model->ports = g_info_ports;
    model->comment = g_info_text;
    model->first_line = g_info_first;
}

void BM_plugin_manager_fill(plugin_manager_t *model)
{
    model->categories = g_category_rows;
    model->category_count = g_category_count;
    model->category_hover = g_category_hover;

    model->plugins = g_plugin_rows;
    model->plugin_count = g_plugin_count;
    // the list widget works in rows it holds, so the hover is relative to the window
    model->plugin_hover = g_plugin_hover - (int16_t) g_plugin_first;

    model->filter = (g_filter == BM_AUDIO) ? "AUDIO"
                  : (g_filter == BM_MIDI) ? "MIDI"
                  : (g_filter == BM_CV) ? "CV" : "ALL";

    if (g_scrubbing && g_scrub_at < (int16_t) g_initial_count)
    {
        g_scrub_text[0] = g_initial[g_scrub_at];
        g_scrub_text[1] = 0;
        model->scrub = g_scrub_text;
    }
    else
    {
        model->scrub = NULL;
    }
}
