#pragma once

void blink_on();
void blink_off();
void set_color(int bg, int fg);
void reset_color();
void print_color(int bg, int fg, const char * msg, const char * endl);
