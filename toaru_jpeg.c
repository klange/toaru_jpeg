/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * libtoaru_jpeg: Decode simple JPEGs.
 *
 * Adapted from Raul Aguaviva's Python "micro JPEG visualizer":
 *
 * MIT License
 *
 * Copyright (c) 2017 Raul Aguaviva
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int image_width;
static int image_height;

static uint8_t * output = NULL;

static void swap16(uint16_t * val) {
	char * a = (char *)val;
	char b = a[0];
	a[0] = a[1];
	a[1] = b;
}

static int zigzag[] = {
	 0,  1,  8, 16,  9,  2,  3, 10,
	17, 24, 32, 25, 18, 11,  4,  5,
	12, 19, 26, 33, 40, 48, 41, 34,
	27, 20, 13,  6,  7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36,
	29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46,
	53, 60, 61, 54, 47, 55, 62, 63
};

static uint8_t quant_mapping[3] = {0};
static uint8_t quant[8][64];

static int clamp(int col) {
	if (col > 255) return 255;
	if (col < 0) return 0;
	return col;
}

static void color_conversion(
		float Y, float Cr, float Cb,
		int *R, int *G, int *B
	) {
	float r = (Cr*(2.0-2.0*0.299) + Y);
	float b = (Cb*(2.0-2.0*0.114) + Y);
	float g = (Y - 0.144 * b - 0.229 * r) / 0.587;

	*R = clamp(r + 128);
	*G = clamp(g + 128);
	*B = clamp(b + 128);
}

static long long decode(long long code, long long bits) {
	long long l = 1L << (code - 1);
	if (bits >= l) {
		return bits;
	} else {
		return bits - (2 * l - 1);
	}
}

static int xy_to_lin(int x, int y) {
	return x + y * 8;
}

struct huffman_table {
	uint8_t lengths[16];
	uint8_t elements[64];
} huffman_tables[256] = {{0},{0}};

struct stream {
	FILE * file;
	uint8_t byte;
	int have;
	int pos;
};

static void define_quant_table(FILE * f, int len) {

	while (len > 0) {
		uint8_t hdr;
		fread(&hdr, 1, 1, f);
		fread(&quant[(hdr) & 0xF], 64, 1, f);
		len -= 65;
	}
}

static void baseline_dct(FILE * f, int len) {

	struct dct {
		uint8_t  hdr;
		uint16_t height;
		uint16_t width;
		uint8_t  components;
	} __attribute__((packed)) dct;

	fread(&dct, sizeof(struct dct), 1, f);
	swap16(&dct.height);
	swap16(&dct.width);

	len -= sizeof(struct dct);

	image_width = dct.width;
	image_height = dct.height;

	output = malloc(3 * image_width * image_height);

	for (int i = 0; i < dct.components; ++i) {
		/* Quant mapping */
		struct {
			uint8_t id;
			uint8_t samp;
			uint8_t qtb_id;
		} __attribute__((packed)) tmp;

		fread(&tmp, sizeof(tmp), 1, f);

		if (i > 3) {
			abort();
		}

		quant_mapping[i] = tmp.qtb_id;

		len -= 3;
	}

	if (len > 0) {
		fseek(f, len, SEEK_CUR);
	}
}

static void define_huffman_table(FILE * f, int len) {
	while (len > 0) {
		uint8_t hdr;
		fread(&hdr, 1, 1, f);
		len--;

		uint8_t lengths[16];
		fread(lengths, 16, 1, f);
		len -= 16;

		size_t required = 0;
		for (int i = 0; i < 16; ++i) {
			required += lengths[i];
		}

		int o = 0;
		for (int i = 0; i < 16; ++i) {
			int l = lengths[i];
			fread(&huffman_tables[hdr].elements[o], l, 1, f);
			o += l;
			len -= l;
		}

		memcpy(huffman_tables[hdr].lengths, lengths, 16);
	}

	if (len > 0) {
		fseek(f, len, SEEK_CUR);
	}
}

struct idct {
	double base[64];
};

static double norm_coeff[2] = {
	0.35355339059,
	0.5,
};

static void add_idc(struct idct * self, int n, int m, int coeff) {
	double an = norm_coeff[!!n];
	double am = norm_coeff[!!m];
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 8; ++x) {
			double nn = an * cos((double)n * M_PI * ((double)x + 0.5) / 8.0);
			double mm = am * cos((double)m * M_PI * ((double)y + 0.5) / 8.0);
			self->base[xy_to_lin(x, y)] += nn * mm * coeff;
		}
	}
}

static void add_zigzag(struct idct * self, int zi, int coeff) {
	int i = zigzag[zi];
	int n = i & 0x7;
	int m = i >> 3;
	add_idc(self, n, m, coeff);
}

static int get_bit(struct stream * st) {
	while ((st->pos >> 3) >= st->have) {
		int t = fgetc(st->file);
		if (t < 0) {
			st->byte = 0;
		} else {
			st->byte = t;
		}
		if (st->byte == 0xFF) {
			int tmp = fgetc(st->file);
			if (tmp != 0) {
				st->byte = 0;
			}
		}
		st->have++;
	}
	uint8_t b = st->byte;
	int s = 7 - (st->pos & 0x7);
	st->pos += 1;
	return (b >> s) & 1;
}

static int get_bitn(struct stream * st, int l) {
	int val = 0;
	for (int i = 0; i < l; ++i) {
		val = val * 2 + get_bit(st);
	}
	return val;
}

static int get_code(struct huffman_table * table, struct stream * st) {
	int val = 0;
	int off = 0;
	int ini = 0;

	for (int i = 0; i < 16; ++i) {
		val = val * 2 + get_bit(st);
		if (table->lengths[i] > 0) {
			if (val - ini < table->lengths[i]) {
				return table->elements[off + val - ini];
			}
			ini = ini + table->lengths[i];
			off += table->lengths[i];
		}
		ini *= 2;
	}

	return -1;
}

static struct idct * build_matrix(struct idct * i, struct stream * st, int idx, uint8_t * quant, int oldcoeff, int * outcoeff) {
	memset(i, 0, sizeof(struct idct));

	int code = get_code(&huffman_tables[idx], st);
	int bits = get_bitn(st, code);
	int dccoeff = decode(code, bits) + oldcoeff;

	add_zigzag(i, 0, dccoeff * quant[0]);
	int l = 1;



	while (l < 64) {
		code = get_code(&huffman_tables[16+idx], st);
		if (code == 0) break;
		if (code > 15) {
			l += (code >> 4);
			code = code & 0xF;
		}
		bits = get_bitn(st, code);
		int coeff = decode(code, bits);
		add_zigzag(i, l, coeff * quant[l]);
		l += 1;
	}

	*outcoeff = dccoeff;
	return i;
}

static void set_pixel(int x, int y, uint32_t color) {
	if ((x < image_width) && (y < image_height)) {
		output[(image_height - y - 1) * image_width * 3 + x * 3 + 2] = (color & 0xFF0000) >> 16;
		output[(image_height - y - 1) * image_width * 3 + x * 3 + 1] = (color & 0xFF00) >> 8;
		output[(image_height - y - 1) * image_width * 3 + x * 3 + 0] = (color & 0xFF) >> 0;
	}
}

static void draw_matrix(int x, int y, struct idct * L, struct idct * cb, struct idct * cr) {
	for (int yy = 0; yy < 8; ++yy) {
		for (int xx = 0; xx < 8; ++xx) {
			int o = xy_to_lin(xx,yy);
			int r, g, b;
			color_conversion(L->base[o], cb->base[o], cr->base[o], &r, &g, &b);
			uint32_t c = 0xFF000000 | (r << 16) | (g << 8) | b;
			set_pixel((x * 8 + xx), (y * 8 + yy), c);
		}
	}

}

static void start_of_scan(FILE * f, int len) {

	/* Skip header */
	fseek(f, len, SEEK_CUR);

	struct stream _st = {0};
	struct stream * st = &_st;
	st->file = f;

	int old_lum = 0;
	int old_crd = 0;
	int old_cbd = 0;
	for (int y = 0; y < image_height / 8 + !!(image_height & 0xf); ++y) {
		for (int x = 0; x < image_width / 8 + !!(image_width & 0xf); ++x) {
			/* Build matrix * 3 */
			struct idct matL, matCr, matCb;
			build_matrix(&matL,  st, 0, quant[quant_mapping[0]], old_lum, &old_lum);
			build_matrix(&matCr, st, 1, quant[quant_mapping[1]], old_crd, &old_crd);
			build_matrix(&matCb, st, 1, quant[quant_mapping[2]], old_cbd, &old_cbd);

			draw_matrix(x, y, &matL, &matCb, &matCr);
		}
	}

	//free(st->data);

	if (len > 0) {
		fseek(f, len, SEEK_CUR);
	}
}

int main(int argc, char * argv[]) {
	if (argc < 2) {
		fprintf(stdout, "usage: %s IMAGE\n", argv[0]);
		return 1;
	}

	FILE * f = fopen(argv[1], "r");

	if (!f) {
		fprintf(stdout, "%s: couldn't open %s\n", argv[0], argv[1]);
		return 1;
	}

	while (1) {
		uint16_t hdr;
		int r = fread(&hdr, 2, 1, f);
		if (r <= 0) {
			break;
		}

		swap16(&hdr);

		if (hdr == 0xffd8) {
			continue;
		} else if (hdr == 0xffd9) {
			break;
		} else {
			uint16_t len;
			fread(&len, 2, 1, f);
			swap16(&len);

			len -= 2;

			if (hdr == 0xffdb) {
				define_quant_table(f, len);
			} else if (hdr == 0xffc0) {
				baseline_dct(f, len);
			} else if (hdr == 0xffc4) {
				define_huffman_table(f, len);
			} else if (hdr == 0xffda) {
				start_of_scan(f, len);
			} else {
				fseek(f, len, SEEK_CUR);
			}
		}
	}

	FILE * out = fopen("out.tga","w");

	struct {
		uint8_t a, b, c;
		uint16_t d, e;
		uint8_t f;
		uint16_t g, h, i, j;
		uint8_t k, l;
	} __attribute__((packed)) header = {
		0, 0, 2, 0, 0, 0, 0, 0,
		image_width, image_height, 24, 0
	};

	fwrite(&header, sizeof(header), 1, out);
	fwrite(output, image_width * image_height * 3, 1, out);
	fclose(out);

	return 0;

}
