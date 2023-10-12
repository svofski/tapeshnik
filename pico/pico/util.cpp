#include <stdio.h>
#include "util.h"
#include <stdarg.h>

constexpr int BG = 40;
constexpr int FG = 30;


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
    printf("\033[%d;%dm", BG + bg, FG + fg);
}

void reset_color()
{
    printf("\033[0m");
}

void set_info_color()
{
    set_color(BLACK, YELLOW);
}

void set_warning_color()
{
    set_color(YELLOW, WHITE);
}

void set_error_color()
{
    set_color(RED, WHITE);
}

void info_println(const char * fmt, ...)
{
    set_info_color();
    va_list arg_ptr;
    va_start(arg_ptr, fmt);
    vprintf(fmt, arg_ptr);
    va_end(arg_ptr);
    reset_color();
    putchar('\n');
}

void warning_println(const char * fmt, ...)
{
    set_warning_color();
    va_list arg_ptr;
    va_start(arg_ptr, fmt);
    vprintf(fmt, arg_ptr);
    va_end(arg_ptr);
    reset_color();
    putchar('\n');
}

void error_println(const char * fmt, ...)
{
    set_error_color();
    va_list arg_ptr;
    va_start(arg_ptr, fmt);
    vprintf(fmt, arg_ptr);
    va_end(arg_ptr);
    reset_color();
    putchar('\n');
}
