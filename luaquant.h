// Most of this code was copied from the pngquant source.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <lauxlib.h>
#include "rwpng.h"
#include "imagequant/libimagequant.h"

typedef struct luaquant_result {
  char *data;
  size_t size;
} luaquant_result;

luaquant_result* write_image(png8_image *output_image);
pngquant_error read_image(liq_attr *options, const char *bitmap, png24_image *input_image_p, liq_image **liq_image_p, size_t *len);
pngquant_error prepare_output_image(liq_result *result, liq_image *input_image, png8_image *output_image);
void set_palette(liq_result *result, png8_image *output_image);
luaquant_result* convert(char* bitmap, int len, int speed);
