/******************************************************************************
** Copyright (C), 2011-2013, OPPO Mobile Comm Corp., Ltd
** VENDOR_EDIT 
** FileName: oppo_get_bits.h
** Author: luodexiang
** Create Date: 2012-6-19
** Description: The files on the decoder is important.Bit operation based on byte stream.
** Version: 1.0
** ------------------Revision History: ------------------------
** <author>       <time>    <version >    <desc>
** luodexiang 2012-6-19  Revision 2.1   create this moudle  
******************************************************************************/

#ifndef OPPO_CODEC_GET_BITS_H
#define OPPO_CODEC_GET_BITS_H

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include "oppo_bswap.h"
#include "oppo_common.h"
#include "oppo_intreadwrite.h"
#include "oppo_log.h"
#include "oppo_mathops.h"


#define ALT_BITSTREAM_READER

/* bit input */
/* buffer, buffer_end and size_in_bits must be present and used by every reader */
typedef struct GetBitContext {
    const uint8_t *buffer, *buffer_end;
#ifdef ALT_BITSTREAM_READER
    int index;
#endif
    int size_in_bits;
} GetBitContext;

#define VLC_TYPE int16_t

typedef struct VLC {
    int bits;
    VLC_TYPE (*table)[2]; ///< code, bits
    int table_size, table_allocated;
} VLC;

typedef struct RL_VLC_ELEM {
    int16_t level;
    int8_t len;
    uint8_t run;
} RL_VLC_ELEM;

/* Bitstream reader API docs:
name
    arbitrary name which is used as prefix for the internal variables

gb
    getbitcontext

OPEN_READER(name, gb)
    loads gb into local variables

CLOSE_READER(name, gb)
    stores local vars in gb

UPDATE_CACHE(name, gb)
    refills the internal cache from the bitstream
    after this call at least MIN_CACHE_BITS will be available,

GET_CACHE(name, gb)
    will output the contents of the internal cache, next bit is MSB of 32 or 64 bit (FIXME 64bit)

SHOW_UBITS(name, gb, num)
    will return the next num bits

SHOW_SBITS(name, gb, num)
    will return the next num bits and do sign extension

SKIP_BITS(name, gb, num)
    will skip over the next num bits
    note, this is equivalent to SKIP_CACHE; SKIP_COUNTER

SKIP_CACHE(name, gb, num)
    will remove the next num bits from the cache (note SKIP_COUNTER MUST be called before UPDATE_CACHE / CLOSE_READER)

SKIP_COUNTER(name, gb, num)
    will increment the internal bit counter (see SKIP_CACHE & SKIP_BITS)

LAST_SKIP_CACHE(name, gb, num)
    will remove the next num bits from the cache if it is needed for UPDATE_CACHE otherwise it will do nothing

LAST_SKIP_BITS(name, gb, num)
    is equivalent to LAST_SKIP_CACHE; SKIP_COUNTER

for examples see get_bits, show_bits, skip_bits, get_vlc
*/

#ifdef ALT_BITSTREAM_READER
#   define MIN_CACHE_BITS 25

#   define OPEN_READER(name, gb)\
        unsigned int name##_index= (gb)->index;\
        int name##_cache= 0;\

#   define CLOSE_READER(name, gb)\
        (gb)->index= name##_index;\


#   define UPDATE_CACHE(name, gb)\
        name##_cache= AV_RB32( ((const uint8_t *)(gb)->buffer)+(name##_index>>3) ) << (name##_index&0x07);\

#   define SKIP_CACHE(name, gb, num)\
        name##_cache <<= (num);


// FIXME name?
#   define SKIP_COUNTER(name, gb, num)\
        name##_index += (num);\

#   define SKIP_BITS(name, gb, num)\
        {\
            SKIP_CACHE(name, gb, num)\
            SKIP_COUNTER(name, gb, num)\
        }\

#   define LAST_SKIP_BITS(name, gb, num) SKIP_COUNTER(name, gb, num)
#   define LAST_SKIP_CACHE(name, gb, num) ;


#   define SHOW_UBITS(name, gb, num)\
        NEG_USR32(name##_cache, num)

#   define SHOW_SBITS(name, gb, num)\
        NEG_SSR32(name##_cache, num)


#   define GET_CACHE(name, gb)\
        ((uint32_t)name##_cache)

static inline int get_bits_count(const GetBitContext *s){
    return s->index;
}

static inline void skip_bits_long(GetBitContext *s, int n){
    s->index += n;
}

#endif

/**
 * read mpeg1 dc style vlc (sign bit + mantisse with no MSB).
 * if MSB not set it is negative
 * @param n length in bits
 * @author BERO
 */
static inline int get_xbits(GetBitContext *s, int n){
    register int sign;
    register int32_t cache;
    OPEN_READER(re, s)
    UPDATE_CACHE(re, s)
    cache = GET_CACHE(re,s);
    sign=(~cache)>>31;
    LAST_SKIP_BITS(re, s, n)
    CLOSE_READER(re, s)
    return (NEG_USR32(sign ^ cache, n) ^ sign) - sign;
}

static inline int get_sbits(GetBitContext *s, int n){
    register int tmp;
    OPEN_READER(re, s)
    UPDATE_CACHE(re, s)
    tmp= SHOW_SBITS(re, s, n);
    LAST_SKIP_BITS(re, s, n)
    CLOSE_READER(re, s)
    return tmp;
}

/**
 * reads 1-17 bits.
 * Note, the alt bitstream reader can read up to 25 bits, but the libmpeg2 reader can't
 */
static inline unsigned int get_bits(GetBitContext *s, int n){
    register int tmp;
    OPEN_READER(re, s)
    UPDATE_CACHE(re, s)
    tmp= SHOW_UBITS(re, s, n);
    LAST_SKIP_BITS(re, s, n)
    CLOSE_READER(re, s)
    return tmp;
}

/**
 * shows 1-17 bits.
 * Note, the alt bitstream reader can read up to 25 bits, but the libmpeg2 reader can't
 */
static inline unsigned int show_bits(GetBitContext *s, int n){
    register int tmp;
    OPEN_READER(re, s)
    UPDATE_CACHE(re, s)
    tmp= SHOW_UBITS(re, s, n);
//    CLOSE_READER(re, s)
    return tmp;
}

static inline void skip_bits(GetBitContext *s, int n){
 //Note gcc seems to optimize this to s->index+=n for the ALT_READER :))
    OPEN_READER(re, s)
    UPDATE_CACHE(re, s)
    LAST_SKIP_BITS(re, s, n)
    CLOSE_READER(re, s)
}

static inline unsigned int get_bits1(GetBitContext *s){
#ifdef ALT_BITSTREAM_READER
    unsigned int index= s->index;
    uint8_t result= s->buffer[ index>>3 ];

    result<<= (index&0x07);
    result>>= 8 - 1;
    index++;
    s->index= index;

    return result;
#endif
}

static inline unsigned int show_bits1(GetBitContext *s){
    return show_bits(s, 1);
}

static inline void skip_bits1(GetBitContext *s){
    skip_bits(s, 1);
}

/**
 * reads 0-32 bits.
 */
static inline unsigned int get_bits_long(GetBitContext *s, int n){
    if(n<=MIN_CACHE_BITS) return get_bits(s, n);
    else{

        int ret= get_bits(s, 16) << (n-16);
        return ret | get_bits(s, n-16);
    }
}

/**
 * reads 0-32 bits as a signed integer.
 */
static inline int get_sbits_long(GetBitContext *s, int n) {
    return sign_extend(get_bits_long(s, n), n);
}

/**
 * shows 0-32 bits.
 */
static inline unsigned int show_bits_long(GetBitContext *s, int n){
    if(n<=MIN_CACHE_BITS) return show_bits(s, n);
    else{
        GetBitContext gb= *s;
        return get_bits_long(&gb, n);
    }
}

static inline int check_marker(GetBitContext *s, const char *msg)
{
    int bit= get_bits1(s);
    if(!bit)
        av_log(NULL, AV_LOG_INFO, "Marker bit missing %s\n", msg);

    return bit;
}

/**
 * init GetBitContext.
 * @param buffer bitstream buffer, must be FF_INPUT_BUFFER_PADDING_SIZE bytes larger then the actual read bits
 * because some optimized bitstream readers read 32 or 64 bit at once and could read over the end
 * @param bit_size the size of the buffer in bits
 *
 * While GetBitContext stores the buffer size, for performance reasons you are
 * responsible for checking for the buffer end yourself (take advantage of the padding)!
 */
static inline void init_get_bits(GetBitContext *s,
                   const uint8_t *buffer, int bit_size)
{
    int buffer_size= (bit_size+7)>>3;
    if(buffer_size < 0 || bit_size < 0) {
        buffer_size = bit_size = 0;
        buffer = NULL;
    }

    s->buffer= buffer;
    s->size_in_bits= bit_size;
    s->buffer_end= buffer + buffer_size;
#ifdef ALT_BITSTREAM_READER
    s->index=0;
#endif
}

static inline void align_get_bits(GetBitContext *s)
{
    int n= (-get_bits_count(s)) & 7;
    if(n) skip_bits(s, n);
}

#define init_vlc(vlc, nb_bits, nb_codes,\
                 bits, bits_wrap, bits_size,\
                 codes, codes_wrap, codes_size,\
                 flags)\
        init_vlc_sparse(vlc, nb_bits, nb_codes,\
                 bits, bits_wrap, bits_size,\
                 codes, codes_wrap, codes_size,\
                 NULL, 0, 0, flags)

int init_vlc_sparse(VLC *vlc, int nb_bits, int nb_codes,
             const void *bits, int bits_wrap, int bits_size,
             const void *codes, int codes_wrap, int codes_size,
             const void *symbols, int symbols_wrap, int symbols_size,
             int flags);
#define INIT_VLC_LE         2
#define INIT_VLC_USE_NEW_STATIC 4
void free_vlc(VLC *vlc);

#define INIT_VLC_STATIC(vlc, bits, a,b,c,d,e,f,g, static_size)\
{\
    static VLC_TYPE table[static_size][2];\
    (vlc)->table= table;\
    (vlc)->table_allocated= static_size;\
    init_vlc(vlc, bits, a,b,c,d,e,f,g, INIT_VLC_USE_NEW_STATIC);\
}


/**
 *
 * If the vlc code is invalid and max_depth=1, then no bits will be removed.
 * If the vlc code is invalid and max_depth>1, then the number of bits removed
 * is undefined.
 */
#define GET_VLC(code, name, gb, table, bits, max_depth)\
{\
    int n, nb_bits;\
    unsigned int index;\
\
    index= SHOW_UBITS(name, gb, bits);\
    code = table[index][0];\
    n    = table[index][1];\
\
    if(max_depth > 1 && n < 0){\
        LAST_SKIP_BITS(name, gb, bits)\
        UPDATE_CACHE(name, gb)\
\
        nb_bits = -n;\
\
        index= SHOW_UBITS(name, gb, nb_bits) + code;\
        code = table[index][0];\
        n    = table[index][1];\
        if(max_depth > 2 && n < 0){\
            LAST_SKIP_BITS(name, gb, nb_bits)\
            UPDATE_CACHE(name, gb)\
\
            nb_bits = -n;\
\
            index= SHOW_UBITS(name, gb, nb_bits) + code;\
            code = table[index][0];\
            n    = table[index][1];\
        }\
    }\
    SKIP_BITS(name, gb, n)\
}

#define GET_RL_VLC(level, run, name, gb, table, bits, max_depth, need_update)\
{\
    int n, nb_bits;\
    unsigned int index;\
\
    index= SHOW_UBITS(name, gb, bits);\
    level = table[index].level;\
    n     = table[index].len;\
\
    if(max_depth > 1 && n < 0){\
        SKIP_BITS(name, gb, bits)\
        if(need_update){\
            UPDATE_CACHE(name, gb)\
        }\
\
        nb_bits = -n;\
\
        index= SHOW_UBITS(name, gb, nb_bits) + level;\
        level = table[index].level;\
        n     = table[index].len;\
    }\
    run= table[index].run;\
    SKIP_BITS(name, gb, n)\
}


/**
 * parses a vlc code, faster then get_vlc()
 * @param bits is the number of bits which will be read at once, must be
 *             identical to nb_bits in init_vlc()
 * @param max_depth is the number of times bits bits must be read to completely
 *                  read the longest vlc code
 *                  = (max_vlc_length + bits - 1) / bits
 */
static av_always_inline int get_vlc2(GetBitContext *s, VLC_TYPE (*table)[2],
                                  int bits, int max_depth)
{
    int code;

    OPEN_READER(re, s)
    UPDATE_CACHE(re, s)

    GET_VLC(code, re, s, table, bits, max_depth)

    CLOSE_READER(re, s)
    return code;
}

static inline int decode012(GetBitContext *gb){
    int n;
    n = get_bits1(gb);
    if (n == 0)
        return 0;
    else
        return get_bits1(gb) + 1;
}

static inline int decode210(GetBitContext *gb){
    if (get_bits1(gb))
        return 0;
    else
        return 2 - get_bits1(gb);
}

static inline int get_bits_left(GetBitContext *gb)
{
    return gb->size_in_bits - get_bits_count(gb);
}

#ifdef TRACE
static inline void print_bin(int bits, int n){
    int i;

    for(i=n-1; i>=0; i--){
        av_log(NULL, AV_LOG_DEBUG, "%d", (bits>>i)&1);
    }
    for(i=n; i<24; i++)
        av_log(NULL, AV_LOG_DEBUG, " ");
}

static inline int get_bits_trace(GetBitContext *s, int n, char *file, const char *func, int line){
    int r= get_bits(s, n);

    print_bin(r, n);
    av_log(NULL, AV_LOG_DEBUG, "%5d %2d %3d bit @%5d in %s %s:%d\n", r, n, r, get_bits_count(s)-n, file, func, line);
    return r;
}
static inline int get_vlc_trace(GetBitContext *s, VLC_TYPE (*table)[2], int bits, int max_depth, char *file, const char *func, int line){
    int show= show_bits(s, 24);
    int pos= get_bits_count(s);
    int r= get_vlc2(s, table, bits, max_depth);
    int len= get_bits_count(s) - pos;
    int bits2= show>>(24-len);

    print_bin(bits2, len);

    av_log(NULL, AV_LOG_DEBUG, "%5d %2d %3d vlc @%5d in %s %s:%d\n", bits2, len, r, pos, file, func, line);
    return r;
}

static inline int get_xbits_trace(GetBitContext *s, int n, char *file, const char *func, int line){
    int show= show_bits(s, n);
    int r= get_xbits(s, n);

    print_bin(show, n);
    av_log(NULL, AV_LOG_DEBUG, "%5d %2d %3d xbt @%5d in %s %s:%d\n", show, n, r, get_bits_count(s)-n, file, func, line);
    return r;
}

#define get_bits(s, n)  get_bits_trace(s, n, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#define get_bits1(s)    get_bits_trace(s, 1, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#define get_xbits(s, n) get_xbits_trace(s, n, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#define get_vlc(s, vlc)            get_vlc_trace(s, (vlc)->table, (vlc)->bits, 3, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#define get_vlc2(s, tab, bits, max) get_vlc_trace(s, tab, bits, max, __FILE__, __PRETTY_FUNCTION__, __LINE__)

#define tprintf(p, ...) av_log(p, AV_LOG_DEBUG, __VA_ARGS__)

#else //TRACE
#define tprintf(p, ...) {}
#endif

#endif /* OPPO_CODEC_GET_BITS_H */
