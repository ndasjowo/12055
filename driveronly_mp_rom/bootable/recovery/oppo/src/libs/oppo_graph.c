/*
 * Copyright (C) 2011 Ahmad Amarullah ( http://amarullz.com/ )
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

/*
 * Descriptions:
 * -------------
 * Graph, Framebuffer, Color Calculators, Canvas, and Drawings
 *
 */

#include <signal.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <pthread.h>
#include <math.h>
#include "../oppo_inter.h"
#include <asm-generic/ioctl.h>
#include <linux/msm_mdp.h>

#define MSMFB_OVERLAY_VSYNC_CTRL _IOW(MSMFB_IOCTL_MAGIC, 160, unsigned int)

/*****************************[ GLOBAL VARIABLES ]*****************************/
static int                             ag_fb   = 0;       //-- FrameBuffer Handler
static dword                           ag_fbsz = 0;
static word*                           ag_fbuf = NULL;    //-- FrameBuffer Direct Memory
static byte*                           ag_fbuf32 = NULL;
static word*                           ag_b = NULL;       //-- FrameBuffer Cache Memory
static dword*                          ag_bf32 = NULL;
static CANVAS                          ag_c;           //-- FrameBuffer Main Canvas
static struct fb_fix_screeninfo        ag_fbf;         //-- FrameBuffer Info
static struct fb_var_screeninfo        ag_fbv;
static byte                            ag_32;          //-- FrameBuffer Type 32/16bit
static pthread_t                       status_pthread;    
static pthread_t                       refresh_pthread;     
static CANVAS                          cfresh;            
static unsigned int                     sz_fb_kernel;                          
static int                               use_couple_fb = 0;
static volatile int                            ag_isrun;
static byte                            ag_16strd;
static unsigned int                             ag_16w;
static PNGFONTS                        AG_SMALL_FONT;  //-- Fonts Variables
static PNGFONTS                        AG_BIG_FONT;
static byte                            AG_SMALL_FONT_FT=0;  //-- Small Font is Freetype
static byte                            AG_BIG_FONT_FT=0;    //-- Big Font is Freetype
static int                             ag_dp;          //-- Device Pixel
static byte                            agclp;
static byte                            ag_font_onload=0;
static byte                            ag_oncopybusy=0;

pthread_mutex_t refresh_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t refresh_cond = PTHREAD_COND_INITIALIZER;
static volatile  int need_refresh = 0;

/****************************[ DECLARED FUNCTIONS ]*****************************/
static void *status_thread(void *cookie);
static void *refresh_thread(void *cookie);
/*******************[ CALCULATING ALPHA COLOR WITH NEON ]***********************/
dword ag_calchighlight(color c1,color c2){
  color vc1 = ag_calculatealpha(c1,0xffff,40);
  color vc2 = ag_calculatealpha(ag_calculatealpha(c1,c2,110),0xffff,20);
  return MAKEDWORD(vc1,vc2);
}
dword ag_calcpushlight(color c1,color c2){
  color vc1 = ag_calculatealpha(c1,0xffff,20);
  color vc2 = ag_calculatealpha(ag_calculatealpha(c1,c2,100),0xffff,10);
  return MAKEDWORD(vc1,vc2);
}
color ag_calpushad(color c_g){
  byte sg_r = ag_r(c_g);
  byte sg_g = ag_g(c_g);
  byte sg_b = ag_b(c_g);
  sg_r = floor(sg_r*0.6);
  sg_g = floor(sg_g*0.6);
  sg_b = floor(sg_b*0.6);
  return ag_rgb(sg_r,sg_g,sg_b);
}
color ag_calculatecontrast(color c,float intensity){
  return ag_rgb(
        (byte) min(ag_r(c)*intensity,255),
        (byte) min(ag_g(c)*intensity,255),
        (byte) min(ag_b(c)*intensity,255)
  );
}
//-- Calculate 2 Pixel
color ag_calculatealpha(color dcl,color scl,byte l){
  if (scl==dcl) return scl;
  else if (l==0) return dcl;
  else if (l==255) return scl;
  byte  ralpha = 255 - l;
  byte r = (byte) (((((int) ag_r(dcl)) * ralpha) + (((int) ag_r(scl)) * l)) >> 8);
  byte g = (byte) (((((int) ag_g(dcl)) * ralpha) + (((int) ag_g(scl)) * l)) >> 8);
  byte b = (byte) (((((int) ag_b(dcl)) * ralpha) + (((int) ag_b(scl)) * l)) >> 8);
  return ag_rgb(r,g,b);  
}

dword ag_calculatealpha32(dword dcl,dword scl,byte l){
  if (scl==dcl) return scl;
  else if (l==0) return dcl;
  else if (l==255) return scl;
  byte  ralpha = 255 - l;
  byte r = (byte) (((((int) ag_r32(dcl)) * ralpha) + (((int) ag_r32(scl)) * l)) >> 8);
  byte g = (byte) (((((int) ag_g32(dcl)) * ralpha) + (((int) ag_g32(scl)) * l)) >> 8);
  byte b = (byte) (((((int) ag_b32(dcl)) * ralpha) + (((int) ag_b32(scl)) * l)) >> 8);
  return ag_rgb32(r,g,b);
}

dword ag_calculatealphaTo32(color dcl,color scl,byte l){
  if (scl==dcl) return ag_rgbto32(scl);
  else if (l==0) return ag_rgbto32(dcl);
  else if (l==255) return ag_rgbto32(scl);
  byte  ralpha = 255 - l;
  byte r = (byte) (((((int) ag_r(dcl)) * ralpha) + (((int) ag_r(scl)) * l)) >> 8);
  byte g = (byte) (((((int) ag_g(dcl)) * ralpha) + (((int) ag_g(scl)) * l)) >> 8);
  byte b = (byte) (((((int) ag_b(dcl)) * ralpha) + (((int) ag_b(scl)) * l)) >> 8);
  return ag_rgb32(r,g,b);
}

dword ag_calculatealpha16to32(color dcl,dword scl,byte l){
  if (scl==ag_rgbto32(dcl)) return scl;
  else if (l==0) return ag_rgbto32(dcl);
  else if (l==255) return scl;
  byte  ralpha = 255 - l;
  byte r = (byte) (((((int) ag_r(dcl)) * ralpha) + (((int) ag_r32(scl)) * l)) >> 8);
  byte g = (byte) (((((int) ag_g(dcl)) * ralpha) + (((int) ag_g32(scl)) * l)) >> 8);
  byte b = (byte) (((((int) ag_b(dcl)) * ralpha) + (((int) ag_b32(scl)) * l)) >> 8);
  return ag_rgb32(r,g,b);
}
static void ag_changecolorspace(int r, int g, int b, int a){
  if (ag_32){
    oppo_debug("%d, %d, %d, %d\n", r, g, b, a);
    ag_fbv.red.offset   = r;
    ag_fbv.green.offset = g;
    ag_fbv.blue.offset  = b;
    ag_fbv.transp.offset= a;
    
    ag_blank(NULL); //-- 32bit Use Blank

    unsigned int x,y;
    for (y=0;y<ag_fbv.yres;y++){
      int yp = y * ag_fbv.xres;
      int yd = (ag_fbf.line_length*y);
      for (x=0;x<ag_fbv.xres;x++){
        int xy = yp+x;
        int dxy= yd+(x*agclp);
        ag_bf32[xy] = ag_rgb32(
          ag_fbuf32[dxy+(ag_fbv.red.offset>>3)],
          ag_fbuf32[dxy+(ag_fbv.green.offset>>3)],
          ag_fbuf32[dxy+(ag_fbv.blue.offset>>3)]);
        ag_setpixel(&ag_c,x,y,ag_rgbto16(ag_bf32[xy]));
      }
    }
    
  }
}

int ag_changcolor(char ch1, char ch2, char ch3, char ch4)
{
    int i = 0;
    int arg[4]={0,0,0,0};
    char str[4]={ch1, ch2, ch3, ch4};
    int sum = 0;
    oppo_debug("args:%c, %c, %c, %c\n", ch1, ch2, ch3, ch4);
    for (i = 0; i < 4; i++)
    {
        switch(str[i])
        {
        case 'r':
            arg[i] = 0;
            sum += 0x1;
            break;
        case 'g':
            sum += 0x2;
            arg[i] = 8;
            break;
        case 'b':
            sum += 0x4;
            arg[i] = 16;
            break;
        case  'a':
            sum += 0x8;
            arg[i] = 24;
            break;
        default:
            oppo_printf("invalid args %c\n", str[i]);
            sum = 0;
            return -1;
        }
    }
    return_val_if_fail(sum == 0xf, -1);
    ag_changecolorspace(arg[0], arg[1], arg[2], arg[3]);
    return 0;
}

/*********************************[ FUNCTIONS ]********************************/
//-- INITIALIZING AMARULLZ GRAPHIC
extern int write_char(char *file, char *value);
static void gr_fb_blank(int blank)
{
    int ret;

    ret = ioctl(ag_fb, FBIOBLANK, blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK);
    if (ret < 0)
        perror("ioctl(): blank");
}

#ifdef FORCE_USE_RGB565

byte ag_init(){
  oppo_debug("function ag_init entry\n");
  if (ag_fb>0) return 0;
  
  //-- Open Framebuffer
  ag_fb = open(OPPO_FRAMEBUFFER, O_RDWR, 0);
  
  if (ag_fb>0){
    //-- Init Info from IO
    ioctl(ag_fb, FBIOGET_VSCREENINFO, &ag_fbv);

    /* force to use RGB565*/
    ag_fbv.bits_per_pixel = 16;	     
    ag_fbv.red.offset     = 11;
    ag_fbv.red.length     = 5;
    ag_fbv.green.offset   = 5;
    ag_fbv.green.length   = 6;
    ag_fbv.blue.offset    = 0;
    ag_fbv.blue.length    = 5;
    ag_fbv.transp.offset  = 0;
    ag_fbv.transp.length  = 0;
      
    if (ioctl(ag_fb, FBIOPUT_VSCREENINFO, &ag_fbv) < 0) {
      perror("failed to put fb0 info");
      close(ag_fb);
      return -1;
    }
	  
    ioctl(ag_fb, FBIOGET_FSCREENINFO, &ag_fbf);
    
    //-- Init 32 Buffer
    ag_canvas(&ag_c,ag_fbv.xres,ag_fbv.yres);
    ag_canvas(&cfresh,ag_fbv.xres,ag_fbv.yres);        
    ag_dp = floor( min(ag_fbv.xres,ag_fbv.yres) / 160);
    oppo_debug("ag_dp=%d, ag_fbf.smem_len=%u, ag_fbf.line_length=%u\n", ag_dp, ag_fbf.smem_len, ag_fbf.line_length);
    
    //-- Init Frame Buffer Size
    agclp    = (ag_fbv.bits_per_pixel>>3);
    ag_fbsz  = (ag_fbv.xres * ag_fbv.yres * ((agclp==3)?4:agclp));
    sz_fb_kernel = ag_fbf.line_length* ag_fbv.yres;

    if (ag_fbf.smem_len >= 2*sz_fb_kernel)   use_couple_fb=1;
	
    //-- Init Frame Buffer
    oppo_debug("ag_fbv.bits_per_pixel = %d agclp=%d, sz_fb_kernel=%u\n", ag_fbv.bits_per_pixel, agclp, sz_fb_kernel);
    oppo_debug("ag_fbv.red.offset=%d, ag_fbv.green.offset=%d, ag_fbv.blue.offset=%d\n",
		ag_fbv.red.offset, ag_fbv.green.offset, ag_fbv.blue.offset);
	    
      ag_32   = 0;
      ag_fbuf = (word*) mmap(0,ag_fbf.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,ag_fb,0);
      ag_b    = (word*) malloc(ag_fbsz);
      
      //-- Resolution with Stride
      ag_16strd = 0;
      ag_16w    = ag_fbf.line_length/2;
      if (ag_16w!=ag_fbv.xres){
        if (ag_16w/2==ag_fbv.xres){
          ag_16strd = 0;
          ag_16w    = ag_fbv.xres;
        }
        else{
          ag_16strd=1;
        }
      }
      
      if (ag_16strd==0){
        //-- Can Use memcpy
        memcpy(ag_b,ag_fbuf,ag_fbsz);
        memcpy(ag_c.data,ag_fbuf,ag_fbsz);
      }
      else{
        //-- Should Bit per bit
        unsigned int x,y;
        for (y=0;y<ag_fbv.yres;y++){
          int yp = y * ag_fbv.xres;
          int yd = (ag_16w*y);
          for (x=0;x<ag_fbv.xres;x++){
            int xy = yp+x;
            int dxy= yd+x;
            ag_b[xy] = ag_fbuf[dxy];
            ag_setpixel(&ag_c,x,y,ag_b[xy]);
          }
        }
      }
	  
    oppo_debug("ag_32=%d, ag_16strd=%d\n", ag_32, ag_16strd);

    gr_fb_blank(1);
    gr_fb_blank(0);
    write_char("/sys/class/leds/lcd-backlight/brightness", "102");	

	
    //-- Refresh Draw Lock Thread
    ag_isrun = 1;
    pthread_create(&status_pthread, NULL, status_thread, NULL); 
    pthread_create(&refresh_pthread, NULL, refresh_thread, NULL);     
    //-- Init FreeType
    oppo_printf("Opening Freetype\n");
    aft_open();
    
    return 1;
  }
  oppo_debug("open framebuffer failed\n");
  return 0;
}

#else

byte ag_init(){
  oppo_debug("function ag_init entry\n");
  if (ag_fb>0) return 0;
  
  //-- Open Framebuffer
  ag_fb = open(OPPO_FRAMEBUFFER, O_RDWR, 0);
  
  if (ag_fb>0){
    //-- Init Info from IO
    ioctl(ag_fb, FBIOGET_FSCREENINFO, &ag_fbf);
    ioctl(ag_fb, FBIOGET_VSCREENINFO, &ag_fbv);
    
    //-- Init 32 Buffer
    ag_canvas(&ag_c,ag_fbv.xres,ag_fbv.yres);
    ag_canvas(&cfresh,ag_fbv.xres,ag_fbv.yres);        
    ag_dp = floor( min(ag_fbv.xres,ag_fbv.yres) / 160);
    oppo_debug("ag_dp=%d, ag_fbf.smem_len=%u, ag_fbf.line_length=%u\n", ag_dp, ag_fbf.smem_len, ag_fbf.line_length);
    
    //-- Init Frame Buffer Size
    agclp    = (ag_fbv.bits_per_pixel>>3);
    ag_fbsz  = (ag_fbv.xres * ag_fbv.yres * ((agclp==3)?4:agclp));
    sz_fb_kernel = ag_fbf.line_length* ag_fbv.yres;

    if (ag_fbf.smem_len >= 2*sz_fb_kernel)   use_couple_fb=1;
	
    //-- Init Frame Buffer
    oppo_debug("ag_fbv.bits_per_pixel = %d agclp=%d, sz_fb_kernel=%u\n", ag_fbv.bits_per_pixel, agclp, sz_fb_kernel);
    oppo_debug("ag_fbv.red.offset=%d, ag_fbv.green.offset=%d, ag_fbv.blue.offset=%d\n",
		ag_fbv.red.offset, ag_fbv.green.offset, ag_fbv.blue.offset);
	
    if (ag_fbv.bits_per_pixel==16){
     /*RGB565*/
      ag_fbv.red.offset        = 11;
      ag_fbv.red.length        = 5;
      ag_fbv.green.offset      = 5;
      ag_fbv.green.length      = 6;
      ag_fbv.blue.offset       = 0;
      ag_fbv.blue.length       = 5;
      ag_fbv.transp.offset     = 0;
      ag_fbv.transp.length     = 0;
      if (ioctl(ag_fb, FBIOPUT_VSCREENINFO, &ag_fbv) < 0)
      {
          perror("failed to put fb0 info");
          close(ag_fb);
          return -1;
      }
      ag_32   = 0;
      ag_fbuf = (word*) mmap(0,ag_fbf.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,ag_fb,0);
      ag_b    = (word*) malloc(ag_fbsz);
      
      //-- Resolution with Stride
      ag_16strd = 0;
      ag_16w    = ag_fbf.line_length/2;
      if (ag_16w!=ag_fbv.xres){
        if (ag_16w/2==ag_fbv.xres){
          ag_16strd = 0;
          ag_16w    = ag_fbv.xres;
        }
        else{
          ag_16strd=1;
        }
      }
      
      if (ag_16strd==0){
        //-- Can Use memcpy
        memcpy(ag_b,ag_fbuf,ag_fbsz);
        memcpy(ag_c.data,ag_fbuf,ag_fbsz);
      }
      else{
        //-- Should Bit per bit
        unsigned int x,y;
        for (y=0;y<ag_fbv.yres;y++){
          int yp = y * ag_fbv.xres;
          int yd = (ag_16w*y);
          for (x=0;x<ag_fbv.xres;x++){
            int xy = yp+x;
            int dxy= yd+x;
            ag_b[xy] = ag_fbuf[dxy];
            ag_setpixel(&ag_c,x,y,ag_b[xy]);
          }
        }
      }
      
      
    }
    else{
      ag_32     = 1;
      
      //-- Memory Allocation
      ag_fbuf32 = (byte*) mmap(0,ag_fbf.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,ag_fb,0);
      ag_bf32   = (dword*) malloc(ag_fbsz);
      memset(ag_bf32,0,ag_fbsz);
      ag_blank(NULL); //-- 32bit Use Blank

      unsigned int x,y;
      for (y=0;y<ag_fbv.yres;y++){
        int yp = y * ag_fbv.xres;
        int yd = (ag_fbf.line_length*y);
        for (x=0;x<ag_fbv.xres;x++){
          int xy = yp+x;
          int dxy= yd+(x*agclp);
          ag_bf32[xy] = ag_rgb32(
            ag_fbuf32[dxy+(ag_fbv.red.offset>>3)],
            ag_fbuf32[dxy+(ag_fbv.green.offset>>3)],
            ag_fbuf32[dxy+(ag_fbv.blue.offset>>3)]);
          ag_setpixel(&ag_c,x,y,ag_rgbto16(ag_bf32[xy]));
        }
      }
    }
    oppo_debug("ag_32=%d, ag_16strd=%d\n", ag_32, ag_16strd);

    gr_fb_blank(1);
    gr_fb_blank(0);
    write_char("/sys/class/leds/lcd-backlight/brightness", "102");	
	  
    //-- Refresh Draw Lock Thread
    ag_isrun = 1;
    pthread_create(&status_pthread, NULL, status_thread, NULL); 
    pthread_create(&refresh_pthread, NULL, refresh_thread, NULL);     
    //-- Init FreeType
    LOGS("Opening Freetype\n");
    aft_open();
    
    return 1;
  }
  oppo_debug("open framebuffer failed\n");
  return 0;
}
#endif
void ag_close_thread(){
  oppo_debug("in...\n");	
  ag_isrun=0;
  pthread_mutex_lock(&refresh_mutex);
  need_refresh = 1;
  pthread_cond_signal(&refresh_cond);
  pthread_mutex_unlock(&refresh_mutex);
  pthread_join(status_pthread,NULL);
  pthread_join(refresh_pthread,NULL); 
  oppo_debug("exit...\n");
}

//-- RELEASE AMARULLZ GRAPHIC
void ag_close(){
  if (ag_fbv.bits_per_pixel!=16){
    if (ag_bf32!=NULL) free(ag_bf32);
    if (ag_fbuf32!=NULL) munmap(ag_fbuf32,ag_fbsz);
  }
  else if (ag_fbv.bits_per_pixel==16){
    if (ag_b!=NULL) free(ag_b);
    if (ag_fbuf!=NULL) munmap(ag_fbuf,ag_fbsz);
  }
  
  //-- Cleanup Canvas & FrameBuffer
  ag_ccanvas(&ag_c);
  ag_ccanvas(&cfresh);  
  close(ag_fb);
  ag_fb = 0;
  
  //-- Cleanup Freetype
  LOGS("Closing Freetype\n");
  aft_close();
}

//-- Draw Main Canvas Into FrameBuffer
byte ag_isbusy  = 0;
byte ag_refreshlock=0;
int  ag_busypos = 0;
int  ag_busywinW= 0;
long ag_lastbusy= 0;

void ag_setbusy(){
  if (ag_isbusy==0){
    ag_isbusy   = 1;
    ag_lastbusy = alib_tick();
  } 
}
void ag_busyprogress(){
  ag_busypos--; //=agdp();
  if (ag_busypos<0) ag_busypos=ag_busywinW;
  int bs_x = (agw()/2) - (ag_busywinW/2);
  int bs_y = (agh()/2) + ag_fontheight(0) - (agdp()*2);
  int bs_h = agdp()*2;
  int bs_w = ag_busywinW;
  int bs_w2= bs_w/2;
  int x,y;
  if (ag_32==1){
    if (agclp==4){      
      for (x=bs_x;x<bs_x+bs_w;x++){
        if ((x+ag_busypos)%(bs_h*2)<bs_h){
          int i=x-bs_x;
          int alp;
          if (i<bs_w2)
            alp = ((i*255)/bs_w2);
          else
            alp = (((bs_w-i)*255)/bs_w2);
          alp=min(alp,255);
          for (y=bs_y;y<bs_y+bs_h;y++){
            int yp = y * ag_fbv.xres;
            int xy  = yp+x;
            int dxy = (ag_fbf.line_length*y)+(x*agclp);
            
            *((dword*) (ag_fbuf32+dxy)) =
              (alp << ag_fbv.red.offset)|
              (alp << ag_fbv.green.offset)|
              (alp << ag_fbv.blue.offset);
          }
        }
      }
    }
    else{
      for (x=bs_x;x<bs_x+bs_w;x++){
        if ((x+ag_busypos)%(bs_h*2)<bs_h){
          int i=x-bs_x;
          int alp;
          if (i<bs_w2)
            alp = ((i*255)/bs_w2);
          else
            alp = (((bs_w-i)*255)/bs_w2);
          alp=min(alp,255);
          for (y=bs_y;y<bs_y+bs_h;y++){
            int yp = y * ag_fbv.xres;
            int xy  = yp+x;
            int dxy = (ag_fbf.line_length*y)+(x*agclp);
            ag_fbuf32[dxy+(ag_fbv.red.offset>>3)]  =alp;
            ag_fbuf32[dxy+(ag_fbv.green.offset>>3)]=alp;
            ag_fbuf32[dxy+(ag_fbv.blue.offset>>3)] =alp;
          }
        }
      }
    }
  }
  else{
    for (x=bs_x;x<bs_x+bs_w;x++){
      if ((x+ag_busypos)%(bs_h*2)<bs_h){
        int i=x-bs_x;
        int alp;
        if (i<bs_w2)
          alp = ((i*255)/bs_w2);
        else
          alp = (((bs_w-i)*255)/bs_w2);
        alp=min(alp,255);
    
        for (y=bs_y;y<bs_y+bs_h;y++){
          int yp = y * ag_16w;
          int xy  = yp+x;
          ag_fbuf[xy]=ag_rgb(alp,alp,alp);
        }
      }
    }
  }
}

void ag32fbufcopy(color *data, int active_fb){
  unsigned int x,y;
  byte * pfb;
  pfb = ag_fbuf32 + active_fb*sz_fb_kernel;
  	
  if (agclp==4){
    for (y=0;y<ag_fbv.yres;y++){
      int yp = y * ag_fbv.xres;
      int yd = (ag_fbf.line_length*y);
      for (x=0;x<ag_fbv.xres;x++){
        int xy = yp+x;
        color c = data[xy];
#ifdef RGB888_OFFSET_SPECIAL		
	 *((dword*) (pfb+yd+(x*agclp))) =
            (ag_r(c) << (24 - ag_fbv.red.offset))|
            (ag_g(c)<< (24 - ag_fbv.green.offset))|
            (ag_b(c) << (24 -ag_fbv.blue.offset));
#else
	 *((dword*) (pfb+yd+(x*agclp))) =
            (ag_r(c) << (ag_fbv.red.offset))|
            (ag_g(c)<< (ag_fbv.green.offset))|
            (ag_b(c) << (ag_fbv.blue.offset));
#endif
      }
    }
  }
  else{
    for (y=0;y<ag_fbv.yres;y++){
      int yp = y * ag_fbv.xres;
      int yd = (ag_fbf.line_length*y);
      for (x=0;x<ag_fbv.xres;x++){
        int xy = yp+x;
        int dxy= yd+(x*agclp);
	 color c = data[xy];
        pfb[dxy+(ag_fbv.red.offset>>3)]   = ag_r(c);
        pfb[dxy+(ag_fbv.green.offset>>3)] = ag_g(c);
        pfb[dxy+(ag_fbv.blue.offset>>3)]  = ag_b(c);
      }
    }
  }
}

void ag16fbufcopy(word * bfbz, int active_fb){
  unsigned int x,y;
  word * pfb;
  pfb = (word*)((byte *)ag_fbuf + active_fb*sz_fb_kernel);
  for (y=0;y<ag_fbv.yres;y++){
    int yp = y * ag_fbv.xres;
    int yd = (ag_16w*y);
    for (x=0;x<ag_fbv.xres;x++){
      int xy = yp+x;
      int dxy= yd+x;
      pfb[dxy]=bfbz[xy];
    }
  }
}

static int start_fresh = 0;
 
extern void oppo_set_title(CANVAS *cdst);
static void *refresh_thread(void *cookie) {

  static int nm_active_fb = 0;	
  oppo_debug("in\n");
  
  while(ag_isrun) {
    pthread_mutex_lock(&refresh_mutex);
    while (need_refresh == 0){
      pthread_cond_wait(&refresh_cond, &refresh_mutex);
    }
	
    if (ag_isrun == 0) {
        pthread_mutex_unlock(&refresh_mutex);	
        break;
    }

    start_fresh = 1;
    need_refresh = 0;

    oppo_set_title(&cfresh);

    if (use_couple_fb) {
        nm_active_fb = (nm_active_fb + 1) & 1;
    }
	
    //oppo_debug("nm_active_fb=%d, ag_fbf.line_length=%d, ag_fbv.xres=%d, ag_fbv.yres=%d, agclp=%d, ag_fbv.yres_virtual=%d, sz_fb_kernel=%u\n", 
		//nm_active_fb, ag_fbf.line_length, ag_fbv.xres, ag_fbv.yres, agclp, ag_fbv.yres_virtual, sz_fb_kernel);
		
    if (ag_32==1){
  	ag32fbufcopy(cfresh.data, nm_active_fb);
    }
    else{
      
      if (ag_16strd==0){
	 byte *pfb = (byte *)ag_fbuf + nm_active_fb*sz_fb_kernel;   	
        memcpy(pfb,cfresh.data,ag_fbsz);
      }
      else{
	 memcpy(ag_b,cfresh.data,ag_fbsz); 	
        ag16fbufcopy(ag_b, nm_active_fb);
      }	
    }

    pthread_mutex_unlock(&refresh_mutex);

#ifdef XIAOMI_M2
    int enable = 1;
    ioctl(ag_fb, MSMFB_OVERLAY_VSYNC_CTRL, &enable);
#endif	
    //-- Force Refresh Display
    ag_fbv.yoffset = nm_active_fb * ag_fbv.yres;
    ag_fbv.activate |= FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
    ioctl(ag_fb, FBIOPUT_VSCREENINFO, &ag_fbv);  
    //oppo_debug("end\n");	
  
  }
  
  oppo_debug("exit\n");
  return NULL;
   
}

void ag_sync(){
	
  pthread_mutex_lock(&refresh_mutex);
  memcpy(cfresh.data, ag_c.data, cfresh.sz);
  need_refresh = 1;
  pthread_cond_signal(&refresh_cond);
  pthread_mutex_unlock(&refresh_mutex);

}

static void status_refresh() {
 	
  pthread_mutex_lock(&refresh_mutex);
  need_refresh = 1;
  pthread_cond_signal(&refresh_cond);
  pthread_mutex_unlock(&refresh_mutex);
 
}
	
extern int oppo_is_ac_usb_online();
extern int cnt_capacity_chg;
static void *status_thread(void *cookie){
  oppo_debug("start...\n");
  while (ag_isrun) {

    if (!start_fresh) {
      usleep(500*1000);
      continue;
    }
	
    if (oppo_is_ac_usb_online()) {
       cnt_capacity_chg++;
    }	else {
       cnt_capacity_chg = 0;
    }
    status_refresh();	
    usleep(500*1000);
  }
  oppo_debug("end...\n");
  return NULL;
}

void ag_sync_fade(int frame){
 ag_sync();
}

byte ag_blur_h(CANVAS * d,CANVAS * s, int radius){
  if (radius<1) return 0;
  if (s==NULL) return 0;
  if (d==NULL) d=&ag_c;
  
  int x, y, k;
  int rad=radius*2;
  int radd=rad+1;
  for (y=0;y<s->h;y++){
    dword r = 0; dword g = 0; dword b = 0;    
    for (k=0;(k<=radius)&&(k<s->w);k++){
      color * cl = agxy(s,k,y);
      if (cl!=NULL){
        r += ag_r(cl[0]); g += ag_g(cl[0]); b += ag_b(cl[0]);
      }
    }
    
    //-- Dither Engine
    float vr = r/radd;
    float vg = g/radd;
    float vb = b/radd;
    byte  nr = ag_close_r(round(vr));
    byte  ng = ag_close_g(round(vg));
    byte  nb = ag_close_b(round(vb));
    float er = vr-nr;
    float eg = vg-ng;
    float eb = vb-nb;
    
    //-- Save
    ag_setpixel(d,0,y,ag_rgb(nr,ng,nb));
    
    
    for (x=1;x<s->w;x++){
      if (x>radius){
        color * cl = agxy(s,x-radius-1,y);
        r -= ag_r(cl[0]); g -= ag_g(cl[0]); b -= ag_b(cl[0]);
      }
      if (x<s->w-(radius+1)){
        color * cl = agxy(s,x+radius,y);
        r += ag_r(cl[0]); g += ag_g(cl[0]); b += ag_b(cl[0]);
      }
      
      //-- Dither Engine
      vr = min((r/radd)+er,255);
      vg = min((g/radd)+eg,255);
      vb = min((b/radd)+eb,255);
      nr = ag_close_r(round(vr));
      ng = ag_close_g(round(vg));
      nb = ag_close_b(round(vb));
      er = vr-nr;
      eg = vg-ng;
      eb = vb-nb;
      
      //-- Save
      ag_setpixel(d,x,y,ag_rgb(nr,ng,nb));
    }
  }
  return 1;
}
byte ag_blur_v(CANVAS * d,CANVAS * s, int radius){
  if (radius<1) return 0;
  if (s==NULL) return 0;
  if (d==NULL) d=&ag_c;
  
  int x, y, k;
  int rad=radius*2;
  int radd=rad+1;
  
  for (x=0;x<s->w;x++){
    dword r = 0; dword g = 0; dword b = 0;
    for (k=0;(k<=radius)&&(k<s->h);k++){
      color * cl = agxy(s,x,k);
      if (cl!=NULL){
        r += ag_r(cl[0]); g += ag_g(cl[0]); b += ag_b(cl[0]);
      }
    }
    
    //-- Dither Engine
    float vr = r/radd;
    float vg = g/radd;
    float vb = b/radd;
    byte  nr = ag_close_r(round(vr));
    byte  ng = ag_close_g(round(vg));
    byte  nb = ag_close_b(round(vb));
    float er = vr-nr;
    float eg = vg-ng;
    float eb = vb-nb;
    //-- Save
    ag_setpixel(d,x,0,ag_rgb(nr,ng,nb));
    
    for (y=1;y<s->h;y++){
      if (y>radius){
        color * cl = agxy(s,x,y-radius-1);
        r -= ag_r(cl[0]); g -= ag_g(cl[0]); b -= ag_b(cl[0]);
      }
      if (y<s->h-(radius+1)){
        color * cl = agxy(s,x,y+radius);
        r += ag_r(cl[0]); g += ag_g(cl[0]); b += ag_b(cl[0]);
      }
      
      //-- Dither Engine
      vr = min((r/radd)+er,255);
      vg = min((g/radd)+eg,255);
      vb = min((b/radd)+eb,255);
      nr = ag_close_r(round(vr));
      ng = ag_close_g(round(vg));
      nb = ag_close_b(round(vb));
      er = vr-nr;
      eg = vg-ng;
      eb = vb-nb;
      
      //--Save
      ag_setpixel(d,x,y,ag_rgb(nr,ng,nb));
    }
  }
  return 1;
}
byte ag_blur(CANVAS * d,CANVAS * s, int radius){
  if (radius<1) return 0;
  CANVAS tmp;
  ag_canvas(&tmp,s->w,s->h);
  ag_blur_h(&tmp,s,radius);
  ag_blur_v(d,&tmp,radius);
  ag_ccanvas(&tmp);
  return 1;
}
//-- CREATE CANVAS
void ag_canvas(CANVAS * c,int w,int h){
  c->w      = w;
  c->h      = h;
  c->sz     = (w*h*2);
  c->data   = (color *) malloc(c->sz);
  memset(c->data,0,c->sz);
}

//-- RELEASE CANVAS
void ag_ccanvas(CANVAS * c){
  if (c->data) free(c->data);
  c->data=NULL;
}

//-- Get Main Canvas
CANVAS * agc(){
  return &ag_c;
}

//-- Clear Canvas
void ag_blank(CANVAS * c){
  if (c==NULL) c=&ag_c;
  memset(c->data,0,c->sz);
}

//-- Width
int agw(){
  return ag_fbv.xres;
}

//-- Height
int agh(){
  return ag_fbv.yres;
}

int agdp(){
  return ag_dp;
}
void set_agdp(int dp){
  ag_dp=dp;
  oppo_debug("ag_dp=%d\n", ag_dp);
}

//-- Convert String to Color
color strtocolor(char * c){
  if (c[0]!='#') return 0;
  char out[9]={'0','x'};
  int  i;
  if (strlen(c)==7){
    for (i=1;i<7;i++){
      out[i+1]=c[i];
    }
  }
  else if (strlen(c)==4){
    for (i=0;i<3;i++){
      out[(i*2)+2]=c[i+1];
      out[(i*2)+3]=c[i+1];
    }
  }
  else
    return 0;
  out[8]=0;
  dword ul = strtoul(out,NULL,0);

  return ag_rgb(ag_r32(ul),ag_g32(ul),ag_b32(ul));
}

//-- Draw Canvas To Canvas Extra
byte ag_draw_ex(CANVAS * dc,CANVAS * sc, int dx, int dy, int sx, int sy, int sw, int sh){
  if (sc==NULL) return 0;
  if (dc==NULL) dc=&ag_c;
  if (dx>=dc->w) return 0;
  if (dy>=dc->h) return 0;
  if (sx<0){
    dx+=abs(sx);
    sw-=abs(sx);
    sx=0;
  }
  if (sy<0){
    dy+=abs(sy);
    sh-=abs(sy);
    sy=0;
  }
  if (sw+sx>=sc->w) sw-=(sw+sx) - sc->w;
  if (sh+sy>=sc->h) sh-=(sh+sy) - sc->h;
  if ((sw<=0)||(sh<=0)) return 0;
  int sr_w = sw;
  int sr_h = sh;
  int sr_x = sx;
  int sr_y = sy;
  int ds_x = dx;
  int ds_y = dy;
  if (dx<0){
    int ndx = abs(dx);
    sr_x+= abs(ndx);
    sr_w-= ndx;
    ds_x = 0;
  }
  if (dy<0){
    int ndy = abs(dy);
    sr_y+= ndy;
    sr_h-= ndy;
    ds_y = 0;
  }
  if (sr_w+dx>dc->w) sr_w-=(sr_w+dx) - dc->w;
  if (sr_h+dy>dc->h) sr_h-=(sr_h+dy) - dc->h;
  int y;
  int pos_sr_x = sr_x*2;
  int pos_ds_x = ds_x*2;
  int pos_sc_w = sc->w*2;
  int pos_dc_w = dc->w*2;
  int copy_sz  = sr_w*2;
  byte * src   = ((byte *) sc->data);
  byte * dst   = ((byte *) dc->data);
  for (y=0;y<sr_h;y++){
    memcpy(
      dst + ((ds_y+y)*pos_dc_w)+pos_ds_x,
      src + ((sr_y+y)*pos_sc_w)+pos_sr_x,
      copy_sz
    );
  }
  return 1;
}

//-- Draw Canvas To Canvas
byte ag_draw(CANVAS * dc,CANVAS * sc,int dx, int dy){
  if (sc==NULL) return 0;
  return ag_draw_ex(dc,sc,dx,dy,0,0,sc->w,sc->h);
}

//-- Pixel
color * agxy(CANVAS *_b, int x, int y){
  if (_b==NULL) _b=&ag_c;
  if ((x<0)||(y<0)) return NULL;
  if ((x>=_b->w)||(y>=_b->h)) return NULL;
  return _b->data + ((y * _b->w) + x);
}

//-- SetPixel
byte ag_setpixel(CANVAS *_b,int x, int y,color cl){
  color * c = agxy(_b,x,y);
  if (c==NULL) return 0;
  c[0]=cl;
  return 1;
}

byte ag_spixel(CANVAS *_b,float x, float y, color cl){
  if (_b==NULL) _b=&ag_c;
  int fx=floor(x);
  int fy=floor(y);
  float ax=x-fx;
  float ay=y-fy;
  float sz=ax+ay;
  if (sz==0)
    return ag_setpixel(_b,fx,fy,cl);
  ag_subpixel(_b, fx    ,fy,   cl,  (byte) ((((1-ax)+(1-ay))  * 255) / 4));
  ag_subpixel(_b, fx+1  ,fy,   cl,  (byte) (((ax+(1-ay))      * 255) / 4));
  ag_subpixel(_b, fx    ,fy+1, cl,  (byte) ((((1-ax)+ay)      * 255) / 4));
  ag_subpixel(_b, fx+1  ,fy+1, cl,  (byte) (((ax+ay)          * 255) / 4));
  return 0;
}

//-- SubPixel
byte ag_subpixel(CANVAS *_b,int x, int y, color cl,byte l){
  if (_b==NULL) _b=&ag_c;
  if (l>=255) return ag_setpixel(_b,x,y,cl);
  if (l<=0) return 1;
  
  color * c = agxy(_b,x,y);
  if (c==NULL) return 0;
  c[0]   =  ag_calculatealpha(c[0],cl,l);
  return 1;
}

//-- SubPixelGet
color ag_subpixelget(CANVAS *_b,int x, int y, color cl,byte l){
  if (_b==NULL) _b=&ag_c;
  if (l>=255) return cl;
  color * c = agxy(_b,x,y);
  if (c==NULL) return 0;
  return ag_calculatealpha(c[0],cl,l);
}
//-- SubPixelGet32
dword ag_subpixelget32(CANVAS *_b,int x, int y, dword cl,byte l){
  if (_b==NULL) _b=&ag_c;
  if (l>=255) return cl;
  color * c = agxy(_b,x,y);
  if (c==NULL) return 0;
    
  return ag_calculatealpha16to32(c[0],cl,l);
}
//-- Draw Rectangle
byte ag_rect(CANVAS *_b,int x, int y, int w, int h, color cl){
  if (_b==NULL) _b=&ag_c;
    
  //-- FIXING
  int x2 = x+w; if (x2>_b->w) x2=_b->w; 
  int y2 = y+h; if (y2>_b->h) y2=_b->h; 
  if (x<0) x=0; if (y<0) y=0;
  w=x2-x; h=y2-y;
  
  //-- LOOPS
  int xx, yy;
  for (yy=y;yy<y2;yy++){
    int i = yy * _b->w;
    for (xx=x;xx<x2;xx++){
      _b->data[i + xx] = cl;
    }
  }
  
  return 1;
}
//-- Draw Rectangle
byte ag_rectopa(CANVAS *_b,int x, int y, int w, int h, color cl,byte l){
  if (_b==NULL) _b=&ag_c;
    
  //-- FIXING
  int x2 = x+w; if (x2>_b->w) x2=_b->w; 
  int y2 = y+h; if (y2>_b->h) y2=_b->h; 
  if (x<0) x=0; if (y<0) y=0;
  w=x2-x; h=y2-y;
  
  byte ll = 255-l;
  int sr  = ag_r(cl);
  int sg  = ag_g(cl);
  int sb  = ag_b(cl);
  
  //-- LOOPS
  int xx, yy;
  for (yy=y;yy<y2;yy++){
    byte er= 0;
    byte eg= 0;
    byte eb= 0;
    for (xx=x;xx<x2;xx++){
      color * cv = agxy(_b,xx,yy);
      if (cv[0]!=cl){
        byte  ralpha = 255 - l;
        byte r = min(((byte) (((((int) ag_r(cv[0])) * ll) + (sr*l)) >> 8))+er,255);
        byte g = min(((byte) (((((int) ag_g(cv[0])) * ll) + (sg*l)) >> 8))+eg,255);
        byte b = min(((byte) (((((int) ag_b(cv[0])) * ll) + (sb*l)) >> 8))+eb,255);
        byte nr= ag_close_r(r);
        byte ng= ag_close_g(g);
        byte nb= ag_close_b(b);
        er     = r-nr;
        eg     = g-ng;
        eb     = b-nb;
        cv[0]  = ag_rgb(nr,ng,nb);
      }else{
        er= 0;
        eg= 0;
        eb= 0;
      }
    }
  }
  
  return 1;
}
//-- Draw Rounded Gradient Rectangle
#define ag_rndsave(a,b,c) a=min( a+((byte) (((b+c)  * 255) / 4)) , 255)
byte ag_roundgrad(CANVAS *_b,int x, int y, int w, int h, color cl1, color cl2, int roundsz){
  return ag_roundgrad_ex(_b,x,y,w,h,cl1,cl2,roundsz,1,1,1,1);
}
byte ag_roundgrad_ex(CANVAS *_b,int x, int y, int w, int h, color cl1, color cl2, int roundsz, byte tlr, byte trr, byte blr, byte brr){
  if (_b==NULL) _b=&ag_c;
  if ((tlr==2)||(trr==2)||(blr==2)||(brr==2)){
    if (tlr==2) tlr==1;
    if (trr==2) trr==1;
    if (blr==2) blr==1;
    if (brr==2) brr==1;
  }
  else{
    if (roundsz>h/2) roundsz=h/2;
    if (roundsz>w/2) roundsz=w/2;
  }
  
  if (roundsz<0) roundsz=0;

  //-- ANTIALIAS ROUNDED
  int rndsz;
  byte * rndata;
  if (roundsz>0){
    rndsz  = roundsz*roundsz;
    rndata = malloc(rndsz);
    memset(rndata,0,rndsz);
    float inc = 180;
    float incz= 40/roundsz;
    if (roundsz>40) incz=1;
    while (inc<=270){
      float rd  = (inc * M_PI / 180);
      float xp  = roundsz+(sin(rd)*roundsz); // X Axis
      float yp  = roundsz+(cos(rd)*roundsz); // Y Axis
      int fx    = floor(xp);
      int fy    = floor(yp);
      float ax  = xp-fx;
      float ay  = yp-fy;
      float sz  = ax+ay;
      if ((fx>=0)&&(fy>=0)&&(fx<roundsz)&&(fy<roundsz)){
        ag_rndsave(rndata[fx+fy*roundsz],1-ax,1-ay);
        if (fx<roundsz-1) ag_rndsave(rndata[fx+1+fy*roundsz],ax,1-ay);
        if (fy<roundsz-1) ag_rndsave(rndata[fx+(1+fy)*roundsz],1-ax,ay);
        if ((fx<roundsz-1)&&(fy<roundsz-1)) ag_rndsave(rndata[(fx+1)+(1+fy)*roundsz],ax,ay);
      }
      inc += incz;
    }
    int rndx, rndy;
    for (rndy=0;rndy<roundsz;rndy++){
      byte alpy=0;
      byte alpf=0;
      for (rndx=0;rndx<roundsz;rndx++){
        byte alpx=rndata[rndx+rndy*roundsz];
        if ((alpy<alpx)&&(!alpf)) alpy=alpx;
        else if (alpf||(alpy>alpx)){
          alpf=1;
          rndata[rndx+rndy*roundsz]=255;
        }
      }
    }
  }
  
  //-- FIXING
  int x2 = x+w;
  int y2 = y+h;
  /*int x2 = x+w; if (x2>_b->w) x2=_b->w; 
  int y2 = y+h; if (y2>_b->h) y2=_b->h; 
  if (x<0) x=0; if (y<0) y=0;
  w=x2-x; h=y2-y;*/
  
  //-- QUARTZ ERRORS BUFFER
  int xx,yy;
  int qz      = w * h * 3;
  byte * qe   = (byte*) malloc(qz);
  memset(qe,0,qz);
  
  //-- LOOPS
  for (yy=y;yy<y2;yy++){
    //-- Vertical Pos
    int z   = yy * _b->w;
    //int zq  = (yy-y) * w;
    
    //-- Calculate Row Color
    byte falpha = (byte) min((((float) 255/h) * (yy-y)),255);
    dword linecolor = ag_calculatealphaTo32(cl1,cl2,falpha);
    byte r = ag_r32(linecolor);
    byte g = ag_g32(linecolor);
    byte b = ag_b32(linecolor);
    
    for (xx=x;xx<x2;xx++){
      int qx = (((yy-y)) * w + (xx-x)) * 3;
      //int xy = z+xx;
      
      color * dx = agxy(_b,xx,yy);
      if (dx!=NULL){
        int absy = yy-y;
        dword curpix=ag_rgb32(r,g,b);
        if (roundsz>0){
          // tlr, trr, blr, brr //
          if ((tlr)&&(xx-x<roundsz)&&(absy<roundsz)){
            int absx = xx-x;
            curpix=ag_subpixelget32(_b,xx,yy,curpix,rndata[absy*roundsz+absx]);
          }
          else if ((trr)&&(xx>=(w+x)-roundsz)&&(absy<roundsz)){
            int absx = roundsz-((xx+roundsz)-(x+w))-1;
            curpix=ag_subpixelget32(_b,xx,yy,curpix,rndata[absy*roundsz+absx]);
          }
          else if ((blr)&&(xx-x<roundsz)&&(yy>=(h+y)-roundsz)){
            int absx = xx-x;
            int abyy = roundsz-((yy+roundsz)-(y+h))-1;
            curpix=ag_subpixelget32(_b,xx,yy,curpix,rndata[abyy*roundsz+absx]);
          }
          else if ((brr)&&(xx>=(w+x)-roundsz)&&(yy>=(h+y)-roundsz)){
            int absx = roundsz-((xx+roundsz)-(x+w))-1;
            int abyy = roundsz-((yy+roundsz)-(y+h))-1;
            curpix=ag_subpixelget32(_b,xx,yy,curpix,rndata[abyy*roundsz+absx]);
          }
        }
        
        //-- Amarullz Dithering
        byte old_r = (byte) min(((int) ag_r32(curpix)) + ((int) qe[qx]),  255);
        byte old_g = (byte) min(((int) ag_g32(curpix)) + ((int) qe[qx+1]),255);
        byte old_b = (byte) min(((int) ag_b32(curpix)) + ((int) qe[qx+2]),255);
        byte new_r = ag_close_r(old_r);
        byte new_g = ag_close_g(old_g);
        byte new_b = ag_close_b(old_b);
        byte err_r = old_r - new_r;
        byte err_g = old_g - new_g;
        byte err_b = old_b - new_b;
        
        if (xx-x<w-1) qe[qx+4] += err_g; // Save Green QE
        if (yy-y<h-1){
          qx = ((yy-y+1) * w + (xx-x)) * 3;
          qe[qx] += err_r; // Save Red QE
          if (xx-x<w-1) qe[qx+5] += err_b; // Save Blue QE
        }
      
        dx[0] = ag_rgb( ((byte) new_r), ((byte) new_g), ((byte) new_b) );
        // _b->data[xy] = ag_rgb( ((byte) new_r), ((byte) new_g), ((byte) new_b) );
      }
    }
  }
  if (roundsz>0) free (rndata);
  free (qe);
  return 1;
}

/******************************[ FONT FUNCTIONS ]******************************/
//-- Load Small Font
/* DRAW LIST BULLET */
byte ag_fontready(byte isbig){
  if (ag_font_onload) return 0;
  byte isfreetype=isbig?AG_BIG_FONT_FT:AG_SMALL_FONT_FT;
  if (isfreetype) return aft_fontready(isbig);
  PNGFONTS * fnt = isbig?&AG_BIG_FONT:&AG_SMALL_FONT;
  return fnt->loaded;
}
int ag_bulletwidth(byte isbig){
  if (!ag_fontready(isbig)) return 0;
  float h=(float) ag_fontheight(isbig);
  int s=ceil(h/2.5);
  if (s%2!=0) s--;
  if (s==0) s=2;
  return s;
}
void ag_draw_bullet(CANVAS * _b,int x,int y,color cl,byte isbig, byte type){
  if (!ag_fontready(isbig)) return;
  int h = ag_fontheight(isbig);
  int w = ag_bulletwidth(isbig);
  int s = min(h,w);
  int vx= ceil(((float) (w-s))/2);
  int vy= ceil(((float) (h-s))/2);
  ag_roundgrad(_b,vx+x,vy+y,s,s,cl,cl,(type%2==0)?0:s);
}
byte ag_loadsmallfont(char * fontname, byte is_freetype, char * relativeto){
  while (ag_oncopybusy) usleep(50);
  ag_font_onload=1;
  byte r=0;
  if ((is_freetype!=0)&&(relativeto!=NULL)){
    AG_SMALL_FONT_FT = 0;
    r=aft_load(fontname, is_freetype+1, 0,relativeto);
    if (r) AG_SMALL_FONT_FT = 1;
  }
  else{
    apng_closefont(&AG_SMALL_FONT);
    r=apng_loadfont(&AG_SMALL_FONT,fontname);
    if (r) AG_SMALL_FONT_FT = 0;
  }
  ag_font_onload=0;
  return r;
}
//-- Load Big Font
byte ag_loadbigfont(char * fontname, byte is_freetype, char * relativeto){
  while (ag_oncopybusy) usleep(50);
  ag_font_onload=1;
  byte r=0;
  if ((is_freetype!=0)&&(relativeto!=NULL)){
    AG_BIG_FONT_FT = 0;
    r=aft_load(fontname, is_freetype+1, 1,relativeto);
    if (r) AG_BIG_FONT_FT = 1;
  }
  else{
    apng_closefont(&AG_BIG_FONT);
    r=apng_loadfont(&AG_BIG_FONT,fontname);
    if (r) AG_BIG_FONT_FT = 0;
  }
  ag_font_onload=0;
  return r;
}
void ag_closefonts(){
  apng_closefont(&AG_BIG_FONT);
  apng_closefont(&AG_SMALL_FONT);
}

//-- Draw Character
byte ag_drawchar_ex(CANVAS *_b,int x, int y, int c, color cl, byte isbig, byte underline, byte bold){
  if (!ag_fontready(isbig)) return 0;

  if (_b==NULL) _b=&ag_c;
  byte isfreetype = isbig?AG_BIG_FONT_FT:AG_SMALL_FONT_FT;
  if (isfreetype){
    return aft_drawfont(_b,isbig,c,x,y,cl,underline,bold);
  }
  
  int yy,xx;
  y++;
  int cd = ((int) c)-32;
  if (cd<0) return 0;
  if (cd==137) cd = 95;  
  if (cd>95) return 0;
  PNGFONTS * fnt = isbig?&AG_BIG_FONT:&AG_SMALL_FONT;
  return apng_drawfont(_b,fnt,cd,x,y,cl,underline,bold);
}
byte ag_drawchar(CANVAS *_b,int x, int y, int c, color cl, byte isbig){
  return ag_drawchar_ex(_b,x, y, c, cl, isbig,0,0);
}
//-- Calculate Font Width
byte ag_fontwidth(int c,byte isbig){
  if (!ag_fontready(isbig)) return 0;
  byte isfreetype = isbig?AG_BIG_FONT_FT:AG_SMALL_FONT_FT;
  if (isfreetype){
    return aft_fontwidth(c,isbig);
  }
  
  PNGFONTS * fnt = isbig?&AG_BIG_FONT:&AG_SMALL_FONT;
  int cd = ((int) c)-32;
  if (cd<0) return 0;
  if (cd==137) cd = 95;  
  if (cd>95) return 0;
  return fnt->fw[cd];
}
int ag_fontwidth_kerning(int c,int p, byte isbig){
  if (!ag_fontready(isbig)) return 0;
  byte isfreetype = isbig?AG_BIG_FONT_FT:AG_SMALL_FONT_FT;
  if (isfreetype){
    return aft_fontwidth(c,isbig)+aft_kern(c,p,isbig);
  }
  return ag_fontwidth(c,isbig);
}
byte ag_isfreetype(byte isbig){
  return (isbig?AG_BIG_FONT_FT:AG_SMALL_FONT_FT);
}
int ag_tabwidth(int x, byte isbig){
  if (!ag_fontready(isbig)) return 0;
  byte isfreetype = isbig?AG_BIG_FONT_FT:AG_SMALL_FONT_FT;
  if (isfreetype){
    int spacesz = aft_spacewidth(isbig)*8;
    return (spacesz-(x%spacesz));
  }
  PNGFONTS * fnt = isbig?&AG_BIG_FONT:&AG_SMALL_FONT;
  if (!fnt->loaded) return 0;
  int spacesz = fnt->fw[0]*8;
  return (spacesz-(x%spacesz));
}

//-- Colorset
//notice: length of string must <=14
static char ag_colorsets[28][14]={
  "#winbg",
  "#selectbg",
  "#txt_title",
  "#txt_menu",
  "#txt_select",
  "#txt_version",
  "#txt_pic_tit",
  "#txt_pic_desc",
  "#menuline_top",
  "#menuline_bot",
  "#title_line",
  "#border",  
  "#statusbg",  
  "#txt_status",    
};

//-- get Color By Index
color ag_getcolorset(int color_index){
  color cl=0;
  switch(color_index){
    case 0:  cl=acfg()->winbg; break;
    case 1:  cl=acfg()->selectbg; break;
    case 2:  cl=acfg()->txt_title; break;
    case 3:  cl=acfg()->txt_menu; break;
    case 4:  cl=acfg()->txt_select; break;
    case 5:  cl=acfg()->txt_pic_tit; break;
    case 6:  cl=acfg()->txt_pic_desc; break;
    case 7:  cl=acfg()->txt_version; break;
    case 8:  cl=acfg()->menuline_top; break;
    case 9:  cl=acfg()->menuline_bot; break;
    case 10: cl=acfg()->title_line; break;
    case 11: cl=acfg()->border; break;	
    case 12: cl=acfg()->statusbg; break;	
    case 13: cl=acfg()->txt_status; break;		
  };
  return cl;
}
byte ag_check_escape(int * soff, const char ** ssource, char * buf, byte realescape, byte * o){
  if (*soff>255) return 0;
    
  const char * s = *ssource;
  char off = (char) *soff;
  int  i=0;
  char tb[15];
  
  if ((off=='\\')&&(*s=='<')){ *soff = *s++; *ssource=s; if (o!=NULL) *o=1; }
  else if ((off=='<')&&((*s=='u')||(*s=='b')||(*s=='q')||(*s=='*')||(*s=='@')||(*s=='#')||(*s=='/'))){
    const char * sv = s;
    memset(tb,0,15);
    byte foundlt = 0;
    for (i=0;i<15;i++){
      char cv=*sv++;
      if (cv=='>'){
        tb[i]   = 0;
        foundlt = 1;
        break;
      }
      tb[i]=cv;
    }
    if (foundlt){
      if (tb[0]=='#'){
        int ci=0;
        for (ci=0;ci<28;ci++){
          if (strcmp(tb,ag_colorsets[ci])==0){
            if (buf!=NULL){
              if (realescape){
                snprintf(buf,15,"%s", tb);
              }
              else{
                color ccolor=ag_getcolorset(ci);
                snprintf(buf,8,"#%02x%02x%02x",ag_r(ccolor),ag_g(ccolor),ag_b(ccolor));
              }
            }
            *ssource=sv;
            return 1;
          }
        }
      }
      
      if (
          (strcmp(tb,"u")==0)||
          (strcmp(tb,"/u")==0)||
          (strcmp(tb,"b")==0)||
          (strcmp(tb,"/b")==0)||
          (strcmp(tb,"q")==0)||
          (strcmp(tb,"/q")==0)||
          (strcmp(tb,"*")==0)||
          (strcmp(tb,"/*")==0)||
          (strcmp(tb,"/#")==0)||
          (strcmp(tb,"/@")==0)||
          
          //-- ALIGN
          (strcmp(tb,"@left")==0)||
          (strcmp(tb,"@right")==0)||
          (strcmp(tb,"@center")==0)||
          (strcmp(tb,"@fill")==0)||
          
          ((tb[0]=='#') && ((strlen(tb)==4)||(strlen(tb)==7)))
      ){
        if (buf!=NULL) sprintf(buf,"%s",tb);
        *ssource=sv;
        return 1;
      }
    }
  }
  return 0;
}
//-- Calculate 1 Line Text Width
int ag_txtwidth(const char *ss, byte isbig){
  if (!ag_fontready(isbig)) return 0;
  int w = 0;
  int x = 0;
  int  i=0;
  char tb[8];
  int off;
  int move=0;
  int p=0;
  byte isfreetype = isbig?AG_BIG_FONT_FT:AG_SMALL_FONT_FT;
  
  char * sams   = alang_ams(ss);
  const char * s= sams;
  while((off=utf8c(s,&s,&move))){
    if ((move==1)&&(ag_check_escape(&off,&s,NULL,1,NULL))) continue;
    
    int is_arabic=0;
    if (isfreetype){
      int   arabic_str[1024];
      byte  arabic_prop[1024];
      int   outlength=0;
      int   ctmp = off;
      const char * stmp = s;
      if (aft_read_arabic(&ctmp,stmp,&stmp,arabic_str,arabic_prop,1024,&outlength,&move)){
        int rtl_arabic_i = 0;
        int arabic_width = 0;
        int rtl_poff = p;
        for (rtl_arabic_i=0;rtl_arabic_i<outlength;rtl_arabic_i++){
          arabic_width += ag_fontwidth(arabic_str[rtl_arabic_i],isbig)+aft_kern(arabic_str[rtl_arabic_i],rtl_poff,isbig);
          rtl_poff = arabic_str[rtl_arabic_i];
        }
        w+=arabic_width;
        is_arabic = 1;
        off = ctmp;
        s = stmp;
      }
    }
    if (!is_arabic){
      if (off=='\t')
        w+=ag_tabwidth(w,isbig);
      else
        w+=ag_fontwidth_kerning(off,p,isbig);
    }
    p=off;
  }
  free(sams);
  return w;
}
int ag_fontheight(byte isbig){
  if (!ag_fontready(isbig)) return 0;
  byte isfreetype = isbig?AG_BIG_FONT_FT:AG_SMALL_FONT_FT;
  if (isfreetype){
    return aft_fontheight(isbig);
  }
  PNGFONTS * fnt = isbig?&AG_BIG_FONT:&AG_SMALL_FONT;
  return fnt->fh;
}
//-- Draw Text
byte ag_text(CANVAS *_b,int maxwidth,int x,int y, const char *s, color cl_def,byte isbig){
  return ag_text_ex(_b,maxwidth,x,y,s,cl_def,isbig,0);
}
byte ag_textf(CANVAS *_b,int maxwidth,int x,int y, const char *s, color cl_def,byte isbig){
  return ag_text_ex(_b,maxwidth,x,y,s,cl_def,isbig,1);
}
byte ag_text_ex(CANVAS *_b,int maxwidth,int x,int y, const char *s, color cl_def,byte isbig,byte forcecolor){
  return ag_text_exl(_b,maxwidth,x,y,s,cl_def,isbig,forcecolor,1);
}
byte ag_texts(CANVAS *_b,int maxwidth,int x,int y, const char *s, color cl_def,byte isbig){
  return ag_text_exl(_b,maxwidth,x,y,s,cl_def,isbig,0,0);
}
byte ag_textfs(CANVAS *_b,int maxwidth,int x,int y, const char *s, color cl_def,byte isbig){
  return ag_text_exl(_b,maxwidth,x,y,s,cl_def,isbig,1,0);
}

//############################ NEW TEXT HANDLER
int ag_txt_getline(const char * s, int maxwidth_ori, byte isbig, byte * ischangealign, int * indent, int * next_indent, byte * endofstring){
  if (maxwidth_ori==0) return 0;
  if (!ag_fontready(isbig)) return 0;
  if (maxwidth_ori<ag_fontheight(isbig)*2) maxwidth_ori=ag_fontheight(isbig)*2;

  char tb[15];//-- Escape Data
  int  c=0;   //-- Current Char
  byte o=0;   //-- Previous Char
  int  l=0;   //-- Line String Length
  int  w=0;   //-- Current Width
  int  p=-1;  //-- Previous Space Pos
  int  maxwidth = maxwidth_ori - indent[0];
  
  byte isfreetype = isbig?AG_BIG_FONT_FT:AG_SMALL_FONT_FT;  
  int  indentsz = (ag_fontwidth(' ',isbig)*2)+ag_bulletwidth(isbig); // +ag_fontwidth(isfreetype?0x2022:0xa9,isbig);
  
  byte fns=0; //-- No Space Exists
  int  move=0;
  int  pc=0;
  
  // while ((c=*s++)){
  while((c=utf8c(s,&s,&move))){
    if ((move==1)&&(ag_check_escape(&c,&s,tb,1,&o))) {
      if (w>0){
        if  (
              (strcmp(tb,"/@")==0)||
              (strcmp(tb,"@left")==0)||
              (strcmp(tb,"@right")==0)||
              (strcmp(tb,"@center")==0)||
              (strcmp(tb,"@fill")==0)
            ){
          if (ischangealign!=NULL) ischangealign[0]=1;
          if (*s=='\n') return (l+3+strlen(tb));
          return l;
        }
        else if ((strcmp(tb,"/q")==0)||(strcmp(tb,"/*")==0)){
          next_indent[0]=indent[0]-indentsz;
          if (next_indent[0]<0) next_indent[0] = 0;
          
          if (fns){
            if (ischangealign!=NULL) ischangealign[0]=1;
            if (*s=='\n') return (l+3+strlen(tb));
            return l;
          }
          else{
            indent[0]=next_indent[0];
            maxwidth = maxwidth_ori - indent[0];
          }
        }
        else if ((strcmp(tb,"q")==0)||(strcmp(tb,"*")==0)) {
          next_indent[0]=indent[0]+indentsz;
          if (next_indent[0]>indentsz*5) next_indent[0] = indentsz*5;
          
          if (fns){
            if (ischangealign!=NULL) ischangealign[0]=1;
            if (*s=='\n') return (l+3+strlen(tb));
            return l;
          }
          else{
            indent[0]=next_indent[0];
            maxwidth = maxwidth_ori - indent[0];
          }
        }
      }
      else if ((strcmp(tb,"/q")==0)||(strcmp(tb,"/*")==0)){
        w=0;
        indent[0]-=indentsz;
        if (indent[0]<0) indent[0] = 0;
        next_indent[0]=indent[0];
        maxwidth = maxwidth_ori - indent[0];
      }
      else if ((strcmp(tb,"q")==0)||(strcmp(tb,"*")==0)){
        w=0;
        indent[0]+=indentsz;
        if (indent[0]>indentsz*5) indent[0] = indentsz*5;
        next_indent[0]=indent[0];
        maxwidth = maxwidth_ori - indent[0];
      }
      l+=2+strlen(tb);
      p=l;
    }
    else{
      byte is_arabic = 0;
      if (isfreetype){
        int   arabic_str[1024];
        byte  arabic_prop[1024];
        int   outlength=0;
        
        int   ctmp = c;
        const char * stmp = s;
        if (aft_read_arabic(&ctmp,stmp,&stmp,arabic_str,arabic_prop,1024,&outlength,&move)){
          int rtl_arabic_i = 0;
          int arabic_width = 0;
          int rtl_poff = pc;
          for (rtl_arabic_i=0;rtl_arabic_i<outlength;rtl_arabic_i++){
            arabic_width += ag_fontwidth(arabic_str[rtl_arabic_i],isbig)+aft_kern(arabic_str[rtl_arabic_i],rtl_poff,isbig);
            rtl_poff = arabic_str[rtl_arabic_i];
          }
          w+=arabic_width;
          is_arabic = 1;
          c = ctmp;
          s = stmp;
        }
      }
      if (!is_arabic){
        if (c=='\n'){
          if (ischangealign!=NULL) ischangealign[0]=1;
          return l+move;
        }
        else if (c=='\t')
          w+=ag_tabwidth(w,isbig);
        else
          w+=ag_fontwidth_kerning(c,pc,isbig);
          // w+=ag_fontwidth(c,isbig);
      }
      
      if (w>maxwidth){
        if (p==-1)
          return l;
        return p;
      }
      else if ((c==' ')||(c=='\t')){
        l+=move;
        p=l;
      }
      else if (c=='<'){
        l+=move;
        if (o) l++;
        fns=1;
      }
      else{
        l+=move;
        fns=1;
      }
      pc=c;
    }
    o = 0;
  }
  endofstring[0]=1;
  return l;
}
char * ag_substring(const char * s, int len){
  if (len<1) return NULL;

  char * ln = malloc(len+1);
  memset(ln,0,len+1);
  
  int i;
  for (i=0;i<len;i++){
    if ((s[i]=='\n')||(!s[i])){
      ln[i]=0;
      break;
    }
    ln[i]=s[i];
  }
  return ln;
}
int ag_txtheight(int maxwidth, const char *ss, byte isbig){
  if (maxwidth==0) return 0;
  if (!ag_fontready(isbig)) return 0;
  int  fheight = ag_fontheight(isbig);
  if (fheight==0) return 0;
  if (maxwidth<fheight*2) maxwidth=fheight*2;

  char * sams   = alang_ams(ss);
  const char * s= sams;
  int indent= 0;
  int lines = 0;
  while (*s!=0){
    int next_indent = indent;
    byte eos = 0;
    int line_width  = ag_txt_getline(s,maxwidth,isbig,NULL,&indent,&next_indent,&eos);
    if (line_width==0) break;
    lines++;
    s+=line_width;
    indent=next_indent;
    if (eos) break;
  }
  free(sams);
  
  return (lines*fheight);
}

/* HuangGuoDong@Drv.recovery, 2013/1/23, add for mutex draw_text, otherwise recovery may halt when multi thread invoke ag_text_exl functoin*/
static pthread_mutex_t draw_text_mutex = PTHREAD_MUTEX_INITIALIZER;

/* DRAW TEXT */
byte ag_text_exl(CANVAS *_b,int maxwidth,int x,int y, const char *ss, color cl_def,byte isbig,byte forcecolor,byte multiline){
  if (maxwidth==0) return 0;
  if (!ag_fontready(isbig)) return 0;
  if (_b==NULL) _b=&ag_c;
  if (!maxwidth) maxwidth = _b->w-x;

  int  fheight = ag_fontheight(isbig);
  if (fheight==0) return 0;
  if (maxwidth<fheight*2) maxwidth=fheight*2;
  
/* HuangGuoDong@Drv.recovery, 2013/1/23, add for mutex draw_text, otherwise recovery may halt when multi thread invoke ag_text_exl functoin*/  
  pthread_mutex_lock(&draw_text_mutex);

  byte isfreetype = isbig?AG_BIG_FONT_FT:AG_SMALL_FONT_FT;
  
  char * sams   = alang_ams(ss);
  const char * s= sams;
  
  char tb[8];         //-- Escape Data
  byte bold = 0;      //-- Bold
  byte undr = 0;      //-- Underline
  byte algn = 0;      //-- Alignment
  color cl  = cl_def; //-- Current Color
  int  cx   = x;
  int indent= 0;
  while (*s!=0){
    byte chalign   = 0;
    int next_indent= indent;
    byte eos = 0;
    int line_width = ag_txt_getline(s,maxwidth,isbig,&chalign,&indent,&next_indent,&eos);
    
    if (line_width==0) break;
    
    char * bf=ag_substring(s,line_width);
    if (bf!=NULL){
      const char * line_string  = ai_rtrim(bf);
      int lwpx                  = ag_txtwidth(line_string,isbig);
      int ldpx                  = (maxwidth-indent)-lwpx;
      int off                   = 0;
      
      //-- Alignment
      if (algn==1)
        cx=ldpx/2 + x + indent;
      else if (algn==2)
        cx=ldpx + x + indent;
      else
        cx=x + indent;
      
      int first_cx = cx;
      
      int sp_n    = 0;    //-- space count
      int * sp_v  = NULL; //-- space add sz
      if (chalign==0){
        if (algn==3){
          sp_n=0;
          int vc = 0;
          byte vf =0;
          int move=0;
          const char * lstr = line_string;
          //while((vc = *lstr++)){
          while((vc=utf8c(lstr,&lstr,&move))){
            if ((move!=1)||(!ag_check_escape(&vc,&lstr,NULL,1,NULL))) {
              if (vc=='\t'){
                sp_n = 0;
                break;
              }
              else if (vf){
                if (vc==' ') sp_n++;
              }
              else if(vc!=' ') vf = 1;
            }
          }
        }
        if (sp_n>0){
          sp_v    = malloc(sizeof(int) * sp_n);
          memset(sp_v,0,sizeof(int) * sp_n);
          int pn  = 0;
          int pz  = lwpx;
          while (pz<maxwidth-indent){
            sp_v[pn]++;
            pz++;
            if (++pn>sp_n-1) pn=0;
          }
        }
      }
      
      byte first_space=0;
      int  space_pos  =0;
      int  move_main  =0;
      int  pc         =0;
      // while((off = *line_string++)){
      while((off=utf8c(line_string,&line_string,&move_main))){
        if ((move_main==1)&&(ag_check_escape(&off,&line_string,tb,0,NULL))) {
          if (strcmp(tb,"/#")==0){
            if (!forcecolor) cl=cl_def;
          }
          else if ((tb[0]=='#')&&((strlen(tb)==4)||(strlen(tb)==7))){
            if (!forcecolor) cl=strtocolor(tb);
          }
          else if (strcmp(tb,"*")==0){
            if (indent>0){
              int vcx = (first_space)?cx:first_cx;
              // ag_drawchar_ex(_b,vcx-(ag_fontwidth(' ',isbig)+ag_fontwidth(isfreetype?0x2022:0xa9,isbig)),y,isfreetype?0x2022:0xa9,cl,isbig,0,0);
              int indentsz = ((ag_fontwidth(' ',isbig)*2)+ag_bulletwidth(isbig));
              ag_draw_bullet(_b,vcx-(indentsz-ag_fontwidth(' ',isbig)),y,cl,isbig,round(indent/indentsz));
              if (!first_space) cx = first_cx;
            }
          }
          else if (strcmp(tb,"/u")==0)      undr=0;
          else if (strcmp(tb,"u")==0)       undr=1;
          else if (strcmp(tb,"/b")==0)      bold=0;
          else if (strcmp(tb,"b")==0)       bold=1;
          else if (strcmp(tb,"@center")==0){
            algn=1;
            cx = ldpx/2 + x + indent;
            first_cx = cx;
          }
          else if (strcmp(tb,"@right")==0){
            algn=2;
            cx = ldpx + x + indent;
            first_cx = cx;
          }
          else if (strcmp(tb,"@fill")==0){
            algn=3;
            cx = x + indent;
            first_cx = cx;
            
            if (chalign==0){
              sp_n=0;
              int vc = 0;
              byte vf =0;
              int move=0;
              const char * lstr = line_string;
              while((vc=utf8c(lstr,&lstr,&move))){
                if ((move!=1)||(!ag_check_escape(&vc,&lstr,NULL,1,NULL))) {
                  if (vc=='\t'){
                    sp_n = 0;
                    break;
                  }
                  else if (vf){
                    if (vc==' ') sp_n++;
                  }
                  else if(vc!=' ') vf = 1;
                }
              }
              
              if (sp_n>0){
                sp_v    = malloc(sizeof(int) * sp_n);
                memset(sp_v,0,sizeof(int) * sp_n);
                int pn  = 0;
                int pz  = lwpx;
                while (pz<maxwidth-indent){
                  sp_v[pn]++;
                  pz++;
                  if (++pn>sp_n-1) pn=0;
                }
              }
            }
          }
          else if ((strcmp(tb,"@left")==0)||(strcmp(tb,"/@")==0)){
            algn=0;
            cx = x + indent;
            first_cx = cx;
          }
        }
        else{   
          int fwidth = 0;
          if (off=='\t'){
            fwidth = ag_tabwidth(cx-x,isbig);
          }
          else{
            int krn=0;
            if (isfreetype) krn = aft_kern(off,pc,isbig);
            fwidth = ag_fontwidth(off,isbig)+krn;
            
            if (isfreetype&&aft_isrtl(off,0)){
              
              const char * rtl_line_string  = line_string;
              const char * rtl_last_string  = line_string;
              int rtl_last_off              = off;
              int rtl_off                   = off;
              int rtl_width                 = 0;
              int rtl_length                = 0;
              int rtl_poff                  = pc;
              int rtl_spacepos              = space_pos;
              int rtl_out[1024];
              int rtl_fwidth[1024];
              memset(rtl_out,0,sizeof(int)*1024);
              memset(rtl_fwidth,0,sizeof(int)*1024);
              
              do{
                int   arabic_str[1024];
                byte  arabic_prop[1024];
                int   outlength=0;
                
                if (aft_read_arabic(&rtl_off,rtl_line_string,&rtl_line_string,arabic_str,arabic_prop,1024,&outlength,&move_main)){
                  int rtl_arabic_i = 0;
                  for (rtl_arabic_i=0;rtl_arabic_i<outlength;rtl_arabic_i++){
                    int char_width = ag_fontwidth(arabic_str[rtl_arabic_i],isbig)+aft_kern(arabic_str[rtl_arabic_i],rtl_poff,isbig);
                    rtl_out[rtl_length+rtl_arabic_i] = arabic_str[rtl_arabic_i];
                    rtl_fwidth[rtl_length+rtl_arabic_i]=char_width;
                    rtl_width += char_width;
                    rtl_poff = arabic_str[rtl_arabic_i];
                  }
                  rtl_length+=outlength;
                }
                else{
                  int rtl_char_w = ag_fontwidth(rtl_off,isbig)+aft_kern(rtl_off,rtl_poff,isbig);
                  if(rtl_off==' '){
                    if (sp_n>rtl_spacepos){
                      rtl_char_w+=sp_v[rtl_spacepos];
                      rtl_spacepos++;
                    }
                  }
                  rtl_width += rtl_char_w;
                  rtl_fwidth[rtl_length]= rtl_char_w;
                  rtl_out[rtl_length++] = rtl_off;
                }
                
                rtl_poff              = rtl_off;
                rtl_last_off          = rtl_off;
                rtl_last_string       = rtl_line_string;
                
                rtl_off = utf8c(rtl_line_string,&rtl_line_string,&move_main);
                if ((aft_isrtl(rtl_off,1)==0)||(rtl_off=='<')) break;
                if (rtl_length>1023) break;
              }while(rtl_off!=0);
              
              int rtl_draw_i  = 0;
              int rtl_pos     = cx+rtl_width;
              
              for (rtl_draw_i=0;rtl_draw_i<rtl_length;rtl_draw_i++){
                int fxw = rtl_fwidth[rtl_draw_i];
                int fch = rtl_out[rtl_draw_i];
                rtl_pos-= fxw;
                
                if(fch!=' '){
                  aft_drawfont(_b,isbig,fch,rtl_pos,y,cl,undr,bold);
                }
              }
              
              off          = rtl_last_off;
              fwidth      += rtl_width;
              line_string  = rtl_last_string;
            }
            else{
              ag_drawchar_ex(_b,cx+krn,y,off,cl,isbig,undr,bold);
            }
          }
          
          pc = off;
          
          if (first_space){
            if(off==' '){
              if (sp_n>space_pos){
                fwidth+=sp_v[space_pos];
                space_pos++;
              }
            }
          }
          else if(off!=' ') first_space = 1;
          
          cx+= fwidth;
        }
      }
      
      if (sp_v!=NULL) free(sp_v);
      free(bf);
    }
    
    if (!multiline) break;
    
    indent=next_indent;
    y+=fheight;
    s+=line_width;
    
    if (eos) break;
  }
  
  free(sams);

/* HuangGuoDong@Drv.recovery, 2013/1/23, add for mutex draw_text, otherwise recovery may halt when multi thread invoke ag_text_exl functoin*/ 
  pthread_mutex_unlock(&draw_text_mutex);

  return 1;
}
