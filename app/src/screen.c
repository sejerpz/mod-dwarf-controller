
/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include "screen.h"
#include "utils.h"
#include "glcd.h"
#include "glcd_widget.h"
#include "naveg.h"
#include "hardware.h"
#include "images.h"
#include "protocol.h"
#include "mode_tools.h"
#include "mode_navigation.h"
#include <string.h>
#include <stdio.h>

/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/

//make the menu boxes rectangle or cut a pixel
#define RECT_MENU_BOXES

/*
************************************************************************************************************************
*           LOCAL CONSTANTS
************************************************************************************************************************
*/

/*
************************************************************************************************************************
*           LOCAL DATA TYPES
************************************************************************************************************************
*/

/*
************************************************************************************************************************
*           LOCAL MACROS
************************************************************************************************************************
*/

/*
************************************************************************************************************************
*           LOCAL GLOBAL VARIABLES
************************************************************************************************************************
*/

static tuner_t g_tuner = {0, NULL, 0, TUNER_REFERENCE_FREQ_DEFAULT - TUNER_REFERENCE_FREQ_MIN, 1};
static bool g_hide_non_assigned_actuators = 0;
static bool g_control_mode_header = 0;
static bool g_foots_grouped = 0;

extern int8_t g_tuner_input;
extern int8_t g_tuner_reference_freq;

/*
************************************************************************************************************************
*           LOCAL FUNCTION PROTOTYPES
************************************************************************************************************************
*/

/*
************************************************************************************************************************
*           LOCAL CONFIGURATION ERRORS
************************************************************************************************************************
*/

/*
************************************************************************************************************************
*           LOCAL FUNCTIONS
************************************************************************************************************************
*/


/*
* Print the 3 boxes for the menu items
 */
void print_menu_boxes(void)
{
    glcd_t *display = hardware_glcds(0);

    #ifdef RECT_MENU_BOXES

    glcd_rect(display, 14, DISPLAY_HEIGHT - 9, 31, 9, GLCD_BLACK);
    glcd_rect(display, 48, DISPLAY_HEIGHT - 9, 31, 9, GLCD_BLACK);
    glcd_rect(display, 82, DISPLAY_HEIGHT - 9, 31, 9, GLCD_BLACK);

#else

    //box 1
    glcd_hline(display, 15, DISPLAY_HEIGHT-9, 29, GLCD_BLACK);
    glcd_hline(display, 15, DISPLAY_HEIGHT-1, 29, GLCD_BLACK);
    glcd_vline(display, 14, DISPLAY_HEIGHT-8, 7, GLCD_BLACK);
    glcd_vline(display, 44, DISPLAY_HEIGHT-8, 7, GLCD_BLACK);

    //box 2
    glcd_hline(display, 49, DISPLAY_HEIGHT-9, 29, GLCD_BLACK);
    glcd_hline(display, 49, DISPLAY_HEIGHT-1, 29, GLCD_BLACK);
    glcd_vline(display, 48, DISPLAY_HEIGHT-8, 7, GLCD_BLACK);
    glcd_vline(display, 78, DISPLAY_HEIGHT-8, 7, GLCD_BLACK);

    //box 3
    glcd_hline(display, 83, DISPLAY_HEIGHT-9, 29, GLCD_BLACK);
    glcd_hline(display, 83, DISPLAY_HEIGHT-1, 29, GLCD_BLACK);
    glcd_vline(display, 82, DISPLAY_HEIGHT-8, 7, GLCD_BLACK);
    glcd_vline(display, 112, DISPLAY_HEIGHT-8, 7, GLCD_BLACK);

#endif
}

/*
 * Print the outlines of the menu
 * and a box around the screen
 */
 
void print_menu_outlines(void)
{
    glcd_t *display = hardware_glcds(0);
    glcd_vline(display, 0, 7, DISPLAY_HEIGHT - 11, GLCD_BLACK);
    glcd_vline(display, DISPLAY_WIDTH-1, 7, DISPLAY_HEIGHT - 11, GLCD_BLACK);
    glcd_hline(display, 0, DISPLAY_HEIGHT - 5, 14, GLCD_BLACK);
    glcd_hline(display, 45, DISPLAY_HEIGHT - 5, 3, GLCD_BLACK);
    glcd_hline(display, 79, DISPLAY_HEIGHT - 5, 3, GLCD_BLACK);
    glcd_hline(display, 112, DISPLAY_HEIGHT - 5, 15, GLCD_BLACK);


    print_menu_boxes();
}

void print_tripple_menu_items(menu_item_t *item_child, uint8_t knob, uint8_t tool_mode)
{
    glcd_t *display = hardware_glcds(0);

    //fist decide posistion
    uint8_t item_x;
    uint8_t item_y = 15;

    switch(knob)
    {
        case 0:
            item_x = 5;
        break;

        case 1:
            item_x = 47;
        break;

        case 2:
            item_x = 90;
        break;

        default:
            return;
        break;
    }

    //print the title
    char first_line[10] = {};
    char second_line[10] = {};

    uint8_t q, p = 0, line = 0;
    for (q = 0; q < 21; q++)
    {
        if (item_child->desc->name[q] != 0)
        {
            if (line)
            {
                second_line[p] = item_child->desc->name[q];
                p++;
            }
            else
            {
                if (item_child->desc->name[q] ==  ' ')
                {
                    first_line[q] = 0;
                    line = 1;
                }
                else
                    first_line[q] = item_child->desc->name[q];
            }
        }
        else
            break;
    }
    second_line[p] = 0;

    //check if we have 2 lines
    if (second_line[0] != 0)
    {
        glcd_text(display, (item_x + 17 - 2*strlen(first_line)), item_y, first_line, Terminal3x5, GLCD_BLACK);
        glcd_text(display, (item_x + 17 - 2*strlen(second_line)), item_y + 6, second_line, Terminal3x5, GLCD_BLACK);      
    }
    else
    {
        glcd_text(display, (item_x + 17 - 2*strlen(first_line)), item_y+3, first_line, Terminal3x5, GLCD_BLACK);
    }

    switch(item_child->desc->type)
    {
        case MENU_FOOT:
        case MENU_TOGGLE:
        {
            if (tool_mode)
                glcd_vline(display, item_x+16, item_y+13, 8, GLCD_BLACK_WHITE);
            toggle_t menu_toggle = {};
            menu_toggle.x = item_x;
            menu_toggle.y = item_y+23 - (tool_mode?15:0);
            menu_toggle.color = GLCD_BLACK;
            menu_toggle.width = 35;
            menu_toggle.height = 11;
            menu_toggle.value = item_child->data.value;
            widget_toggle(display, &menu_toggle);
        }
        break;

        case MENU_BAR:
        {
            //print the bar
            menu_bar_t bar = {};
            bar.x = item_x;
            bar.y = item_y + 12 - (tool_mode?5:0);
            bar.color = GLCD_BLACK;
            bar.width = 35;
            bar.height = 7;
            bar.min = item_child->data.min;
            bar.max = item_child->data.max;
            bar.value = item_child->data.value;
            widget_bar(display, &bar);
                
            if (item_child->data.unit_text)
            {
                char bfr_upper_line[7] = {};
                char bfr_lower_line[7] = {};

                uint8_t t, r = 0, enter = 0;
                for (t = 0; t < 15; t++) {
                    if (item_child->data.unit_text[t] != 0) {
                        if (enter) {
                            bfr_lower_line[r] = item_child->data.unit_text[t];
                            r++;
                        }
                        else {
                            if (item_child->data.unit_text[t] ==  ' ') {
                                bfr_upper_line[t] = 0;
                                enter = 1;
                            }
                            else
                                bfr_upper_line[t] = item_child->data.unit_text[t];
                        }
                    }
                    else
                        break;
                }
                bfr_lower_line[r] = 0;

                //check if we have 2 lines
                if (bfr_lower_line[0] != 0) {
                    //we dont have this in tool mode, so join the strings
                    if (tool_mode) {
                        char str_bfr[14] = {};
                        strcat(str_bfr, bfr_upper_line);
                        strcat(str_bfr, " ");
                        strcat(str_bfr, bfr_lower_line);
                        glcd_text(display, (item_x + 19 - 2*strlen(str_bfr)), item_y+22, str_bfr, Terminal3x5, GLCD_BLACK);
                    }
                    else {
                        glcd_text(display, (item_x + 19 - 2*strlen(bfr_upper_line)), item_y+(tool_mode?22:27), bfr_upper_line, Terminal3x5, GLCD_BLACK);
                        glcd_text(display, (item_x + 19 - 2*strlen(bfr_lower_line)), item_y+(tool_mode?22:33), bfr_lower_line, Terminal3x5, GLCD_BLACK);
                    }
                }
                else {
                    glcd_text(display, (item_x + 19 - 2*strlen(bfr_upper_line)), item_y+(tool_mode?22:30), bfr_upper_line, Terminal3x5, GLCD_BLACK);
                }

                print_menu_outlines();
            }
        }
        break;

        case MENU_CLICK_LIST:
        case MENU_LIST:
            if (item_child->data.unit_text)
            {
                //print the value
                char first_val_line[10] = {};
                char second_val_line[10] = {};

                p = 0;
                uint8_t val_line = 0;
                
                for (q = 0; q < 21; q++)
                {
                    if(item_child->data.unit_text[q] != 0)
                    {
                        if (val_line)
                        {
                            second_val_line[p] = item_child->data.unit_text[q];
                            p++;                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       
                        }
                        else
                        {
                            if (item_child->data.unit_text[q] == ' ')
                            {
                                first_val_line[q] = 0;
                                val_line = 1;                                
                            }
                            else
                                first_val_line[q] = item_child->data.unit_text[q];
                        }
                    }
                    else
                        break;
                }

                //check if we have 2 lines
                second_val_line[p] = 0;
                if (p != 0)
                {
                    glcd_text(display, (item_x + 17 - 2*strlen(first_val_line)), item_y+(tool_mode?17:24), first_val_line, Terminal3x5, GLCD_BLACK);
                    glcd_text(display, (item_x + 17 - 2*strlen(second_val_line)), item_y+(tool_mode?24:31), second_val_line, Terminal3x5, GLCD_BLACK);
                }
                else
                {
                    glcd_text(display, (item_x + 17 - 2*strlen(first_val_line)), item_y+(tool_mode?19:26), first_val_line, Terminal3x5, GLCD_BLACK);
                }

                //display 'click msg'
                if ((item_child->desc->type == MENU_CLICK_LIST) && ((int)item_child->data.value != item_child->data.selected))
                {
                    //text
                    glcd_text(display, item_x + 7, item_y+16, "CLICK", Terminal3x5, GLCD_BLACK);

                    //boxes
                    glcd_rect(display, item_x + 5, item_y+14, 24, 9, GLCD_BLACK);
                    glcd_rect(display, item_x-2, item_y+14, 37, 24, GLCD_BLACK);

                    //incert area
                    glcd_rect_invert(display, item_x-3, item_y+13, 39, 26);
                }
                else
                    glcd_vline(display, item_x+16, item_y+13, tool_mode?4:10, GLCD_BLACK_WHITE);
            }
        break;

        //others, dont use
        //TODO check if remove? most come from MDX codebase
        case MENU_MAIN:
        case MENU_ROOT:
        case MENU_CONFIRM2:
        case MENU_OK:
        case MENU_NONE:
        case MENU_CONFIRM:
        case MENU_TOOL:
        break;
    }
}

/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

void screen_clear(void)
{
    glcd_clear(hardware_glcds(0), GLCD_WHITE);
}

void screen_force_update(void)
{
    glcd_update(hardware_glcds(0));
}

void screen_set_hide_non_assigned_actuators(uint8_t hide)
{
    g_hide_non_assigned_actuators = hide;
}

void screen_set_control_mode_header(uint8_t toggle)
{
    g_control_mode_header = toggle;
}

void screen_group_foots(uint8_t toggle)
{
    g_foots_grouped = toggle;
}

void screen_encoder(const control_t *control, uint8_t encoder)
{    
    static char buffer_8[8 + 1];
    static char *labels_list[64];
    glcd_t *display = hardware_glcds(0);

    //fist decide posistion
    uint8_t encoder_x;
    uint8_t encoder_y = 16;
    switch(encoder)
    {
        case 0:
            encoder_x = 4;
        break;

        case 1:
            encoder_x = 47;
        break;

        case 2:
            encoder_x = 90;
        break;

        default:
            return;
        break;
    }

    //clear the designated area
    glcd_rect_fill(display, encoder_x, encoder_y, 36, 27, GLCD_WHITE);

    //check what control type we need

    //no control
    if (!control)
    {
        char text[sizeof(SCREEN_ROTARY_DEFAULT_NAME) + 2];
        uint8_t item_x = encoder_x+5;
        if (g_hide_non_assigned_actuators)
        {
            text[0] = '-';
            text[1] = 0;
            item_x+=10;
        }
        else
        {
            strcpy(text, SCREEN_ROTARY_DEFAULT_NAME);
            text[sizeof(SCREEN_ROTARY_DEFAULT_NAME)-1] = encoder + '1';
            text[sizeof(SCREEN_ROTARY_DEFAULT_NAME)] = 0;
        }

        glcd_text(display, item_x, encoder_y+10, text, Terminal3x5, GLCD_BLACK);
        return;
    }

    //draw the title
    uint8_t char_cnt_name = strlen(control->label);

    if (char_cnt_name > 8)
    {
        char_cnt_name = 8;
    }

    strncpy(buffer_8, control->label, 8);
    buffer_8[MIN(char_cnt_name, 8)] = '\0';

    //allign to middle, (full width / 2) - (text width / 2)
    glcd_text(display, (encoder_x + 18 - 2*char_cnt_name), encoder_y, buffer_8, Terminal3x5, GLCD_BLACK);
    // list type control
    if (control->properties & (FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS))
    {
        uint8_t scalepoint_count_local = control->scale_points_count > 64 ? 64 : control->scale_points_count;

        uint8_t i;
        for (i = 0; i < scalepoint_count_local; i++)
        {
            labels_list[i] = control->scale_points[i]->label;
        }

        listbox_t list;
        list.x = encoder_x;
        list.y = encoder_y + 7;
        list.width = 36;
        list.height = 19;
        list.color = GLCD_BLACK;
        list.font = Terminal3x5;
        list.font_highlight = Terminal5x7;
        list.selected = control->step;
        list.count = scalepoint_count_local;
        list.list = labels_list;
        list.line_space = 1;
        list.line_top_margin = 1;
        list.line_bottom_margin = 1;
        list.text_left_margin = 1;
        widget_list_value(display, &list);
    }
    else if ((control->properties & FLAG_CONTROL_TRIGGER) && (floats_are_equal(control->screen_indicator_widget_val, -1.f)))
    {
        toggle_t toggle;
        toggle.x = encoder_x;
        toggle.y = encoder_y + 6;
        toggle.width = 35;
        toggle.height = 18;
        toggle.color = GLCD_BLACK;
        //we use 2 and 3 to indicate a trigger in the widget
        toggle.value = float_is_not_zero(control->value) ? 3 : 2;
        toggle.inner_border = 1;
        widget_toggle(display, &toggle);
    }
    else if ((control->properties & (FLAG_CONTROL_TOGGLED | FLAG_CONTROL_BYPASS)) && (floats_are_equal(control->screen_indicator_widget_val, -1.f)))
    {
        toggle_t toggle;
        toggle.x = encoder_x;
        toggle.y = encoder_y + 6;
        toggle.width = 35;
        toggle.height = 18;
        toggle.color = GLCD_BLACK;
        // correct here for the inverted bypass value
        toggle.value = control->properties & FLAG_CONTROL_BYPASS ? 1.f - control->value : control->value;
        toggle.inner_border = 1;
        widget_toggle(display, &toggle);
    }
    //linear / log / int
    else
    {
        bar_t bar;
        bar.x = encoder_x;
        bar.y = encoder_y;
        bar.width = 35;
        bar.height = 6;
        bar.color = GLCD_BLACK;
        if (floats_are_equal(control->screen_indicator_widget_val, -1.f)) {
            bar.step = control->step;
            bar.steps = control->steps - 1;
        }
        else {
            bar.step = control->screen_indicator_widget_val * 100;
            bar.steps = 100;
        }

        if (!control->value_string)
        {
            char str_bfr[15] = {0};

            if ((control->properties == FLAG_CONTROL_INTEGER) || (control->value > 999.9f) || (control->value < -999.9f))
                int_to_str(control->value, str_bfr, sizeof(str_bfr), 0);
            else if ((control->value > 99.99f) || (control->value < -99.99f))
                float_to_str((control->value), str_bfr, sizeof(str_bfr), 1);
            else if ((control->value > 9.99f) || (control->value < -9.99f))
                float_to_str((control->value), str_bfr, sizeof(str_bfr), 2);
            else
                float_to_str((control->value), str_bfr, sizeof(str_bfr), 3);

            str_bfr[14] = 0;

            bar.value = str_bfr;

            widget_bar_encoder(display, &bar);
        }
        else
        {
            //draw the value string
            uint8_t char_cnt_value = strlen(control->value_string);

            if (char_cnt_value > 8)
                char_cnt_value = 8;

            strncpy(buffer_8, control->value_string, 8);
            buffer_8[MIN(char_cnt_value, 8)] = '\0';
            bar.value = buffer_8;

            widget_bar_encoder(display, &bar);
        }

        //check what to do with the unit
        if (strcmp("", control->unit) != 0)
        {
            uint8_t char_cnt_unit = strlen(control->unit);

            if (char_cnt_unit > 7)
            {
                //limit string
                char_cnt_unit = 7;
            }

            strncpy(buffer_8, control->unit, 8);
            buffer_8[MIN(char_cnt_unit, 8)] = '\0';

            glcd_text(display, (encoder_x + 18 - 2*char_cnt_unit), encoder_y + 12 + 7, buffer_8, Terminal3x5, GLCD_BLACK);
            return;
        }
    }
}

void screen_page_index(uint8_t current, uint8_t available)
{
    //precent widget from tripin
    if (current > available)
        current = available;

    char str_current[4];
    char str_available[4];
    int_to_str((current+1), str_current, sizeof(str_current), 1);
    str_current[1] = '/';
    int_to_str((available), str_available, sizeof(str_available), 1);
    str_current[2] = str_available[0];
    str_current[3] = 0;

    glcd_t *display = hardware_glcds(0);

    //clear the part
    glcd_rect_fill(display, 0, 51, 24, 13, GLCD_WHITE);

    //draw the square
    glcd_hline(display, 0, 51, 21, GLCD_BLACK);
    glcd_vline(display, 0, 51, 13, GLCD_BLACK);
    glcd_vline(display, 21, 51, 13, GLCD_BLACK);

    //draw the indicator
    //we have 19 pixels available and possible 8 pages
    uint8_t amount_of_pixel_per_page = (int)20/available;
    uint8_t remaining_pixels = 20%available;

    uint8_t i, page_bar_x = 1;;
    for (i = 0; i < current; i++)
    {
        page_bar_x += amount_of_pixel_per_page;

        //add a remainder pixel
        if (remaining_pixels > i)
           page_bar_x++; 
    }

    glcd_rect_fill(display, page_bar_x, 52, (remaining_pixels > i) ? amount_of_pixel_per_page+1:amount_of_pixel_per_page, 2, GLCD_BLACK);

    //draw devision line
    glcd_hline(display, 0, 54, 21, GLCD_BLACK);

    // draws the text field
    glcd_text(display, 3, 56, str_current, Terminal5x7, GLCD_BLACK);
}

static void screen_encoder_containers(uint8_t mode, uint8_t current_page, uint8_t page_count)
{
    glcd_t *display = hardware_glcds(0);

    //clear the part
    glcd_rect_fill(display, 0, 12, DISPLAY_WIDTH, 38, GLCD_WHITE);
    //clear the part of the small boxes below
    glcd_rect_fill(display, 31, 46, 66, 5, GLCD_WHITE);

    //draw the 3 main lines 
    glcd_hline(display, 0, 13, DISPLAY_WIDTH, GLCD_BLACK);
    glcd_vline(display, 0, 13, 34, GLCD_BLACK);
    glcd_vline(display, DISPLAY_WIDTH - 1, 13, 34, GLCD_BLACK);
  
    //draw the bottom lines
    glcd_hline(display, 0,  47, 31, GLCD_BLACK);
    glcd_hline(display, 97, 47, 31, GLCD_BLACK);

    if (mode == 1)
    {
        char buffer[10];
        int len;

        if (page_count > 0)
        {
            len = snprintf(buffer, 10, "%d / %d", current_page + 1, page_count);
        }
        else
        {
            len = 8;
            strcpy(buffer, "no pages");
        }

        if (len < 0)
        {
            // somethig went wrong with snprintf
            strcpy(buffer, "err");
        }
      
        glcd_rect(display, 31, 43, 66, 9, GLCD_BLACK);
        // current page indicator
        glcd_text(display, (DISPLAY_WIDTH / 2) - (3 * len) + 7, 45, buffer, Terminal3x5, GLCD_BLACK);
    }
    else
    {
        //draw the 3 boxes
        glcd_rect(display, 31, 43, 21, 9, GLCD_BLACK);
        glcd_rect(display, 54, 43, 21, 9, GLCD_BLACK);
        glcd_rect(display, 77, 43, 21, 9, GLCD_BLACK);

        //draw the devision lines between boxes
        glcd_hline(display, 52, 47, 2, GLCD_BLACK);
        glcd_hline(display, 75, 47, 2, GLCD_BLACK);

        // three subpages mode
        //indicator 1
        glcd_text(display, 40, 45, "I", Terminal3x5, GLCD_BLACK);

        //indicator 2
        glcd_text(display, 61, 45, "II", Terminal3x5, GLCD_BLACK);

        //indicator 3
        glcd_text(display, 82, 45, "III", Terminal3x5, GLCD_BLACK);

        //invert the current one
        switch (current_page)
        {
            case 0:
                glcd_rect_invert(display, 32, 44, 19, 7);
            break;

            case 1:
                glcd_rect_invert(display, 55, 44, 19, 7);
            break;

            case 2:
                glcd_rect_invert(display, 78, 44, 19, 7);;
            break;

            default:
                glcd_rect_invert(display, 32, 44, 19, 7);
            break;
        }
    }
}

/*
 * print the 3 encoder containers
 * with the standard 3 subpages button and the current page highlighted
 */ 
void screen_encoder_container(uint8_t current_encoder_page)
{
    screen_encoder_containers(0, current_encoder_page, 3);
}

/*
 * print the 3 encoder containers
 * with the scroll button and the current page highlighted
 * 
 * used on the builder for plugin parameters
 */
void screen_encoder_container_paged(uint8_t current_page, uint8_t page_count)
{
    screen_encoder_containers(1, current_page, page_count);
}

void screen_footer(uint8_t foot_id, const char *name, const char *value, int16_t property)
{
    static char buffer_16[16];
    glcd_t *display = hardware_glcds(0);

    uint8_t foot_y = 54;
    uint8_t foot_x;
    switch(foot_id)
    {
        case 0:
            foot_x = 24;
        break;

        case 1:
            foot_x = 77;
        break;

        default:
            foot_x = 24;
        break;
    }

    if (g_foots_grouped && (naveg_get_current_mode() == MODE_CONTROL))
    {
        glcd_rect_fill(display, 24, foot_y, 102, 10, GLCD_WHITE);
        glcd_hline(display, 24, foot_y, 104, GLCD_BLACK);
        glcd_vline(display, 24, foot_y, 10, GLCD_BLACK);
    }
    else
    {
        // clear the footer area
        if (foot_id == 0)
            glcd_rect_fill(display, foot_x, foot_y, 53, 10, GLCD_WHITE);
        else
            glcd_rect_fill(display, foot_x, foot_y, 50, 10, GLCD_WHITE);

        //draw the footer box
        glcd_hline(display, foot_x, foot_y, 50, GLCD_BLACK);
        glcd_vline(display, foot_x, foot_y, 10, GLCD_BLACK);
        glcd_vline(display, foot_x+50, foot_y, 10, GLCD_BLACK);
    }

    if (name == NULL || value == NULL)
    {
        char text[sizeof(SCREEN_FOOT_DEFAULT_NAME) + 2];

        if (g_hide_non_assigned_actuators)
        {
            text[0] = '-';
            text[1] = 0;
        }
        else
        {
            strcpy(text, SCREEN_FOOT_DEFAULT_NAME);
            if (foot_id == 0)
                text[sizeof(SCREEN_FOOT_DEFAULT_NAME)-1] = 'B';
            else 
                text[sizeof(SCREEN_FOOT_DEFAULT_NAME)-1] = 'C';
            text[sizeof(SCREEN_FOOT_DEFAULT_NAME)] = 0;
        }

        glcd_text(display, foot_x + (26 - (strlen(text) * 3)), foot_y + 2, text, Terminal5x7, GLCD_BLACK);
        return;
    }

    //if we are in toggle, trigger or byoass mode we dont have a value
    else if ((property & FLAG_CONTROL_TOGGLED) || (property & FLAG_CONTROL_BYPASS) || (property & FLAG_CONTROL_TRIGGER) || (property & FLAG_CONTROL_MOMENTARY))
    {
        uint8_t char_cnt_name = strlen(name);
        if (char_cnt_name > 7)
        {
            //limit string
            char_cnt_name = 7;
        }

        strncpy(buffer_16, name, 15);
        buffer_16[MIN(char_cnt_name, 15)] = '\0';

        glcd_text(display, foot_x + (26 - (strlen(buffer_16) * 3)), foot_y + 2, buffer_16, Terminal5x7, GLCD_BLACK);
    
        if (value[1] == 'N')
            glcd_rect_invert(display, foot_x + 1, foot_y + 1, 49, 9);

        if ((property & FLAG_CONTROL_BYPASS) && (property & FLAG_CONTROL_MOMENTARY))
            glcd_rect_invert(display, foot_x + 1, foot_y + 1, 49, 9);

    }
    else
    {
        uint8_t char_cnt_name = strlen(name);
        uint8_t char_cnt_value = strlen(value);

        if (g_foots_grouped && (naveg_get_current_mode() == MODE_CONTROL))
        {
            //limit the strings for the screen properly
            if ((char_cnt_value + char_cnt_name) > 14) {
                //both bigger then the limmit
                if ((char_cnt_value > 7) && (char_cnt_name > 7)) {
                    char_cnt_name = 7;
                    char_cnt_value = 7;
                }
                else if (char_cnt_value > 7) {
                    if ((14 - char_cnt_name) < char_cnt_value)
                        char_cnt_value = 14 - char_cnt_name;
                }
                else if (char_cnt_name > 7) {
                    if ((14 - char_cnt_value) < char_cnt_name)
                        char_cnt_name = 14 - char_cnt_value;
                }
            }

            memset(buffer_16, 0, 16);

            strncpy(buffer_16, name, MIN(char_cnt_name, 16));
            strncat(buffer_16, ":", 15);
            strncat(buffer_16, value, 15);
            buffer_16[MIN(char_cnt_name + char_cnt_value + 1, 15)] = '\0';
            glcd_text(display, 26, foot_y + 2, buffer_16, Terminal5x7, GLCD_BLACK);

            //group icon
            icon_footswitch_groups(display, DISPLAY_WIDTH-12, foot_y+1);

        }
        else
        {
            if ((char_cnt_value + char_cnt_name) > 7) {
                //both bigger then the limmit
                if ((char_cnt_value > 4) && (char_cnt_name > 3)) {
                    char_cnt_name = 3;
                    char_cnt_value = 4;
                }
                else if (char_cnt_value > 4) {
                    char_cnt_value = 7 - char_cnt_name;
                }
                else if (char_cnt_name > 3) {
                    char_cnt_name = 7 - char_cnt_value;
                }
            }

            //draw name
            strncpy(buffer_16, name, 16);
            buffer_16[char_cnt_name] = '\0';
            glcd_text(display, foot_x + 2, foot_y + 2, buffer_16, Terminal5x7, GLCD_BLACK);

            // draws the value field
            strncpy(buffer_16, value, 16);
            buffer_16[char_cnt_value] = '\0';
            glcd_text(display, foot_x + (50 - ((strlen(buffer_16)) * 6)), foot_y + 2,buffer_16, Terminal5x7, GLCD_BLACK);
        }
    }
    
}

void screen_tittle(int8_t pb_ss)
{
    glcd_t *display = hardware_glcds(0);

    if (pb_ss == -1)
        pb_ss = g_control_mode_header;

    //we dont display inside a menu
    if (naveg_get_current_mode() != MODE_CONTROL && naveg_get_current_mode() != MODE_BUILDER) return;

    //we dont display a not selected mode
    if (g_control_mode_header != pb_ss) return;

    char* name = NM_get_pbss_name(pb_ss);
    uint8_t name_len = strlen(name);

    // clear the name area
    glcd_rect_fill(display, 0, 0, DISPLAY_WIDTH, 9, GLCD_WHITE);

    glcd_text(display, ((DISPLAY_WIDTH / 2) - (3*name_len) + 7), 1, name, Terminal5x7, GLCD_BLACK);

    if (pb_ss)
        icon_snapshot(display, ((DISPLAY_WIDTH / 2) - (3*name_len) + 7 ) - 11, 1);
    else
        icon_pedalboard(display, ((DISPLAY_WIDTH / 2) - (3*name_len) + 7) - 11, 1);

    icon_custom_firmware(display, DISPLAY_WIDTH - 10, 1);
    //invert the top bar
    glcd_rect_invert(display, 0, 0, DISPLAY_WIDTH, 9);
}

void screen_bank_list(bp_list_t *list, const char *name)
{
    listbox_t list_box = {};

    glcd_t *display = hardware_glcds(0);
    uint8_t type = NM_get_current_list();
    screen_clear();

    //print outlines
    print_menu_outlines();

    //print the 3 buttons
    switch (type) {
        case BANKS_LIST:
            glcd_text(display, 16, DISPLAY_HEIGHT - 7, "ENTER >", Terminal3x5, GLCD_BLACK);
            //draw the second box
            glcd_text(display, 58, DISPLAY_HEIGHT - 7, "NEW", Terminal3x5, GLCD_BLACK);
            //draw the third box, new bank
            if (NM_get_current_bp_flag(BANKS_LIST))
                glcd_text(display, 96, DISPLAY_HEIGHT - 7, "-", Terminal3x5, GLCD_BLACK);
            else
                glcd_text(display, 86, DISPLAY_HEIGHT - 7, "DELETE", Terminal3x5, GLCD_BLACK);

            list_box.selected_ids = NULL;
        break;

        case BANK_LIST_CHECKBOXES:
            if (NM_get_current_bp_flag(BANKS_LIST))
                glcd_text(display, 28, DISPLAY_HEIGHT - 7, "-", Terminal3x5, GLCD_BLACK);
            else
                glcd_text(display, 16, DISPLAY_HEIGHT - 7, "ENTER >", Terminal3x5, GLCD_BLACK);

            if (NM_get_current_bp_flag(BANKS_LIST))
                glcd_text(display, 62, DISPLAY_HEIGHT - 7, "-", Terminal3x5, GLCD_BLACK);
            else
                glcd_text(display, 52, DISPLAY_HEIGHT - 7, "SELECT", Terminal3x5, GLCD_BLACK);

            glcd_text(display, 86, DISPLAY_HEIGHT - 7, "CANCEL", Terminal3x5, GLCD_BLACK);
            list_box.selected_ids = NULL;
        break;

        case PB_LIST_CHECKBOXES:
            glcd_text(display, 18, DISPLAY_HEIGHT - 7, "< BACK", Terminal3x5, GLCD_BLACK);

            if (list->menu_max == 0)
                glcd_text(display, 62, DISPLAY_HEIGHT - 7, "-", Terminal3x5, GLCD_BLACK);
            else
                glcd_text(display, 52, DISPLAY_HEIGHT - 7, "SELECT", Terminal3x5, GLCD_BLACK);

            glcd_text(display, 86, DISPLAY_HEIGHT - 7, "CANCEL", Terminal3x5, GLCD_BLACK);
            list_box.selected_ids = NULL;
        break;

        case BANK_LIST_CHECKBOXES_ENGAGED:
            if (NM_get_current_bp_flag(BANKS_LIST))
                glcd_text(display, 62, DISPLAY_HEIGHT - 7, "-", Terminal3x5, GLCD_BLACK);
            else
                glcd_text(display, 52, DISPLAY_HEIGHT - 7, "SELECT", Terminal3x5, GLCD_BLACK);

            glcd_text(display, 24, DISPLAY_HEIGHT - 7, "ADD", Terminal3x5, GLCD_BLACK);
            glcd_text(display, 86, DISPLAY_HEIGHT - 7, "CANCEL", Terminal3x5, GLCD_BLACK);

            list_box.selected_ids = list->selected_pb_uids;
            list_box.selected_count = list->selected_count;
        break;

        case PB_LIST_CHECKBOXES_ENGAGED:
            glcd_text(display, 24, DISPLAY_HEIGHT - 7, "ADD", Terminal3x5, GLCD_BLACK);
            glcd_text(display, 52, DISPLAY_HEIGHT - 7, "SELECT", Terminal3x5, GLCD_BLACK);
            glcd_text(display, 86, DISPLAY_HEIGHT - 7, "CANCEL", Terminal3x5, GLCD_BLACK);

            list_box.selected_ids = list->selected_pb_uids;
            list_box.selected_count = list->selected_count;
        break;
    }

    // draws the list, check if there are items to avoid a crash
    if (list)
    {
        uint8_t count = strarr_length(list->names);
        list_box.x = 1;
        list_box.name = name;
        list_box.y = 11;
        list_box.width = DISPLAY_WIDTH-2;
        list_box.height = 39;
        list_box.color = GLCD_BLACK;
        list_box.hover = list->hover - list->page_min;
        list_box.selected = list->selected - list->page_min;
        list_box.count = count;
        list_box.list = list->names;
        list_box.list_item_flags = list->bp_flag;
        list_box.font = Terminal3x5;
        list_box.type = type;
        list_box.line_space = 2;
        list_box.line_top_margin = 1;
        list_box.line_bottom_margin = 1;
        list_box.text_left_margin = 7;
        list_box.page_min_offset = list->page_min;
        widget_banks_listbox(display, &list_box);
    }

    // Show empty bank msg
    // A big assumption here that a factory bank will never be empty
    if ((type == PB_LIST_CHECKBOXES) && (list->menu_max == 0))
        glcd_text(display, DISPLAY_WIDTH/2 - 30, DISPLAY_HEIGHT/2, "< empty bank >", Terminal3x5, GLCD_BLACK);
}

void screen_pbss_list(const char *title, bp_list_t *list, uint8_t pb_ss_toggle, int8_t hold_item_index, 
                      const char *hold_item_label)
{
    listbox_t list_box;

    glcd_t *display = hardware_glcds(0);
    uint8_t type = NM_get_current_list();
    screen_clear();

    // draws the list, check if there are items to avoid a crash
    if (list)
    {
        //(ab)use bank function
        if ((type == PB_LIST_CHECKBOXES) || (type == PB_LIST_CHECKBOXES_ENGAGED)){
            screen_bank_list(list, title);
            return;
        }

        char str_bfr[18];
        uint8_t char_cnt = strlen(title);
        if (char_cnt > 17)
            char_cnt = 17;

        memset(str_bfr, 0, sizeof(str_bfr));
        strncpy(str_bfr, title, sizeof(str_bfr)-1);
        str_bfr[char_cnt] = 0;

        glcd_text(display, ((DISPLAY_WIDTH / 2) - (3*char_cnt) + 7), 1, str_bfr, Terminal5x7, GLCD_BLACK);
        //snapshot
        if (!pb_ss_toggle)
            icon_bank(display, ((DISPLAY_WIDTH / 2) - (3*char_cnt) + 7) - 12, 1);
        //pb's
        else 
            icon_pedalboard(display, ((DISPLAY_WIDTH / 2) - (3*char_cnt) + 7) - 12, 1);

        //invert the top bar
        glcd_rect_invert(display, 0, 0, DISPLAY_WIDTH, 9);

        glcd_vline(display, 0, 17, DISPLAY_HEIGHT - 21, GLCD_BLACK);
        glcd_vline(display, DISPLAY_WIDTH-1, 17, DISPLAY_HEIGHT - 21, GLCD_BLACK);
        glcd_hline(display, 0, DISPLAY_HEIGHT - 5, 14, GLCD_BLACK);
        glcd_hline(display, 45, DISPLAY_HEIGHT - 5, 3, GLCD_BLACK);
        glcd_hline(display, 79, DISPLAY_HEIGHT - 5, 3, GLCD_BLACK);
        glcd_hline(display, 112, DISPLAY_HEIGHT - 5, 15, GLCD_BLACK);
        glcd_rect(display, 14, DISPLAY_HEIGHT - 9, 31, 9, GLCD_BLACK);
        glcd_rect(display, 48, DISPLAY_HEIGHT - 9, 31, 9, GLCD_BLACK);
        glcd_rect(display, 82, DISPLAY_HEIGHT - 9, 31, 9, GLCD_BLACK);

        uint8_t count = strarr_length(list->names);

        const uint8_t *title_font = Terminal5x7;
        list_box.x = 0;
        list_box.y = 11;
        list_box.width = DISPLAY_WIDTH;
        list_box.height = 45;
        list_box.color = GLCD_BLACK;
        list_box.count = count;
        list_box.font = Terminal7x8;
        list_box.line_space = 4;
        list_box.line_top_margin = 1;
        list_box.line_bottom_margin = 1;
        list_box.text_left_margin = 2;
        list_box.page_min_offset = list->page_min;
        if (!pb_ss_toggle)
            list_box.name = "PEDALBOARDS";
        else
            list_box.name = "SNAPSHOTS";

        if (count != 0) {
            list_box.hover = list->hover - list->page_min;
            list_box.selected = list->selected - list->page_min;
            list_box.list = list->names;

            if (hold_item_index != -1)
                widget_listbox_pedalboard_draging(display, &list_box, title_font, pb_ss_toggle, hold_item_index, hold_item_label);
            else
                widget_listbox_pedalboard(display, &list_box, title_font, pb_ss_toggle);
        }
        else {
            //finish drawing some stuff
            widget_pb_ss_title(display, &list_box, title_font, pb_ss_toggle);
        }

        //if we are in pb mode, and at the top of the list, display the 'add pb to bank' button
        if ((((type == PB_LIST_BEGINNING_BOX) || (type == PB_LIST_BEGINNING_BOX_SELECTED))
            && (!pb_ss_toggle)) && (NM_get_current_selected(BANKS_LIST) != 0)) {
            widget_add_pb_button(display, 31, 22, (type == PB_LIST_BEGINNING_BOX_SELECTED)?1:0);
        }

        //print the 3 buttons
        //draw the first box, back
        
        uint8_t x;
        const char *text;
        if (hold_item_index == -1) {
            if (pb_ss_toggle) {
                x = 22;
                text = "SAVE";
            }
            else {
                x = 16;
                text = "< BANKS";
            }
        }
        else {
            x = 28;
            text = "-";
        }

        glcd_text(display, x, DISPLAY_HEIGHT - 7, text, Terminal3x5, GLCD_BLACK);

        //draw the second box
        glcd_text(display, 62, DISPLAY_HEIGHT - 7, "-", Terminal3x5, GLCD_BLACK);

        //draw the third box, we can remove any pb or ss, except from the all-pb bank
        if (((NM_get_current_list() == BANKS_LIST) || (pb_ss_toggle && (list->menu_max > 1)) || (!pb_ss_toggle && (!NM_get_current_bp_flag(BANKS_LIST)) && (type != PB_LIST_BEGINNING_BOX_SELECTED))) && (hold_item_index == -1))
            glcd_text(display, 86, DISPLAY_HEIGHT - 7, "REMOVE", Terminal3x5, GLCD_BLACK);
        else
            glcd_text(display, 96, DISPLAY_HEIGHT - 7, "-", Terminal3x5, GLCD_BLACK);
    }
}

void screen_system_menu(menu_item_t *item)
{
    glcd_t *display;
    display = hardware_glcds(0);

    // clear screen
    glcd_clear(display, GLCD_WHITE);

    // draws the title
    textbox_t title_box = {};
    title_box.color = GLCD_BLACK;
    title_box.mode = TEXT_SINGLE_LINE;
    title_box.font = Terminal3x5;
    title_box.top_margin = 1;
    title_box.align = ALIGN_CENTER_TOP;
    title_box.text = item->name;
    widget_textbox(display, &title_box);

    //invert the title area
    glcd_rect_invert(display, 0, 0, DISPLAY_WIDTH, 7);

    print_menu_outlines();

    //print the 3 buttons
    //draw the first box, back
    glcd_text(display, 16, DISPLAY_HEIGHT - 7, "ENTER >", Terminal3x5, GLCD_BLACK);

    //draw the second box, TODO Builder MODE
    glcd_text(display, 56, DISPLAY_HEIGHT - 7, "EXIT", Terminal3x5, GLCD_BLACK);

    //draw the third box, save PB
    glcd_text(display, 96, DISPLAY_HEIGHT - 7, "-", Terminal3x5, GLCD_BLACK);

    // menu list
    listbox_t list;
    list.x = 6;
    list.y = 12;
    list.width = 116;
    list.height = 40;
    list.color = GLCD_BLACK;
    list.font = Terminal3x5;
    list.line_space = 2;
    list.line_top_margin = 1;
    list.line_bottom_margin = 1;
    list.text_left_margin = 2;

    popup_t popup = {};
    switch (item->desc->type)
    {
        case MENU_ROOT:
            list.hover = item->data.hover;
            list.selected = item->data.selected;
            list.count = MENU_VISIBLE_LIST_CUT;
            list.list = item->data.list;
            widget_menu_listbox(display, &list);
        break;

        case MENU_CONFIRM:
            // popup
            popup.width = DISPLAY_WIDTH;
            popup.height = DISPLAY_HEIGHT;
            popup.font = Terminal3x5;
            popup.type = OK_CANCEL;
            popup.title = item->data.popup_header;
            popup.content = item->data.popup_content;
            popup.button_selected = item->data.hover;
            widget_popup(display, &popup);
            return;
        break;

        case MENU_CONFIRM2:
            // popup
            popup.width = DISPLAY_WIDTH;
            popup.height = DISPLAY_HEIGHT;
            popup.font = Terminal3x5;
            popup.type = YES_NO;
            popup.title = item->data.popup_header;
            popup.content = item->data.popup_content;
            popup.button_selected = item->data.hover;
            widget_popup(display, &popup);
            return;
        break;

        case MENU_OK:
            // popup
            popup.width = DISPLAY_WIDTH;
            popup.height = DISPLAY_HEIGHT;
            popup.font = Terminal3x5;
            popup.type = OK_ONLY;
            popup.title = item->data.popup_header;
            popup.content = item->data.popup_content;
            popup.button_selected = item->data.hover;
            widget_popup(display, &popup);
            return;
        break;

        case MENU_MAIN:
        case MENU_TOGGLE:
        case MENU_BAR:
        case MENU_NONE:
        case MENU_LIST:
        case MENU_TOOL:
        case MENU_FOOT:
        case MENU_CLICK_LIST:
        break;
    }
}

void screen_menu_page(node_t *node)
{
    //clear screen first
    screen_clear();

    glcd_t *display = hardware_glcds(0);

    menu_item_t *main_item = node->data;

    //print the title
    //draw the title 
    char title_str[45] = {0};
    strncpy(title_str, "SETTINGS > ", 12);
    strcat(title_str, main_item->desc->name);
    title_str[32] = '\0';
    textbox_t title = {};
    title.color = GLCD_BLACK;
    title.mode = TEXT_SINGLE_LINE;
    title.font = Terminal3x5;
    title.top_margin = 1;
    title.text = title_str;
    title.align = ALIGN_CENTER_TOP;
    widget_textbox(display, &title);

    //invert the title area
    glcd_rect_invert(display, 0, 0, DISPLAY_WIDTH, 7);

    //print the 3 buttons
    //draw the first box, back
    glcd_text(display, 18, DISPLAY_HEIGHT - 7, "< BACK", Terminal3x5, GLCD_BLACK);

    uint8_t x;
    const char *text;
    if (node->prev)
    {
        x = 56;
        text = "PREV";
    }
    else
    {
        x = 62;
        text = "-";
    }
    glcd_text(display, x, DISPLAY_HEIGHT - 7, text, Terminal3x5, GLCD_BLACK);

    //draw the third box, save PB
    menu_item_t *end_item = node->next->data;
    if (end_item->desc->id != BLUETOOTH_ID)
    {
        text = "NEXT";
        x = 90;
    }
    else
    {
        text = "-";
        x = 96; 
    }
    glcd_text(display, x, DISPLAY_HEIGHT - 7, text, Terminal3x5, GLCD_BLACK);

    node_t *child_nodes = node->first_child;
    
    uint8_t i;
    for (i = 0; i < 3; i++)
    {
        menu_item_t *item_child = child_nodes->data;

        if (item_child->data.popup_active == 1)
        {
            // popup
            popup_t popup = {};
            popup.width = DISPLAY_WIDTH;
            popup.height = DISPLAY_HEIGHT;
            popup.font = Terminal3x5;

            if (item_child->desc->parent_id == USER_PROFILE_ID)
                popup.type = YES_NO;
            else
                popup.type = OK_CANCEL;

            popup.title = item_child->data.popup_header;
            popup.content = item_child->data.popup_content;
            popup.button_selected = item_child->data.hover;
            widget_popup(display, &popup);
            return;
        }

        print_tripple_menu_items(item_child, i, 0);

        if (!child_nodes->next)
        {
            if (i <= 0)
                glcd_text(display, 62, 26, "-", Terminal3x5, GLCD_BLACK);

            if (i <= 1)
                glcd_text(display, 105, 26, "-", Terminal3x5, GLCD_BLACK);

            return;
        }
        else
            child_nodes = child_nodes->next; 

        //draw the outlines
        print_menu_outlines();
    }
}

void screen_tool_control_page(node_t *node)
{
    //clear screen first
    screen_clear();

    //something off
    if (!node)
        return;

    glcd_t *display = hardware_glcds(0);

    //draw the outlines
    glcd_vline(display, 0, 14, DISPLAY_HEIGHT - 32, GLCD_BLACK);
    glcd_vline(display, DISPLAY_WIDTH - 1, 14, DISPLAY_HEIGHT - 32, GLCD_BLACK);
    glcd_hline(display, 0, DISPLAY_HEIGHT - 51, DISPLAY_WIDTH , GLCD_BLACK);
    glcd_hline(display, 0,  45, DISPLAY_WIDTH, GLCD_BLACK);

    //root node (name is title)
    menu_item_t *item = node->data;

    //draw the title
    textbox_t title = {};
    title.color = GLCD_BLACK;
    title.mode = TEXT_SINGLE_LINE;
    title.font = Terminal5x7;
    title.top_margin = 1;
    title.text = item->name;
    title.align = ALIGN_CENTER_TOP;
    widget_textbox(display, &title);

    //invert the top bar
    glcd_rect_invert(display, 0, 0, DISPLAY_WIDTH, 9);

    //draw the 3 menu items if applicable
    node_t *child_nodes = node->first_child;
    uint8_t i;
    for (i = 0; i < 3; i++)
    {
        menu_item_t *item_child = child_nodes->data;

        //reached footer section
        if (item_child->desc->type == MENU_FOOT)
            break;

        print_tripple_menu_items(item_child, i, 1);

        //check end of items
        if (!child_nodes->next)
            break;
        else
            child_nodes = child_nodes->next;
    }

    //clear some extra menu lines
    glcd_rect_fill(display, 0, 9, DISPLAY_WIDTH, 4, GLCD_WHITE);
    glcd_rect_fill(display, 0, DISPLAY_HEIGHT-18, 3, 5, GLCD_WHITE);
    glcd_rect_fill(display, DISPLAY_WIDTH-3, DISPLAY_HEIGHT-18, 3, 8, GLCD_WHITE);
}

void screen_toggle_tuner(float frequency, const char *note, int16_t cents)
{
    screen_clear();

    glcd_t *display = hardware_glcds(0);

    g_tuner.frequency = frequency;
    g_tuner.note = note;
    g_tuner.cents = cents;

    textbox_t title = {};
    title.color = GLCD_BLACK;
    title.mode = TEXT_SINGLE_LINE;
    title.font = Terminal5x7;
    title.top_margin = 1;
    title.text = "TOOL - TUNER";
    title.align = ALIGN_CENTER_TOP;
    widget_textbox(display, &title);

    //invert the top bar
    glcd_rect_invert(display, 0, 0, DISPLAY_WIDTH, 9);

    //draw tuner
    widget_tuner(display, &g_tuner);
}

void screen_update_tuner(float frequency, char *note, int16_t cents)
{
    g_tuner.frequency = frequency;
    g_tuner.note = note;
    g_tuner.cents = cents;

    //draw tuner
    if (naveg_get_current_mode() == MODE_TOOL_FOOT)
        widget_tuner(hardware_glcds(0), &g_tuner);
}

void screen_update_tuner_input(uint8_t input)
{
    g_tuner_input = input;

    system_tuner_input_cb(TM_get_menu_item_by_ID(TUNER_INPUT_ID), MENU_EV_NONE);
}

void screen_update_tuner_ref_freq(int8_t ref_freq)
{
    g_tuner_reference_freq = g_tuner.ref_freq = ref_freq;

    //draw tuner
    if (naveg_get_current_mode() == MODE_TOOL_FOOT)
        widget_tuner(hardware_glcds(0), &g_tuner);
}

void screen_image(uint8_t display, const uint8_t *image)
{
    glcd_t *display_img = hardware_glcds(display);
    glcd_draw_image(display_img, 0, 0, image, GLCD_BLACK);
}

void screen_shift_overlay(int8_t prev_mode, int16_t *item_ids, uint8_t ui_connection)
{
    //TODO WE NEED THIS VALUE ONCE BUILDER MODE IS SELECTABLE
    (void) ui_connection;

    static uint8_t previous_mode;
    static int16_t last_item_ids[3] = {-1};
    uint8_t i;
    
    if (prev_mode != -1)
    {
        previous_mode = prev_mode;
    }

    if (item_ids)
    {
        for (i = 0; i < 3; i++)
        {
            last_item_ids[i] = item_ids[i];
        }
    }

    //we dont have any items to display
    if (last_item_ids[0] == -1)
        return;

    //clear screen first
    screen_clear();

    glcd_t *display = hardware_glcds(0);

    //draw the title 
    textbox_t title = {};
    title.color = GLCD_BLACK;
    title.mode = TEXT_SINGLE_LINE;
    title.font = Terminal5x7;
    title.top_margin = 1;
    title.text = "MENU";
    title.align = ALIGN_CENTER_TOP;
    widget_textbox(display, &title);

    icon_custom_firmware(display, DISPLAY_WIDTH - 10, 1);
    //invert the title area
    glcd_rect_invert(display, 0, 0, DISPLAY_WIDTH, 9);

    //draw the outlines
    print_menu_outlines();

    //draw the first box, save pb
    glcd_text(display, 22, DISPLAY_HEIGHT - 7, "SAVE", Terminal3x5, GLCD_BLACK);

    //draw the second box, menu/control mode
    glcd_text(display, 50, DISPLAY_HEIGHT - 7, "SETTNGS", Terminal3x5, GLCD_BLACK);
    //invert because this is active rn
    if (previous_mode == MODE_TOOL_MENU)
        glcd_rect_invert(display, 49, DISPLAY_HEIGHT - 8, 30, 8);

    //draw the third box, menu/builder mode
    glcd_text(display, 90, DISPLAY_HEIGHT - 7, "EDIT", Terminal3x5, GLCD_BLACK);
    if (previous_mode == MODE_BUILDER)
        glcd_rect_invert(display, 83, DISPLAY_HEIGHT - 8, 30, 8);

    //print the 3 quick controls
    for (i = 0; i < 3; i++)
    {
        print_tripple_menu_items(TM_get_menu_item_by_ID(last_item_ids[i]), i, 0);
    }
}

void screen_control_overlay(control_t *control)
{
    static char *labels_list[64];
    overlay_t overlay;
    overlay.x = 0;
    overlay.y = 11;
    overlay.width = DISPLAY_WIDTH;
    overlay.height = 38;
    overlay.value_num = control->value;

    glcd_t *display = hardware_glcds(0);

    uint8_t foot_val = control->value;
    if (control->properties & (FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS))
    {
        uint8_t scalepoint_count_local = control->scale_points_count > 64 ? 64 : control->scale_points_count;

        uint8_t i;
        for (i = 0; i < scalepoint_count_local; i++)
        {
            labels_list[i] = control->scale_points[i]->label;
        }

        //trigger list overlay widget
        listbox_t list;
        list.name = control->label;
        list.hover = control->step;
        list.selected = control->step;
        list.count = control->scale_points_count;
        list.list = labels_list;
        list.x = 0;
        list.y = 11;
        list.width = DISPLAY_WIDTH;
        list.height = 38;
        list.color = GLCD_BLACK;
        list.font = Terminal5x7;
        list.line_space = 2;
        list.line_top_margin = 1;
        list.line_bottom_margin = 1;
        list.text_left_margin = 0;
        widget_listbox_overlay(display, &list);
    }
    else if (control->properties & FLAG_CONTROL_TRIGGER)
    {
        //trigger trigger overlay widget
        overlay.color = GLCD_BLACK;
        overlay.font = Terminal5x7;
        overlay.name = control->label;
        overlay.value = "TRIGGER";
        overlay.properties = control->properties;

        widget_foot_overlay(display, &overlay);
    }
    else if (control->properties & (FLAG_CONTROL_TOGGLED | FLAG_CONTROL_BYPASS))
    {    
        if (control->properties & FLAG_CONTROL_BYPASS)
            foot_val = !foot_val;

        //trigger toggle overlay widget
        overlay.color = GLCD_BLACK;
        overlay.font = Terminal5x7;
        overlay.name = control->label;
        overlay.value = foot_val?"ON":"OFF";
        overlay.properties = control->properties;

        widget_foot_overlay(display, &overlay);
    }
    else
    {
        // footer text composition
        char value_txt[33];
        uint8_t i = 0;

        //if unit=ms or unit=bpm -> use 0 decimal points
        if (strcasecmp(control->unit, "ms") == 0 || strcasecmp(control->unit, "bpm") == 0)
            i = int_to_str(control->value, value_txt, sizeof(value_txt), 0);
        //if unit=s or unit=hz or unit=something else-> use 2 decimal points
        else
            i = float_to_str(control->value, value_txt, sizeof(value_txt), 2);

        //add space to footer
        value_txt[i++] = ' ';
        strncpy(&value_txt[i], control->unit, sizeof(value_txt) - i - 1);
        value_txt[32] = '\0';

        //trigger trigger overlay widget
        overlay.color = GLCD_BLACK;
        overlay.font = Terminal5x7;
        overlay.name = control->label;
        overlay.value = value_txt;
        overlay.properties = control->properties;
        //trigger value overlay widget
        widget_foot_overlay(display, &overlay);
    }
}

void screen_widget_overlay(int8_t style, char *header, char *text)
{
    overlay_t overlay;
    overlay.x = 0;
    overlay.y = 11;
    overlay.width = DISPLAY_WIDTH;
    overlay.height = 38;
    overlay.value_num = style;
    overlay.color = GLCD_BLACK;
    overlay.font = Terminal5x7;
    overlay.name = header;
    overlay.value = text;
    overlay.properties = FLAG_CONTROL_TOGGLED;

    glcd_t *display = hardware_glcds(0);

    widget_foot_overlay(display, &overlay);
}

void screen_popup(system_popup_t *popup_data)
{
    glcd_t *display = hardware_glcds(0);

    //clear screen
    screen_clear();

    //display the popup
    popup_t popup = {};
    popup.width = DISPLAY_WIDTH;
    popup.height = DISPLAY_HEIGHT;
    popup.font = Terminal3x5;
    popup.type = EMPTY_POPUP;
    popup.title = popup_data->title;
    popup.content = popup_data->popup_text;
    popup.button_selected = popup_data->button_value;
    widget_popup(display, &popup);

    //full buttons
    glcd_text(display, 30 - (2*strlen(popup_data->btn1_txt)), DISPLAY_HEIGHT - 7, popup_data->btn1_txt, Terminal3x5, GLCD_BLACK);
    glcd_text(display, 64 - (2*strlen(popup_data->btn2_txt)), DISPLAY_HEIGHT - 7, popup_data->btn2_txt, Terminal3x5, GLCD_BLACK);
    glcd_text(display, 98 - (2*strlen(popup_data->btn3_txt)), DISPLAY_HEIGHT - 7, popup_data->btn3_txt, Terminal3x5, GLCD_BLACK);

    //when we have a naming widget, we can not use the encoders to trigger the button actions, so dont print
    //we do need to print the naming box and name
    if (popup_data->has_naming_input) {
        //box
        glcd_rect(display, 4, 11, DISPLAY_WIDTH - 8, 14, GLCD_BLACK);

        //text
        glcd_text(display, 8, 14, popup_data->input_name, Terminal5x7, GLCD_BLACK);

        //cursor
        glcd_rect(display, 8 + (6*popup_data->cursor_index), 22, 5, 1, GLCD_BLACK);
    }
    else {
        switch (popup_data->button_value) {
            case 0:
                glcd_rect_invert(display, 15, DISPLAY_HEIGHT - 8, 29, 7);
            break;
            case 1:
                if (popup_data->button_max == 2)
                    glcd_rect_invert(display, 83, DISPLAY_HEIGHT - 8, 29, 7);
                else
                    glcd_rect_invert(display, 49, DISPLAY_HEIGHT - 8, 29, 7);
            break;
            case 2:
                glcd_rect_invert(display, 83, DISPLAY_HEIGHT - 8, 29, 7);
            break;
        }
    }
}

void screen_keyboard(system_popup_t *popup_data, uint8_t keyboard_index)
{
    glcd_t *display = hardware_glcds(0);

    //clear screen
    screen_clear();

    screen_image(0, MDW_Naming_Widget_withButtonsandSpace);

    glcd_rect_fill(display, 0, 0, DISPLAY_WIDTH, 9, GLCD_WHITE);

    //draw the tittle
    switch(popup_data->id){
        case POPUP_SAVE_PB_ID:
            glcd_text(display, 20, 1, "SAVE PEDALBOARD", Terminal5x7, GLCD_BLACK);
        break;

        case POPUP_SAVE_SS_ID:
            glcd_text(display, 26, 1, "SAVE SNAPSHOT", Terminal5x7, GLCD_BLACK);
        break;

        case POPUP_NEW_BANK_ID:
            glcd_text(display, 32, 1, "CREATE BANK", Terminal5x7, GLCD_BLACK);
        break;
    }

    // draws the title background
    glcd_rect_invert(display, 0, 0, DISPLAY_WIDTH, 9);

    //draw the highlighted char
    icon_keyboard_invert(display, keyboard_index);

    //draw the current name
    glcd_text(display, 8, 14, popup_data->input_name, Terminal5x7, GLCD_BLACK);

    //draw the current cursor
    glcd_rect(display, 8 + (6*popup_data->cursor_index), 22, 5, 1, GLCD_BLACK);

    //draw the buttons
    glcd_text(display, 30 - (2*strlen("DONE")), DISPLAY_HEIGHT - 7, "DONE", Terminal3x5, GLCD_BLACK);
    glcd_text(display, 64 - (2*strlen("CLEAR")), DISPLAY_HEIGHT - 7, "CLEAR", Terminal3x5, GLCD_BLACK);
    glcd_text(display, 98 - (2*strlen("DELETE")), DISPLAY_HEIGHT - 7, "DELETE", Terminal3x5, GLCD_BLACK);
}

void screen_msg_overlay(const char *message)
{
    glcd_t *display;
    display = hardware_glcds(0);

    // clear screen
    glcd_clear(display, GLCD_WHITE);

    glcd_text(display, 42, 1, "ATTENTION", Terminal5x7, GLCD_BLACK);

    //drraw the title area
    glcd_rect_invert(display, 0, 0, DISPLAY_WIDTH, 9);

    //draw the outlinbes
    glcd_vline(display, 0, 7, DISPLAY_HEIGHT - 8, GLCD_BLACK);
    glcd_vline(display, DISPLAY_WIDTH - 1, 7, DISPLAY_HEIGHT - 8, GLCD_BLACK);
    glcd_hline(display, 0, DISPLAY_HEIGHT - 1, DISPLAY_WIDTH, GLCD_BLACK);

    //draw the message
    textbox_t text_box;
    text_box.color = GLCD_BLACK;
    text_box.mode = TEXT_MULTI_LINES;
    text_box.font = Terminal5x7;
    text_box.top_margin = 10;
    text_box.bottom_margin = 2;
    text_box.left_margin = 1;
    text_box.right_margin = 2;
    text_box.height = 53;
    text_box.width = 126;
    text_box.text = message;
    text_box.align = ALIGN_CENTER_TOP;
    widget_textbox(display, &text_box);
}

void screen_text_box(uint8_t x, uint8_t y, const char *text)
{
    glcd_t *hardware_display = hardware_glcds(0);

    textbox_t text_box;
    text_box.color = GLCD_BLACK;
    text_box.mode = TEXT_MULTI_LINES;
    text_box.font = Terminal3x5;
    text_box.top_margin = 1;
    text_box.bottom_margin = 0;
    text_box.left_margin = 1;
    text_box.right_margin = 0;
    text_box.height = 63;
    text_box.width = 127;
    text_box.text = text;
    text_box.align = ALIGN_NONE_NONE;
    text_box.y = y;
    text_box.x = x;
    widget_textbox(hardware_display, &text_box);
}


void screen_plugins_list(menu_item_t *item)
{
    glcd_t *display;
    display = hardware_glcds(0);

    // clear screen
    glcd_clear(display, GLCD_WHITE);

    // draws the title
    textbox_t title_box = {};
    title_box.color = GLCD_BLACK;
    title_box.mode = TEXT_SINGLE_LINE;
    title_box.font = Terminal3x5;
    title_box.top_margin = 1;
    title_box.align = ALIGN_CENTER_TOP;
    title_box.text = item->name;
    widget_textbox(display, &title_box);

    //invert the title area
    glcd_rect_invert(display, 0, 0, DISPLAY_WIDTH, 7);

    print_menu_outlines();

    //print the 3 buttons
    glcd_text(display, 22, DISPLAY_HEIGHT - 7, "EXIT", Terminal3x5, GLCD_BLACK);
    glcd_text(display, 61, DISPLAY_HEIGHT - 7, "-", Terminal3x5, GLCD_BLACK);

    // menu list
    if (item->data.list)
    {
        //draw the third box, save PB
        glcd_text(display, 86, DISPLAY_HEIGHT - 7, "SELECT", Terminal3x5, GLCD_BLACK);

        listbox_t list;
        list.x = 6;
        list.y = 12;
        list.width = 116;
        list.height = 40;
        list.color = GLCD_BLACK;
        list.font = Terminal3x5;
        list.line_space = 2;
        list.line_top_margin = 1;
        list.line_bottom_margin = 1;
        list.text_left_margin = 2;

        list.hover = item->data.hover;
        list.selected = item->data.selected;
        list.count = item->data.list_count;
        list.list = item->data.list;
        widget_menu_listbox(display, &list);
    }
    else
    {
        glcd_text(display, 34, DISPLAY_HEIGHT - 7, "-", Terminal3x5, GLCD_BLACK);

        glcd_text(display, DISPLAY_WIDTH / 2 - 32, DISPLAY_HEIGHT / 2 -5, "NO PLUGINS", Terminal7x8, GLCD_BLACK);
    }

    // led handling
    //turn off foot leds
    for (uint8_t i = 0; i < FOOTSWITCHES_COUNT; i++)
        ledz_off(hardware_leds(i), WHITE);

    led_state_t led_state;
    led_state.color = BUILDER_COLOR;

    ledz_t* led = hardware_leds(3);
    set_ledz_trigger_by_color_id(led, LED_ON, led_state);

    led = hardware_leds(4);
    set_ledz_trigger_by_color_id(led, LED_OFF, led_state);

    led = hardware_leds(5);
    set_ledz_trigger_by_color_id(led, LED_ON, led_state);

    led = hardware_leds(6);
    set_ledz_trigger_by_color_id(led, LED_OFF, led_state);
}

/*
 * BUILDER: draw the pedalboard graph
 *
 * The chrome is the same as the plugin list it replaces -- title bar, outlines, three
 * button labels -- and the graph is drawn into the gap between them. plugin_map_draw()
 * clips to the viewport mode_builder.c set, so nothing here can spill over the footer.
 */
void screen_plugin_map(plugin_map_t *map, uint8_t loaded, uint8_t armed, uint8_t blink_reverse)
{
    glcd_t *display;
    display = hardware_glcds(0);

    const plugin_map_node_t *node = loaded ? plugin_map_selected(map) : NULL;

    // clear screen
    glcd_clear(display, GLCD_WHITE);

    // draws the title: the box label is cut to the width of the box, so the selected
    // node repeats itself here with the longer label mod-ui sends for this bar
    textbox_t title_box = {};
    title_box.color = GLCD_BLACK;
    title_box.mode = TEXT_SINGLE_LINE;
    title_box.font = Terminal3x5;
    title_box.top_margin = 1;
    title_box.align = ALIGN_CENTER_TOP;
    title_box.text = (node && node->title[0]) ? node->title : "PEDALBOARD";
    widget_textbox(display, &title_box);

    //invert the title area
    glcd_rect_invert(display, 0, 0, DISPLAY_WIDTH, 7);

    print_menu_outlines();

    // The three buttons. Opening a plugin is the encoder click now, not a button: the
    // encoder is already under the thumb that moved the selection there.
    glcd_text(display, 22, DISPLAY_HEIGHT - 7, "EXIT", Terminal3x5, GLCD_BLACK);
    glcd_text(display, 58, DISPLAY_HEIGHT - 7, "ADD", Terminal3x5, GLCD_BLACK);

    /*
     * DEL, on anything that is actually removable: the capture and playback boxes are part
     * of the picture and not part of the pedalboard. Once armed it flashes in step with the
     * box it would remove, the whole button the way print_menu_boxes() draws it, so the two
     * read as one thing about to happen.
     */
    if (node && node->kind == BM_PLUGIN)
    {
        glcd_text(display, 92, DISPLAY_HEIGHT - 7, "DEL", Terminal3x5, GLCD_BLACK);

        if (armed && blink_reverse)
            glcd_rect_invert(display, 82, DISPLAY_HEIGHT - 9, 31, 9);
    }

    if (loaded && map->n_nodes > 0)
    {
        plugin_map_draw(display, map);
    }
    else
    {
        glcd_text(display, DISPLAY_WIDTH / 2 - 32, DISPLAY_HEIGHT / 2 -5, "NO PLUGINS", Terminal7x8, GLCD_BLACK);
    }

    // led handling
    //turn off foot leds
    for (uint8_t i = 0; i < FOOTSWITCHES_COUNT; i++)
        ledz_off(hardware_leds(i), WHITE);

    led_state_t led_state;
    led_state.color = BUILDER_COLOR;

    /*
     * B and C are the two ways of reading the board, and each names one rather than
     * toggling: the lit switch is the view that is on screen, so it reads without being
     * pressed. The third foot is not ours and stays dark.
     */
    set_ledz_trigger_by_color_id(hardware_leds(0),
                                 map->view_mode == PLUGIN_MAP_VIEW_LIST ? LED_OFF : LED_ON,
                                 led_state);
    set_ledz_trigger_by_color_id(hardware_leds(1),
                                 map->view_mode == PLUGIN_MAP_VIEW_LIST ? LED_ON : LED_OFF,
                                 led_state);

    ledz_t* led = hardware_leds(3);
    set_ledz_trigger_by_color_id(led, LED_ON, led_state);

    led = hardware_leds(4);
    set_ledz_trigger_by_color_id(led, LED_OFF, led_state);

    led = hardware_leds(5);
    set_ledz_trigger_by_color_id(led, LED_ON, led_state);

    led = hardware_leds(6);
    set_ledz_trigger_by_color_id(led, LED_OFF, led_state);
}

/*
 * BUILDER: draw the connection menu of one box
 *
 * Same chrome as the graph view it opens from. The rows come ready to draw: the caller
 * blanks the armed one on the off half of its blink, so the list never reflows under it.
 */
/*
 * The strip under the connection list: one line, in reverse, above the button row. Inset
 * by a pixel either side because print_menu_outlines() runs the frame down x=0 and x=127
 * through this very band, and inverting those two columns would break the frame in half.
 */
#define SCREEN_PAIR_X       1
#define SCREEN_PAIR_Y       46
#define SCREEN_PAIR_W       (DISPLAY_WIDTH - 2 * SCREEN_PAIR_X)
#define SCREEN_PAIR_H       7
/*
 * Never a multiple of 8. glcd_text() writes a whole page at a time and only merges with
 * what is already there when the row is off a page boundary -- write_data() in the driver
 * takes the plain WRITE_BUFFER branch when y % 8 is zero. Text at row 48 would therefore
 * blank rows 48..55 under every column it touches, spaces between characters included,
 * and row 55 is the top edge of the buttons.
 */
#define SCREEN_PAIR_TEXT_Y  (SCREEN_PAIR_Y + 1)
// half the width, less the " + " between the two names and the arrows at either edge
#define SCREEN_PAIR_ROOM    13

static uint8_t copy_upto(char *dst, uint8_t at, const char *text, uint8_t room)
{
    uint8_t length = (uint8_t) strlen(text);

    if (length > room) length = room;
    memcpy(&dst[at], text, length);

    return (uint8_t)(at + length);
}

void screen_connections(connections_t *model)
{
    glcd_t *display;
    display = hardware_glcds(0);

    glcd_clear(display, GLCD_WHITE);

    textbox_t title_box = {};
    title_box.color = GLCD_BLACK;
    title_box.mode = TEXT_SINGLE_LINE;
    title_box.font = Terminal3x5;
    title_box.top_margin = 1;
    title_box.align = ALIGN_CENTER_TOP;
    title_box.text = model->title ? model->title : "CONNECTIONS";
    widget_textbox(display, &title_box);

    glcd_rect_invert(display, 0, 0, DISPLAY_WIDTH, 7);

    print_menu_outlines();

    glcd_text(display, 22, DISPLAY_HEIGHT - 7, "BACK", Terminal3x5, GLCD_BLACK);

    // the filter cycles on the third button, so its label is what it currently shows
    if (model->filter)
        glcd_text(display, 97 - (glcd_text_width(Terminal3x5, model->filter) / 2),
                  DISPLAY_HEIGHT - 7, model->filter, Terminal3x5, GLCD_BLACK);

    /*
     * One button for both, told apart by whether a cable is armed: ADD while the list is
     * idle, DEL once one is picked. DEL blinks in step with the row it would delete, so
     * the two read as one thing about to happen -- the whole button, the same rectangle
     * print_menu_boxes() draws for it, so it reads as the button flashing rather than the
     * word inside it.
     */
    if (model->can_delete)
    {
        glcd_text(display, 58, DISPLAY_HEIGHT - 7, "DEL", Terminal3x5, GLCD_BLACK);

        if (model->blink_reverse)
            glcd_rect_invert(display, 48, DISPLAY_HEIGHT - 9, 31, 9);
    }
    else if (model->can_add)
    {
        glcd_text(display, 58, DISPLAY_HEIGHT - 7, "ADD", Terminal3x5, GLCD_BLACK);
    }
    else if (model->can_select)
    {
        glcd_text(display, 52, DISPLAY_HEIGHT - 7, "SELECT", Terminal3x5, GLCD_BLACK);
    }

    /*
     * A box with nothing wired to it, which is where every pedalboard starts. Saying so in
     * the middle of the empty frame is what points at the ADD button under it.
     */
    if (model->count == 0 && model->can_add)
    {
        const char *empty = "NO CONNECTIONS";

        glcd_text(display, (DISPLAY_WIDTH - glcd_text_width(Terminal3x5, empty)) / 2,
                  25, empty, Terminal3x5, GLCD_BLACK);
    }

    if (model->count > 0)
    {
        listbox_t list;
        list.x = 6;
        // up against the title bar, to pay for the strip that took the bottom of the box
        list.y = 9;
        list.width = 116;
        list.height = 36;
        list.color = GLCD_BLACK;
        list.font = Terminal3x5;
        list.line_space = 2;
        list.line_top_margin = 1;
        list.line_bottom_margin = 1;
        list.text_left_margin = 2;

        list.hover = model->hover;
        list.selected = model->hover;
        list.count = model->count;
        list.list = model->rows;
        widget_menu_listbox(display, &list);

        /*
         * widget_menu_listbox() always draws the hovered row in reverse, which is the
         * half of the blink we want; inverting it a second time on the other half puts
         * it back to normal. The row's rectangle is worked out here the same way the
         * widget works it out -- the two have to agree, and there is no way to ask it
         * where it drew.
         */
        if (model->can_delete && !model->blink_reverse)
        {
            uint8_t font_height = Terminal3x5[FONT_HEIGHT];
            uint8_t line_pitch = font_height + list.line_space;
            uint8_t max_lines = list.height / line_pitch;
            uint8_t center_focus = (max_lines / 2) - (1 - (max_lines % 2));
            int16_t first_line = 0;
            int16_t focus;

            if (list.hover > center_focus && list.count > max_lines)
            {
                first_line = list.hover - center_focus;
                if (first_line > (list.count - max_lines))
                    first_line = list.count - max_lines;
            }

            focus = list.hover - first_line;
            glcd_rect_invert(display, list.x,
                             list.y + list.line_space + focus * line_pitch
                                 - list.line_top_margin,
                             list.width,
                             font_height + list.line_top_margin + list.line_bottom_margin);
        }
    }

    /*
     * The cable the hovered row stands for, named end to end and drawn in reverse so it
     * reads as a caption rather than one more row of the list. A row is one line per box
     * at the far end however many cables run there, so the third encoder walks them and
     * the arrows at the edges are what says so.
     */
    if (model->pair_source && model->pair_sink)
    {
        char line[2 * SCREEN_PAIR_ROOM + 4];
        uint8_t at = 0;

        at = copy_upto(line, at, model->pair_source, SCREEN_PAIR_ROOM);
        line[at++] = ' ';
        line[at++] = '+';
        line[at++] = ' ';
        at = copy_upto(line, at, model->pair_sink, SCREEN_PAIR_ROOM);
        line[at] = 0;

        glcd_text(display, (DISPLAY_WIDTH - glcd_text_width(Terminal3x5, line)) / 2,
                  SCREEN_PAIR_TEXT_Y, line, Terminal3x5, GLCD_BLACK);

        if (model->pair_count > 1)
        {
            glcd_text(display, SCREEN_PAIR_X + 2, SCREEN_PAIR_TEXT_Y, "<", Terminal3x5,
                      GLCD_BLACK);
            glcd_text(display, SCREEN_PAIR_X + SCREEN_PAIR_W - 5, SCREEN_PAIR_TEXT_Y, ">",
                      Terminal3x5, GLCD_BLACK);
        }

        glcd_rect_invert(display, SCREEN_PAIR_X, SCREEN_PAIR_Y, SCREEN_PAIR_W,
                         SCREEN_PAIR_H);
    }

    // led handling, same as the graph view
    for (uint8_t i = 0; i < FOOTSWITCHES_COUNT; i++)
        ledz_off(hardware_leds(i), WHITE);

    led_state_t led_state;
    led_state.color = BUILDER_COLOR;

    ledz_t* led = hardware_leds(3);
    set_ledz_trigger_by_color_id(led, LED_ON, led_state);

    led = hardware_leds(4);
    set_ledz_trigger_by_color_id(led, LED_OFF, led_state);

    led = hardware_leds(5);
    set_ledz_trigger_by_color_id(led, LED_ON, led_state);

    led = hardware_leds(6);
    set_ledz_trigger_by_color_id(led, LED_OFF, led_state);
}

/*
 * One column of the Add screen.
 *
 * Drawn here rather than with widget_menu_listbox, which lays its rows out with
 * ALIGN_CENTER_NONE -- centred on the whole display, not on the box it was given. With one
 * list that looks right and nobody notices; with two the texts land on top of each other
 * in the middle of the panel.
 */
static void draw_offset_column(glcd_t *display, uint8_t x, uint8_t width, uint8_t top,
                               char **rows, uint8_t count, int16_t hover,
                               const uint8_t *marks, uint8_t armed, uint8_t blink_off);

static void draw_column(glcd_t *display, uint8_t x, uint8_t width,
                        char **rows, uint8_t count, int16_t hover)
{
    draw_offset_column(display, x, width, 12, rows, count, hover, NULL, 0, 0);
}

/*
 * `marks` puts a dot against the rows that are spoken for, which is the one thing about an
 * actuator a user cannot work out by looking at the panel. `armed` and `blink_off` flash
 * the hovered row: the widget always draws it in reverse, so inverting it a second time on
 * the other half of the blink puts it back to normal.
 */
static void draw_offset_column(glcd_t *display, uint8_t x, uint8_t width, uint8_t top,
                               char **rows, uint8_t count, int16_t hover,
                               const uint8_t *marks, uint8_t armed, uint8_t blink_off)
{
    const uint8_t height = (uint8_t)(52 - top);
    uint8_t font_height = Terminal3x5[FONT_HEIGHT];
    uint8_t pitch = font_height + 2;
    uint8_t visible = height / pitch;
    int16_t first = 0;
    uint8_t i;

    if (count == 0) return;
    if (visible > count) visible = count;

    // scroll only as far as it takes to keep the hovered row on screen
    if (hover >= visible) first = hover - visible + 1;
    if (first > (int16_t)(count - visible)) first = count - visible;
    if (first < 0) first = 0;

    for (i = 0; i < visible; i++)
    {
        uint8_t y = top + i * pitch;

        glcd_text(display, x + 2, y, rows[first + i], Terminal3x5, GLCD_BLACK);

        if (marks && marks[first + i])
            glcd_text(display, (uint8_t)(x + width - 4), y, "*", Terminal3x5, GLCD_BLACK);

        if ((int16_t)(first + i) == hover)
        {
            if (!(armed && blink_off))
                glcd_rect_invert(display, x, y - 1, width, pitch);
        }
    }
}

/*
 * BUILDER: the plugin_map doing duty as a chooser
 *
 * The same widget as the graph view, in whichever of its two modes, with the cursor
 * restricted to the boxes that could take the cable. Picking a destination by pointing at
 * it on the picture beats picking it off a list of names that says nothing about where it
 * sits -- and the list mode is there for when the name is what you know.
 */
void screen_connection_pick(plugin_map_t *map, const char *title)
{
    glcd_t *display;
    display = hardware_glcds(0);

    glcd_clear(display, GLCD_WHITE);

    textbox_t title_box = {};
    title_box.color = GLCD_BLACK;
    title_box.mode = TEXT_SINGLE_LINE;
    title_box.font = Terminal3x5;
    title_box.top_margin = 1;
    title_box.align = ALIGN_CENTER_TOP;
    title_box.text = title ? title : "PICK A BOX";
    widget_textbox(display, &title_box);

    glcd_rect_invert(display, 0, 0, DISPLAY_WIDTH, 7);

    print_menu_outlines();

    glcd_text(display, 22, DISPLAY_HEIGHT - 7, "BACK", Terminal3x5, GLCD_BLACK);

    // the same gesture as every other step of building a connection
    glcd_text(display, 52, DISPLAY_HEIGHT - 7, "SELECT", Terminal3x5, GLCD_BLACK);

    plugin_map_draw(display, map);

    for (uint8_t i = 0; i < FOOTSWITCHES_COUNT; i++)
        ledz_off(hardware_leds(i), WHITE);

    led_state_t led_state;
    led_state.color = BUILDER_COLOR;

    // B and C read the board the two ways here too, and the lit one says which is on screen
    set_ledz_trigger_by_color_id(hardware_leds(0),
                                 map->view_mode == PLUGIN_MAP_VIEW_LIST ? LED_OFF : LED_ON,
                                 led_state);
    set_ledz_trigger_by_color_id(hardware_leds(1),
                                 map->view_mode == PLUGIN_MAP_VIEW_LIST ? LED_ON : LED_OFF,
                                 led_state);

    ledz_t* led = hardware_leds(3);
    set_ledz_trigger_by_color_id(led, LED_ON, led_state);

    led = hardware_leds(4);
    set_ledz_trigger_by_color_id(led, LED_OFF, led_state);

    led = hardware_leds(5);
    set_ledz_trigger_by_color_id(led, LED_ON, led_state);

    led = hardware_leds(6);
    set_ledz_trigger_by_color_id(led, LED_OFF, led_state);
}

/*
 * BUILDER: draw the Add screen, two columns side by side
 *
 * Categories on the left and their contents on the right, each on its own encoder, so both
 * show a highlighted row at once -- there is no single focus to move between them. The
 * labels arrive already cut to a column's width: nothing here clips, and a long name would
 * otherwise be drawn straight across the other column.
 */
void screen_plugin_manager(plugin_manager_t *model)
{
    glcd_t *display;
    display = hardware_glcds(0);

    glcd_clear(display, GLCD_WHITE);

    textbox_t title_box = {};
    title_box.color = GLCD_BLACK;
    title_box.mode = TEXT_SINGLE_LINE;
    title_box.font = Terminal3x5;
    title_box.top_margin = 1;
    title_box.align = ALIGN_CENTER_TOP;
    title_box.text = "ADD PLUGIN";
    widget_textbox(display, &title_box);

    glcd_rect_invert(display, 0, 0, DISPLAY_WIDTH, 7);

    print_menu_outlines();

    glcd_text(display, 22, DISPLAY_HEIGHT - 7, "BACK", Terminal3x5, GLCD_BLACK);

    // the second button instantiates the row under the cursor
    if (model->plugin_count > 0)
        glcd_text(display, 58, DISPLAY_HEIGHT - 7, "ADD", Terminal3x5, GLCD_BLACK);

    if (model->filter)
        glcd_text(display, 97 - (glcd_text_width(Terminal3x5, model->filter) / 2),
                  DISPLAY_HEIGHT - 7, model->filter, Terminal3x5, GLCD_BLACK);

    // the line between the columns, so the two lists do not read as one
    glcd_vline(display, 63, 9, 43, GLCD_BLACK);

    draw_column(display, 1, 61, model->categories, model->category_count,
                model->category_hover);

    if (model->plugin_count > 0)
        draw_column(display, 65, 61, model->plugins, model->plugin_count,
                    model->plugin_hover);
    else
        glcd_text(display, 74, DISPLAY_HEIGHT / 2 - 3, "EMPTY", Terminal3x5, GLCD_BLACK);

    /*
     * The letter being scrubbed to, over the middle of both lists. Drawn last and filled
     * behind, so it reads on top of whatever rows it lands on -- there is nowhere on a
     * 128x64 panel to put it that is not already busy.
     */
    if (model->scrub)
    {
        uint8_t width = glcd_text_width(Terminal7x8, model->scrub);
        uint8_t box_x = (DISPLAY_WIDTH / 2) - 11;
        uint8_t box_y = (DISPLAY_HEIGHT / 2) - 9;

        glcd_rect_fill(display, box_x, box_y, 22, 18, GLCD_WHITE);
        glcd_rect(display, box_x, box_y, 22, 18, GLCD_BLACK);
        glcd_text(display, box_x + (22 - width) / 2, box_y + 5, model->scrub,
                  Terminal7x8, GLCD_BLACK);
    }

    // led handling, same as the rest of the builder
    for (uint8_t i = 0; i < FOOTSWITCHES_COUNT; i++)
        ledz_off(hardware_leds(i), WHITE);

    led_state_t led_state;
    led_state.color = BUILDER_COLOR;

    ledz_t* led = hardware_leds(3);
    set_ledz_trigger_by_color_id(led, LED_ON, led_state);

    led = hardware_leds(4);
    set_ledz_trigger_by_color_id(led, LED_OFF, led_state);

    led = hardware_leds(5);
    set_ledz_trigger_by_color_id(led, LED_ON, led_state);

    led = hardware_leds(6);
    set_ledz_trigger_by_color_id(led, LED_OFF, led_state);
}

/*
 * A notice over whatever is on screen, for when the host is taking its time.
 *
 * Filled behind, so it reads on top of the lists it covers, and drawn by whoever is doing
 * the waiting -- there is nobody else awake to do it.
 */
/*
 * The description has the panel to itself, so it is worth wrapping properly. Terminal3x5
 * is fixed width -- three pixels and a gap between glyphs -- so a line holds width/4
 * characters and the wrap is a count rather than a measurement. Greedy, breaking on the
 * last space that fits and falling back to a hard cut for a word longer than the line: a
 * URI in a description would otherwise stop the wrap dead.
 */
#define SCREEN_INFO_PITCH   4
#define SCREEN_INFO_X       3
#define SCREEN_INFO_W       116     /* the arrows at the right margin take the rest */
#define SCREEN_INFO_ARROW   121
/*
 * Two lines, inverted, holding everything that is not the description: the name and the
 * port shape on the first, the brand and the category on the second.
 */
#define SCREEN_INFO_HEAD    15
#define SCREEN_INFO_ROW_1   1
#define SCREEN_INFO_ROW_2   9
/*
 * 17, 24, 31, 38, 45. glcd_text() blanks a whole page when the row is a multiple of eight,
 * and the only one of these that is, 24, blanks rows 24..31 -- inside the block itself and
 * under lines not yet drawn. Starting a row lower would put a line on 48, whose page runs
 * to 55 and would eat the top edge of the buttons.
 */
#define SCREEN_INFO_Y       17

static uint8_t wrap_room(void)
{
    uint8_t room = SCREEN_INFO_W / SCREEN_INFO_PITCH;

    return room > SCREEN_INFO_ROOM ? SCREEN_INFO_ROOM : room;
}

/* how much of `text` fits one line, and how many spaces to step over after it */
static uint8_t wrap_take(const char *text, uint8_t room, uint8_t *skip)
{
    uint8_t at = 0, take = 0, gap = 0;

    while (text[at] && at < room)
    {
        if (text[at] == ' ') take = at;
        at++;
    }

    // the tail fits whole, or the break lands exactly where the line ends
    if (text[at] == 0 || text[at] == ' ') take = at;
    else if (take == 0) take = at;          // one word longer than the line

    while (text[take + gap] == ' ') gap++;

    *skip = gap;
    return take;
}

uint8_t screen_plugin_info_lines(const char *text)
{
    uint8_t room = wrap_room();
    uint8_t lines = 0;

    if (!text) return 0;

    while (*text && lines < 255)
    {
        uint8_t skip;
        uint8_t take = wrap_take(text, room, &skip);

        lines++;
        if (take + skip == 0) break;        // nothing consumed: stop rather than spin
        text += take + skip;
    }

    return lines;
}

static void draw_description(glcd_t *display, const char *text, uint8_t first)
{
    uint8_t room = wrap_room();
    uint8_t y = SCREEN_INFO_Y;
    uint8_t line_no = 0;
    char line[SCREEN_INFO_ROOM + 1];

    if (!text) return;

    while (*text && line_no < (uint16_t)(first + SCREEN_INFO_LINES))
    {
        uint8_t skip;
        uint8_t take = wrap_take(text, room, &skip);

        if (line_no >= first)
        {
            memcpy(line, text, take);
            line[take] = 0;
            glcd_text(display, SCREEN_INFO_X, y, line, Terminal3x5, GLCD_BLACK);
            y += 7;
        }

        line_no++;
        if (take + skip == 0) break;
        text += take + skip;
    }
}

/* "A2/2 M1/0", and nothing at all for a kind of port the plugin does not have */
static void port_shape(char *out, const uint8_t *ports)
{
    static const char tags[3] = { 'A', 'M', 'C' };
    uint8_t at = 0, i;

    for (i = 0; i < 3; i++)
    {
        if (ports[i * 2] == 0 && ports[i * 2 + 1] == 0) continue;

        if (at) out[at++] = ' ';
        out[at++] = tags[i];
        out[at++] = (char)('0' + (ports[i * 2] % 10));
        out[at++] = '/';
        out[at++] = (char)('0' + (ports[i * 2 + 1] % 10));
    }

    out[at] = 0;
}

/* one header row: `right` against the right margin, `left` cut so it cannot reach it */
static void draw_header_row(glcd_t *display, uint8_t y, const char *left, const char *right)
{
    char cut[SCREEN_INFO_ROOM + 1];
    uint8_t width = (right && right[0]) ? glcd_text_width(Terminal3x5, right) : 0;
    uint8_t room, at = 0;

    if (width)
        glcd_text(display, (uint8_t)(DISPLAY_WIDTH - 3 - width), y, right, Terminal3x5,
                  GLCD_BLACK);

    if (!left || !left[0]) return;

    room = (uint8_t)((DISPLAY_WIDTH - 6 - (width ? width + 4 : 0)) / SCREEN_INFO_PITCH);
    if (room > SCREEN_INFO_ROOM) room = SCREEN_INFO_ROOM;

    while (left[at] && at < room) { cut[at] = left[at]; at++; }
    cut[at] = 0;

    glcd_text(display, SCREEN_INFO_X, y, cut, Terminal3x5, GLCD_BLACK);
}

void screen_plugin_info(plugin_info_t *model)
{
    glcd_t *display;
    char shape[16];
    uint8_t total;
    display = hardware_glcds(0);

    glcd_clear(display, GLCD_WHITE);

    port_shape(shape, model->ports);

    draw_header_row(display, SCREEN_INFO_ROW_1, model->name, shape);
    draw_header_row(display, SCREEN_INFO_ROW_2, model->brand, model->category);

    /*
     * Inverted before the frame is drawn, not after: print_menu_outlines() runs the frame
     * down x=0 and x=127 from row 7, and inverting over it would break it in half.
     */
    glcd_rect_invert(display, 0, 0, DISPLAY_WIDTH, SCREEN_INFO_HEAD);

    print_menu_outlines();

    glcd_text(display, 22, DISPLAY_HEIGHT - 7, "BACK", Terminal3x5, GLCD_BLACK);

    draw_description(display, model->comment, model->first_line);

    // which way there is more of it, since the first encoder is what moves it
    total = screen_plugin_info_lines(model->comment);

    if (model->first_line > 0)
        glcd_text(display, SCREEN_INFO_ARROW, SCREEN_INFO_Y, "^", Terminal3x5, GLCD_BLACK);

    if (total > model->first_line + SCREEN_INFO_LINES)
        glcd_text(display, SCREEN_INFO_ARROW, SCREEN_INFO_Y + 4 * 7, "v", Terminal3x5,
                  GLCD_BLACK);

    for (uint8_t i = 0; i < FOOTSWITCHES_COUNT; i++)
        ledz_off(hardware_leds(i), WHITE);

    led_state_t led_state;
    led_state.color = BUILDER_COLOR;

    ledz_t* led = hardware_leds(3);
    set_ledz_trigger_by_color_id(led, LED_ON, led_state);

    led = hardware_leds(4);
    set_ledz_trigger_by_color_id(led, LED_OFF, led_state);

    led = hardware_leds(5);
    set_ledz_trigger_by_color_id(led, LED_ON, led_state);

    led = hardware_leds(6);
    set_ledz_trigger_by_color_id(led, LED_OFF, led_state);
}

void screen_bindings(bindings_t *model)
{
    glcd_t *display;
    display = hardware_glcds(0);

    glcd_clear(display, GLCD_WHITE);

    textbox_t title_box = {};
    title_box.color = GLCD_BLACK;
    title_box.mode = TEXT_SINGLE_LINE;
    title_box.font = Terminal3x5;
    title_box.top_margin = 1;
    title_box.align = ALIGN_CENTER_TOP;
    title_box.text = model->title;
    widget_textbox(display, &title_box);

    glcd_rect_invert(display, 0, 0, DISPLAY_WIDTH, 7);

    print_menu_outlines();

    glcd_text(display, 22, DISPLAY_HEIGHT - 7, "BACK", Terminal3x5, GLCD_BLACK);

    if (model->param_count > 0 && model->actuator_count > 0)
        glcd_text(display, 58, DISPLAY_HEIGHT - 7, "ADD", Terminal3x5, GLCD_BLACK);

    /*
     * DEL only where the slot holds something. Once armed it flashes in step with the row
     * it would empty, the whole button the way print_menu_boxes() draws it, so the two
     * read as one thing about to happen.
     */
    if (model->can_delete)
    {
        glcd_text(display, 92, DISPLAY_HEIGHT - 7, "DEL", Terminal3x5, GLCD_BLACK);

        if (model->armed && model->blink_reverse)
            glcd_rect_invert(display, 82, DISPLAY_HEIGHT - 9, 31, 9);
    }

    /*
     * Two columns, one encoder each: what to bind on the left, what to bind it to on the
     * right. The page is the third encoder and one number, so it is a heading over the
     * column it applies to rather than a column of its own.
     */
    draw_column(display, 2, 60, model->params, model->param_count, model->param_hover);

    glcd_vline(display, 63, 9, 43, GLCD_BLACK);

    if (model->page)
    {
        // stopping short of x=127 leaves print_menu_outlines()' frame down that column
        glcd_text(display, (uint8_t)(95 - glcd_text_width(Terminal3x5, model->page) / 2),
                  11, model->page, Terminal3x5, GLCD_BLACK);
        glcd_rect_invert(display, 65, 9, 61, 8);
    }

    draw_offset_column(display, 65, 61, 20, model->actuators, model->actuator_count,
                       model->actuator_hover, model->actuator_taken,
                       model->armed, model->blink_reverse ? 0 : 1);

    for (uint8_t i = 0; i < FOOTSWITCHES_COUNT; i++)
        ledz_off(hardware_leds(i), WHITE);

    led_state_t led_state;
    led_state.color = BUILDER_COLOR;

    ledz_t* led = hardware_leds(3);
    set_ledz_trigger_by_color_id(led, LED_ON, led_state);

    led = hardware_leds(4);
    set_ledz_trigger_by_color_id(led, LED_OFF, led_state);

    led = hardware_leds(5);
    set_ledz_trigger_by_color_id(led, LED_ON, led_state);

    led = hardware_leds(6);
    set_ledz_trigger_by_color_id(led, LED_OFF, led_state);
}

void screen_notice(const char *first, const char *second)
{
    glcd_t *display;
    display = hardware_glcds(0);

    uint8_t height = second ? 25 : 17;
    uint8_t top = (DISPLAY_HEIGHT / 2) - (height / 2);
    uint8_t width;

    /*
     * Wide enough for the longest notice, which is 111 pixels. Worth checking when one
     * changes: glcd_text() does not clip -- st7565p_set_pixel() wraps -- so text wider
     * than its box is not cut off at the edge, it reappears on the other side.
     */
    glcd_rect_fill(display, 4, top, DISPLAY_WIDTH - 8, height, GLCD_WHITE);
    glcd_rect(display, 4, top, DISPLAY_WIDTH - 8, height, GLCD_BLACK);

    if (first)
    {
        width = glcd_text_width(Terminal3x5, first);
        glcd_text(display, (DISPLAY_WIDTH - width) / 2, top + 6, first,
                  Terminal3x5, GLCD_BLACK);
    }

    if (second)
    {
        width = glcd_text_width(Terminal3x5, second);
        glcd_text(display, (DISPLAY_WIDTH - width) / 2, top + 15, second,
                  Terminal3x5, GLCD_BLACK);
    }
}

/*
 * BUILDER: draw a plugin edit screen page
 */

void screen_plugin_edit(plugin_edit_t *model)
{
    glcd_t *display;
    display = hardware_glcds(0);

    // clear screen
    glcd_clear(display, GLCD_WHITE);

    //screen_tittle(-1);
    int title_len;

    // current plugin name
    if (model->plugin_name)
    {
        title_len = strlen(model->plugin_name);
        glcd_text(display, 
                  ((DISPLAY_WIDTH / 2) - (3 * title_len) + 7),
                  1,
                  model->plugin_name,
                  Terminal5x7,
                  GLCD_BLACK);
    }
    else
    {
        title_len = 9; //len of "(unnamed)"
        glcd_text(display, ((DISPLAY_WIDTH / 2) - (3 * title_len) + 7), 1, "(unnamed)", Terminal5x7, GLCD_BLACK);
    }

    icon_plugin(display, ((DISPLAY_WIDTH / 2) - (3*title_len) + 7) - 11, 1);
    icon_custom_firmware(display, DISPLAY_WIDTH - 10, 1);

    //invert the top bar
    glcd_rect_invert(display, 0, 0, DISPLAY_WIDTH, 9);

    //not printing the outlines, but only the boxes
    print_menu_boxes();

    screen_encoder_container_paged(model->current_page, model->page_count);
    for (int i = 0; i < ENCODERS_COUNT; i++)
    {
        const control_t* control = model->controls[i];

        // checks the function assigned to foot and update the footer
        if (control)
        {
            screen_encoder(control, i);
        }
        else
        {
            screen_encoder(NULL, i);
        }
    }

    // led handling
    //turn off foot leds
    for (uint8_t i = 0; i < FOOTSWITCHES_COUNT; i++)
        ledz_off(hardware_leds(i), WHITE);

    led_state_t led_state;
    led_state.color = BUILDER_COLOR;

    ledz_t* led = hardware_leds(6);
    set_ledz_trigger_by_color_id(led, LED_OFF, led_state);

    led = hardware_leds(3);
    set_ledz_trigger_by_color_id(led, LED_ON, led_state);
    glcd_text(display, 16, DISPLAY_HEIGHT - 7, "PLUGINS", Terminal3x5, GLCD_BLACK);

    led = hardware_leds(4);
    if (model->current_page > 0)
    {
        glcd_text(display, 62, DISPLAY_HEIGHT - 7, "<", Terminal3x5, GLCD_BLACK);
        set_ledz_trigger_by_color_id(led, LED_ON, led_state);
    }
    else
    {
        set_ledz_trigger_by_color_id(led, LED_OFF, led_state);
    }

    led = hardware_leds(5);
    if (model->page_count > 1 && model->current_page < model->page_count - 1)
    {
        glcd_text(display, 96, DISPLAY_HEIGHT - 7, ">", Terminal3x5, GLCD_BLACK);
        set_ledz_trigger_by_color_id(led, LED_ON, led_state);
    }
    else
    {
        set_ledz_trigger_by_color_id(led, LED_OFF, led_state);
    }
}