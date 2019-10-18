#ifndef PIXEL_H_
#define PIXEL_H_

// A pixel is an integer from 0-63 of type d3d_pixel.

#include "d3d.h"

// Default transparent pixel when loading textures.
#define EMPTY_PIXEL pixel_from_char(' ')

// The foreground value of the pixel from 0-7.
#define pixel_fg(pix) ((pix) >> 3 & 7)
// The background value of the pixel from 0-7.
#define pixel_bg(pix) ((pix) & 7)

// Convert a character to a pixel.
#define pixel_from_char(ch) ((d3d_pixel)((ch) - ' ' - 1))

#endif /* PIXEL_H_ */
