// Most of this code was copied from the pngquant source.


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <lauxlib.h>
#include "rwpng.h"
#include "imagequant/libimagequant.h"
#include "luaquant.h"

/*
pngquant_error check_error(lua_State *L, pngquant_error err, const char *context) {
  switch(err) {
    case SUCCESS: break;
    case MISSING_ARGUMENT: luaL_error(L, "error during %s: missing argument", context);
    case READ_ERROR: luaL_error(L, "error during %s: read error", context);
    case INVALID_ARGUMENT: luaL_error(L, "error during %s: invalid argument", context);
    case NOT_OVERWRITING_ERROR: luaL_error(L, "error during %s: not overwriting", context);
    case CANT_WRITE_ERROR: luaL_error(L, "error during %s: can't write", context);
    case OUT_OF_MEMORY_ERROR: luaL_error(L, "error during %s: OOM", context);
    case WRONG_ARCHITECTURE: luaL_error(L, "error during %s: wrong architecture", context);
    case PNG_OUT_OF_MEMORY_ERROR: luaL_error(L, "error during %s: PNG OOM", context);
    case LIBPNG_FATAL_ERROR: luaL_error(L, "error during %s: libpng fatal error", context);
    case LIBPNG_INIT_ERROR: luaL_error(L, "error during %s: libpng init error", context);
    case TOO_LARGE_FILE: luaL_error(L, "error during %s: file too large", context);
    case TOO_LOW_QUALITY: luaL_error(L, "error during %s: quality is too low", context);
    default: break;
  }
  return err;
}
*/

luaquant_result* write_image(png8_image *output_image)
{
  FILE *outfile;

  luaquant_result *result = (luaquant_result *) malloc(sizeof(luaquant_result));

  off_t eob;

  // we get the data as a string, but this code was originally written to work with a file handle.
  // open_memstream is a convenient function that will make a string act like a file handle.
  outfile = open_memstream(&result->data, &result->size);
  pngquant_error retval;
  retval = rwpng_write_image8(outfile, output_image);
  // check_error(L, retval, "rwpng_write_image8");
  fclose(outfile);

  return result;
}

pngquant_error read_image(liq_attr *options, const char *bitmap, png24_image *input_image_p, liq_image **liq_image_p, size_t *len)
{
  FILE *infile;
  infile = fmemopen(bitmap, *len, "rb");

  pngquant_error retval;
  retval = rwpng_read_image24(infile, input_image_p, 0);
  fclose(infile);

  /* if (check_error(L, retval, "rwpng_read_image24")) { */
  /*   return retval; */
  /* } */

  *liq_image_p = liq_image_create_rgba_rows(options, (void**)input_image_p->row_pointers, input_image_p->width, input_image_p->height, input_image_p->gamma);

  if (!*liq_image_p) {
    return OUT_OF_MEMORY_ERROR;
  }

  return SUCCESS;
}

pngquant_error prepare_output_image(liq_result *result, liq_image *input_image, png8_image *output_image)
{
  output_image->width  = liq_image_get_width(input_image);
  output_image->height = liq_image_get_height(input_image);
  output_image->gamma  = liq_get_output_gamma(result);

  /*
   ** Step 3.7 [GRR]: allocate memory for the entire indexed image
   */

  output_image->indexed_data = malloc(output_image->height * output_image->width);
  output_image->row_pointers = malloc(output_image->height * sizeof(output_image->row_pointers[0]));

  if (!output_image->indexed_data || !output_image->row_pointers) {
    return OUT_OF_MEMORY_ERROR;
  }

  unsigned int row = 0;
  for(row = 0;  row < output_image->height;  ++row) {
    output_image->row_pointers[row] = output_image->indexed_data + row*output_image->width;
  }

  const liq_palette *palette = liq_get_palette(result);
  // tRNS, etc.
  output_image->num_palette = palette->count;
  output_image->num_trans = 0;
  unsigned int i=0;
  for(i=0; i < palette->count; i++) {
    if (palette->entries[i].a < 255) {
      output_image->num_trans = i+1;
    }
  }

  return SUCCESS;
}

void set_palette(liq_result *result, png8_image *output_image)
{
  const liq_palette *palette = liq_get_palette(result);

  // tRNS, etc.
  output_image->num_palette = palette->count;
  output_image->num_trans = 0;
  unsigned int i=0;
  for(i=0; i < palette->count; i++) {
    liq_color px = palette->entries[i];
    if (px.a < 255) {
      output_image->num_trans = i+1;
    }
    output_image->palette[i] = (png_color){.red=px.r, .green=px.g, .blue=px.b};
    output_image->trans[i] = px.a;
  }
}

// Use this function to compress PNG data using imagequant
// Usage:
//
// q = require "imagequant"
// f = io.open("original.png", "rb")
// original = f.read("*all")
// compressed = q.convert(original, speed)
//
// speed is a value from 1 to 10. 1 = higher compression but slower.
// If you are unsure what to set, set 10. File size is a little bigger,
// but it runs a lot faster.
luaquant_result* convert(char* bitmap, int len_, int speed) {
  liq_attr *attr = liq_attr_create();
  liq_set_speed(attr, speed);
  pngquant_error retval = SUCCESS;
  liq_image *input_image = NULL;
  png24_image input_image_rwpng = {};
  png8_image output_image = {};
  size_t len = len_;
  read_image(attr, bitmap, &input_image_rwpng, &input_image, &len);
  liq_result *remap = liq_quantize_image(attr, input_image);
  retval = prepare_output_image(remap, input_image, &output_image);
  // check_error(L, retval, "prepare_output_image");
  liq_write_remapped_image_rows(remap, input_image, output_image.row_pointers);
  set_palette(remap, &output_image);

  output_image.chunks = input_image_rwpng.chunks; input_image_rwpng.chunks = NULL;
  
  luaquant_result *result = write_image(&output_image);

  liq_attr_destroy(attr);
  liq_image_destroy(input_image);
  liq_result_destroy(remap);
  rwpng_free_image24(&input_image_rwpng);
  rwpng_free_image8(&output_image);
  return result;
}
