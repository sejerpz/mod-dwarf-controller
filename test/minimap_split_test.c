/*
************************************************************************************************************************
*           Host-side check for the display list surviving the protocol tokenizer.
*
*           mode_builder.c does not get the display list as one string: protocol.c splits every
*           incoming message on spaces before the response callback sees it, and the display list
*           is full of spaces. parse_minimap() puts them back in place rather than copying 3.5kB
*           into a second buffer, which only works because strarr_split() writes its separators
*           over the original text and leaves the tokens contiguous.
*
*           This reads a display list on stdin, runs it through that split and rejoin, and checks
*           the text comes back byte for byte -- and that minimap_parse() then draws exactly the
*           same panel as it does from the untouched original.
*
*           strarr_split() and parse_quote() are transcribed from app/src/utils.c, the same way
*           test/minimap_host_test.c transcribes the ST7565P driver: the real ones pull in
*           FreeRTOS, screen.h and hardware.h and do not build on a host.
*
*           Build and run:
*               gcc -std=gnu99 -Wall -Wextra -Inxp-lpc -Iapp/inc -Idrivers/inc -Ifreertos/inc \
*                   -Imod-controller-proto -Inxp-lpc/CMSISv2p00_LPC177x_8xLib/inc \
*                   -Inxp-lpc/LPC177x_8xLib/inc \
*                   test/minimap_split_test.c app/src/minimap.c app/src/glcd_clip.c \
*                   -o /tmp/minimap_split
*               curl -s "http://localhost:8888/pedalboard/minimap" | /tmp/minimap_split
************************************************************************************************************************
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minimap.h"

#define BUFFER_SIZE 8192

// utils.h, pulled in through minimap.h -> glcd.h -> config.h, already turns this on


/*
************************************************************************************************************************
*           DRIVER STUBS
*
*           Nothing is drawn here: this test only compares what minimap_parse() built, so the
*           primitives glcd_clip.c calls just need to link.
************************************************************************************************************************
*/

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
    (void)disp; (void)x; (void)y; (void)width; (void)height;
}


/*
************************************************************************************************************************
*           UTILS TRANSCRIPTION (app/src/utils.c, MALLOC -> malloc)
************************************************************************************************************************
*/

static void test_parse_quote(char *str)
{
    char *pquote, *pstr = str;

    while (*pstr)
    {
        if (*pstr == '"')
        {
            // shift the string to left
            pquote = pstr;
            while (*pquote)
            {
                *pquote = *(pquote+1);
                pquote++;
            }
        }
        else pstr++;
    }
}

char** strarr_split(char *str, const char token)
{
    uint32_t count;
    char *pstr, **list = NULL;
    uint8_t quote = 0;

    if (!str) return list;

    // count the tokens
    pstr = str;
    count = 1;
    while (*pstr)
    {
        if (*pstr == token && quote == 0)
        {
            count++;
        }
#ifdef ENABLE_QUOTATION_MARKS
        if (*pstr == '"')
        {
            if (quote == 0) quote = 1;
            else
            {
                if (*(pstr+1) == '"') pstr++;
                else quote = 0;
            }
        }
#endif
        pstr++;
    }

    // allocates memory to list
    list = malloc((count + 1) * sizeof(char *));
    if (!list) return NULL;

    // fill the list pointers
    pstr = str;
    list[0] = pstr;
    count = 0;
    while (*pstr)
    {
        if (*pstr == token && quote == 0)
        {
            *pstr = '\0';
            list[++count] = pstr + 1;
        }
#ifdef ENABLE_QUOTATION_MARKS
        if (*pstr == '"')
        {
            if (quote == 0) quote = 1;
            else
            {
                if (*(pstr+1) == '"') pstr++;
                else quote = 0;
            }
        }
#endif
        pstr++;
    }

    list[++count] = NULL;

#ifdef ENABLE_QUOTATION_MARKS
    count = 0;
    while (list[count]) test_parse_quote(list[count++]);
#endif

    return list;
}


/*
************************************************************************************************************************
*           TEST
************************************************************************************************************************
*/

// the loop from mode_builder.c parse_minimap(), kept identical on purpose
static void rejoin(char **list, uint32_t first)
{
    uint32_t i;

    for (i = first; list[i + 1] != NULL; i++)
        list[i][strlen(list[i])] = ' ';
}

static int compare_parse(const char *original, const char *rejoined)
{
    static minimap_t a, b;

    minimap_init(&a);
    minimap_init(&b);

    if (minimap_parse(&a, original) != minimap_parse(&b, rejoined))
    {
        fprintf(stderr, "FAIL: one text parses and the other does not\n");
        return 1;
    }

    if (a.n_nodes != b.n_nodes || a.n_ports != b.n_ports || a.n_edges != b.n_edges)
    {
        fprintf(stderr, "FAIL: scene differs -- nodes %u/%u ports %u/%u edges %u/%u\n",
                a.n_nodes, b.n_nodes, a.n_ports, b.n_ports, a.n_edges, b.n_edges);
        return 1;
    }

    if (memcmp(&a, &b, sizeof(minimap_t)) != 0)
    {
        fprintf(stderr, "FAIL: parsed scenes are not identical\n");
        return 1;
    }

    return 0;
}

int main(void)
{
    static char text[BUFFER_SIZE];
    static char original[BUFFER_SIZE];
    char **list;
    size_t length;
    uint32_t first = 0, tokens = 0;

    length = fread(text, 1, sizeof(text) - 1, stdin);
    text[length] = 0;
    memcpy(original, text, length + 1);

    list = strarr_split(text, ' ');
    if (!list)
    {
        fprintf(stderr, "FAIL: split returned nothing for %lu bytes\n", (unsigned long)length);
        return 1;
    }

    // a real response reads "r 1 <display list>"; a bare display list starts at token 0
    if (list[0] && strcmp(list[0], "r") == 0 && list[1]) first = 2;

    if (!list[first])
    {
        fprintf(stderr, "FAIL: nothing left after the response header\n");
        return 1;
    }

    rejoin(list, first);

    if (strcmp(list[first], original + (list[first] - text)) != 0)
    {
        fprintf(stderr, "FAIL: rejoined text differs from the original\n");
        return 1;
    }

    if (compare_parse(original + (list[first] - text), list[first]) != 0)
        return 1;

    while (list[tokens]) tokens++;
    printf("ok %lu bytes, %u tokens\n", (unsigned long)length, (unsigned)tokens);
    return 0;
}
