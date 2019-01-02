#pragma once

#include <stdint.h>

typedef struct sprite {
	uint16_t width;
	uint16_t height;
	uint32_t * bitmap;
	uint32_t * masks;
	uint32_t blank;
	uint8_t  alpha;
} sprite_t;

#define SPRITE(sprite,x,y) sprite->bitmap[sprite->width * (y) + (x)]
