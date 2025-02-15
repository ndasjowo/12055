/*
 * Copyright (C) 2010 The Android Open Source Project
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

#ifndef _INIT_UTIL_H_
#define _INIT_UTIL_H_

#include <sys/stat.h>
#include <sys/types.h>

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

static const char *coldboot_done = "/dev/.coldboot_done";

int mtd_name_to_number(const char *name);
int create_socket(const char *name, int type, mode_t perm,
                  uid_t uid, gid_t gid);
void *read_file(const char *fn, unsigned *_sz);
time_t gettime(void);
unsigned int decode_uid(const char *s);

int mkdir_recursive(const char *pathname, mode_t mode);
void sanitize(char *p);
void make_link(const char *oldpath, const char *newpath);
void remove_link(const char *oldpath, const char *newpath);
int wait_for_file(const char *filename, int timeout);
void open_devnull_stdio(void);
void get_hardware_name(char *hardware, unsigned int *revision);
void import_kernel_cmdline(int in_qemu, void (*import_kernel_nv)(char *name, int in_qemu));
int make_dir(const char *path, mode_t mode);
int restorecon(const char *pathname);
#ifdef MTK_NAND_UBIFS_SUPPORT
int ubi_attach_mtd(const char *name);
int ubi_detach_dev(int dev);
#endif
#endif
