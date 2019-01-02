/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2019 K. Lange
 *
 * Sprti
 */

#include <stdio.h>
#include "sprite.h"

#define _RED(color) ((color & 0x00FF0000) / 0x10000)
#define _GRE(color) ((color & 0x0000FF00) / 0x100)
#define _BLU(color) ((color & 0x000000FF) / 0x1)
#define _ALP(color) ((color & 0xFF000000) / 0x1000000)

extern int load_sprite_jpg(sprite_t * sprite, char * filename);

sprite_t img = {0};

int main(int argc, char * argv[]) {
	if (argc < 2) {
		fprintf(stdout, "usage: %s IMAGE\n", argv[0]);
		return 1;
	}

	load_sprite_jpg(&img, argv[1]);
	FILE * f = fopen(argv[1], "r");

	FILE * out = fopen("out.tga","w");

	struct {
		uint8_t id_length;
		uint8_t color_map_type;
		uint8_t image_type;

		uint16_t color_map_first_entry;
		uint16_t color_map_length;
		uint8_t color_map_entry_size;

		uint16_t x_origin;
		uint16_t y_origin;
		uint16_t width;
		uint16_t height;
		uint8_t  depth;
		uint8_t  descriptor;
	} __attribute__((packed)) header = {
		0, /* No image ID field */
		0, /* No color map */
		2, /* Uncompressed truecolor */
		0, 0, 0, /* No color map */
		0, 0, /* Don't care about origin */
		img.width, img.height, 24,
		0,
	};

	fwrite(&header, 1, sizeof(header), out);

	for (int y = img.height-1; y>=0; y--) {
		for (int x = 0; x < img.width; ++x) {
			uint8_t buf[3] = {
				_BLU(img.bitmap[y * img.width + x]),
				_GRE(img.bitmap[y * img.width + x]),
				_RED(img.bitmap[y * img.width + x]),
			};
			fwrite(buf, 1, 3, out);
		}
	}

	fclose(out);

	return 0;

}
