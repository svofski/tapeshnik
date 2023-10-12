#pragma once

constexpr int BLACK   = 0;
constexpr int RED     = 1;
constexpr int GREEN   = 2;
constexpr int YELLOW  = 3;
constexpr int BLUE    = 4;
constexpr int PURPLE  = 5;
constexpr int CYAN    = 6;
constexpr int WHITE   = 7;

void blink_on();
void blink_off();
void set_color(int bg, int fg);
void reset_color();
void print_color(int bg, int fg, const char * msg, const char * endl);

void set_info_color();
void set_warning_color();
void set_error_color();
void info_println(const char * fmt, ...);
void warning_println(const char * fmt, ...);
void error_println(const char * fmt, ...);
