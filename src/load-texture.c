#include "load-texture.h"
#include "read-lines.h"
#include "pixel.h"
#include "string.h"
#include "util.h"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static d3d_texture *new_empty_texture(void)
{
	d3d_texture *empty = d3d_new_texture(1, 1);
	*d3d_texture_get(empty, 0, 0) = EMPTY_PIXEL;
	return empty;
}

d3d_texture *load_texture(struct loader *ldr, const char *name)
{
	FILE *file;
	d3d_texture *txtr, **txtrp;
	txtrp = loader_texture(ldr, name, &file);
	if (!txtrp) return NULL;
	txtr = *txtrp;
	if (txtr) return txtr;
	size_t width;
	size_t height;
	struct string *lines = read_lines(file, &height);
	if (!lines) {
		logger_printf(loader_logger(ldr), LOGGER_ERROR,
			"Error while reading lines: %s\n", strerror(errno));
		return NULL;
	}
	width = 0;
	for (size_t y = 0; y < height; ++y) {
		if (lines[y].len > width) width = lines[y].len;
	}
	if (width > 0 && height > 0) {
		txtr = d3d_new_texture(width, height);
		for (size_t y = 0; y < height; ++y) {
			struct string *line = &lines[y];
			size_t x;
			for (x = 0; x < line->len; ++x) {
				*d3d_texture_get(txtr, x, y) =
					pixel_from_char(line->text[x]);
			}
			for (; x < width; ++x) {
				*d3d_texture_get(txtr, x, y) = EMPTY_PIXEL;
			}
		}
	} else {
		txtr = new_empty_texture();
	}
	free(lines);
	*txtrp = txtr;
	return txtr;
}
