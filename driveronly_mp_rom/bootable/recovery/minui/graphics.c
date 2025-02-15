/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include <fcntl.h>
#include <stdio.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include <pixelflinger/pixelflinger.h>

#include "font_10x18.h"
#include "minui.h"

#if defined(RECOVERY_BGRA)
#define PIXEL_FORMAT GGL_PIXEL_FORMAT_BGRA_8888
#define PIXEL_SIZE   4
#elif defined(RECOVERY_RGBX)
#define PIXEL_FORMAT GGL_PIXEL_FORMAT_RGBX_8888
#define PIXEL_SIZE   4
#else
#define PIXEL_FORMAT GGL_PIXEL_FORMAT_RGB_565
#define PIXEL_SIZE   2
#endif

#define NUM_BUFFERS 2

#if 1 //Added by MTK 20130410
#define ALIGN_TO(x, n) \
(((x) + ((n) - 1)) & ~((n) - 1))
#endif //Added by MTK 20130410
typedef struct {
    GGLSurface texture;
    unsigned cwidth;
    unsigned cheight;
    unsigned ascent;
} GRFont;

#ifdef VENDOR_EDIT
//Fangfang.Hui@Prd.PlatSrv.OTA, 2012/06/11, Add for support recovery in chinese
#include "font_16x16.h"
static GRFont *gr_font_cn = 0;
#endif /* VENDOR_EDIT */

static GRFont *gr_font = 0;
static GGLContext *gr_context = 0;
static GGLSurface gr_font_texture;
static GGLSurface gr_framebuffer[NUM_BUFFERS];
static GGLSurface gr_mem_surface;
static unsigned gr_active_fb = 0;
static unsigned double_buffering = 0;

static int gr_fb_fd = -1;
static int gr_vt_fd = -1;

static struct fb_var_screeninfo vi;
static struct fb_fix_screeninfo fi;

static int get_framebuffer(GGLSurface *fb)
{
    int fd;
    void *bits;

    fd = open("/dev/graphics/fb0", O_RDWR);
    if (fd < 0) {
        perror("cannot open fb0");
        return -1;
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) {
        perror("failed to get fb0 info");
        close(fd);
        return -1;
    }

    vi.bits_per_pixel = PIXEL_SIZE * 8;
    if (PIXEL_FORMAT == GGL_PIXEL_FORMAT_BGRA_8888) {
      vi.red.offset     = 8;
      vi.red.length     = 8;
      vi.green.offset   = 16;
      vi.green.length   = 8;
      vi.blue.offset    = 24;
      vi.blue.length    = 8;
      vi.transp.offset  = 0;
      vi.transp.length  = 8;
    } else if (PIXEL_FORMAT == GGL_PIXEL_FORMAT_RGBX_8888) {
      vi.red.offset     = 24;
      vi.red.length     = 8;
      vi.green.offset   = 16;
      vi.green.length   = 8;
      vi.blue.offset    = 8;
      vi.blue.length    = 8;
      vi.transp.offset  = 0;
      vi.transp.length  = 8;
    } else { /* RGB565*/
      vi.red.offset     = 11;
      vi.red.length     = 5;
      vi.green.offset   = 5;
      vi.green.length   = 6;
      vi.blue.offset    = 0;
      vi.blue.length    = 5;
      vi.transp.offset  = 0;
      vi.transp.length  = 0;
    }
	#if 0//Removed by MTK 20130410
    if (ioctl(fd, FBIOPUT_VSCREENINFO, &vi) < 0) {
        perror("failed to put fb0 info");
        close(fd);
        return -1;
    }
	#endif//Removed by MTK 20130410

    if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) {
        perror("failed to get fb0 info");
        close(fd);
        return -1;
    }

    bits = mmap(0, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (bits == MAP_FAILED) {
        perror("failed to mmap framebuffer");
        close(fd);
        return -1;
    }

    fb->version = sizeof(*fb);
#if 0 //wschen 2012-07-10
    fb->width = vi.xres;
    fb->height = vi.yres;
    fb->stride = fi.line_length/PIXEL_SIZE;
#else
    if (0 == strncmp(MTK_LCM_PHYSICAL_ROTATION, "270", 3) || 0 == strncmp(MTK_LCM_PHYSICAL_ROTATION, "90", 2)) {
        fb->width = vi.yres;
        fb->height = vi.xres;
        fb->stride = vi.yres;
    } else {
        fb->width = vi.xres;
        fb->height = vi.yres;
        fb->stride = vi.xres;
    }
#endif
    fb->data = bits;
    fb->format = PIXEL_FORMAT;
	#if 0 //Removed by MTK 20130410
    memset(fb->data, 0, vi.yres * fi.line_length);
	#else //Added by MTK 20130410
    memset(fb->data, 0, vi.yres * ALIGN_TO(vi.xres, 32) * PIXEL_SIZE);
    #endif

    fb++;

    /* check if we can use double buffering */
#if 0 //Removed by MTK 20130410
    if (vi.yres * fi.line_length * 2 > fi.smem_len)
    #else //Added by MTK 20130410
    if (vi.yres * ALIGN_TO(vi.xres, 32)* PIXEL_SIZE * 2 > fi.smem_len)
        return fd;
#endif

    double_buffering = 1;

    fb->version = sizeof(*fb);
#if 0 //wschen 2012-07-10
    fb->width = vi.xres;
    fb->height = vi.yres;
    fb->stride = fi.line_length/PIXEL_SIZE;
#else
    if (0 == strncmp(MTK_LCM_PHYSICAL_ROTATION, "270", 3) || 0 == strncmp(MTK_LCM_PHYSICAL_ROTATION, "90", 2)) {
        fb->width = vi.yres;
        fb->height = vi.xres;
        fb->stride = vi.yres;
    } else {
        fb->width = vi.xres;
        fb->height = vi.yres;
        fb->stride = vi.xres;
    }
#endif
#if 0 //Removed by MTK 20130410
    fb->data = (void*) (((unsigned) bits) + vi.yres * fi.line_length);
#else //Added by MTK 20130410
    fb->data = (void*) (((unsigned) bits) + vi.yres * ALIGN_TO(vi.xres, 32)* PIXEL_SIZE);
#endif
    fb->format = PIXEL_FORMAT;
#if 0 //Removed by MTK 20130410
    memset(fb->data, 0, vi.yres * fi.line_length);
#else //Added by MTK 20130410
    memset(fb->data, 0, vi.yres * ALIGN_TO(vi.xres, 32)* PIXEL_SIZE);
#endif
    return fd;
}

static void get_memory_surface(GGLSurface* ms) {
  ms->version = sizeof(*ms);
#if 0 //wschen 2012-07-10
  ms->width = vi.xres;
  ms->height = vi.yres;
  ms->stride = fi.line_length/PIXEL_SIZE;
#else
  if (0 == strncmp(MTK_LCM_PHYSICAL_ROTATION, "270", 3) || 0 == strncmp(MTK_LCM_PHYSICAL_ROTATION, "90", 2)) {
      ms->width = vi.yres;
      ms->height = vi.xres;
      ms->stride = vi.yres;
  } else {
      ms->width = vi.xres;
      ms->height = vi.yres;
      ms->stride = vi.xres;
  }
#endif
  ms->data = malloc(fi.line_length * vi.yres);
  ms->format = PIXEL_FORMAT;
}

static void set_active_framebuffer(unsigned n)
{
    if (n > 1 || !double_buffering) return;
    vi.yres_virtual = vi.yres * NUM_BUFFERS;
    vi.yoffset = n * vi.yres;
    vi.bits_per_pixel = PIXEL_SIZE * 8;
    if (ioctl(gr_fb_fd, FBIOPUT_VSCREENINFO, &vi) < 0) {
        perror("active fb swap failed");
    }
}

void gr_flip(void)
{
    GGLContext *gl = gr_context;
#if 1 //wschen 2012-07-10
    int j, k;
    unsigned fb_lineLength = vi.xres_virtual * 2;
    unsigned mem_surface_lineLength = vi.xres * 2;
    void *d = NULL;
    void *s = gr_mem_surface.data;
    unsigned int width = vi.xres_virtual;
    unsigned int height = vi.yres;
#endif
    /* swap front and back buffers */
    if (double_buffering)
    gr_active_fb = (gr_active_fb + 1) & 1;
#if 1 //wschen 2012-07-10
    d = gr_framebuffer[gr_active_fb].data;
#endif

    /* copy data from the in-memory surface to the buffer we're about
     * to make active. */
#if 0 //wschen 2012-07-10
    memcpy(gr_framebuffer[gr_active_fb].data, gr_mem_surface.data,
           fi.line_length * vi.yres);
#else
    if (0 == strncmp(MTK_LCM_PHYSICAL_ROTATION, "270", 3)) {
        unsigned int l;
        unsigned short *s_temp;
        unsigned short *d_temp;

        s_temp = (unsigned short*)s;
        for (j = 0; j < width; j++) {
            for (k = 0, l = height - 1; k < height; k++, l--) {
                d_temp = d + ((width * l + j) << 1);
                *d_temp = *s_temp++;
            }
        }
    } else if (0 == strncmp(MTK_LCM_PHYSICAL_ROTATION, "90", 2)) {
        unsigned int l;
        unsigned short *s_temp;
        unsigned short *d_temp;

        s_temp = (unsigned short*)s;
        for (j = width - 1; j >= 0; j--) {
            for (k = 0, l = 0; k < height; k++, l++) {
                d_temp = d + ((width * l + j) << 1);
                *d_temp = *s_temp++;
            }
        }
    } else {
        for (j = 0; j < vi.yres; ++ j) {
            memcpy(d, s, mem_surface_lineLength);
            d += fb_lineLength;
            s += mem_surface_lineLength;
        }
    }
#endif

    /* inform the display driver */
    set_active_framebuffer(gr_active_fb);
}

void gr_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    GGLContext *gl = gr_context;
    GGLint color[4];
    color[0] = ((r << 8) | r) + 1;
    color[1] = ((g << 8) | g) + 1;
    color[2] = ((b << 8) | b) + 1;
    color[3] = ((a << 8) | a) + 1;
    gl->color4xv(gl, color);
}

int gr_measure(const char *s)
{
    return gr_font->cwidth * strlen(s);
}

void gr_font_size(int *x, int *y)
{
    *x = gr_font->cwidth;
    *y = gr_font->cheight;
}

#ifndef VENDOR_EDIT
//Fangfang.Hui@Prd.PlatSrv.OTA, 2012/06/11, Modify for support recovery in chinese
int gr_text(int x, int y, const char *s)
{
    GGLContext *gl = gr_context;
    GRFont *font = gr_font;
    unsigned off;

    y -= font->ascent;

    gl->bindTexture(gl, &font->texture);
    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->enable(gl, GGL_TEXTURE_2D);

    while((off = *s++)) {
        off -= 32;
        if (off < 96) {
            gl->texCoord2i(gl, (off * font->cwidth) - x, 0 - y);
            gl->recti(gl, x, y, x + font->cwidth, y + font->cheight);
        }
        x += font->cwidth;
    }

    return x;
}
#else /* VENDOR_EDIT */
int gr_text(int x, int y, const char *s)
{
    GGLContext *gl = gr_context;
    GRFont *font = gr_font;
	GRFont *font_cn = gr_font_cn; 
    unsigned off;
	unsigned zonecode, bitcode;

    y -= font->ascent;

    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->enable(gl, GGL_TEXTURE_2D);

    while((off = *s++)) {
        
        if (off < 128) {
            off -= 32;
            gl->bindTexture(gl, &font->texture);
            gl->texCoord2i(gl, (off * font->cwidth) - x, 0 - y);
            gl->recti(gl, x, y, x + font->cwidth, y + font->cheight);
            x += font->cwidth;
        }
        else if (off <= 0xf7 && off >= 0xb0)
        {    
            gl->bindTexture(gl, &font_cn->texture);
		
            zonecode = off - 0xb0;
            off = *s++;
            if (off>=0xa1) 
			{
				bitcode = off - 0xa1;
			}
            else 
			{
			    bitcode = off < 94 ? off : 0x5d;
			}
			
            gl->texCoord2i(gl, (bitcode * font_cn->cwidth) - x, (zonecode * font_cn->cheight)- y);

            gl->recti(gl, x, y, x + font_cn->cwidth, y + font_cn->cheight);
            x += font_cn->cwidth;
        }
    }

    return x;
}
#endif /* VENDOR_EDIT */

void gr_texticon(int x, int y, gr_surface icon) {
    if (gr_context == NULL || icon == NULL) {
        return;
    }
    GGLContext* gl = gr_context;

    gl->bindTexture(gl, (GGLSurface*) icon);
    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->enable(gl, GGL_TEXTURE_2D);

    int w = gr_get_width(icon);
    int h = gr_get_height(icon);

    gl->texCoord2i(gl, -x, -y);
    gl->recti(gl, x, y, x+gr_get_width(icon), y+gr_get_height(icon));
}

void gr_fill(int x, int y, int w, int h)
{
    GGLContext *gl = gr_context;
    gl->disable(gl, GGL_TEXTURE_2D);
    gl->recti(gl, x, y, w, h);
}

void gr_blit(gr_surface source, int sx, int sy, int w, int h, int dx, int dy) {
    if (gr_context == NULL || source == NULL) {
        return;
    }
    GGLContext *gl = gr_context;

    gl->bindTexture(gl, (GGLSurface*) source);
    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->enable(gl, GGL_TEXTURE_2D);
    gl->texCoord2i(gl, sx - dx, sy - dy);
    gl->recti(gl, dx, dy, dx + w, dy + h);
}

unsigned int gr_get_width(gr_surface surface) {
    if (surface == NULL) {
        return 0;
    }
    return ((GGLSurface*) surface)->width;
}

unsigned int gr_get_height(gr_surface surface) {
    if (surface == NULL) {
        return 0;
    }
    return ((GGLSurface*) surface)->height;
}

static void gr_init_font(void)
{
    GGLSurface *ftex;
    unsigned char *bits, *rle;
    unsigned char *in, data;
#ifdef VENDOR_EDIT
//Fangfang.Hui@Prd.PlatSrv.OTA, 2012/06/11, Add for support recovery in chinese
    GGLSurface *ftex_cn;
	unsigned char *bits_cn;
#endif /* VENDOR_EDIT */

    gr_font = calloc(sizeof(*gr_font), 1);
    ftex = &gr_font->texture;

    bits = malloc(font.width * font.height);

    ftex->version = sizeof(*ftex);
    ftex->width = font.width;
    ftex->height = font.height;
    ftex->stride = font.width;
    ftex->data = (void*) bits;
    ftex->format = GGL_PIXEL_FORMAT_A_8;

    in = font.rundata;
    while((data = *in++)) {
        memset(bits, (data & 0x80) ? 255 : 0, data & 0x7f);
        bits += (data & 0x7f);
    }

    gr_font->cwidth = font.cwidth;
    gr_font->cheight = font.cheight;
    gr_font->ascent = font.cheight - 2;
#ifdef VENDOR_EDIT
//Fangfang.Hui@Prd.PlatSrv.OTA, 2012/06/11, Add for support recovery in chinese
	gr_font_cn = calloc(sizeof(*gr_font_cn),1);
	ftex_cn = &gr_font_cn->texture;

    bits_cn = malloc(font_cn.width * font_cn.height);

    ftex_cn->version = sizeof(*ftex_cn);
    ftex_cn->width = font_cn.width;
    ftex_cn->height = font_cn.height;
    ftex_cn->stride = font_cn.width;
    ftex_cn->data = (void*) bits_cn;
    ftex_cn->format = GGL_PIXEL_FORMAT_A_8;

    in = font_cn.rundata;
    while((data = *in++)) {
        memset(bits_cn, (data & 0x80) ? 255 : 0, data & 0x7f);
        bits_cn += (data & 0x7f);
    }

    gr_font_cn->cwidth = font_cn.cwidth;
    gr_font_cn->cheight = font_cn.cheight;
    gr_font_cn->ascent = font_cn.cheight - 2;
#endif /* VENDOR_EDIT */
}

int gr_init(void)
{
    gglInit(&gr_context);
    GGLContext *gl = gr_context;

    gr_init_font();
    gr_vt_fd = open("/dev/tty0", O_RDWR | O_SYNC);
    if (gr_vt_fd < 0) {
        // This is non-fatal; post-Cupcake kernels don't have tty0.
        perror("can't open /dev/tty0");
    } else if (ioctl(gr_vt_fd, KDSETMODE, (void*) KD_GRAPHICS)) {
        // However, if we do open tty0, we expect the ioctl to work.
        perror("failed KDSETMODE to KD_GRAPHICS on tty0");
        gr_exit();
        return -1;
    }

    gr_fb_fd = get_framebuffer(gr_framebuffer);
    if (gr_fb_fd < 0) {
        gr_exit();
        return -1;
    }

    get_memory_surface(&gr_mem_surface);

    fprintf(stderr, "framebuffer: fd %d (%d x %d)\n",
            gr_fb_fd, gr_framebuffer[0].width, gr_framebuffer[0].height);

        /* start with 0 as front (displayed) and 1 as back (drawing) */
    gr_active_fb = 0;
    set_active_framebuffer(0);
    gl->colorBuffer(gl, &gr_mem_surface);

    gl->activeTexture(gl, 0);
    gl->enable(gl, GGL_BLEND);
    gl->blendFunc(gl, GGL_SRC_ALPHA, GGL_ONE_MINUS_SRC_ALPHA);

    gr_fb_blank(true);
    gr_fb_blank(false);

    return 0;
}

void gr_exit(void)
{
    close(gr_fb_fd);
    gr_fb_fd = -1;

    free(gr_mem_surface.data);

    ioctl(gr_vt_fd, KDSETMODE, (void*) KD_TEXT);
    close(gr_vt_fd);
    gr_vt_fd = -1;
}

int gr_fb_width(void)
{
    return gr_framebuffer[0].width;
}

int gr_fb_height(void)
{
    return gr_framebuffer[0].height;
}

gr_pixel *gr_fb_data(void)
{
    return (unsigned short *) gr_mem_surface.data;
}

void gr_fb_blank(bool blank)
{
    int ret;

    ret = ioctl(gr_fb_fd, FBIOBLANK, blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK);
    if (ret < 0)
        perror("ioctl(): blank");
}
