#include <stdio.h>
#include "util.h"

void blink_on()
{
    printf("\033[5m");
}

void blink_off()
{
    printf("\033[25m");
}

void print_color(int c1, int c2, const char * msg, const char * endl)
{
    printf("\033[%d;%dm%s\033[0m%s", c1, c2, msg, endl);
}

// bg: 40..47 fg: 30..37
void set_color(int bg, int fg)
{
    printf("\033[%d;%dm", bg, fg);
}

void reset_color()
{
    printf("\033[0m");
}
