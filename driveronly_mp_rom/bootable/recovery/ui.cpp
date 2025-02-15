/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#if 1 //wschen 2012-07-10
#include "cust_keys.h"
#endif

#include <cutils/android_reboot.h>

#include "common.h"
#include "device.h"
#include "minui/minui.h"
#include "screen_ui.h"
#include "ui.h"

#define UI_WAIT_KEY_TIMEOUT_SEC    120

// There's only (at most) one of these objects, and global callbacks
// (for pthread_create, and the input event system) need to find it,
// so use a global variable.
static RecoveryUI* self = NULL;

RecoveryUI::RecoveryUI() :
    key_queue_len(0),
    key_last_down(-1) {
    pthread_mutex_init(&key_queue_mutex, NULL);
    pthread_cond_init(&key_queue_cond, NULL);
    self = this;
}

void RecoveryUI::Init() {
    ev_init(input_callback, NULL);
    pthread_create(&input_t, NULL, input_thread, NULL);
}


int RecoveryUI::input_callback(int fd, short revents, void* data)
{
    struct input_event ev;
    int ret;

    ret = ev_get_input(fd, revents, &ev);
    if (ret)
        return -1;

    if (ev.type == EV_SYN) {
        return 0;
    } else if (ev.type == EV_REL) {
        if (ev.code == REL_Y) {
            // accumulate the up or down motion reported by
            // the trackball.  When it exceeds a threshold
            // (positive or negative), fake an up/down
            // key event.
            self->rel_sum += ev.value;
            if (self->rel_sum > 3) {
#if 0 //wschen 2012-07-10
                self->process_key(KEY_DOWN, 1);   // press down key
                self->process_key(KEY_DOWN, 0);   // and release it
#else
                self->process_key(RECOVERY_KEY_DOWN, 1);   // press down key
                self->process_key(RECOVERY_KEY_DOWN, 0);   // and release it
#endif
                self->rel_sum = 0;
            } else if (self->rel_sum < -3) {
#if 0 //wschen 2012-07-10
                self->process_key(KEY_UP, 1);     // press up key
                self->process_key(KEY_UP, 0);     // and release it
#else
                self->process_key(RECOVERY_KEY_UP, 1);     // press up key
                self->process_key(RECOVERY_KEY_UP, 0);     // and release it
#endif
                self->rel_sum = 0;
            }
        }
    } else {
        self->rel_sum = 0;
    }

    if (ev.type == EV_KEY && ev.code <= KEY_MAX)
        self->process_key(ev.code, ev.value);

    return 0;
}

// Process a key-up or -down event.  A key is "registered" when it is
// pressed and then released, with no other keypresses or releases in
// between.  Registered keys are passed to CheckKey() to see if it
// should trigger a visibility toggle, an immediate reboot, or be
// queued to be processed next time the foreground thread wants a key
// (eg, for the menu).
//
// We also keep track of which keys are currently down so that
// CheckKey can call IsKeyPressed to see what other keys are held when
// a key is registered.
//
// updown == 1 for key down events; 0 for key up events
void RecoveryUI::process_key(int key_code, int updown) {
    bool register_key = false;

    pthread_mutex_lock(&key_queue_mutex);
    key_pressed[key_code] = updown;
    if (updown) {
        key_last_down = key_code;
    } else {
        if (key_last_down == key_code)
            register_key = true;
        key_last_down = -1;
    }
    pthread_mutex_unlock(&key_queue_mutex);
#ifndef VENDOR_EDIT
//Fangfang.Hui@Prd.PlatSrv.OTA, 2013/04/19, Modify for improve UE
    if (register_key) {
#else /* VENDOR_EDIT */
    if (updown) {
#endif /* VENDOR_EDIT */
        switch (CheckKey(key_code)) {
          case RecoveryUI::IGNORE:
            break;

          case RecoveryUI::TOGGLE:
            ShowText(!IsTextVisible());
            break;

          case RecoveryUI::REBOOT:
            android_reboot(ANDROID_RB_RESTART, 0, 0);
            break;

          case RecoveryUI::ENQUEUE:
            pthread_mutex_lock(&key_queue_mutex);
            const int queue_max = sizeof(key_queue) / sizeof(key_queue[0]);
            if (key_queue_len < queue_max) {
                key_queue[key_queue_len++] = key_code;
                pthread_cond_signal(&key_queue_cond);
            }
            pthread_mutex_unlock(&key_queue_mutex);
            break;
        }
    }
}

// Reads input events, handles special hot keys, and adds to the key queue.
void* RecoveryUI::input_thread(void *cookie)
{
    for (;;) {
        if (!ev_wait(-1))
            ev_dispatch();
    }
    return NULL;
}

int RecoveryUI::WaitKey()
{
    pthread_mutex_lock(&key_queue_mutex);

    // Time out after UI_WAIT_KEY_TIMEOUT_SEC, unless a USB cable is
    // plugged in.
    do {
        struct timeval now;
        struct timespec timeout;
        gettimeofday(&now, NULL);
        timeout.tv_sec = now.tv_sec;
        timeout.tv_nsec = now.tv_usec * 1000;
        timeout.tv_sec += UI_WAIT_KEY_TIMEOUT_SEC;

        int rc = 0;
        while (key_queue_len == 0 && rc != ETIMEDOUT) {
            rc = pthread_cond_timedwait(&key_queue_cond, &key_queue_mutex,
                                        &timeout);
        }
    } while (usb_connected() && key_queue_len == 0);

    int key = -1;
    if (key_queue_len > 0) {
        key = key_queue[0];
        memcpy(&key_queue[0], &key_queue[1], sizeof(int) * --key_queue_len);
    }
    pthread_mutex_unlock(&key_queue_mutex);
    return key;
}

// Return true if USB is connected.
bool RecoveryUI::usb_connected() {
    int fd = open("/sys/class/android_usb/android0/state", O_RDONLY);
    if (fd < 0) {
        printf("failed to open /sys/class/android_usb/android0/state: %s\n",
               strerror(errno));
        return 0;
    }

    char buf;
    /* USB is connected if android_usb state is CONNECTED or CONFIGURED */
    int connected = (read(fd, &buf, 1) == 1) && (buf == 'C');
    if (close(fd) < 0) {
        printf("failed to close /sys/class/android_usb/android0/state: %s\n",
               strerror(errno));
    }
    return connected;
}

bool RecoveryUI::IsKeyPressed(int key)
{
    pthread_mutex_lock(&key_queue_mutex);
    int pressed = key_pressed[key];
    pthread_mutex_unlock(&key_queue_mutex);
    return pressed;
}

void RecoveryUI::FlushKeys() {
    pthread_mutex_lock(&key_queue_mutex);
    key_queue_len = 0;
    pthread_mutex_unlock(&key_queue_mutex);
}

RecoveryUI::KeyAction RecoveryUI::CheckKey(int key) {
    return RecoveryUI::ENQUEUE;
}
