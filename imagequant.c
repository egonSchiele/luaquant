/*
 *  imagequant.c
 *  lua-imagequant
 *
 *  Created by Masatoshi Teruya on 14/07/09.
 *  Copyright 2014 Masatoshi Teruya. All rights reserved.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <lauxlib.h>
#include "rwpng.h"
#include "imagequant/libimagequant.h"
// helper macros for lua_State
#define lstate_fn2tbl(L,k,v) do{ \
    lua_pushstring(L,k); \
    lua_pushcfunction(L,v); \
    lua_rawset(L,-3); \
}while(0)

#define lstate_num2tbl(L,k,v) do{ \
    lua_pushstring(L,k); \
    lua_pushnumber(L,v); \
    lua_rawset(L,-3); \
}while(0)


#define lstate_checkrange(L,chk,arg,rmin,rmax) ({ \
    int v = chk( L, arg ); \
    if( v < rmin || v > rmax ){ \
        return luaL_argerror( L, arg, \
            "argument must be range of " #rmin "-" #rmax \
        ); \
    } \
    v; \
})

#define CHECK_ARG_LT(arg,v,lt) do { \
    if( v > lt ){ \
        return luaL_argerror( L, arg, \
            "argument must be less than " #lt \
        ); \
    } \
}while(0)


#define MODULE_MT     "imagequant"

typedef enum {
    OPT_OK = 0,
    OPT_ESYS = 1,
    OPT_QUALITY_TOO_LOW = 99,
    OPT_VALUE_OUT_OF_RANGE = 100,
    OPT_OUT_OF_MEMORY,
    OPT_NOT_READY,
    OPT_BITMAP_NOT_AVAILABLE,
    OPT_BUFFER_TOO_SMALL,
    OPT_INVALID_POINTER,
} optimize_error_t;

typedef struct {
    liq_attr *attr;
} quant_t;

typedef struct {
    const char path[PATH_MAX];
    FILE *fp;
} optimize_t;

#define LQUANT_ATTR_SET(L,q,fn,...) do { \
    if( fn( q->attr, __VA_ARGS__ ) != LIQ_OK ){ \
        return luaL_error( L, "failed to " #fn ); \
    } \
}while(0)

static void set_binary_mode(FILE *fp)
{
#if defined(WIN32) || defined(__WIN32__)
    setmode(fp == stdout ? 1 : 0, O_BINARY);
#endif
}
static pngquant_error write_image(lua_State *L, png8_image *output_image, png24_image *output_image24)
{
    FILE *outfile;

    off_t eob;
    size_t len;
    char *buf;
    outfile = open_memstream(&buf, &len);
    pngquant_error retval;
    #pragma omp critical (libpng)
    {
        if (output_image) {
            retval = rwpng_write_image8(outfile, output_image);
        } else {
            retval = rwpng_write_image24(outfile, output_image24);
        }
    }
    fclose(outfile);
    // printf ("buf=%s, len=%zu\n", buf, len);
   // use lstring to send binary string, second arg is the length of the string
   lua_pushlstring(L, buf, len);
    return retval;
}

static pngquant_error read_image(liq_attr *options, const char *filename, int using_stdin, png24_image *input_image_p, liq_image **liq_image_p)
{
    FILE *infile;

    if (using_stdin) {
        set_binary_mode(stdin);
        infile = stdin;
    } else if ((infile = fopen(filename, "rb")) == NULL) {
        fprintf(stderr, "  error: cannot open %s for reading\n", filename);
        return READ_ERROR;
    }

    pngquant_error retval;
    #pragma omp critical (libpng)
    {
        retval = rwpng_read_image24(infile, input_image_p, 0);
    }

    if (!using_stdin) {
        fclose(infile);
    }

    if (retval) {
        fprintf(stderr, "  error: rwpng_read_image() error %d\n", retval);
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
    output_image->width = liq_image_get_width(input_image);
    output_image->height = liq_image_get_height(input_image);
    output_image->gamma = liq_get_output_gamma(result);

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

static int adit (lua_State *L) {
liq_attr *attr = liq_attr_create();
// char *bitmap = luaL_checkstring(L,1);
int width = 100;
int height = 100;
int bitmap_size = 100;
// liq_image *image = liq_image_create_rgba(attr, bitmap, width, height, 0);
    pngquant_error retval = SUCCESS;

    liq_image *input_image = NULL;
    png24_image input_image_rwpng = {};
    png8_image output_image = {};
    char *filename = "/tmp/test.png";
    read_image(attr, filename, 0, &input_image_rwpng, &input_image);
liq_result *remap = liq_quantize_image(attr, input_image);
            retval = prepare_output_image(remap, input_image, &output_image);
liq_write_remapped_image_rows(remap, input_image, output_image.row_pointers);
                set_palette(remap, &output_image);

            liq_result_destroy(remap);
        output_image.chunks = input_image_rwpng.chunks; input_image_rwpng.chunks = NULL;
        retval = write_image(L,&output_image, NULL);

    liq_image_destroy(input_image);
    rwpng_free_image24(&input_image_rwpng);
    rwpng_free_image8(&output_image);
  // lua_pushstring(L,buf);
  return 1;
}

LUALIB_API int luaopen_imagequant( lua_State *L )
{
    // Expose the functions to the lua environment
    lua_pushcfunction(L, adit);
    lua_setglobal(L, "adit");
    //
    
    return 1;
}

