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
 * Input Event Hook and Manager
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/poll.h>
#include <linux/input.h>
#include <pthread.h>
#include "../oppo_inter.h"

//-- Input Device
#include "input_device.c"

//-- GLOBAL EVENT VARIABLE
/* HuangGuoDong@Drv.recovery, 2013/1/17, Modify for array must greater than the value of atouch_message_code*/	
static  char      key_pressed[900];
//-- AROMA CUSTOM MESSAGE
static  dword     atouch_winmsg[64];
static  byte      atouch_winmsg_n = 0;
static  int       atouch_message_code = 889;

//-- KEY QUEUE
static  int       key_queue[256];
static  int       key_queue_len = 0;
static pthread_mutex_t key_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t key_queue_cond = PTHREAD_COND_INITIALIZER;

//-- TOUCH SCREEN VAR
static  byte      evthread_active = 1;
static  int       evtouch_state   = 0;  //-- Touch State
static  int       evtouch_x       = 0;  //-- Translated X (Ready to use)
static  int       evtouch_y       = 0;  //-- Translated Y (Ready to use)
static  int       evtouch_code    = 888;//-- Touch Virtual Code

//-- PASS TOUCH STATE FUNCTIONS
int touchX()  { return evtouch_x; }
int touchY()  { return evtouch_y; }
int ontouch() { return ((evtouch_state==0)?0:1); }
  
dword atouch_winmsg_get(byte cleanup){
  dword out=0;
  if (atouch_winmsg_n>0){
    out=atouch_winmsg[0];
    if (cleanup){
      int i=0;
      for (i=0;i<atouch_winmsg_n;i++){
        atouch_winmsg[i]=atouch_winmsg[i+1];
      }
      atouch_winmsg_n--;
    }
  }
  return out;
}
byte atouch_winmsg_push(dword msg){
  if (atouch_winmsg_n<64){
    atouch_winmsg[atouch_winmsg_n++]=msg;
    return 1;
  }
  return 0;
}

//-- VIBRATE FUNCTION
int vibrate(int timeout_ms){
    char str[20];
    int fd;
    int ret;
    fd = open("/sys/class/timed_output/vibrator/enable", O_WRONLY);
    if (fd < 0) return -1;
    ret = snprintf(str, sizeof(str), "%d", timeout_ms);
    ret = write(fd, str, ret);
    close(fd);
    if (ret < 0)
       return -1;
    return 0;
}

//-- KEYPRESS MANAGER
int ui_key_pressed(int key){
    return key_pressed[key];
}
void set_key_pressed(int key,char val){
  key_pressed[key]=val;
}

//-- INPUT EVENT POST MESSAGE
void ev_post_message(int key, int value){
  set_key_pressed(key,value);
  pthread_mutex_lock(&key_queue_mutex);
  const int queue_max = sizeof(key_queue) / sizeof(key_queue[0]);
  if (key_queue_len<queue_max){
    key_queue[key_queue_len++] = key;
    pthread_cond_signal(&key_queue_cond);
  }
  pthread_mutex_unlock(&key_queue_mutex);
}

//-- INPUT CALLBACK
void ev_input_callback(struct input_event * ev){
  if (ev->type==EV_KEY){
  	if ((ev->code == KEY_VOLUMEDOWN) && (key_pressed[KEY_VOLUMEDOWN] == 0) && (ev->value == 0))
		return;
    ev_post_message(ev->code,ev->value);
#ifdef  SIMULATE_KEY_UP_EVENT   	
	if (ev->code == KEY_BACK)
		ev_post_message(KEY_BACK, 0);
#endif	
  }
  else if (ev->type == EV_ABS) {
    evtouch_x = ev->value >> 16;
    evtouch_y = ev->value & 0xFFFF;
    
    if ((evtouch_x>0)&&(evtouch_y>0)){
      if (ev->code==0){
        evtouch_state = 0;
      }
      else if (evtouch_state==0){
        evtouch_state = 1;
      }
      else{
        evtouch_state = 2;
      }
      ev_post_message(evtouch_code,evtouch_state);
    }
    else{
      //-- False Event
      evtouch_state = 0;
      evtouch_x = 0;
      evtouch_y = 0;
    }
  }
}

//-- INPUT THREAD
static void *ev_input_thread(void *cookie){
  //-- Loop for Input
  oppo_debug("start...\n");
  while (evthread_active){
    struct input_event ev;
    byte res=aipGetInput(&ev, 500);
    if (res){
      ev_input_callback(&ev);
    }
  }
  oppo_debug("end...\n");
  return NULL;
}
//-- INIT INPUT DEVICE
void ui_init(){
  ev_init();
}

/*
 *function:filter out unused input event;
 *para:name->the file name ,execude path, no prefix
 *ins: "input0"
 *return value: 1 if filter out
 *              0 filter in
 *
 */
int event_filter(char *name)
{
    //if have filter ,return 1
    return_val_if_fail(strncmp(name, "event", 5) == 0, 0);
    return_val_if_fail(strlen(name) >= 5, 0);
    return_val_if_fail(name != NULL, 0);
    u32 mask = acfg()->input_filter;//from acfg()->input_filter
    char a= *(name + 5);//"inputn", extract n
    return_val_if_fail(a >= 0x30, 0);
    return_val_if_fail(a < 0x40, 0);
    byte i = a - 0x30;
    return_val_if_fail(i > 0, 0);
    return_val_if_fail(i < 32, 0);
    if((mask>>i) & 0x1) {
        return 1; //filter out
}
    return 0;
}

static pthread_t input_thread_t;
int ev_init(){
  aipInit(&event_filter);
  
  //-- Create Watcher Thread
  evthread_active = 1;
  pthread_create(&input_thread_t, NULL, ev_input_thread, NULL);
  //pthread_detach(input_thread_t);
  return 0;
}

//-- RELEASE INPUT DEVICE
void ev_exit(void){
  evthread_active = 0;
  pthread_join(input_thread_t,NULL);
  aipRelease();
}

//-- SEND ATOUCH CUSTOM MESSAGE
byte atouch_send_message(dword msg){
  if (atouch_winmsg_push(msg)){
    ev_post_message(atouch_message_code,0);
    return 1;
  }
  return 0;
}

//-- Clear Queue
void ui_clear_key_queue_ex(){
  pthread_mutex_lock(&key_queue_mutex);
  key_queue_len = 0;
  pthread_mutex_unlock(&key_queue_mutex);
  atouch_winmsg_n=0;
}
void ui_clear_key_queue() {
  pthread_mutex_lock(&key_queue_mutex);
  key_queue_len = 0;
  pthread_mutex_unlock(&key_queue_mutex);
  if (atouch_winmsg_n>0) ev_post_message(atouch_message_code,0);
}

//-- Wait For Key
int ui_wait_key(){
  pthread_mutex_lock(&key_queue_mutex);
  while (key_queue_len == 0){
    pthread_cond_wait(&key_queue_cond, &key_queue_mutex);
  }
  int key = key_queue[0];
  memcpy(&key_queue[0], &key_queue[1], sizeof(int) * --key_queue_len);
  pthread_mutex_unlock(&key_queue_mutex);
  return key;
}

static void input_event_dump(const char *str, struct input_event *ev)
{
    oppo_debug("[%s]ev->type:%4x , ev->code:%4x, ev->value:%4x\n",str,  ev->type, ev->code, ev->value);
}

//-- AROMA Input Handler
int atouch_wait(ATEV *atev){
#ifdef DEBUG
	input_event_dump("atouch message", atev);
#endif
  return atouch_wait_ex(atev,0);
}
int atouch_wait_ex(ATEV *atev, byte calibratingtouch){
  atev->x = -1;
  atev->y = -1;
  
  while (1){
    int key = ui_wait_key();

    //-- Custom Message
    if (key==atouch_message_code){
      atev->msg = atouch_winmsg_get(1);
      atev->d   = 0;
      atev->x   = 0;
      atev->y   = 0;
      atev->k   = 0;
      return ATEV_MESSAGE;
    }
    
    atev->d = ui_key_pressed(key);
    atev->k = key;
    
    if (key==evtouch_code){
      if ((evtouch_x>0)&&(evtouch_y>0)){
        atev->x = evtouch_x;
        atev->y = evtouch_y;
        switch(evtouch_state){
          case 1:  return ATEV_MOUSEDN; break;
          case 2:  return ATEV_MOUSEMV; break;
          default: return ATEV_MOUSEUP; break;
        }
      }
    }
    else if ((key!=0)&&(key==acfg()->ckey_up))      return ATEV_UP;
    else if ((key!=0)&&(key==acfg()->ckey_down))    return ATEV_DOWN;
    else if ((key!=0)&&(key==acfg()->ckey_select))  return ATEV_SELECT;
    else if ((key!=0)&&(key==acfg()->ckey_back))    return ATEV_BACK;
    else if ((key!=0)&&(key==acfg()->ckey_menu))    return ATEV_MENU;
    else{
      /* DEFINED KEYS */
      switch (key){
        /* RIGHT */
        case KEY_RIGHT: return ATEV_RIGHT; break;
        /* LEFT */
        case KEY_LEFT:  return ATEV_LEFT; break;
        
        /* DOWN */
        case KEY_DOWN:
        case KEY_CAPSLOCK:
        case KEY_VOLUMEDOWN:
          return ATEV_DOWN; break;
        
        /* UP */
        case KEY_UP:
        case KEY_LEFTSHIFT:
        case KEY_VOLUMEUP:
          return ATEV_UP; break;
        
        /* SELECT */
        case KEY_LEFTBRACE:
        case KEY_POWER:    
        case BTN_MOUSE:
        case KEY_ENTER:
        case KEY_CENTER:
        case KEY_CAMERA:
        case KEY_F21:
        case KEY_SEND:
        case KEY_END:
        case KEY_HOMEPAGE:

        case KEY_SEARCH:       
           return ATEV_SELECT; break;
        
        /* SHOW MENU */
        case 229:
          return ATEV_MENU; break;
        
        /* BACK */
        case KEY_BACKSPACE:
		case KEY_BACK:
          return ATEV_BACK; break;
      }
    }
  }
  return 0;
}
//-- 
