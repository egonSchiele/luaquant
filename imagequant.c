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


// TODO: read image with libpng
static optimize_error_t read_image( optimize_t *opt,  quant_t *q, 
                                    const char *srcpath, size_t len )
{
    const char path[PATH_MAX];
    FILE *fp = NULL;
    
    if( !realpath( srcpath, (char*)&path ) ||
        !( fp = fopen( path, "rb" ) ) ){
        return OPT_ESYS;
    }
    
    return OPT_OK;
}

// TODO: write image
static optimize_error_t optimize( quant_t *q, const char *srcpath, size_t len )
{
    optimize_t opt;
    optimize_error_t rc = read_image( &opt, q, srcpath, len );
    
    if( rc != OPT_OK ){
        return rc;
    }
    
    
    return 0;
}


static int optimize_lua( lua_State *L )
{
    quant_t *q = luaL_checkudata( L, 1, MODULE_MT );
    size_t len = 0;
    const char *path = luaL_checklstring( L, 2, &len );
    const char *errstr = NULL;
    
    switch( optimize( q, path, len ) ){
        case OPT_ESYS:
            errstr = strerror( errno );
        break;
        
        default:
            lua_pushboolean( L, 1 );
            return 1;
    }
    
    lua_pushboolean( L, 0 );
    lua_pushstring( L, errstr );
    
    return 2;
}


static int maxcolor_lua( lua_State *L )
{
    int argc = lua_gettop( L );
    quant_t *q = luaL_checkudata( L, 1, MODULE_MT );
    int colors;
    
    if( argc > 1 ){
        colors = lstate_checkrange( L, luaL_checkint, 2, 2, 256 );
        LQUANT_ATTR_SET( L, q, liq_set_max_colors, colors );
    }
    else {
        // colors = liq_get_max_colors( q->attr );
    }
    
    lua_pushinteger( L, colors );
    
    return 1;
}

static int quality_lua( lua_State *L )
{
    int argc = lua_gettop( L );
    quant_t *q = luaL_checkudata( L, 1, MODULE_MT );
    int min, max;
    
    if( argc > 1 )
    {
        min = lstate_checkrange( L, luaL_checkint, 2, 0, 100 );
        max = lstate_checkrange( L, luaL_checkint, 3, 0, 100 );
        if( min > max ){
            return luaL_error( L, "min must be less than %d", max );
        }
        LQUANT_ATTR_SET( L, q, liq_set_quality, min, max );
    }
    else {
        // min = liq_get_min_quality( q->attr );
        // max = liq_get_max_quality( q->attr );
    }
    
    lua_pushinteger( L, min );
    lua_pushinteger( L, max );
    
    return 2;
}


static int speed_lua( lua_State *L )
{
    int argc = lua_gettop( L );
    quant_t *q = luaL_checkudata( L, 1, MODULE_MT );
    int val;
    
    if( argc > 1 ){
        val = lstate_checkrange( L, luaL_checkint, 2, 1, 10 );
        LQUANT_ATTR_SET( L, q, liq_set_speed, val );
    }
    else {
        // val = liq_get_speed( q->attr );
    }
    
    lua_pushinteger( L, val );
    
    return 1;
}

static int min_opacity_lua( lua_State *L )
{
    int argc = lua_gettop( L );
    quant_t *q = luaL_checkudata( L, 1, MODULE_MT );
    int val;
    
    if( argc > 1 ){
        val = lstate_checkrange( L, luaL_checkint, 2, 0, 255 );
        LQUANT_ATTR_SET( L, q, liq_set_min_opacity, val );
    }
    else {
       //  val = liq_get_min_opacity( q->attr );
    }
    
    lua_pushinteger( L, val );
    
    return 1;
}


static int min_posterization_lua( lua_State *L )
{
    int argc = lua_gettop( L );
    quant_t *q = luaL_checkudata( L, 1, MODULE_MT );
    int val;
    
    if( argc > 1 ){
        val = lstate_checkrange( L, luaL_checkint, 2, 0, 4 );
        // LQUANT_ATTR_SET( L, q, liq_set_min_posterization, val );
    }
    else {
        // val = liq_get_min_posterization( q->attr );
    }
    
    lua_pushinteger( L, val );
    
    return 1;
}


static int gc_lua( lua_State *L )
{
    quant_t *q = (quant_t*)lua_touserdata( L, 1 );
    
    if( q->attr ){
        liq_attr_destroy( q->attr );
    }
    
    return 0;
}

static int tostring_lua( lua_State *L )
{
    lua_pushfstring( L, MODULE_MT ": %p", lua_touserdata( L, 1 ) );
    return 1;
}


static int alloc_lua( lua_State *L )
{
    quant_t *q = lua_newuserdata( L, sizeof( quant_t ) );
    
    if( q && ( q->attr = liq_attr_create() ) ){
        luaL_getmetatable( L, MODULE_MT );
        lua_setmetatable( L, -2 );
        return 1;
    }
    
    // got error
    lua_pushnil( L );
    lua_pushstring( L, strerror( errno ) );
    
    return 2;
}


LUALIB_API int luaopen_imagequant( lua_State *L )
{
    struct luaL_Reg mmethod[] = {
        { "__gc", gc_lua },
        { "__tostring", tostring_lua },
        { NULL, NULL }
    };
    struct luaL_Reg method[] = {
        { "maxColor", maxcolor_lua },
        { "quality", quality_lua },
        { "speed", speed_lua },
        { "minOpacity", min_opacity_lua },
        { "minPosterization", min_posterization_lua },
        { "optimize", optimize_lua },
        { NULL, NULL }
    };
    int i;
    
    // create table __metatable
    luaL_newmetatable( L, MODULE_MT );
    // metamethods
    i = 0;
    while( mmethod[i].name ){
        lstate_fn2tbl( L, mmethod[i].name, mmethod[i].func );
        i++;
    }
    // methods
    lua_pushstring( L, "__index" );
    lua_newtable( L );
    i = 0;
    while( method[i].name ){
        lstate_fn2tbl( L, method[i].name, method[i].func );
        i++;
    }
    lua_rawset( L, -3 );
    lua_pop( L, 1 );
    
    // add methods
    lua_pushcfunction( L, alloc_lua );
    
    return 1;
}

