// Most of this code was copied from the pngquant source.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <lauxlib.h>
#include "rwpng.h"
#include "imagequant/libimagequant.h"

static pngquant_error check_error(lua_State *L, pngquant_error err, const char *context) {
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

static pngquant_error write_image(lua_State *L, png8_image *output_image)
{
  FILE *outfile;

  off_t eob;
  size_t len;
  char *buf;

  // we get the data as a string, but this code was originally written to work with a file handle.
  // open_memstream is a convenient function that will make a string act like a file handle.
  outfile = open_memstream(&buf, &len);
  pngquant_error retval;
  retval = rwpng_write_image8(outfile, output_image);
  check_error(L, retval, "rwpng_write_image8");
  fclose(outfile);

  // push the resulting string onto the lua stack
  // (i.e. this is the only argument that gets returned from the `convert` function)
  // use lstring to send binary string, second arg is the length of the string (set by open_memstream)
  lua_pushlstring(L, buf, len);
  return retval;
}

static pngquant_error read_image(lua_State *L, liq_attr *options, const char *bitmap, png24_image *input_image_p, liq_image **liq_image_p, size_t *len)
{
  FILE *infile;
  infile = fmemopen(bitmap, *len, "rb");

  pngquant_error retval;
  retval = rwpng_read_image24(infile, input_image_p, 0);
  fclose(infile);

  if (check_error(L, retval, "rwpng_read_image24")) {
    return retval;
  }

  *liq_image_p = liq_image_create_rgba_rows(options, (void**)input_image_p->row_pointers, input_image_p->width, input_image_p->height, input_image_p->gamma);

  if (!*liq_image_p) {
    return OUT_OF_MEMORY_ERROR;
  }

  return SUCCESS;
}

static pngquant_error prepare_output_image(liq_result *result, liq_image *input_image, png8_image *output_image)
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

static void set_palette(liq_result *result, png8_image *output_image)
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
static int convert (lua_State *L) {


  if (lua_gettop(L) != 2) {
    luaL_error(L, "string convert(string original_image, int speed)");
  }

  liq_attr *attr = liq_attr_create();

  // this is needed because it is binary data, so it could contain \0's that don't mean
  // the end of the string. So lua_tolstring will store the real length of the data in `len`.
  size_t len;
  const char *bitmap = lua_tolstring(L,1, &len);
  int speed = lua_tonumber(L, 2);
  liq_set_speed(attr, speed);
  pngquant_error retval = SUCCESS;

  liq_image *input_image = NULL;
  png24_image input_image_rwpng = {};
  png8_image output_image = {};
  read_image(L, attr, bitmap, &input_image_rwpng, &input_image, &len);
  liq_result *remap = liq_quantize_image(attr, input_image);
  retval = prepare_output_image(remap, input_image, &output_image);
  check_error(L, retval, "prepare_output_image");
  liq_write_remapped_image_rows(remap, input_image, output_image.row_pointers);
  set_palette(remap, &output_image);

  output_image.chunks = input_image_rwpng.chunks; input_image_rwpng.chunks = NULL;
  retval = write_image(L,&output_image);

  liq_attr_destroy(attr);
  liq_image_destroy(input_image);
  liq_result_destroy(remap);
  rwpng_free_image24(&input_image_rwpng);
  rwpng_free_image8(&output_image);
  // return 1 == this function returns one argument to lua.
  // Argument is returned by the `write_image` function.
  return 1;
}

// functions to expose to lua
static const luaL_Reg funcs[] = {
  {"convert",   convert},
  { NULL, NULL} // fuck lua
};

// This function gets called to set up the lib in lua.
// `lua_newtable` makes a new table in lua with the name given
// by the user. So for example you can use the library like this:
//
// q = require "imagequant"
// q.convert(...)
LUALIB_API int luaopen_imagequant( lua_State *L )
{
  lua_newtable(L);
  luaL_register(L, NULL, funcs);
  return 1;
}

