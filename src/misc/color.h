#ifndef COLOR_H
#define COLOR_H

#include <stdint.h>

// RGBA values shared by color parsing and window-manager state.
struct rgba_color
{
    uint32_t p;
    float r;
    float g;
    float b;
    float a;
};

#endif
