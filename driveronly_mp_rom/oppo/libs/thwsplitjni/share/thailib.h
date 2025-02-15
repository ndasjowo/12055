/*
 * $Id: thailib.h,v 1.7 2006-07-31 13:55:51 thep Exp $
 * thailib.h - general declarations for libthai
 * Created: 2001-05-17
 */

#ifndef THAI_THAILIB_H
#define THAI_THAILIB_H

/**
 * @file   thailib.h
 * @brief  General declarations for libthai
 */

/**
 * @mainpage
 *
 * LibThai is a set of Thai language support routines aimed to ease
 * developers' tasks to incorporate Thai language support in their
 * applications. It includes important Thai-specific functions e.g. word
 * breaking, input and output methods as well as basic character and
 * string supports. LibThai is an Open Source and collaborative effort
 * initiated by Thai Linux Working Group and opened for all contributors.
 *
 * @section LibThaiFuncs LibThai Functions by Category
 *
 * LibThai provides functions to handle both tis-620 character
 * set (single-byte) and unicode (multi-byte). A function name which
 * includes `w', such as th_wbrk() is unicode version of th_brk() function. 
 *
 * @subsection ThCType Functions for classifying characters
 *
 * th_istis(), th_isthai(), th_iseng(), th_isthcons(), th_isthvowel(), 
 * th_isthtone(), th_isthdiac(), th_isthdigit(), th_isthpunct(), 
 * th_istaillesscons(), th_isovershootcons(), th_isundershootcons(), 
 * th_isundersplitcons(), th_isldvowel(), th_isflvowel(), th_isupvowel(), 
 * th_isblvowel(), th_chlevel(), th_iscombchar(), th_wcistis(), th_wcisthai(), 
 * th_wciseng(), th_wcisthcons(), th_wcisthvowel(), th_wcisthtone(), 
 * th_wcisthdiac(), th_wcisthdigit(), th_wcisthpunct(), th_wcistaillesscons(), 
 * th_wcisovershootcons(), th_wcisundershootcons(), th_wcisundersplitcons(), 
 * th_wcisldvowel(), th_wcisflvowel(), th_wcisupvowel(), th_wcisblvowel(), 
 * th_wcchlevel(), th_wciscombchar()
 *
 * @subsection ThBrk Functions for word segmentation
 *
 * th_brk(), th_brk_line(), th_wbrk(), th_wbrk_line()
 *
 * @subsection ThColl Functions for Thai string collation
 *
 * th_strcoll(), th_strxfrm(), th_wcstrcoll(), th_wcstrxfrm()
 *
 * @subsection ThStr Functions for correct the sequence of Thai string
 *
 * th_normalize(), th_wnormalize()
 *
 * @subsection ThCell Functions for Thai string cell operation
 *
 * th_next_cell(), th_prev_cell(), th_make_cells()
 *
 * @subsection ThInp Functions for Thai characters input
 *
 * th_isaccept(), th_validate()
 *
 * @subsection ThRend Functions for Thai string rendering
 *
 * th_render_cell_tis(), th_render_cell_win(), th_render_cell_mac(),
 * th_render_text_tis(), th_render_text_win(), th_render_text_mac()
 *
 * @subsection ThWChar Functions for converting between unicode and tis-620
 *
 * th_tis2uni(), th_tis2uni_line(), th_winthai2uni(), th_macthai2uni(),
 * th_uni2tis(), th_uni2tis_line(), th_uni2winthai(), th_uni2macthai()
 *
 */
 
/**
 * @brief  Character value indicating error
 */
#define THCHAR_ERR  ((thchar_t)~0)
typedef unsigned int size_t;
#ifndef NULL
#define NULL 0
#endif

#ifndef _STDC_H
#define _STDC_H
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
/* <hExpand noExpand> DO NOT REMOVE THIS LINE */
#endif /* _STDC_H */ 

typedef unsigned char   U8;
#define TRUE 1
#define FALSE 0
#define ENCODING_LENGTH       2
typedef unsigned short      U16;
typedef signed int              kal_int32;
typedef unsigned int            kal_uint32;
typedef signed char             kal_int8;
typedef signed int S32;
typedef char S8;
typedef unsigned int        U32;
typedef unsigned short          kal_wchar;
/**
 * @brief  Thai character type for storing TIS-620 character
 */
typedef unsigned char thchar_t;

#endif  /* THAI_THAILIB_H */

