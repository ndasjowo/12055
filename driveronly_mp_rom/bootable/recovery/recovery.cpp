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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if 1 //wschen 2012-07-10
#include <sys/statfs.h>
#endif
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

#include "bootloader.h"
#include "common.h"
#include "cutils/properties.h"
#include "cutils/android_reboot.h"
#include "install.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "roots.h"
#include "ui.h"
#include "screen_ui.h"
#include "device.h"
#include "adb_install.h"
extern "C" {
#include "minadbd/adb.h"
}

#if 1 //wschen 2012-07-10
#ifdef SUPPORT_FOTA
extern "C" {
#include "fota/fota.h"
}
#endif

#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-03-09 
#include "minzip/Zip.h"
#include "backup_restore.h"
#endif //SUPPORT_DATA_BACKUP_RESTORE

#ifdef SUPPORT_SBOOT_UPDATE
#include "sec/sec.h"
#endif

#endif

struct selabel_handle *sehandle;

#ifndef VENDOR_EDIT
//Fangfang.Hui@Prd.PlatSrv.OTA, 2012/06/11, Modify for OTA transplant
static const struct option OPTIONS[] = {
  { "send_intent", required_argument, NULL, 's' },
  { "update_package", required_argument, NULL, 'u' },
  { "wipe_data", no_argument, NULL, 'w' },
  { "wipe_cache", no_argument, NULL, 'c' },
  { "show_text", no_argument, NULL, 't' },
  { "just_exit", no_argument, NULL, 'x' },
  { "locale", required_argument, NULL, 'l' },
#ifdef SUPPORT_FOTA
  { "fota_delta_path", required_argument, NULL, 'f' },
  //{ "fota_delta_path_1", required_argument, NULL, 'g' },
#endif
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-05-16 
  { "restore_data", required_argument, NULL, 'r' },
#endif //SUPPORT_DATA_BACKUP_RESTORE
  { NULL, 0, NULL, 0 },
};

static const char *COMMAND_FILE = "/cache/recovery/command";
static const char *INTENT_FILE = "/cache/recovery/intent";
static const char *LOG_FILE = "/cache/recovery/log";
static const char *LAST_LOG_FILE = "/cache/recovery/last_log";
static const char *LAST_LOG_FILE_1 = "/cache/recovery/last_log_r";
static const char *LAST_INSTALL_FILE = "/cache/recovery/last_install";
static const char *LOCALE_FILE = "/cache/recovery/last_locale";
static const char *CACHE_ROOT = "/cache";
static const char *SDCARD_ROOT = "/sdcard";
#if defined(SUPPORT_SDCARD2) && !defined(MTK_SHARED_SDCARD) //wschen 2012-11-15
static const char *SDCARD2_ROOT = "/sdcard2";
#endif
static const char *TEMPORARY_LOG_FILE = "/tmp/recovery.log";
static const char *TEMPORARY_INSTALL_FILE = "/tmp/last_install";
static const char *SIDELOAD_TEMP_DIR = "/tmp/sideload";
#if 1 //wschen 2012-07-10
static const char *DEFAULT_MOTA_FILE = "/data/data/com.mediatek.GoogleOta/files/update.zip";
static const char *DEFAULT_FOTA_FOLDER = "/data/data/com.mediatek.dm/files";
static const char *MOTA_RESULT_FILE = "/data/data/com.mediatek.GoogleOta/files/updateResult";
static const char *FOTA_RESULT_FILE = "/data/data/com.mediatek.dm/files/updateResult";
#endif

RecoveryUI* ui = NULL;
char* locale = NULL;

/*
 * The recovery tool communicates with the main system through /cache files.
 *   /cache/recovery/command - INPUT - command line for tool, one arg per line
 *   /cache/recovery/log - OUTPUT - combined log file from recovery run(s)
 *   /cache/recovery/intent - OUTPUT - intent that was passed in
 *
 * The arguments which may be supplied in the recovery.command file:
 *   --send_intent=anystring - write the text out to recovery.intent
 *   --update_package=path - verify install an OTA package file
 *   --wipe_data - erase user data (and cache), then reboot
 *   --wipe_cache - wipe cache (but not user data), then reboot
 *   --set_encrypted_filesystem=on|off - enables / diasables encrypted fs
 *   --just_exit - do nothing; exit and reboot
 *
 * After completing, we remove /cache/recovery/command and reboot.
 * Arguments may also be supplied in the bootloader control block (BCB).
 * These important scenarios must be safely restartable at any point:
 *
 * FACTORY RESET
 * 1. user selects "factory reset"
 * 2. main system writes "--wipe_data" to /cache/recovery/command
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--wipe_data"
 *    -- after this, rebooting will restart the erase --
 * 5. erase_volume() reformats /data
 * 6. erase_volume() reformats /cache
 * 7. finish_recovery() erases BCB
 *    -- after this, rebooting will restart the main system --
 * 8. main() calls reboot() to boot main system
 *
 * OTA INSTALL
 * 1. main system downloads OTA package to /cache/some-filename.zip
 * 2. main system writes "--update_package=/cache/some-filename.zip"
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--update_package=..."
 *    -- after this, rebooting will attempt to reinstall the update --
 * 5. install_package() attempts to install the update
 *    NOTE: the package install must itself be restartable from any point
 * 6. finish_recovery() erases BCB
 *    -- after this, rebooting will (try to) restart the main system --
 * 7. ** if install failed **
 *    7a. prompt_and_wait() shows an error icon and waits for the user
 *    7b; the user reboots (pulling the battery, etc) into the main system
 * 8. main() calls maybe_install_firmware_update()
 *    ** if the update contained radio/hboot firmware **:
 *    8a. m_i_f_u() writes BCB with "boot-recovery" and "--wipe_cache"
 *        -- after this, rebooting will reformat cache & restart main system --
 *    8b. m_i_f_u() writes firmware image into raw cache partition
 *    8c. m_i_f_u() writes BCB with "update-radio/hboot" and "--wipe_cache"
 *        -- after this, rebooting will attempt to reinstall firmware --
 *    8d. bootloader tries to flash firmware
 *    8e. bootloader writes BCB with "boot-recovery" (keeping "--wipe_cache")
 *        -- after this, rebooting will reformat cache & restart main system --
 *    8f. erase_volume() reformats /cache
 *    8g. finish_recovery() erases BCB
 *        -- after this, rebooting will (try to) restart the main system --
 * 9. main() calls reboot() to boot main system
 */

static const int MAX_ARG_LENGTH = 4096;
static const int MAX_ARGS = 100;

#if 1 //wschen 2012-07-10
static bool check_otaupdate_done(void)
{
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    get_bootloader_message(&boot);  // this may fail, leaving a zeroed structure

    boot.recovery[sizeof(boot.recovery) - 1] = '\0';  // Ensure termination
    const char *arg = strtok(boot.recovery, "\n");

    if (arg != NULL && !strcmp(arg, "sdota"))
    {
        LOGI("Got arguments from boot message is %s\n", boot.recovery);
        return true;
    }
    else
    {
        LOGI("no boot messages %s\n", boot.recovery);
        return false;
    }
}
#endif

// open a given path, mounting partitions as necessary
FILE*
fopen_path(const char *path, const char *mode) {
    if (ensure_path_mounted(path) != 0) {
        LOGE("Can't mount %s\n", path);
        return NULL;
    }

    // When writing, try to create the containing directory, if necessary.
    // Use generous permissions, the system (init.rc) will reset them.
    if (strchr("wa", mode[0])) dirCreateHierarchy(path, 0777, NULL, 1, sehandle);

    FILE *fp = fopen(path, mode);
    return fp;
}

// close a file, log an error if the error indicator is set
static void
check_and_fclose(FILE *fp, const char *name) {
    fflush(fp);
    if (ferror(fp)) LOGE("Error in %s\n(%s)\n", name, strerror(errno));
    fclose(fp);
}

// command line args come from, in decreasing precedence:
//   - the actual command line
//   - the bootloader control block (one per line, after "recovery")
//   - the contents of COMMAND_FILE (one per line)
static void
get_args(int *argc, char ***argv) {
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    get_bootloader_message(&boot);  // this may fail, leaving a zeroed structure

    if (boot.command[0] != 0 && boot.command[0] != 255) {
        LOGI("Boot command: %.*s\n", sizeof(boot.command), boot.command);
    }

    if (boot.status[0] != 0 && boot.status[0] != 255) {
        LOGI("Boot status: %.*s\n", sizeof(boot.status), boot.status);
    }

    // --- if arguments weren't supplied, look in the bootloader control block
    if (*argc <= 1) {
        boot.recovery[sizeof(boot.recovery) - 1] = '\0';  // Ensure termination
        const char *arg = strtok(boot.recovery, "\n");
        if (arg != NULL && !strcmp(arg, "recovery")) {
            *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
            (*argv)[0] = strdup(arg);
            for (*argc = 1; *argc < MAX_ARGS; ++*argc) {
                if ((arg = strtok(NULL, "\n")) == NULL) break;
                (*argv)[*argc] = strdup(arg);
            }
            LOGI("Got arguments from boot message\n");
        } else if (boot.recovery[0] != 0 && boot.recovery[0] != 255) {
            LOGE("Bad boot message\n\"%.20s\"\n", boot.recovery);
        }
    }
#if 1 //wschen 2012-07-10
      else {
          ensure_path_mounted("/cache");
    }

    if (check_otaupdate_done()) {
        return;
    }
#endif

    // --- if that doesn't work, try the command file
    if (*argc <= 1) {
        FILE *fp = fopen_path(COMMAND_FILE, "r");
        if (fp != NULL) {
            char *argv0 = (*argv)[0];
            *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
            (*argv)[0] = argv0;  // use the same program name

            char buf[MAX_ARG_LENGTH];
            for (*argc = 1; *argc < MAX_ARGS; ++*argc) {
                if (!fgets(buf, sizeof(buf), fp)) break;
                (*argv)[*argc] = strdup(strtok(buf, "\r\n"));  // Strip newline.
            }

            check_and_fclose(fp, COMMAND_FILE);
            LOGI("Got arguments from %s\n", COMMAND_FILE);
        }
    }

    // --> write the arguments we have back into the bootloader control block
    // always boot into recovery after this (until finish_recovery() is called)
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
    int i;
    for (i = 1; i < *argc; ++i) {
        strlcat(boot.recovery, (*argv)[i], sizeof(boot.recovery));
        strlcat(boot.recovery, "\n", sizeof(boot.recovery));
    }
    set_bootloader_message(&boot);
}

static void
set_sdcard_update_bootloader_message() {
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
#if 0 //wschen 2012-07-10
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
#else
    strlcpy(boot.recovery, "sdota\n", sizeof(boot.recovery));
#endif
    set_bootloader_message(&boot);
}

// How much of the temp log we have copied to the copy in cache.
static long tmplog_offset = 0;

static void
copy_log_file(const char* source, const char* destination, int append) {
    FILE *log = fopen_path(destination, append ? "a" : "w");
    if (log == NULL) {
        LOGE("Can't open %s\n", destination);
    } else {
        FILE *tmplog = fopen(source, "r");
        if (tmplog != NULL) {
            if (append) {
                fseek(tmplog, tmplog_offset, SEEK_SET);  // Since last write
            }
            char buf[4096];
            while (fgets(buf, sizeof(buf), tmplog)) fputs(buf, log);
            if (append) {
                tmplog_offset = ftell(tmplog);
            }
            check_and_fclose(tmplog, source);
        }
        check_and_fclose(log, destination);
    }
}

extern "C" {

void write_all_log(void)
{
    copy_log_file(TEMPORARY_LOG_FILE, LOG_FILE, true);
    copy_log_file(TEMPORARY_LOG_FILE, LAST_LOG_FILE, false);
    copy_log_file(TEMPORARY_INSTALL_FILE, LAST_INSTALL_FILE, false);
    chmod(LOG_FILE, 0600);
    chown(LOG_FILE, 1000, 1000);   // system user
    chmod(LAST_LOG_FILE, 0640);
    chmod(LAST_INSTALL_FILE, 0644);

    rename(LAST_LOG_FILE, LAST_LOG_FILE_1);

    sync();  // For good measure.
}

}

// clear the recovery command and prepare to boot a (hopefully working) system,
// copy our log file to cache as well (for the system to read), and
// record any intent we were asked to communicate back to the system.
// this function is idempotent: call it as many times as you like.
static void
finish_recovery(const char *send_intent) {
    // By this point, we're ready to return to the main system...
    if (send_intent != NULL) {
        FILE *fp = fopen_path(INTENT_FILE, "w");
        if (fp == NULL) {
            LOGE("Can't open %s\n", INTENT_FILE);
        } else {
            fputs(send_intent, fp);
            check_and_fclose(fp, INTENT_FILE);
        }
    }

    // Save the locale to cache, so if recovery is next started up
    // without a --locale argument (eg, directly from the bootloader)
    // it will use the last-known locale.
    if (locale != NULL) {
        LOGI("Saving locale \"%s\"\n", locale);
        FILE* fp = fopen_path(LOCALE_FILE, "w");
        fwrite(locale, 1, strlen(locale), fp);
        fflush(fp);
        fsync(fileno(fp));
        check_and_fclose(fp, LOCALE_FILE);
    }

    // Copy logs to cache so the system can find out what happened.
    copy_log_file(TEMPORARY_LOG_FILE, LOG_FILE, true);
    copy_log_file(TEMPORARY_LOG_FILE, LAST_LOG_FILE, false);
    copy_log_file(TEMPORARY_INSTALL_FILE, LAST_INSTALL_FILE, false);
    chmod(LOG_FILE, 0600);
    chown(LOG_FILE, 1000, 1000);   // system user
    chmod(LAST_LOG_FILE, 0640);
    chmod(LAST_INSTALL_FILE, 0644);

    // Reset to normal system boot so recovery won't cycle indefinitely.
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    set_bootloader_message(&boot);

    // Remove the command file, so recovery won't repeat indefinitely.
    if (ensure_path_mounted(COMMAND_FILE) != 0 ||
        (unlink(COMMAND_FILE) && errno != ENOENT)) {
        LOGW("Can't unlink %s\n", COMMAND_FILE);
    }

#if 0 //wschen 2012-07-10
    ensure_path_unmounted(CACHE_ROOT);
#endif
    sync();  // For good measure.
}

static int
erase_volume(const char *volume) {
    ui->SetBackground(RecoveryUI::ERASING);
    ui->SetProgressType(RecoveryUI::INDETERMINATE);
    ui->Print("Formatting %s...\n", volume);

    ensure_path_unmounted(volume);

    if (strcmp(volume, "/cache") == 0) {
        // Any part of the log we'd copied to cache is now gone.
        // Reset the pointer so we copy from the beginning of the temp
        // log.
        tmplog_offset = 0;
    }

    return format_volume(volume);
}

static char*
copy_sideloaded_package(const char* original_path) {
  if (ensure_path_mounted(original_path) != 0) {
    LOGE("Can't mount %s\n", original_path);
    return NULL;
  }

  if (ensure_path_mounted(SIDELOAD_TEMP_DIR) != 0) {
    LOGE("Can't mount %s\n", SIDELOAD_TEMP_DIR);
    return NULL;
  }

  if (mkdir(SIDELOAD_TEMP_DIR, 0700) != 0) {
    if (errno != EEXIST) {
      LOGE("Can't mkdir %s (%s)\n", SIDELOAD_TEMP_DIR, strerror(errno));
      return NULL;
    }
  }

  // verify that SIDELOAD_TEMP_DIR is exactly what we expect: a
  // directory, owned by root, readable and writable only by root.
  struct stat st;
  if (stat(SIDELOAD_TEMP_DIR, &st) != 0) {
    LOGE("failed to stat %s (%s)\n", SIDELOAD_TEMP_DIR, strerror(errno));
    return NULL;
  }
  if (!S_ISDIR(st.st_mode)) {
    LOGE("%s isn't a directory\n", SIDELOAD_TEMP_DIR);
    return NULL;
  }
  if ((st.st_mode & 0777) != 0700) {
    LOGE("%s has perms %o\n", SIDELOAD_TEMP_DIR, st.st_mode);
    return NULL;
  }
  if (st.st_uid != 0) {
    LOGE("%s owned by %lu; not root\n", SIDELOAD_TEMP_DIR, st.st_uid);
    return NULL;
  }

  char copy_path[PATH_MAX];
  strcpy(copy_path, SIDELOAD_TEMP_DIR);
  strcat(copy_path, "/package.zip");

  char* buffer = (char*)malloc(BUFSIZ);
  if (buffer == NULL) {
    LOGE("Failed to allocate buffer\n");
    return NULL;
  }

  size_t read;
  FILE* fin = fopen(original_path, "rb");
  if (fin == NULL) {
    LOGE("Failed to open %s (%s)\n", original_path, strerror(errno));
    return NULL;
  }
  FILE* fout = fopen(copy_path, "wb");
  if (fout == NULL) {
    LOGE("Failed to open %s (%s)\n", copy_path, strerror(errno));
    return NULL;
  }

  while ((read = fread(buffer, 1, BUFSIZ, fin)) > 0) {
    if (fwrite(buffer, 1, read, fout) != read) {
      LOGE("Short write of %s (%s)\n", copy_path, strerror(errno));
      return NULL;
    }
  }

  free(buffer);

  if (fclose(fout) != 0) {
    LOGE("Failed to close %s (%s)\n", copy_path, strerror(errno));
    return NULL;
  }

  if (fclose(fin) != 0) {
    LOGE("Failed to close %s (%s)\n", original_path, strerror(errno));
    return NULL;
  }

  // "adb push" is happy to overwrite read-only files when it's
  // running as root, but we'll try anyway.
  if (chmod(copy_path, 0400) != 0) {
    LOGE("Failed to chmod %s (%s)\n", copy_path, strerror(errno));
    return NULL;
  }

  return strdup(copy_path);
}

static const char**
prepend_title(const char* const* headers) {
    const char* title[] = { "Android system recovery <"
                            EXPAND(RECOVERY_API_VERSION) "e>",
                            "",
                            NULL };

    // count the number of lines in our title, plus the
    // caller-provided headers.
    int count = 0;
    const char* const* p;
    for (p = title; *p; ++p, ++count);
    for (p = (char **)headers; *p; ++p, ++count);

    const char** new_headers = (const char**)malloc((count+1) * sizeof(char*));
    const char** h = new_headers;
    for (p = title; *p; ++p, ++h) *h = *p;
    for (p = (char **)headers; *p; ++p, ++h) *h = *p;
    *h = NULL;

    return new_headers;
}

static int
get_menu_selection(const char* const * headers, const char* const * items,
                   int menu_only, int initial_selection, Device* device) {
    // throw away keys pressed previously, so user doesn't
    // accidentally trigger menu items.
    ui->FlushKeys();

    ui->StartMenu(headers, items, initial_selection);
    int selected = initial_selection;
    int chosen_item = -1;

    while (chosen_item < 0) {
        int key = ui->WaitKey();
        int visible = ui->IsTextVisible();

        if (key == -1) {   // ui_wait_key() timed out
            if (ui->WasTextEverVisible()) {
                continue;
            } else {
                LOGI("timed out waiting for key input; rebooting.\n");
                ui->EndMenu();
                return 0; // XXX fixme
            }
        }

        int action = device->HandleMenuKey(key, visible);

        if (action < 0) {
            switch (action) {
                case Device::kHighlightUp:
                    --selected;
                    selected = ui->SelectMenu(selected);
                    break;
                case Device::kHighlightDown:
                    ++selected;
                    selected = ui->SelectMenu(selected);
                    break;
                case Device::kInvokeItem:
                    chosen_item = selected;
                    break;
                case Device::kNoAction:
                    break;
            }
        } else if (!menu_only) {
            chosen_item = action;
        }
    }

    ui->EndMenu();
    return chosen_item;
}

static int compare_string(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-03-09 

static int sdcard_restore_directory(const char *path, Device *device) {

    const char *MENU_HEADERS[] = { "Choose a backup file to restore:",
                                   path,
                                   "",
                                   NULL };
    DIR *d;
    struct dirent *de;
    d = opendir(path);
    if (d == NULL) {
        LOGE("error opening %s: %s\n", path, strerror(errno));
        ensure_path_unmounted(SDCARD_ROOT);
        return 0;
    }

    const char **headers = prepend_title(MENU_HEADERS);

    int d_size = 0;
    int d_alloc = 10;
    char **dirs = (char**)malloc(d_alloc * sizeof(char *));
    int z_size = 1;
    int z_alloc = 10;
    char **zips = (char**)malloc(z_alloc * sizeof(char *));
    zips[0] = strdup("../");

    while ((de = readdir(d)) != NULL) {
        int name_len = strlen(de->d_name);

        if (de->d_type == DT_DIR) {
            // skip "." and ".." entries
            if (name_len == 1 && de->d_name[0] == '.') {
                continue;
            }
            if (name_len == 2 && de->d_name[0] == '.' && de->d_name[1] == '.') {
                continue;
            }

            if (d_size >= d_alloc) {
                d_alloc *= 2;
                dirs = (char**)realloc(dirs, d_alloc * sizeof(char*));
            }
            dirs[d_size] = (char*)malloc(name_len + 2);
            strcpy(dirs[d_size], de->d_name);
            dirs[d_size][name_len] = '/';
            dirs[d_size][name_len+1] = '\0';
            ++d_size;
        } else if (de->d_type == DT_REG &&
                   name_len >= 7 &&
                   strncasecmp(de->d_name + (name_len - 7), ".backup", 7) == 0) {
            if (z_size >= z_alloc) {
                z_alloc *= 2;
                zips = (char**)realloc(zips, z_alloc * sizeof(char*));
            }
            zips[z_size++] = strdup(de->d_name);
        }
    }
    closedir(d);

    qsort(dirs, d_size, sizeof(char *), compare_string);
    qsort(zips, z_size, sizeof(char *), compare_string);

    // append dirs to the zips list
    if (d_size + z_size + 1 > z_alloc) {
        z_alloc = d_size + z_size + 1;
        zips = (char**)realloc(zips, z_alloc * sizeof(char *));
    }
    memcpy(zips + z_size, dirs, d_size * sizeof(char *));
    free(dirs);
    z_size += d_size;
    zips[z_size] = NULL;

    int result;
    int chosen_item = 0;
    do {
        chosen_item = get_menu_selection(headers, zips, 1, chosen_item, device);

        char* item = zips[chosen_item];
        int item_len = strlen(item);
        if (chosen_item == 0) {          // item 0 is always "../"
            // go up but continue browsing (if the caller is sdcard_directory)
            result = -1;
            break;
        } else if (item[item_len-1] == '/') {
            // recurse down into a subdirectory
            char new_path[PATH_MAX];
            strlcpy(new_path, path, PATH_MAX);
            strlcat(new_path, "/", PATH_MAX);
            strlcat(new_path, item, PATH_MAX);
            new_path[strlen(new_path)-1] = '\0';  // truncate the trailing '/'
            result = sdcard_restore_directory(new_path, device);
            if (result >= 0) {
                break;
            }
        } else {
            // selected a backup file:  attempt to restore it, and return
            // the status to the caller.
            char new_path[PATH_MAX];
#if 1 //wschen 2011-05-16
            struct bootloader_message boot;
            memset(&boot, 0, sizeof(boot));
#endif
            strlcpy(new_path, path, PATH_MAX);
            strlcat(new_path, "/", PATH_MAX);
            strlcat(new_path, item, PATH_MAX);

            ui->Print("\n-- Restore %s ...\n", item);
            //do restore and update result
#if 1 //wschen 2011-05-16
            strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
            strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
            strlcat(boot.recovery, "--restore_data=", sizeof(boot.recovery));
            strlcat(boot.recovery, new_path, sizeof(boot.recovery));
            strlcat(boot.recovery, "\n", sizeof(boot.recovery));
            set_bootloader_message(&boot);
            sync();
#endif
            result = userdata_restore(new_path, 0);

            memset(&boot, 0, sizeof(boot));
            set_bootloader_message(&boot);
            sync();
            break;
        }
    } while (true);

    int i;
    for (i = 0; i < z_size; ++i) {
        free(zips[i]);
    }
    free(zips);
    free(headers);

    if (result != -1) {
        ensure_path_unmounted(SDCARD_ROOT);
    }
    return result;
}

#endif //SUPPORT_DATA_BACKUP_RESTORE

static int
update_directory(const char* path, const char* unmount_when_done,
                 int* wipe_cache, Device* device) {
    ensure_path_mounted(path);

    const char* MENU_HEADERS[] = { "Choose a package to install:",
                                   path,
                                   "",
                                   NULL };
    DIR* d;
    struct dirent* de;
    d = opendir(path);
    if (d == NULL) {
        LOGE("error opening %s: %s\n", path, strerror(errno));
        if (unmount_when_done != NULL) {
            ensure_path_unmounted(unmount_when_done);
        }
        return 0;
    }

    const char** headers = prepend_title(MENU_HEADERS);

    int d_size = 0;
    int d_alloc = 10;
    char** dirs = (char**)malloc(d_alloc * sizeof(char*));
    int z_size = 1;
    int z_alloc = 10;
    char** zips = (char**)malloc(z_alloc * sizeof(char*));
    zips[0] = strdup("../");

    while ((de = readdir(d)) != NULL) {
        int name_len = strlen(de->d_name);

        if (de->d_type == DT_DIR) {
            // skip "." and ".." entries
            if (name_len == 1 && de->d_name[0] == '.') continue;
            if (name_len == 2 && de->d_name[0] == '.' &&
                de->d_name[1] == '.') continue;

            if (d_size >= d_alloc) {
                d_alloc *= 2;
                dirs = (char**)realloc(dirs, d_alloc * sizeof(char*));
            }
            dirs[d_size] = (char*)malloc(name_len + 2);
            strcpy(dirs[d_size], de->d_name);
            dirs[d_size][name_len] = '/';
            dirs[d_size][name_len+1] = '\0';
            ++d_size;
        } else if (de->d_type == DT_REG &&
                   name_len >= 4 &&
                   strncasecmp(de->d_name + (name_len-4), ".zip", 4) == 0) {
            if (z_size >= z_alloc) {
                z_alloc *= 2;
                zips = (char**)realloc(zips, z_alloc * sizeof(char*));
            }
            zips[z_size++] = strdup(de->d_name);
        }
    }
    closedir(d);

    qsort(dirs, d_size, sizeof(char*), compare_string);
    qsort(zips, z_size, sizeof(char*), compare_string);

    // append dirs to the zips list
    if (d_size + z_size + 1 > z_alloc) {
        z_alloc = d_size + z_size + 1;
        zips = (char**)realloc(zips, z_alloc * sizeof(char*));
    }
    memcpy(zips + z_size, dirs, d_size * sizeof(char*));
    free(dirs);
    z_size += d_size;
    zips[z_size] = NULL;

    int result;
    int chosen_item = 0;
    do {
        chosen_item = get_menu_selection(headers, zips, 1, chosen_item, device);

        char* item = zips[chosen_item];
        int item_len = strlen(item);
        if (chosen_item == 0) {          // item 0 is always "../"
            // go up but continue browsing (if the caller is update_directory)
            result = -1;
            break;
        } else if (item[item_len-1] == '/') {
            // recurse down into a subdirectory
            char new_path[PATH_MAX];
            strlcpy(new_path, path, PATH_MAX);
            strlcat(new_path, "/", PATH_MAX);
            strlcat(new_path, item, PATH_MAX);
            new_path[strlen(new_path)-1] = '\0';  // truncate the trailing '/'
            result = update_directory(new_path, unmount_when_done, wipe_cache, device);
            if (result >= 0) break;
        } else {
            // selected a zip file:  attempt to install it, and return
            // the status to the caller.
            char new_path[PATH_MAX];
            strlcpy(new_path, path, PATH_MAX);
            strlcat(new_path, "/", PATH_MAX);
            strlcat(new_path, item, PATH_MAX);

            ui->Print("\n-- Install %s ...\n", path);
            set_sdcard_update_bootloader_message();
            char* copy = copy_sideloaded_package(new_path);
            if (unmount_when_done != NULL) {
                ensure_path_unmounted(unmount_when_done);
            }
            if (copy) {
                result = install_package(copy, wipe_cache, TEMPORARY_INSTALL_FILE);
                free(copy);
            } else {
                result = INSTALL_ERROR;
            }
            break;
        }
    } while (true);

    int i;
    for (i = 0; i < z_size; ++i) free(zips[i]);
    free(zips);
    free(headers);

    if (unmount_when_done != NULL) {
        ensure_path_unmounted(unmount_when_done);
    }
    return result;
}

static void
wipe_data(int confirm, Device* device) {
#if 1 //wschen 2012-04-12
    struct phone_encrypt_state ps;
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
#endif

    if (confirm) {
        static const char** title_headers = NULL;

        if (title_headers == NULL) {
            const char* headers[] = { "Confirm wipe of all user data?",
                                      "  THIS CAN NOT BE UNDONE.",
                                      "",
                                      NULL };
            title_headers = prepend_title((const char**)headers);
        }

        const char* items[] = { " No",
                                " No",
                                " No",
                                " No",
                                " No",
                                " No",
                                " No",
                                " Yes -- delete all user data",   // [7]
                                " No",
                                " No",
                                " No",
                                NULL };

        int chosen_item = get_menu_selection(title_headers, items, 1, 0, device);
        if (chosen_item != 7) {
            return;
        }
    }

    ui->Print("\n-- Wiping data...\n");
#if 1 //wschen 2011-05-16
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
    strlcat(boot.recovery, "--wipe_data\n", sizeof(boot.recovery));
    set_bootloader_message(&boot);
    sync();
#endif
    device->WipeData();
#ifdef SPECIAL_FACTORY_RESET //wschen 2011-06-16
    if (special_factory_reset()) {
        return;
    }
#else
    erase_volume("/data");
#endif
    erase_volume("/cache");
#if 1 //wschen 2011-05-16
    memset(&boot, 0, sizeof(boot));
    set_bootloader_message(&boot);
    ps.state = 0;
    set_phone_encrypt_state(&ps);
    sync();
#endif
    ui->Print("Data wipe complete.\n");
}

#if 1 //wschen 2012-07-10
static void prompt_error_message(int reason)
{
    switch (reason)  {
        case INSTALL_NO_SDCARD:
            ui->Print("No SD-Card.\n");
            break;
        case INSTALL_NO_UPDATE_PACKAGE:
            ui->Print("Can not find update.zip.\n");
            break;
        case INSTALL_NO_KEY:
            ui->Print("Failed to load keys\n");
            break;
        case INSTALL_SIGNATURE_ERROR:
            ui->Print("Signature verification failed\n");
            break;
        case INSTALL_CORRUPT:
            ui->Print("The update.zip is corrupted\n");
            break;
        case INSTALL_FILE_SYSTEM_ERROR:
            ui->Print("Can't create/copy file\n");
            break;
        case INSTALL_ERROR:
            ui->Print("Update.zip is not correct\n");
            break;
    }
}
#endif

static void
prompt_and_wait(Device* device, int status) {
    const char* const* headers = prepend_title(device->GetMenuHeaders());

#if 0 //wschen 2012-07-10
    for (;;) {
        finish_recovery(NULL);
        switch (status) {
            case INSTALL_SUCCESS:
            case INSTALL_NONE:
                ui->SetBackground(RecoveryUI::NO_COMMAND);
                break;

            case INSTALL_ERROR:
            case INSTALL_CORRUPT:
                ui->SetBackground(RecoveryUI::ERROR);
                break;
        }
        ui->SetProgressType(RecoveryUI::EMPTY);

        int chosen_item = get_menu_selection(headers, device->GetMenuItems(), 0, 0, device);

        // device-specific code may take some action here.  It may
        // return one of the core actions handled in the switch
        // statement below.
        chosen_item = device->InvokeMenuItem(chosen_item);

        int wipe_cache;
        switch (chosen_item) {
            case Device::REBOOT:
                return;

            case Device::WIPE_DATA:
                wipe_data(ui->IsTextVisible(), device);
                if (!ui->IsTextVisible()) return;
                break;

            case Device::WIPE_CACHE:
                ui->ShowText(false);
                ui->Print("\n-- Wiping cache...\n");
                erase_volume("/cache");
                ui->Print("Cache wipe complete.\n");
                if (!ui->IsTextVisible()) return;
                break;

            case Device::APPLY_EXT:
                // Some packages expect /cache to be mounted (eg,
                // standard incremental packages expect to use /cache
                // as scratch space).
                ensure_path_mounted(CACHE_ROOT);
                status = update_directory(SDCARD_ROOT, SDCARD_ROOT, &wipe_cache, device);
                if (status == INSTALL_SUCCESS && wipe_cache) {
                    ui->Print("\n-- Wiping cache (at package request)...\n");
                    if (erase_volume("/cache")) {
                        ui->Print("Cache wipe failed.\n");
                    } else {
                        ui->Print("Cache wipe complete.\n");
                    }
                }
                if (status >= 0) {
                    if (status != INSTALL_SUCCESS) {
                        ui->SetBackground(RecoveryUI::ERROR);
                        ui->Print("Installation aborted.\n");
                    } else if (!ui->IsTextVisible()) {
                        return;  // reboot if logs aren't visible
                    } else {
                        ui->Print("\nInstall from sdcard complete.\n");
                    }
                }
                break;

            case Device::APPLY_CACHE:
                // Don't unmount cache at the end of this.
                status = update_directory(CACHE_ROOT, NULL, &wipe_cache, device);
                if (status == INSTALL_SUCCESS && wipe_cache) {
                    ui->Print("\n-- Wiping cache (at package request)...\n");
                    if (erase_volume("/cache")) {
                        ui->Print("Cache wipe failed.\n");
                    } else {
                        ui->Print("Cache wipe complete.\n");
                    }
                }
                if (status >= 0) {
                    if (status != INSTALL_SUCCESS) {
                        ui->SetBackground(RecoveryUI::ERROR);
                        ui->Print("Installation aborted.\n");
                    } else if (!ui->IsTextVisible()) {
                        return;  // reboot if logs aren't visible
                    } else {
                        ui->Print("\nInstall from cache complete.\n");
                    }
                }
                break;

            case Device::APPLY_ADB_SIDELOAD:
                ensure_path_mounted(CACHE_ROOT);
                status = apply_from_adb(ui, &wipe_cache, TEMPORARY_INSTALL_FILE);
                if (status >= 0) {
                    if (status != INSTALL_SUCCESS) {
                        ui->SetBackground(RecoveryUI::ERROR);
                        ui->Print("Installation aborted.\n");
                    } else if (!ui->IsTextVisible()) {
                        return;  // reboot if logs aren't visible
                    } else {
                        ui->Print("\nInstall from ADB complete.\n");
                    }
                }
                break;
        }
    }
#else
    for (;;) {
        ui->Print("\n");
        if (!check_otaupdate_done()) {
            LOGI("[1]check the otaupdate is done!\n");
            finish_recovery(NULL);
            switch (status) {
                case INSTALL_SUCCESS:
                case INSTALL_NONE:
                    ui->SetBackground(RecoveryUI::NO_COMMAND);
                    break;

                case INSTALL_ERROR:
                case INSTALL_CORRUPT:
                    ui->SetBackground(RecoveryUI::ERROR);
                    break;
            }
            ui->SetProgressType(RecoveryUI::EMPTY);

            int chosen_item = get_menu_selection(headers, device->GetMenuItems(), 0, 0, device);

            // device-specific code may take some action here.  It may
            // return one of the core actions handled in the switch
            // statement below.
            chosen_item = device->InvokeMenuItem(chosen_item);
            // clear screen
            ui->Print("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");

            int wipe_cache;

            switch (chosen_item) {
                case Device::REBOOT:
                    return;

                case Device::WIPE_DATA:
                    wipe_data(ui->IsTextVisible(), device);
                    if (!ui->IsTextVisible()) return;
                    break;

                case Device::WIPE_CACHE:
                    {
#if 1 //wschen 2011-05-16
                        struct bootloader_message boot;
#endif
                        ui->Print("\n-- Wiping cache...\n");
#if 1 //wschen 2011-05-16
                        memset(&boot, 0, sizeof(boot));
                        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
                        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
                        strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
                        set_bootloader_message(&boot);
                        sync();
#endif
                        erase_volume("/cache");
                        ui->Print("Cache wipe complete.\n");
#if 1 //wschen 2011-05-16
                        memset(&boot, 0, sizeof(boot));
                        set_bootloader_message(&boot);
                        sync();
#endif
                        if (!ui->IsTextVisible()) return;
                    }
                    break;

                case Device::APPLY_EXT:
                    // Some packages expect /cache to be mounted (eg,
                    // standard incremental packages expect to use /cache
                    // as scratch space).
                    ensure_path_mounted(CACHE_ROOT);
                    status = update_directory(SDCARD_ROOT, SDCARD_ROOT, &wipe_cache, device);
                    if (status == INSTALL_SUCCESS && wipe_cache) {
#if 1 //wschen 2012-07-10
                        struct bootloader_message boot;
#endif
#if 1 //wschen 2012-07-10
                        memset(&boot, 0, sizeof(boot));
                        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
                        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
                        if (wipe_cache == 1) {
                            strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
                            ui->Print("\n-- Wiping cache (at package request)...\n");
                        } else {
                            strlcat(boot.recovery, "--wipe_data\n", sizeof(boot.recovery));
                            ui->Print("\n-- Wiping data (at package request)...\n");
                        }
                        set_bootloader_message(&boot);
                        sync();
#endif
                        if (wipe_cache == 1) {
                            if (erase_volume("/cache")) {
                                ui->Print("Cache wipe failed.\n");
                            } else {
                                ui->Print("Cache wipe complete.\n");
                            }
                        } else {
#ifdef SPECIAL_FACTORY_RESET //wschen 2012-07-25 
                            if (special_factory_reset()) {
                                ui->Print("Data wipe failed.\n");
                            } else {
                                ui->Print("Data wipe complete.\n");
                            }
#endif
                        }
#if 1 //wschen 2012-07-10
                        memset(&boot, 0, sizeof(boot));
                        set_bootloader_message(&boot);
                        sync();
#endif
                    }
                    if (status >= 0) {
                        if (status != INSTALL_SUCCESS) {
                            ui->SetBackground(RecoveryUI::ERROR);
                            ui->Print("Installation aborted.\n");
                        } else if (!ui->IsTextVisible()) {
#if 1 //wschen 2012-07-10
                            finish_recovery(NULL);
#endif
                            return;  // reboot if logs aren't visible
                        } else {
                            ui->Print("\nInstall from sdcard complete.\n");
#if 1 //wschen 2012-07-10
                            finish_recovery(NULL);
#endif
                        }
                    }
                    break;

#if defined(SUPPORT_SDCARD2) && !defined(MTK_SHARED_SDCARD) //wschen 2012-11-15
                case Device::APPLY_SDCARD2:
                    // Some packages expect /cache to be mounted (eg,
                    // standard incremental packages expect to use /cache
                    // as scratch space).
                    ensure_path_mounted(CACHE_ROOT);
                    status = update_directory(SDCARD2_ROOT, SDCARD2_ROOT, &wipe_cache, device);
                    if (status == INSTALL_SUCCESS && wipe_cache) {
                        struct bootloader_message boot;
                        memset(&boot, 0, sizeof(boot));
                        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
                        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
                        if (wipe_cache == 1) {
                            strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
                            ui->Print("\n-- Wiping cache (at package request)...\n");
                        } else {
                            strlcat(boot.recovery, "--wipe_data\n", sizeof(boot.recovery));
                            ui->Print("\n-- Wiping data (at package request)...\n");
                        }
                        set_bootloader_message(&boot);
                        sync();
                        if (wipe_cache == 1) {
                            if (erase_volume("/cache")) {
                                ui->Print("Cache wipe failed.\n");
                            } else {
                                ui->Print("Cache wipe complete.\n");
                            }
                        } else {
#ifdef SPECIAL_FACTORY_RESET //wschen 2012-07-25 
                            if (special_factory_reset()) {
                                ui->Print("Data wipe failed.\n");
                            } else {
                                ui->Print("Data wipe complete.\n");
                            }
#endif
                        }
                        memset(&boot, 0, sizeof(boot));
                        set_bootloader_message(&boot);
                        sync();
                    }
                    if (status >= 0) {
                        if (status != INSTALL_SUCCESS) {
                            ui->SetBackground(RecoveryUI::ERROR);
                            ui->Print("Installation aborted.\n");
                        } else if (!ui->IsTextVisible()) {
                            finish_recovery(NULL);
                            return;  // reboot if logs aren't visible
                        } else {
                            ui->Print("\nInstall from sdcard2 complete.\n");
                            finish_recovery(NULL);
                        }
                    }
                    break;
#endif //SUPPORT_SDCARD2

                case Device::APPLY_CACHE:
                    // Don't unmount cache at the end of this.
                    status = update_directory(CACHE_ROOT, NULL, &wipe_cache, device);
                    if (status == INSTALL_SUCCESS && wipe_cache) {
#if 1 //wschen 2012-07-10
                        struct bootloader_message boot;
                        memset(&boot, 0, sizeof(boot));
                        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
                        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
                        if (wipe_cache == 1) {
                            strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
                            ui->Print("\n-- Wiping cache (at package request)...\n");
                        } else {
                            strlcat(boot.recovery, "--wipe_data\n", sizeof(boot.recovery));
                            ui->Print("\n-- Wiping data (at package request)...\n");
                        }
                        set_bootloader_message(&boot);
                        sync();
#endif
                        if (wipe_cache == 1) {
                            if (erase_volume("/cache")) {
                                ui->Print("Cache wipe failed.\n");
                            } else {
                                ui->Print("Cache wipe complete.\n");
                            }
                        } else {
#ifdef SPECIAL_FACTORY_RESET //wschen 2012-07-25 
                            if (special_factory_reset()) {
                                ui->Print("Data wipe failed.\n");
                            } else {
                                ui->Print("Data wipe complete.\n");
                            }
#endif
                        }
#if 1 //wschen 2012-07-10
                        memset(&boot, 0, sizeof(boot));
                        set_bootloader_message(&boot);
                        sync();
#endif
                    }
                    if (status >= 0) {
                        if (status != INSTALL_SUCCESS) {
                            ui->SetBackground(RecoveryUI::ERROR);
                            ui->Print("Installation aborted.\n");
                        } else if (!ui->IsTextVisible()) {
                            return;  // reboot if logs aren't visible
                        } else {
                            ui->Print("\nInstall from cache complete.\n");
                        }
                    }
                    break;

#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-03-09 

                case Device::USER_DATA_BACKUP:
                    {
                        int status;

                        ui->SetBackground(RecoveryUI::INSTALLING_UPDATE);
                        ui->SetProgressType(RecoveryUI::EMPTY);
                        ui->ShowProgress(1.0, 0);

                        status = userdata_backup();

                        if (status == INSTALL_SUCCESS) {
                            ui->SetBackground(RecoveryUI::NONE);
                            ui->SetProgressType(RecoveryUI::EMPTY);
                            ui->Print("Backup user data complete.\n");
                            finish_recovery(NULL);
                        } else {
                            ui->SetBackground(RecoveryUI::ERROR);
                        }
                    }
                    break;
                case Device::USER_DATA_RESTORE:
                    {
                        int status;

                        if (ensure_path_mounted(SDCARD_ROOT) != 0) {
                            ui->SetBackground(RecoveryUI::ERROR);
                            ui->Print("No SD-Card.\n");
                            break;
                        }

                        status = sdcard_restore_directory(SDCARD_ROOT, device);

                        if (status >= 0) {
                            if (status != INSTALL_SUCCESS) {
                                ui->SetBackground(RecoveryUI::ERROR);
                            } else {
#if 1 //wschen 2011-05-16
                                struct bootloader_message boot;
                                memset(&boot, 0, sizeof(boot));
                                set_bootloader_message(&boot);
                                sync();
#endif
                                ui->SetBackground(RecoveryUI::NONE);
                                ui->SetProgressType(RecoveryUI::EMPTY);
                                if (!ui->IsTextVisible()) {
                                    finish_recovery(NULL);
                                    return;  // reboot if logs aren't visible
                                } else {
                                    ui->Print("Restore user data complete.\n");
                                    finish_recovery(NULL);
                                }
                            }
                        }
                    }
                    break;
#endif
                case Device::APPLY_ADB_SIDELOAD:
                    ensure_path_mounted(CACHE_ROOT);
                    status = apply_from_adb(ui, &wipe_cache, TEMPORARY_INSTALL_FILE);

#if 1 //wschen 2012-07-25
                    if ((status == INSTALL_SUCCESS) && wipe_cache) {
                        struct bootloader_message boot;
                        memset(&boot, 0, sizeof(boot));
                        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
                        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
                        if (wipe_cache == 1) {
                            strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
                            ui->Print("\n-- Wiping cache (at package request)...\n");
                        } else {
                            strlcat(boot.recovery, "--wipe_data\n", sizeof(boot.recovery));
                            ui->Print("\n-- Wiping data (at package request)...\n");
                        }
                        set_bootloader_message(&boot);
                        sync();
                        if (wipe_cache == 1) {
                            if (erase_volume("/cache")) {
                                ui->Print("Cache wipe failed.\n");
                            } else {
                                ui->Print("Cache wipe complete.\n");
                            }
                        } else {
#ifdef SPECIAL_FACTORY_RESET //wschen 2012-07-25 
                            if (special_factory_reset()) {
                                ui->Print("Data wipe failed.\n");
                            } else {
                                ui->Print("Data wipe complete.\n");
                            }
#endif
                        }
                        memset(&boot, 0, sizeof(boot));
                        set_bootloader_message(&boot);
                        sync();
                    }
#endif

                    if (status >= 0) {
                        if (status != INSTALL_SUCCESS) {
                            ui->SetBackground(RecoveryUI::ERROR);
                            ui->Print("Installation aborted.\n");
                        } else if (!ui->IsTextVisible()) {
                            return;  // reboot if logs aren't visible
                        } else {
                            ui->Print("\nInstall from ADB complete.\n");
                        }
                    }
                    break;
            }
        } else {
            ui->SetProgressType(RecoveryUI::EMPTY);
            ui->Print("\n Please continue to update your system !\n");
            int chosen_item = get_menu_selection(headers, device->GetForceMenuItems(), 0, 0, device);

            // device-specific code may take some action here.  It may
            // return one of the core actions handled in the switch
            // statement below.
            chosen_item = device->InvokeForceMenuItem(chosen_item);

            switch (chosen_item)
            {
                case Device::REBOOT:
                    return;

                case Device::FORCE_APPLY_SDCARD_SIDELOAD:
                    {
                    int wipe_cache;
                    //M Removed by QA's resutest.
                    //ui->Print("\n-- Install from sdcard...\n");
                    set_sdcard_update_bootloader_message();
                    int status = update_directory(SDCARD_ROOT, SDCARD_ROOT, &wipe_cache, device);
                    if ((status == INSTALL_SUCCESS) && wipe_cache) {
                        struct bootloader_message boot;
                        memset(&boot, 0, sizeof(boot));
                        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
                        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
                        if (wipe_cache == 1) {
                            strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
                            ui->Print("\n-- Wiping cache (at package request)...\n");
                        } else {
                            strlcat(boot.recovery, "--wipe_data\n", sizeof(boot.recovery));
                            ui->Print("\n-- Wiping data (at package request)...\n");
                        }
                        set_bootloader_message(&boot);
                        sync();
                        if (wipe_cache == 1) {
                            if (erase_volume("/cache")) {
                                ui->Print("Cache wipe failed.\n");
                            } else {
                                ui->Print("Cache wipe complete.\n");
                            }
                        } else {
#ifdef SPECIAL_FACTORY_RESET //wschen 2012-07-25 
                            if (special_factory_reset()) {
                                ui->Print("Data wipe failed.\n");
                            } else {
                                ui->Print("Data wipe complete.\n");
                            }
#endif
                        }
                        memset(&boot, 0, sizeof(boot));
                        set_bootloader_message(&boot);
                        sync();
                    }

                    if (status >= 0) {
                        if (status != INSTALL_SUCCESS) {
                            ui->SetBackground(RecoveryUI::ERROR);
                            prompt_error_message(status);
                            ui->Print("Installation aborted.\n");
                        } else if (!ui->IsTextVisible()) {
                            finish_recovery(NULL);
                            return;  // reboot if logs aren't visible
                        } else {
                            ui->Print("\nInstall from sdcard complete.\n");
                            finish_recovery(NULL);
                        }
                    }
                    }
                    break;

#if defined(SUPPORT_SDCARD2) && !defined(MTK_SHARED_SDCARD) //wschen 2012-11-15

                case Device::FORCE_APPLY_SDCARD2_SIDELOAD:
                    {
                        int wipe_cache;
                        set_sdcard_update_bootloader_message();
                        int status = update_directory(SDCARD2_ROOT, SDCARD2_ROOT, &wipe_cache, device);
                        if ((status == INSTALL_SUCCESS) && wipe_cache) {
                            struct bootloader_message boot;
                            memset(&boot, 0, sizeof(boot));
                            strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
                            strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
                            if (wipe_cache == 1) {
                                strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
                                ui->Print("\n-- Wiping cache (at package request)...\n");
                            } else {
                                strlcat(boot.recovery, "--wipe_data\n", sizeof(boot.recovery));
                                ui->Print("\n-- Wiping data (at package request)...\n");
                            }
                            set_bootloader_message(&boot);
                            sync();
                            if (wipe_cache == 1) {
                                if (erase_volume("/cache")) {
                                    ui->Print("Cache wipe failed.\n");
                                } else {
                                    ui->Print("Cache wipe complete.\n");
                                }
                            } else {
#ifdef SPECIAL_FACTORY_RESET //wschen 2012-07-25 
                                if (special_factory_reset()) {
                                    ui->Print("Data wipe failed.\n");
                                } else {
                                    ui->Print("Data wipe complete.\n");
                                }
#endif
                            }
                            memset(&boot, 0, sizeof(boot));
                            set_bootloader_message(&boot);
                            sync();
                        }

                        if (status >= 0) {
                            if (status != INSTALL_SUCCESS) {
                                ui->SetBackground(RecoveryUI::ERROR);
                                prompt_error_message(status);
                                ui->Print("Installation aborted.\n");
                            } else if (!ui->IsTextVisible()) {
                                finish_recovery(NULL);
                                return;  // reboot if logs aren't visible
                            } else {
                                ui->Print("\nInstall from sdcard2 complete.\n");
                                finish_recovery(NULL);
                            }
                        }
                    }
                    break;
#endif //SUPPORT_SDCARD2
            }
        }
    }
#endif
}

static void
print_property(const char *key, const char *name, void *cookie) {
    printf("%s=%s\n", key, name);
}

static void
load_locale_from_cache() {
    FILE* fp = fopen_path(LOCALE_FILE, "r");
    char buffer[80];
    if (fp != NULL) {
        fgets(buffer, sizeof(buffer), fp);
        int j = 0;
        unsigned int i;
        for (i = 0; i < sizeof(buffer) && buffer[i]; ++i) {
            if (!isspace(buffer[i])) {
                buffer[j++] = buffer[i];
            }
        }
        buffer[j] = 0;
        locale = strdup(buffer);
        check_and_fclose(fp, LOCALE_FILE);
    }
}

#if 1 //wschen 2012-07-10
static bool remove_mota_file(const char *file_name)
{
    int  ret = 0;

    //LOG_INFO("[%s] %s\n", __func__, file_name);

    ret = unlink(file_name);

    if (ret == 0)
		return true;

	if (ret < 0 && errno == ENOENT)
		return true;

    return false;
}

static void write_result_file(const char *file_name, int result)
{
#ifdef MTK_EMMC_SUPPORT

    if (INSTALL_SUCCESS == result) {
        set_ota_result(1);
    } else {
        set_ota_result(0);
    }

#else
    char  dir_name[256];

    ensure_path_mounted("/data");

    strcpy(dir_name, file_name);
    char *p = strrchr(dir_name, '/');
    *p = 0;

    fprintf(stdout, "dir_name = %s\n", dir_name);

    if (opendir(dir_name) == NULL)  {
        fprintf(stdout, "dir_name = '%s' does not exist, create it.\n", dir_name);
        if (mkdir(dir_name, 0666))  {
            fprintf(stdout, "can not create '%s' : %s\n", dir_name, strerror(errno));
            return;
        }
    }

    int result_fd = open(file_name, O_RDWR | O_CREAT, 0644);

    if (result_fd < 0) {
        fprintf(stdout, "cannot open '%s' for output : %s\n", file_name, strerror(errno));
        return;
    }

    //LOG_INFO("[%s] %s %d\n", __func__, file_name, result);

    char buf[4];
    if (INSTALL_SUCCESS == result)
        strcpy(buf, "1");
    else
        strcpy(buf, "0");
    write(result_fd, buf, 1);
    close(result_fd);
#endif
}
#endif

int
main(int argc, char **argv) {
    time_t start = time(NULL);

    // If these fail, there's not really anywhere to complain...
    freopen(TEMPORARY_LOG_FILE, "a", stdout); setbuf(stdout, NULL);
    freopen(TEMPORARY_LOG_FILE, "a", stderr); setbuf(stderr, NULL);

    // If this binary is started with the single argument "--adbd",
    // instead of being the normal recovery binary, it turns into kind
    // of a stripped-down version of adbd that only supports the
    // 'sideload' command.  Note this must be a real argument, not
    // anything in the command file or bootloader control block; the
    // only way recovery should be run with this argument is when it
    // starts a copy of itself from the apply_from_adb() function.
    if (argc == 2 && strcmp(argv[1], "--adbd") == 0) {
        adb_main();
        return 0;
    }

    printf("Starting recovery on %s", ctime(&start));

#if defined(SUPPORT_SDCARD2) && !defined(MTK_SHARED_SDCARD) //wschen 2012-11-15
    struct stat sd2_st;
    if (stat(SDCARD2_ROOT, &sd2_st) == -1) {
        if (errno == ENOENT) {
            if (mkdir(SDCARD2_ROOT, S_IRWXU | S_IRWXG | S_IRWXO) == 0) {
                printf("Trun on SUPPORT_SDCARD2\n");
            } else {
                printf("mkdir fail %s\n", strerror(errno));
            }
        }
    }
#endif


    load_volume_table();
    get_args(&argc, &argv);

    int previous_runs = 0;
    const char *send_intent = NULL;
    const char *update_package = NULL;
    int wipe_data = 0, wipe_cache = 0, show_text = 0;
    bool just_exit = false;

#if 1 //wschen 2012-07-10
#ifdef SUPPORT_FOTA
    const char *fota_delta_path = NULL;
#endif
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-05-16 
    const char *restore_data_path = NULL;
#endif //SUPPORT_DATA_BACKUP_RESTORE
#endif

    int arg;
    while ((arg = getopt_long(argc, argv, "", OPTIONS, NULL)) != -1) {
        switch (arg) {
        case 'p': previous_runs = atoi(optarg); break;
        case 's': send_intent = optarg; break;
        case 'u': update_package = optarg; break;
        case 'w': wipe_data = wipe_cache = 1; break;
        case 'c': wipe_cache = 1; break;
        case 't': show_text = 1; break;
        case 'x': just_exit = true; break;
        case 'l': locale = optarg; break;
#if 1 //wschen 2012-07-10
#ifdef SUPPORT_FOTA
        case 'f': fota_delta_path = optarg; break;
#endif
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-05-16 
        case 'r': restore_data_path = optarg; break;
#endif //SUPPORT_DATA_BACKUP_RESTORE
#endif
        case '?':
            LOGE("Invalid command argument\n");
            continue;
        }
    }
    if (locale == NULL) {
        load_locale_from_cache();
    }
    printf("locale is [%s]\n", locale);

    Device* device = make_device();
    ui = device->GetUI();

    ui->Init();
    ui->SetLocale(locale);
    ui->SetBackground(RecoveryUI::NONE);
    if (show_text) ui->ShowText(true);

#ifdef HAVE_SELINUX
    struct selinux_opt seopts[] = {
      { SELABEL_OPT_PATH, "/file_contexts" }
    };

    sehandle = selabel_open(SELABEL_CTX_FILE, seopts, 1);

    if (!sehandle) {
        fprintf(stderr, "Warning: No file_contexts\n");
        ui->Print("Warning:  No file_contexts\n");
    }
#endif

    device->StartRecovery();

    printf("Command:");
    for (arg = 0; arg < argc; arg++) {
        printf(" \"%s\"", argv[arg]);
    }
    printf("\n");

    /* ----------------------------- */
    /* SECURE BOOT INIT              */    
    /* ----------------------------- */    
#ifdef SUPPORT_SBOOT_UPDATE
    sec_init(false);
#endif

    if (update_package) {
        // For backwards compatibility on the cache partition only, if
        // we're given an old 'root' path "CACHE:foo", change it to
        // "/cache/foo".
        if (strncmp(update_package, "CACHE:", 6) == 0) {
            int len = strlen(update_package) + 10;
            char* modified_path = (char*)malloc(len);
            strlcpy(modified_path, "/cache/", len);
            strlcat(modified_path, update_package+6, len);
            printf("(replacing path \"%s\" with \"%s\")\n",
                   update_package, modified_path);
            update_package = modified_path;
        }
    }
    printf("\n");

    property_list(print_property, NULL);
    printf("\n");

#if 0 //wschen 2012-07-10
    int status = INSTALL_SUCCESS;

    if (update_package != NULL) {
        status = install_package(update_package, &wipe_cache, TEMPORARY_INSTALL_FILE);
        if (status == INSTALL_SUCCESS && wipe_cache) {
            if (erase_volume("/cache")) {
                LOGE("Cache wipe (requested by package) failed.");
            }
        }
        if (status != INSTALL_SUCCESS) ui->Print("Installation aborted.\n");
    } else if (wipe_data) {
        if (device->WipeData()) status = INSTALL_ERROR;
        if (erase_volume("/data")) status = INSTALL_ERROR;
        if (wipe_cache && erase_volume("/cache")) status = INSTALL_ERROR;
        if (status != INSTALL_SUCCESS) ui->Print("Data wipe failed.\n");
    } else if (wipe_cache) {
        if (wipe_cache && erase_volume("/cache")) status = INSTALL_ERROR;
        if (status != INSTALL_SUCCESS) ui->Print("Cache wipe failed.\n");
    } else if (!just_exit) {
        status = INSTALL_NONE;  // No command specified
        ui->SetBackground(RecoveryUI::NO_COMMAND);
    }

    if (status == INSTALL_ERROR || status == INSTALL_CORRUPT) {
        ui->SetBackground(RecoveryUI::ERROR);
    }
    if (status != INSTALL_SUCCESS || ui->IsTextVisible()) {
        prompt_and_wait(device, status);
    }

#else

#ifdef SUPPORT_FOTA
    struct stat st;

    fprintf(stdout, "check update_package or fota_delta_path.\n");

    if (update_package || fota_delta_path) {
#ifdef FOTA_FIRST
        if (fota_delta_path) {
            fprintf(stdout, "fota_delta_path exist\n");
            if (find_fota_delta_package(fota_delta_path)) {
                fprintf(stdout, "FOTA_FIRST : fota_delta_path\n");
                update_package = 0;
            } else if (find_fota_delta_package(DEFAULT_FOTA_FOLDER)) {
                fprintf(stdout, "FOTA_FIRST : DEFAULT_FOTA_FOLDER\n");
                update_package = 0;
                fota_delta_path = strdup(DEFAULT_FOTA_FOLDER);
            } else if (!lstat(DEFAULT_MOTA_FILE, &st)) {
                fprintf(stdout, "FOTA_FIRST : DEFAULT_MOTA_FILE\n");
                update_package = strdup(DEFAULT_MOTA_FILE);
                fota_delta_path = 0;
            }
        } else if (update_package) {
            fprintf(stdout, "update_package exist\n");
            if (!lstat(update_package, &st)) {
                fprintf(stdout, "FOTA_SECOND : update_package\n");
            } else if (!lstat(DEFAULT_MOTA_FILE, &st)) {
                fprintf(stdout, "FOTA_SECOND : DEFAULT_MOTA_FILE\n");
                fota_delta_path = 0;
                update_package = strdup(DEFAULT_MOTA_FILE);
            } else if (find_fota_delta_package(DEFAULT_FOTA_FOLDER)) {
                fprintf(stdout, "FOTA_SECOND : DEFAULT_FOTA_FOLDER\n");
                update_package = 0;
                fota_delta_path = strdup(DEFAULT_FOTA_FOLDER);
            }
        }
#elif defined(MOTA_FIRST)
        if (update_package) {
            if (!lstat(update_package, &st)) {
                fota_delta_path = 0;
            } else if (!lstat(DEFAULT_MOTA_FILE, &st)) {
                fota_delta_path = 0;
            } else if (find_fota_delta_package(DEFAULT_FOTA_FOLDER)) {
                update_package = 0;
                fota_delta_path = strdup(DEFAULT_FOTA_FOLDER);
            }
        } else if (fota_delta_path) {
            if (find_fota_delta_package(fota_delta_path)) {
                update_package = 0;
            } else if (find_fota_delta_package(DEFAULT_FOTA_FOLDER)) {
                update_package = 0;
                fota_delta_path = strdup(DEFAULT_FOTA_FOLDER);
            } else if (!lstat(DEFAULT_MOTA_FILE, &st)) {
                fota_delta_path = 0;
                update_package = strdup(DEFAULT_MOTA_FILE);
            }
        }
#else
        #error must specify FOTA_FIRST or MOTA_FIRST
#endif
    }
#endif

    fprintf(stdout, "update_package = %s\n", update_package ? update_package : "NULL");
#ifdef SUPPORT_FOTA
    fprintf(stdout, "fota_delta_path = %s\n", fota_delta_path ? fota_delta_path : "NULL");
#endif

    int status = INSTALL_SUCCESS;

    if (update_package != NULL) {
            struct bootloader_message boot;
            memset(&boot, 0, sizeof(boot));
            strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
            strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
            strlcat(boot.recovery, "--update_package=", sizeof(boot.recovery));
            strlcat(boot.recovery, update_package, sizeof(boot.recovery));
            set_bootloader_message(&boot);
            sync();

            status = install_package(update_package, &wipe_cache, TEMPORARY_INSTALL_FILE);
            if (status == INSTALL_SUCCESS && wipe_cache) {
                struct bootloader_message boot;
                memset(&boot, 0, sizeof(boot));
                strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
                strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
                strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
                set_bootloader_message(&boot);
                sync();

                if (erase_volume("/cache")) {
                    LOGE("Cache wipe (request by package) failed.");
                }

                memset(&boot, 0, sizeof(boot));
                set_bootloader_message(&boot);
                sync();
            }

        prompt_error_message(status);
        if (status != INSTALL_SUCCESS) {
            ui->Print("Installation aborted.\n");
        }
    } else if (wipe_data) {

#if 1 //wschen 2012-04-12
        struct phone_encrypt_state ps;
#endif
#if 1 //wschen 2012-03-21
        struct bootloader_message boot;
        memset(&boot, 0, sizeof(boot));
        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
        strlcat(boot.recovery, "--wipe_data\n", sizeof(boot.recovery));
        set_bootloader_message(&boot);
        sync();
#endif
        if (device->WipeData()) status = INSTALL_ERROR;
#ifdef SPECIAL_FACTORY_RESET //wschen 2011-06-16
        if (special_factory_reset()) status = INSTALL_ERROR;
#else
        if (erase_volume("/data")) status = INSTALL_ERROR;
#endif //SPECIAL_FACTORY_RESET
        if (wipe_cache && erase_volume("/cache")) status = INSTALL_ERROR;
#if 1 //wschen 2012-04-19 write to /dev/misc and reboot soon, it may not really write to flash
        if (status != INSTALL_SUCCESS) {
            ui->Print("Data wipe failed.\n");
        } else {
            memset(&boot, 0, sizeof(boot));
            set_bootloader_message(&boot);
#if 1 //wschen 2012-04-12
            ps.state = 0;
            set_phone_encrypt_state(&ps);
#endif
            sync();
        }
#endif
    } else if (wipe_cache) {
#if 1 //wschen 2012-03-21
        struct bootloader_message boot;
        memset(&boot, 0, sizeof(boot));
        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
        strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
        set_bootloader_message(&boot);
        sync();
#endif
        if (erase_volume("/cache")) status = INSTALL_ERROR;
#if 1 //wschen 2012-04-19 write to /dev/misc and reboot soon, it may not really write to flash
        if (status != INSTALL_SUCCESS) {
            ui->Print("Cache wipe failed.\n");
        } else {
            memset(&boot, 0, sizeof(boot));
            set_bootloader_message(&boot);
            sync();
        }
#endif
#ifdef SUPPORT_FOTA
    } else if (fota_delta_path)  {

        struct bootloader_message boot;
        memset(&boot, 0, sizeof(boot));
        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
        strlcat(boot.recovery, "--fota_delta_path=", sizeof(boot.recovery));
        strlcat(boot.recovery, fota_delta_path, sizeof(boot.recovery));
        set_bootloader_message(&boot);
        sync();

        status = install_fota_delta_package(fota_delta_path);

        /* ----------------------------- */
        /* SECURE BOOT UPDATE            */    
        /* ----------------------------- */            
#ifdef SUPPORT_SBOOT_UPDATE
        if (INSTALL_SUCCESS == status) {
            sec_update(false);
        }
#endif
#endif
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-05-16 
    } else if (restore_data_path) {
        if (ensure_path_mounted(SDCARD_ROOT) != 0) {
            ui->SetBackground(RecoveryUI::ERROR);
            ui->Print("No SD-Card.\n");
            status = INSTALL_ERROR;
        } else {
            struct bootloader_message boot;

            status = userdata_restore((char *)restore_data_path, 0);

            memset(&boot, 0, sizeof(boot));
            set_bootloader_message(&boot);
            sync();
            ensure_path_unmounted(SDCARD_ROOT);
        }
#endif //SUPPORT_DATA_BACKUP_RESTORE
    } else if (!just_exit) {
        status = INSTALL_NONE;  // No command specified
        ui->SetBackground(RecoveryUI::NO_COMMAND);
    }

    if (update_package 
#ifdef SUPPORT_FOTA
            || fota_delta_path
#endif
       ) {
        fprintf(stdout, "write result : MOTA_RESULT_FILE\n");
        write_result_file(MOTA_RESULT_FILE, status);
#ifdef SUPPORT_FOTA
        fprintf(stdout, "write result : FOTA_RESULT_FILE\n");
        write_result_file(FOTA_RESULT_FILE, status);
#endif

#ifdef SUPPORT_FOTA
        fprintf(stdout, "write result : remove_fota_delta_files\n");
        if (fota_delta_path)
            remove_fota_delta_files(fota_delta_path);
#endif
        fprintf(stdout, "write result : remove_mota_file\n");
        if (update_package)
            remove_mota_file(update_package);
#ifdef SUPPORT_FOTA
        fprintf(stdout, "write result : remove_fota_delta_files(DEFAULT_FOTA_FOLDER)\n");
        remove_fota_delta_files(DEFAULT_FOTA_FOLDER);
#endif
        fprintf(stdout, "write result : remove_mota_file(DEFAULT_MOTA_FILE)\n");
        remove_mota_file(DEFAULT_MOTA_FILE);
    }


    if (status == INSTALL_ERROR || status == INSTALL_CORRUPT) {
        ui->SetBackground(RecoveryUI::ERROR);
    }
    if (status != INSTALL_SUCCESS || ui->IsTextVisible()) {
        prompt_and_wait(device, status);
    }
#endif

    // Otherwise, get ready to boot the main system...
    finish_recovery(send_intent);
    ui->Print("Rebooting...\n");
    android_reboot(ANDROID_RB_RESTART, 0, 0);
    return EXIT_SUCCESS;
}

#else /* VENDOR_EDIT */

//Fangfang.Hui@Prd.PlatSrv.OTA, 2012/06/11, Modify for OTA transplant
static const struct option OPTIONS[] = {
  { "send_intent", required_argument, NULL, 's' },
  { "update_package", required_argument, NULL, 'u' },
  { "wipe_data", no_argument, NULL, 'w' },
  { "wipe_cache", no_argument, NULL, 'c' },
  { "show_text", no_argument, NULL, 't' },
  { "just_exit", no_argument, NULL, 'x' },
  { "locale", required_argument, NULL, 'l' },
#ifdef SUPPORT_FOTA
  { "fota_delta_path", required_argument, NULL, 'f' },
  //{ "fota_delta_path_1", required_argument, NULL, 'g' },
#endif
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-05-16 
  { "restore_data", required_argument, NULL, 'r' },
#endif //SUPPORT_DATA_BACKUP_RESTORE
  { NULL, 0, NULL, 0 },
};

static const char *COMMAND_FILE = "/cache/recovery/command";
static const char *INTENT_FILE = "/cache/recovery/intent";
static const char *LOG_FILE = "/cache/recovery/log";
static const char *LAST_LOG_FILE = "/cache/recovery/last_log";
static const char *LAST_LOG_FILE_1 = "/cache/recovery/last_log_r";
static const char *LAST_INSTALL_FILE = "/cache/recovery/last_install";
static const char *LOCALE_FILE = "/cache/recovery/last_locale";
static const char *CACHE_ROOT = "/cache";
static const char *SDCARD_ROOT = "/storage/sdcard0";
static const char *EXTERNAL_SDCARD_ROOT = "/storage/sdcard0/external_sd";
static const char *TEMPORARY_LOG_FILE = "/tmp/recovery.log";
static const char *TEMPORARY_INSTALL_FILE = "/tmp/last_install";
static const char *SIDELOAD_TEMP_DIR = "/tmp/sideload";
#if 1 //wschen 2012-07-10
static const char *DEFAULT_MOTA_FILE = "/data/data/com.mediatek.GoogleOta/files/update.zip";
static const char *DEFAULT_FOTA_FOLDER = "/data/data/com.mediatek.dm/files";
static const char *MOTA_RESULT_FILE = "/data/data/com.mediatek.GoogleOta/files/updateResult";
static const char *FOTA_RESULT_FILE = "/data/data/com.mediatek.dm/files/updateResult";
#endif
//Fangfang.Hui@Prd.PlatSrv.OTA, 2012/09/03, Modify for fitting for the screen
#define MAX_PAGE_DISPLAY_ROW  37

RecoveryUI* ui = NULL;
char* locale = NULL;

/*
 * The recovery tool communicates with the main system through /cache files.
 *   /cache/recovery/command - INPUT - command line for tool, one arg per line
 *   /cache/recovery/log - OUTPUT - combined log file from recovery run(s)
 *   /cache/recovery/intent - OUTPUT - intent that was passed in
 *
 * The arguments which may be supplied in the recovery.command file:
 *   --send_intent=anystring - write the text out to recovery.intent
 *   --update_package=path - verify install an OTA package file
 *   --wipe_data - erase user data (and cache), then reboot
 *   --wipe_cache - wipe cache (but not user data), then reboot
 *   --set_encrypted_filesystem=on|off - enables / diasables encrypted fs
 *   --just_exit - do nothing; exit and reboot
 *
 * After completing, we remove /cache/recovery/command and reboot.
 * Arguments may also be supplied in the bootloader control block (BCB).
 * These important scenarios must be safely restartable at any point:
 *
 * FACTORY RESET
 * 1. user selects "factory reset"
 * 2. main system writes "--wipe_data" to /cache/recovery/command
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--wipe_data"
 *    -- after this, rebooting will restart the erase --
 * 5. erase_volume() reformats /data
 * 6. erase_volume() reformats /cache
 * 7. finish_recovery() erases BCB
 *    -- after this, rebooting will restart the main system --
 * 8. main() calls reboot() to boot main system
 *
 * OTA INSTALL
 * 1. main system downloads OTA package to /cache/some-filename.zip
 * 2. main system writes "--update_package=/cache/some-filename.zip"
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--update_package=..."
 *    -- after this, rebooting will attempt to reinstall the update --
 * 5. install_package() attempts to install the update
 *    NOTE: the package install must itself be restartable from any point
 * 6. finish_recovery() erases BCB
 *    -- after this, rebooting will (try to) restart the main system --
 * 7. ** if install failed **
 *    7a. prompt_and_wait() shows an error icon and waits for the user
 *    7b; the user reboots (pulling the battery, etc) into the main system
 * 8. main() calls maybe_install_firmware_update()
 *    ** if the update contained radio/hboot firmware **:
 *    8a. m_i_f_u() writes BCB with "boot-recovery" and "--wipe_cache"
 *        -- after this, rebooting will reformat cache & restart main system --
 *    8b. m_i_f_u() writes firmware image into raw cache partition
 *    8c. m_i_f_u() writes BCB with "update-radio/hboot" and "--wipe_cache"
 *        -- after this, rebooting will attempt to reinstall firmware --
 *    8d. bootloader tries to flash firmware
 *    8e. bootloader writes BCB with "boot-recovery" (keeping "--wipe_cache")
 *        -- after this, rebooting will reformat cache & restart main system --
 *    8f. erase_volume() reformats /cache
 *    8g. finish_recovery() erases BCB
 *        -- after this, rebooting will (try to) restart the main system --
 * 9. main() calls reboot() to boot main system
 */

static const int MAX_ARG_LENGTH = 4096;
static const int MAX_ARGS = 100;

#if 1 //wschen 2012-07-10
static bool check_otaupdate_done(void)
{
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    get_bootloader_message(&boot);  // this may fail, leaving a zeroed structure

    boot.recovery[sizeof(boot.recovery) - 1] = '\0';  // Ensure termination
    const char *arg = strtok(boot.recovery, "\n");

    if (arg != NULL && !strcmp(arg, "sdota"))
    {
        LOGI("Got arguments from boot message is %s\n", boot.recovery);
        return true;
    }
    else
    {
        LOGI("no boot messages %s\n", boot.recovery);
        return false;
    }
}
#endif

// open a given path, mounting partitions as necessary
FILE*
fopen_path(const char *path, const char *mode) {
    if (ensure_path_mounted(path) != 0) {
        LOGE("Can't mount %s\n", path);
        return NULL;
    }

    // When writing, try to create the containing directory, if necessary.
    // Use generous permissions, the system (init.rc) will reset them.
    if (strchr("wa", mode[0])) dirCreateHierarchy(path, 0777, NULL, 1, sehandle);

    FILE *fp = fopen(path, mode);
    return fp;
}

// close a file, log an error if the error indicator is set
static void
check_and_fclose(FILE *fp, const char *name) {
    fflush(fp);
    if (ferror(fp)) LOGE("Error in %s\n(%s)\n", name, strerror(errno));
    fclose(fp);
}

// command line args come from, in decreasing precedence:
//   - the actual command line
//   - the bootloader control block (one per line, after "recovery")
//   - the contents of COMMAND_FILE (one per line)
static void
get_args(int *argc, char ***argv) {
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    get_bootloader_message(&boot);  // this may fail, leaving a zeroed structure

    if (boot.command[0] != 0 && boot.command[0] != 255) {
        LOGI("Boot command: %.*s\n", sizeof(boot.command), boot.command);
    }

    if (boot.status[0] != 0 && boot.status[0] != 255) {
        LOGI("Boot status: %.*s\n", sizeof(boot.status), boot.status);
    }

    // --- if arguments weren't supplied, look in the bootloader control block
    if (*argc <= 1) {
        boot.recovery[sizeof(boot.recovery) - 1] = '\0';  // Ensure termination
        const char *arg = strtok(boot.recovery, "\n");
        if (arg != NULL && !strcmp(arg, "recovery")) {
            *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
            (*argv)[0] = strdup(arg);
            for (*argc = 1; *argc < MAX_ARGS; ++*argc) {
                if ((arg = strtok(NULL, "\n")) == NULL) break;
                (*argv)[*argc] = strdup(arg);
            }
            LOGI("Got arguments from boot message\n");
        } else if (boot.recovery[0] != 0 && boot.recovery[0] != 255) {
            LOGE("Bad boot message\n\"%.20s\"\n", boot.recovery);
        }
    }
#if 1 //wschen 2012-07-10
      else {
          ensure_path_mounted("/cache");
    }

    if (check_otaupdate_done()) {
        return;
    }
#endif

    // --- if that doesn't work, try the command file
    if (*argc <= 1) {
        FILE *fp = fopen_path(COMMAND_FILE, "r");
        if (fp != NULL) {
            char *argv0 = (*argv)[0];
            *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
            (*argv)[0] = argv0;  // use the same program name

            char buf[MAX_ARG_LENGTH];
            for (*argc = 1; *argc < MAX_ARGS; ++*argc) {
                if (!fgets(buf, sizeof(buf), fp)) break;
                (*argv)[*argc] = strdup(strtok(buf, "\r\n"));  // Strip newline.
            }

            check_and_fclose(fp, COMMAND_FILE);
            LOGI("Got arguments from %s\n", COMMAND_FILE);
        }
    }

    // --> write the arguments we have back into the bootloader control block
    // always boot into recovery after this (until finish_recovery() is called)
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
    int i;
    for (i = 1; i < *argc; ++i) {
        strlcat(boot.recovery, (*argv)[i], sizeof(boot.recovery));
        strlcat(boot.recovery, "\n", sizeof(boot.recovery));
    }
    set_bootloader_message(&boot);
    //Fangfang.Hui@Prd.PlatSrv.OTA, 2012/06/11, Add for synchronize "/misc" file
    sync();
}

//Fangfang.Hui@Prd.PlatSrv.OTA, 2012/06/11, Modify for writing bcb
static void
set_sdcard_update_bootloader_message(char *recovery_command, char *parameter)
{
    struct bootloader_message boot;
	
    memset(&boot, 0, sizeof(boot));
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    //strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
    strlcpy(boot.recovery, "sdota\n", sizeof(boot.recovery));
	strlcat(boot.recovery, recovery_command, sizeof(boot.recovery));
	if(parameter != NULL) {
		strlcat(boot.recovery, parameter, sizeof(boot.recovery));
	}
	
	strlcat(boot.recovery, "\n", sizeof(boot.recovery));
    set_bootloader_message(&boot);
    //Fangfang.Hui@Prd.PlatSrv.OTA, 2012/06/11, Add for synchronize "/misc" file
    sync();
}

// How much of the temp log we have copied to the copy in cache.
static long tmplog_offset = 0;

static void
copy_log_file(const char* source, const char* destination, int append) {
    FILE *log = fopen_path(destination, append ? "a" : "w");
    if (log == NULL) {
        LOGE("Can't open %s\n", destination);
    } else {
        FILE *tmplog = fopen(source, "r");
        if (tmplog != NULL) {
             //no need to use fseek if we open the file in append mode
          /*  if (append) {
                fseek(tmplog, tmplog_offset, SEEK_SET);  // Since last write
            }*/
            char buf[4096];
            while (fgets(buf, sizeof(buf), tmplog)) fputs(buf, log);
           /* if (append) {
                tmplog_offset = ftell(tmplog);
            }*/
            check_and_fclose(tmplog, source);
        }
        check_and_fclose(log, destination);
    }
}

extern "C" {

void write_all_log(void)
{
    copy_log_file(TEMPORARY_LOG_FILE, LOG_FILE, true);
    copy_log_file(TEMPORARY_LOG_FILE, LAST_LOG_FILE, false);
    copy_log_file(TEMPORARY_INSTALL_FILE, LAST_INSTALL_FILE, false);
    chmod(LOG_FILE, 0600);
    chown(LOG_FILE, 1000, 1000);   // system user
    chmod(LAST_LOG_FILE, 0640);
    chmod(LAST_INSTALL_FILE, 0644);

    rename(LAST_LOG_FILE, LAST_LOG_FILE_1);

    sync();  // For good measure.
}

}

// clear the recovery command and prepare to boot a (hopefully working) system,
// copy our log file to cache as well (for the system to read), and
// record any intent we were asked to communicate back to the system.
// this function is idempotent: call it as many times as you like.
static void
finish_recovery(const char *send_intent) {
    // By this point, we're ready to return to the main system...
    if (send_intent != NULL) {
        FILE *fp = fopen_path(INTENT_FILE, "w");
        if (fp == NULL) {
            LOGE("Can't open %s\n", INTENT_FILE);
        } else {
            fputs(send_intent, fp);
            check_and_fclose(fp, INTENT_FILE);
        }
    }

    // Save the locale to cache, so if recovery is next started up
    // without a --locale argument (eg, directly from the bootloader)
    // it will use the last-known locale.
    if (locale != NULL) {
        LOGI("Saving locale \"%s\"\n", locale);
        FILE* fp = fopen_path(LOCALE_FILE, "w");
        fwrite(locale, 1, strlen(locale), fp);
        fflush(fp);
        fsync(fileno(fp));
        check_and_fclose(fp, LOCALE_FILE);
    }

    // Copy logs to cache so the system can find out what happened.
    copy_log_file(TEMPORARY_LOG_FILE, LOG_FILE, true);
    copy_log_file(TEMPORARY_LOG_FILE, LAST_LOG_FILE, false);
    copy_log_file(TEMPORARY_INSTALL_FILE, LAST_INSTALL_FILE, false);
    chmod(LOG_FILE, 0600);
    chown(LOG_FILE, 1000, 1000);   // system user
    chmod(LAST_LOG_FILE, 0640);
    chmod(LAST_INSTALL_FILE, 0644);

    // Reset to normal system boot so recovery won't cycle indefinitely.
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    set_bootloader_message(&boot);

    // Remove the command file, so recovery won't repeat indefinitely.
    if (ensure_path_mounted(COMMAND_FILE) != 0 ||
        (unlink(COMMAND_FILE) && errno != ENOENT)) {
        LOGW("Can't unlink %s\n", COMMAND_FILE);
    }

#if 0 //wschen 2012-07-10
    ensure_path_unmounted(CACHE_ROOT);
#endif
    sync();  // For good measure.
}

static int
erase_volume(const char *volume) {
    ui->SetBackground(RecoveryUI::ERASING);
    ui->SetProgressType(RecoveryUI::INDETERMINATE);
#ifndef DISPLAY_CHINESE_IN_RECOVERY
    ui->Print("Formatting %s...\n", volume);
#else
    ui->Print("正在格式化%s...\n", volume);
#endif

    ensure_path_unmounted(volume);

    if (strcmp(volume, "/cache") == 0) {
        // Any part of the log we'd copied to cache is now gone.
        // Reset the pointer so we copy from the beginning of the temp
        // log.
        tmplog_offset = 0;
	//set_sdcard_update_bootloader_message("--wipe_cache", NULL);
	LOGW("erase cache write bcb...\n");
    }
	
	if (strcmp(volume, "/data") == 0) {
		//set_sdcard_update_bootloader_message("--wipe_data", NULL);
		LOGW("erase data write bcb...\n");
	}

    return format_volume(volume);
}

static char*
copy_sideloaded_package(const char* original_path) {
  if (ensure_path_mounted(original_path) != 0) {
    LOGE("Can't mount %s\n", original_path);
    return NULL;
  }

  if (ensure_path_mounted(SIDELOAD_TEMP_DIR) != 0) {
    LOGE("Can't mount %s\n", SIDELOAD_TEMP_DIR);
    return NULL;
  }

  if (mkdir(SIDELOAD_TEMP_DIR, 0700) != 0) {
    if (errno != EEXIST) {
      LOGE("Can't mkdir %s (%s)\n", SIDELOAD_TEMP_DIR, strerror(errno));
      return NULL;
    }
  }

  // verify that SIDELOAD_TEMP_DIR is exactly what we expect: a
  // directory, owned by root, readable and writable only by root.
  struct stat st;
  if (stat(SIDELOAD_TEMP_DIR, &st) != 0) {
    LOGE("failed to stat %s (%s)\n", SIDELOAD_TEMP_DIR, strerror(errno));
    return NULL;
  }
  if (!S_ISDIR(st.st_mode)) {
    LOGE("%s isn't a directory\n", SIDELOAD_TEMP_DIR);
    return NULL;
  }
  if ((st.st_mode & 0777) != 0700) {
    LOGE("%s has perms %o\n", SIDELOAD_TEMP_DIR, st.st_mode);
    return NULL;
  }
  if (st.st_uid != 0) {
    LOGE("%s owned by %lu; not root\n", SIDELOAD_TEMP_DIR, st.st_uid);
    return NULL;
  }

  char copy_path[PATH_MAX];
  strcpy(copy_path, SIDELOAD_TEMP_DIR);
  strcat(copy_path, "/package.zip");

  char* buffer = (char*)malloc(BUFSIZ);
  if (buffer == NULL) {
    LOGE("Failed to allocate buffer\n");
    return NULL;
  }

  size_t read;
  FILE* fin = fopen(original_path, "rb");
  if (fin == NULL) {
    LOGE("Failed to open %s (%s)\n", original_path, strerror(errno));
    return NULL;
  }
  FILE* fout = fopen(copy_path, "wb");
  if (fout == NULL) {
    LOGE("Failed to open %s (%s)\n", copy_path, strerror(errno));
    return NULL;
  }

  while ((read = fread(buffer, 1, BUFSIZ, fin)) > 0) {
    if (fwrite(buffer, 1, read, fout) != read) {
      LOGE("Short write of %s (%s)\n", copy_path, strerror(errno));
      return NULL;
    }
  }

  free(buffer);

  if (fclose(fout) != 0) {
    LOGE("Failed to close %s (%s)\n", copy_path, strerror(errno));
    return NULL;
  }

  if (fclose(fin) != 0) {
    LOGE("Failed to close %s (%s)\n", original_path, strerror(errno));
    return NULL;
  }

  // "adb push" is happy to overwrite read-only files when it's
  // running as root, but we'll try anyway.
  if (chmod(copy_path, 0400) != 0) {
    LOGE("Failed to chmod %s (%s)\n", copy_path, strerror(errno));
    return NULL;
  }

  return strdup(copy_path);
}

static const char**
prepend_title(const char* const* headers) {
	#ifndef DISPLAY_CHINESE_IN_RECOVERY
		const char* title[] = { "Android system recovery <"
                          EXPAND(RECOVERY_API_VERSION) "e>",
                      "",
                      NULL };
	#else
		const char* title[] = {"安卓系统恢复<"
                          EXPAND(RECOVERY_API_VERSION) "e>",
                      "",
                      NULL };
	#endif

    // count the number of lines in our title, plus the
    // caller-provided headers.
    int count = 0;
    const char* const* p;
    for (p = title; *p; ++p, ++count);
    for (p = (char **)headers; *p; ++p, ++count);

    const char** new_headers = (const char**)malloc((count+1) * sizeof(char*));
    const char** h = new_headers;
    for (p = title; *p; ++p, ++h) *h = *p;
    for (p = (char **)headers; *p; ++p, ++h) *h = *p;
    *h = NULL;

    return new_headers;
}

static int
get_menu_selection(const char* const * headers, const char* const * items,
                   int menu_only, int initial_selection, Device* device) {
    // throw away keys pressed previously, so user doesn't
    // accidentally trigger menu items.
    ui->FlushKeys();

    ui->StartMenu(headers, items, initial_selection);
    int selected = initial_selection;
    int chosen_item = -1;
//Fangfang.Hui@Prd.PlatSrv.OTA, 2012/06/11, Add for wrap select of list items
    int items_count = 0;

	while (*(items++) != NULL) {
		items_count++;
	}
 
    while (chosen_item < 0) {
        int key = ui->WaitKey();
        int visible = ui->IsTextVisible();

        if (key == -1) {   // ui_wait_key() timed out
            if (ui->WasTextEverVisible()) {
                continue;
            } else {
                LOGI("timed out waiting for key input; rebooting.\n");
                ui->EndMenu();
                return 0; // XXX fixme
            }
        }

        int action = device->HandleMenuKey(key, visible);

        if (action < 0) {
            switch (action) {
                case Device::kHighlightUp:
                    //Fangfang.Hui@Prd.PlatSrv.OTA, 2012/06/11, Add for wrap select of list items
                    if (selected == 0) {
						selected = items_count;
					}
                    --selected;
                    selected = ui->SelectMenu(selected);
                    break;
                case Device::kHighlightDown:
                    //Fangfang.Hui@Prd.PlatSrv.OTA, 2012/06/11, Add for wrap select of list items
                    if (selected == items_count - 1) {
						selected = -1;
					}
                    ++selected;
                    selected = ui->SelectMenu(selected);
                    break;
                case Device::kInvokeItem:
                    chosen_item = selected;
                    break;
                case Device::kNoAction:
                    break;
            }
        } else if (!menu_only) {
            chosen_item = action;
        }
    }

    ui->EndMenu();
    return chosen_item;
}

static int
get_menu_selection_page(const char* const * headers, const char* const * items, 
			int initial_selection, Device* device) {

	ui->FlushKeys();
	ui->StartMenu(headers, items + initial_selection/MAX_PAGE_DISPLAY_ROW * MAX_PAGE_DISPLAY_ROW, initial_selection%MAX_PAGE_DISPLAY_ROW);
	ui->SelectMenu(initial_selection%MAX_PAGE_DISPLAY_ROW);
	int selected = initial_selection;
    int chosen_item = -1;
	int items_count = 0;

	int num_selected = selected;
	const char* const *tmp_items = items;
	while (*(tmp_items++) != NULL)
		items_count++;

	
    while (chosen_item < 0) {
        int key = ui->WaitKey();
        int visible = ui->IsTextVisible();

        int action = device->HandleMenuKey(key, visible);

        if (action < 0) {
            switch (action) {
                case Device::kHighlightUp:
					if (num_selected%MAX_PAGE_DISPLAY_ROW == 0)
					{

						if (num_selected == 0)
							num_selected = items_count - 1;
						else --num_selected;
						selected = num_selected%MAX_PAGE_DISPLAY_ROW;
						if (items_count > MAX_PAGE_DISPLAY_ROW) {
							ui->EndMenu();
							ui->StartMenu(headers, items + num_selected - selected, selected);
						}
						selected = ui->SelectMenu(selected);
						break;
					}
					--num_selected;
					selected = num_selected%MAX_PAGE_DISPLAY_ROW;
                    selected = ui->SelectMenu(selected);
                    break;
                case Device::kHighlightDown:
					if ((num_selected + 1)%MAX_PAGE_DISPLAY_ROW == 0 || (num_selected == items_count - 1))
					{
						if (num_selected == items_count - 1)
							num_selected = 0;
						else ++num_selected;
						selected = num_selected%MAX_PAGE_DISPLAY_ROW;
						if (items_count > MAX_PAGE_DISPLAY_ROW) {
							ui->EndMenu();
							ui->StartMenu(headers, items + num_selected, selected);
						}
						selected = ui->SelectMenu(selected);
						break;
					}
					++num_selected;
                    selected = num_selected%MAX_PAGE_DISPLAY_ROW;
                    selected = ui->SelectMenu(selected);
                    break;
                case Device::kInvokeItem:
                    chosen_item = num_selected;
                    break;
                case Device::kNoAction:
                    break;
            }
        }
    }

    ui->EndMenu();
    return chosen_item;
}

static int compare_string(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-03-09 

static int sdcard_restore_directory(const char *path, Device *device) {
	#ifndef DISPLAY_CHINESE_IN_RECOVERY
		const char *MENU_HEADERS[] = { "Choose a backup file to restore:",
                                   path,
                                   "",
                                   NULL };
	#else
		const char *MENU_HEADERS[] = { "选择用来还原数据的备份文件:",
                                   path,
                                   "",
                                   NULL };
	#endif

    DIR *d;
    struct dirent *de;
    d = opendir(path);
    if (d == NULL) {
        LOGE("error opening %s: %s\n", path, strerror(errno));
        ensure_path_unmounted(SDCARD_ROOT);
        return 0;
    }

    const char **headers = prepend_title(MENU_HEADERS);

    int d_size = 0;
    int d_alloc = 10;
    char **dirs = (char**)malloc(d_alloc * sizeof(char *));
    int z_size = 1;
    int z_alloc = 10;
    char **zips = (char**)malloc(z_alloc * sizeof(char *));
    zips[0] = strdup("../");

    while ((de = readdir(d)) != NULL) {
        int name_len = strlen(de->d_name);

        if (de->d_type == DT_DIR) {
            // skip "." and ".." entries
            if (name_len == 1 && de->d_name[0] == '.') {
                continue;
            }
            if (name_len == 2 && de->d_name[0] == '.' && de->d_name[1] == '.') {
                continue;
            }

            if (d_size >= d_alloc) {
                d_alloc *= 2;
                dirs = (char**)realloc(dirs, d_alloc * sizeof(char*));
            }
            dirs[d_size] = (char*)malloc(name_len + 2);
            strcpy(dirs[d_size], de->d_name);
            dirs[d_size][name_len] = '/';
            dirs[d_size][name_len+1] = '\0';
            ++d_size;
        } else if (de->d_type == DT_REG &&
                   name_len >= 7 &&
                   strncasecmp(de->d_name + (name_len - 7), ".backup", 7) == 0) {
            if (z_size >= z_alloc) {
                z_alloc *= 2;
                zips = (char**)realloc(zips, z_alloc * sizeof(char*));
            }
            zips[z_size++] = strdup(de->d_name);
        }
    }
    closedir(d);

    qsort(dirs, d_size, sizeof(char *), compare_string);
    qsort(zips, z_size, sizeof(char *), compare_string);

    // append dirs to the zips list
    if (d_size + z_size + 1 > z_alloc) {
        z_alloc = d_size + z_size + 1;
        zips = (char**)realloc(zips, z_alloc * sizeof(char *));
    }
    memcpy(zips + z_size, dirs, d_size * sizeof(char *));
    free(dirs);
    z_size += d_size;
    zips[z_size] = NULL;

    int result;
    int chosen_item = 0;
    do {
        chosen_item = get_menu_selection(headers, zips, 1, chosen_item, device);

        char* item = zips[chosen_item];
        int item_len = strlen(item);
        if (chosen_item == 0) {          // item 0 is always "../"
            // go up but continue browsing (if the caller is sdcard_directory)
            result = -1;
            break;
        } else if (item[item_len-1] == '/') {
            // recurse down into a subdirectory
            char new_path[PATH_MAX];
            strlcpy(new_path, path, PATH_MAX);
            strlcat(new_path, "/", PATH_MAX);
            strlcat(new_path, item, PATH_MAX);
            new_path[strlen(new_path)-1] = '\0';  // truncate the trailing '/'
            result = sdcard_restore_directory(new_path, device);
            if (result >= 0) {
                break;
            }
        } else {
            // selected a backup file:  attempt to restore it, and return
            // the status to the caller.
            char new_path[PATH_MAX];
#if 1 //wschen 2011-05-16
            struct bootloader_message boot;
            memset(&boot, 0, sizeof(boot));
#endif
            strlcpy(new_path, path, PATH_MAX);
            strlcat(new_path, "/", PATH_MAX);
            strlcat(new_path, item, PATH_MAX);
			#ifndef DISPLAY_CHINESE_IN_RECOVERY
				ui->Print("\n-- Restore %s ...\n", item);
			#else
				ui->Print("\n还原 %s ...\n", item);
			#endif
            
            //do restore and update result
#if 1 //wschen 2011-05-16
            strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
            strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
            strlcat(boot.recovery, "--restore_data=", sizeof(boot.recovery));
            strlcat(boot.recovery, new_path, sizeof(boot.recovery));
            strlcat(boot.recovery, "\n", sizeof(boot.recovery));
            set_bootloader_message(&boot);
            sync();
#endif
            result = userdata_restore(new_path, 0);

            memset(&boot, 0, sizeof(boot));
            set_bootloader_message(&boot);
            sync();
            break;
        }
    } while (true);

    int i;
    for (i = 0; i < z_size; ++i) {
        free(zips[i]);
    }
    free(zips);
    free(headers);

    if (result != -1) {
        ensure_path_unmounted(SDCARD_ROOT);
    }
    return result;
}


#endif //SUPPORT_DATA_BACKUP_RESTORE

static int
update_directory(const char* path, const char* unmount_when_done,
                 int* wipe_cache, Device* device) {
    ensure_path_mounted(path);
    mkdir(EXTERNAL_SDCARD_ROOT, S_IRWXU);

	#ifndef DISPLAY_CHINESE_IN_RECOVERY
		const char* MENU_HEADERS[] = { "Choose a package to install:",
                                   path,
                                   "",
                                   NULL };
	#else
		const char* MENU_HEADERS[] = { "选择一个安装包:",
                                   path,
                                   "",
                                   NULL };
	#endif

    DIR* d;
    struct dirent* de;
    d = opendir(path);
    if (d == NULL) {
        LOGE("error opening %s: %s\n", path, strerror(errno));
        if (unmount_when_done != NULL) {
            ensure_path_unmounted(unmount_when_done);
        }
        return 0;
    }

    const char** headers = prepend_title(MENU_HEADERS);

    int d_size = 0;
    int d_alloc = 10;
    char** dirs = (char**)malloc(d_alloc * sizeof(char*));
    int z_size = 0;
    int z_alloc = 10;
    char** zips = (char**)malloc(z_alloc * sizeof(char*));
  	int h_size = 1;
  	int h_alloc = 10;
  	char **listheaders = (char**)malloc(h_alloc * sizeof(char *));
  	listheaders[0] = strdup("../");
  	//if this is the SDCARD_ROOT directory, add "external_sd" to top of the list
	if ((strlen(path) == strlen(SDCARD_ROOT)) && !strcmp(path, SDCARD_ROOT)){
		h_size++;
		listheaders[1] = strdup("external_sd/");
	}

    while ((de = readdir(d)) != NULL) {
        int name_len = strlen(de->d_name);

        if (de->d_type == DT_DIR) {
            // skip "." and ".." entries
            if (name_len == 1 && de->d_name[0] == '.') continue;
            if (name_len == 2 && de->d_name[0] == '.' &&
                de->d_name[1] == '.') continue;
            //if we already add the external_sd in listheaders, ingore it when we readdir
            if (h_size == 2 && !strcmp(de->d_name, "external_sd")){
                continue;
            }

            if (d_size >= d_alloc) {
                d_alloc *= 2;
                dirs = (char**)realloc(dirs, d_alloc * sizeof(char*));
            }
            dirs[d_size] = (char*)malloc(name_len + 2);
            strcpy(dirs[d_size], de->d_name);
            dirs[d_size][name_len] = '/';
            dirs[d_size][name_len+1] = '\0';
            ++d_size;
        } else if (de->d_type == DT_REG &&
                   name_len >= 4 &&
                   strncasecmp(de->d_name + (name_len-4), ".zip", 4) == 0) {
            if (z_size >= z_alloc) {
                z_alloc *= 2;
                zips = (char**)realloc(zips, z_alloc * sizeof(char*));
            }
            zips[z_size++] = strdup(de->d_name);
        }
    }
    closedir(d);

    qsort(dirs, d_size, sizeof(char*), compare_string);
    qsort(zips, z_size, sizeof(char*), compare_string);

    // append dirs to the zips list
   if (d_size + z_size + h_size + 1 > h_alloc) {
        h_alloc = d_size + z_size + h_size + 1;
        listheaders = (char**)realloc(listheaders, h_alloc * sizeof(char *));
    }
    memcpy(listheaders + h_size, zips, z_size * sizeof(char *));
	memcpy(listheaders + h_size + z_size, dirs, d_size * sizeof(char *));
    free(dirs);
	free(zips);
	h_size += z_size + d_size;
    listheaders[h_size] = NULL;

    int result;
    int chosen_item = 0;
    do {
        chosen_item = get_menu_selection_page(headers, listheaders, chosen_item, device);

        char* item = listheaders[chosen_item];
        int item_len = strlen(item);
        if (chosen_item == 0) {          // item 0 is always "../"
            // go up but continue browsing (if the caller is update_directory)
            result = -1;
            break;
        } else if (item[item_len-1] == '/') {
            // recurse down into a subdirectory
            char new_path[PATH_MAX];
            strlcpy(new_path, path, PATH_MAX);
            strlcat(new_path, "/", PATH_MAX);
            strlcat(new_path, item, PATH_MAX);
            new_path[strlen(new_path)-1] = '\0';  // truncate the trailing '/'

    		if (!strcmp(new_path, EXTERNAL_SDCARD_ROOT) && !(access("/dev/block/mmcblk1", F_OK)
    		         && access("/dev/block/mmcblk1p1", F_OK))) {
    		    ensure_path_mounted(EXTERNAL_SDCARD_ROOT);
    		}

            result = update_directory(new_path, unmount_when_done, wipe_cache, device);
            if (result >= 0) break;
        } else {
            // selected a zip file:  attempt to install it, and return
            // the status to the caller.
            char new_path[PATH_MAX];
            strlcpy(new_path, path, PATH_MAX);
            strlcat(new_path, "/", PATH_MAX);
            strlcat(new_path, item, PATH_MAX);

			#ifndef DISPLAY_CHINESE_IN_RECOVERY
				ui->Print("\n-- Install %s ...\n", path);
			#else
				ui->Print("\n安装%s中...\n", path);
			#endif            
            
            //set_sdcard_update_bootloader_message("--update_package=", new_path);
            struct bootloader_message boot;
            memset(&boot, 0, sizeof(boot));
            strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
            strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
            strlcat(boot.recovery, "--update_package=", sizeof(boot.recovery));
			strlcat(boot.recovery, new_path, sizeof(boot.recovery));
			strlcat(boot.recovery, "\n", sizeof(boot.recovery));
            set_bootloader_message(&boot);
            char* copy = copy_sideloaded_package(new_path);
			ensure_path_unmounted(EXTERNAL_SDCARD_ROOT);
            if (unmount_when_done != NULL) {
                ensure_path_unmounted(unmount_when_done);
            }
            if (copy) {
                result = install_package(copy, wipe_cache, TEMPORARY_INSTALL_FILE);
                free(copy);
            } else {
                result = INSTALL_ERROR;
            }
            break;
        }
    } while (true);

    int i;
    for (i = 0; i < h_size; ++i) {
        free(listheaders[i]);
    }
    free(listheaders);
    free(headers);

    if (unmount_when_done != NULL) {
        ensure_path_unmounted(unmount_when_done);
    }
    return result;
}

static void
wipe_data(int confirm, Device* device) {
#if 1 //wschen 2012-04-12
    struct phone_encrypt_state ps;
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
#endif

    if (confirm) {
        static const char** title_headers = NULL;

        if (title_headers == NULL) {
#ifndef DISPLAY_CHINESE_IN_RECOVERY
	        const char* headers[] = { "Confirm wipe of all user data?",
	                    "  THIS CAN NOT BE UNDONE.",
	                    "",
	                    NULL };
#else
		const char* headers[] = { "确定清除数据?不可恢复.",
	                    "",
	                    NULL };
#endif
            title_headers = prepend_title((const char**)headers);
        }

#ifndef DISPLAY_CHINESE_IN_RECOVERY
	const char* items[] = { " No",
                  " Yes -- delete all user data", // [7]
                  NULL };
#else
	const char* items[] = { " 否",
                  " 是 -- 清除所有用户数据",   // [1]
                  NULL };
#endif

        int chosen_item = get_menu_selection(title_headers, items, 1, 0, device);
        if (chosen_item != 1) {
            return;
        }
    }

	#ifndef DISPLAY_CHINESE_IN_RECOVERY
		ui->Print("\n-- Wiping data...\n");
	#else
		ui->Print("\n清除数据中...\n");
	#endif

#if 1 //wschen 2011-05-16
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
    strlcat(boot.recovery, "--wipe_data\n", sizeof(boot.recovery));
    set_bootloader_message(&boot);
    sync();
#endif
    device->WipeData();
#ifdef SPECIAL_FACTORY_RESET //wschen 2011-06-16
    if (special_factory_reset()) {
        return;
    }
#else
    erase_volume("/data");
#endif //SPECIAL_FACTORY_RESET
    erase_volume("/cache");
#if 1 //wschen 2011-05-16
    memset(&boot, 0, sizeof(boot));
    set_bootloader_message(&boot);
    ps.state = 0;
    set_phone_encrypt_state(&ps);
    sync();
#endif
	#ifndef DISPLAY_CHINESE_IN_RECOVERY
		ui->Print("Data wipe complete.\n");
	#else
		ui->Print("数据清除完成.\n");
	#endif
}

static void
wipe_cache_confirm(int confirm, Device* device) {
#if 1 //wschen 2011-05-16
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
#endif
    if (confirm) {
        static const char** title_headers = NULL;

        if (title_headers == NULL) {
			#ifndef DISPLAY_CHINESE_IN_RECOVERY
				const char* headers[] = { "Confirm wipe of cache?",
                                "  THIS CAN NOT BE UNDONE.",
                                "",
                                NULL };
			#else
				const char* headers[] = { "确定清除缓存?不可恢复.",
                                "",
                                NULL };
			#endif
            title_headers = prepend_title(headers);
        }

#ifndef DISPLAY_CHINESE_IN_RECOVERY
	const char* items[] = { " No",
                  " Yes -- clear cache",   // [1]
                  NULL };
#else
	const char* items[] = { " 否",
                  " 是 -- 清除所有缓存",   // [1]
                  NULL };
#endif

        int chosen_item = get_menu_selection(title_headers, items, 1, 0, device);
        if (chosen_item != 1) {
            return;
        }
    }

	#ifndef DISPLAY_CHINESE_IN_RECOVERY
		ui->Print("\n-- Wiping cache...\n");
	#else
		ui->Print("\n清除缓存中...\n");
	#endif

#if 1 //wschen 2011-05-16
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
    strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
    set_bootloader_message(&boot);
    sync();
#endif

    erase_volume("/cache");
#if 1 //wschen 2011-05-16
    memset(&boot, 0, sizeof(boot));
    set_bootloader_message(&boot);
    sync();
#endif
	#ifndef DISPLAY_CHINESE_IN_RECOVERY
		ui->Print("Cache wipe complete.\n");
	#else
		ui->Print("缓存清除成功.\n");
	#endif
}

#if 1 //wschen 2012-07-10
static void prompt_error_message(int reason)
{
#ifndef DISPLAY_CHINESE_IN_RECOVERY
    switch (reason)  {
        case INSTALL_NO_SDCARD:
            ui->Print("No SD-Card.\n");
            break;
        case INSTALL_NO_UPDATE_PACKAGE:
            ui->Print("Can not find update.zip.\n");
            break;
        case INSTALL_NO_KEY:
            ui->Print("Failed to load keys\n");
            break;
        case INSTALL_SIGNATURE_ERROR:
            ui->Print("Signature verification failed\n");
            break;
        case INSTALL_CORRUPT:
            ui->Print("The update.zip is corrupted\n");
            break;
        case INSTALL_FILE_SYSTEM_ERROR:
            ui->Print("Can't create/copy file\n");
            break;
        case INSTALL_ERROR:
            ui->Print("Update.zip is not correct\n");
            break;
    }
#else
	switch (reason)  {
        case INSTALL_NO_SDCARD:
            ui->Print("未发现SD卡.\n");
            break;
        case INSTALL_NO_UPDATE_PACKAGE:
            ui->Print("无法找到update.zip.\n");
            break;
        case INSTALL_NO_KEY:
            ui->Print("加载密钥失败\n");
            break;
        case INSTALL_SIGNATURE_ERROR:
            ui->Print("签名鉴权失败\n");
            break;
        case INSTALL_CORRUPT:
            ui->Print("安装包文件损坏\n");
            break;
        case INSTALL_FILE_SYSTEM_ERROR:
            ui->Print("无法创建或者拷贝文件\n");
            break;
        case INSTALL_ERROR:
            ui->Print("安装包不正确\n");
            break;
    }
#endif
}
#endif

static void
prompt_and_wait(Device* device, int status) {
    const char* const* headers = prepend_title(device->GetMenuHeaders());

    for (;;) {
        ui->Print("\n");
        if (!check_otaupdate_done()) {
            LOGI("[1]check the otaupdate is done!\n");
            finish_recovery(NULL);
            switch (status) {
                case INSTALL_SUCCESS:
                case INSTALL_NONE:
                    ui->SetBackground(RecoveryUI::NO_COMMAND);
                    break;

                case INSTALL_ERROR:
                case INSTALL_CORRUPT:
                    ui->SetBackground(RecoveryUI::ERROR);
                    break;
            }
            ui->SetProgressType(RecoveryUI::EMPTY);

            int chosen_item = get_menu_selection(headers, device->GetMenuItems(), 0, 0, device);

            // device-specific code may take some action here.  It may
            // return one of the core actions handled in the switch
            // statement below.
            chosen_item = device->InvokeMenuItem(chosen_item);
            // clear screen
            ui->Print("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");

            int wipe_cache;

            switch (chosen_item) {
                case Device::REBOOT:
                    return;

                case Device::WIPE_DATA:
                    wipe_data(ui->IsTextVisible(), device);
                    if (!ui->IsTextVisible()) return;
                    break;

                case Device::WIPE_CACHE:					
                    wipe_cache_confirm(ui->IsTextVisible(), device);
                    if (!ui->IsTextVisible()) return;
                    break;

                case Device::APPLY_EXT:
                    // Some packages expect /cache to be mounted (eg,
                    // standard incremental packages expect to use /cache
                    // as scratch space).
                    ensure_path_mounted(CACHE_ROOT);
                    status = update_directory(SDCARD_ROOT, SDCARD_ROOT, &wipe_cache, device);
                    if (status == INSTALL_SUCCESS && wipe_cache) {
#if 1 //wschen 2012-07-10
                        struct bootloader_message boot;
#endif
#if 1 //wschen 2012-07-10
                        memset(&boot, 0, sizeof(boot));
                        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
                        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
                        if (wipe_cache == 1) {
                            strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
                            ui->Print("\n-- Wiping cache (at package request)...\n");
                        } else {
                            strlcat(boot.recovery, "--wipe_data\n", sizeof(boot.recovery));
                            ui->Print("\n-- Wiping data (at package request)...\n");
                        }
                        set_bootloader_message(&boot);
                        sync();
#endif
                        if (wipe_cache == 1) {
#ifndef DISPLAY_CHINESE_IN_RECOVERY
				   if (erase_volume("/cache")) {
	                            ui->Print("Cache wipe failed.\n");
	                        } else {
	                            ui->Print("Cache wipe complete.\n");
	                        }
#else
							if (erase_volume("/cache")) {
	                            ui->Print("缓存清除失败.\n");
	                        } else {
	                            ui->Print("缓存清除成功.\n");
	                        }
#endif
                        } else {
#ifdef SPECIAL_FACTORY_RESET //wschen 2012-07-25 
                            if (special_factory_reset()) {
                                ui->Print("Data wipe failed.\n");
                            } else {
                                ui->Print("Data wipe complete.\n");
                            }
#endif
                        }
#if 1 //wschen 2012-07-10
                        memset(&boot, 0, sizeof(boot));
                        set_bootloader_message(&boot);
                        sync();
#endif
                    }
                    if (status >= 0) {
                        if (status != INSTALL_SUCCESS) {
                            ui->SetBackground(RecoveryUI::ERROR);
				#ifndef DISPLAY_CHINESE_IN_RECOVERY
					ui->Print("Installation aborted.\n");
				#else
					ui->Print("安装失败.\n");
				#endif
                        } else if (!ui->IsTextVisible()) {
#if 1 //wschen 2012-07-10
                            finish_recovery(NULL);
#endif
                            return;  // reboot if logs aren't visible
                        } else {
				#ifndef DISPLAY_CHINESE_IN_RECOVERY
					ui->Print("\nInstall from sdcard complete.\n");
				#else
					ui->Print("\n安装完成.\n");
				#endif
#if 1 //wschen 2012-07-10
                            finish_recovery(NULL);
#endif
                        }
                    }
                    break;

                case Device::APPLY_CACHE:					
                    // Don't unmount cache at the end of this.
                    status = update_directory(CACHE_ROOT, NULL, &wipe_cache, device);
                    if (status == INSTALL_SUCCESS && wipe_cache) {
#if 1 //wschen 2012-07-10
                        struct bootloader_message boot;
                        memset(&boot, 0, sizeof(boot));
                        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
                        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
                        if (wipe_cache == 1) {
                            strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
                            ui->Print("\n-- Wiping cache (at package request)...\n");
                        } else {
                            strlcat(boot.recovery, "--wipe_data\n", sizeof(boot.recovery));
                            ui->Print("\n-- Wiping data (at package request)...\n");
                        }
                        set_bootloader_message(&boot);
                        sync();
#endif
                        if (wipe_cache == 1) {
#ifndef DISPLAY_CHINESE_IN_RECOVERY
				  ui->Print("\n-- Wiping cache (at package request)...\n");
				  if (erase_volume("/cache")) {
	                            ui->Print("Cache wipe failed.\n");
	                        } else {
	                            ui->Print("Cache wipe complete.\n");
	                        }
#else
				  ui->Print("\n--清除缓存中(升级包请求)...\n");
				  if (erase_volume("/cache")) {
	                            ui->Print("缓存清除失败.\n");
	                        } else {
	                            ui->Print("缓存清除成功.\n");
	                        }
#endif
                        } else {
#ifdef SPECIAL_FACTORY_RESET //wschen 2012-07-25 
                            if (special_factory_reset()) {
                                ui->Print("Data wipe failed.\n");
                            } else {
                                ui->Print("Data wipe complete.\n");
                            }
#endif
                        }
#if 1 //wschen 2012-07-10
                        memset(&boot, 0, sizeof(boot));
                        set_bootloader_message(&boot);
                        sync();
#endif
                    }
                    if (status >= 0) {
                        if (status != INSTALL_SUCCESS) {
                            ui->SetBackground(RecoveryUI::ERROR);
                            ui->Print("Installation aborted.\n");
                        } else if (!ui->IsTextVisible()) {
                            return;  // reboot if logs aren't visible
                        } else {
                            ui->Print("\nInstall from cache complete.\n");
                        }
                    }
                    break;

#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-03-09 

                case Device::USER_DATA_BACKUP:
                    {
                        int status;

                        ui->SetBackground(RecoveryUI::INSTALLING_UPDATE);
                        ui->SetProgressType(RecoveryUI::EMPTY);
                        ui->ShowProgress(1.0, 0);

                        status = userdata_backup();

                        if (status == INSTALL_SUCCESS) {
                            ui->SetBackground(RecoveryUI::NONE);
                            ui->SetProgressType(RecoveryUI::EMPTY);
			#ifndef DISPLAY_CHINESE_IN_RECOVERY
				ui->Print("Backup user data complete.\n");
			#else
				ui->Print("备份用户数据成功.\n");
			#endif
                            finish_recovery(NULL);
                        } else {
                            ui->SetBackground(RecoveryUI::ERROR);
                        }
                    }
                    break;
                case Device::USER_DATA_RESTORE:
                    {
                        int status;
				
                        if (ensure_path_mounted(SDCARD_ROOT) != 0) {
                            ui->SetBackground(RecoveryUI::ERROR);
			#ifndef DISPLAY_CHINESE_IN_RECOVERY
				ui->Print("No SD-Card.\n");
			#else
				ui->Print("未发现SD卡.\n");
			#endif
                            break;
                        }

                        status = sdcard_restore_directory(SDCARD_ROOT, device);

                        if (status >= 0) {
                            if (status != INSTALL_SUCCESS) {
                                ui->SetBackground(RecoveryUI::ERROR);
                            } else {
#if 1 //wschen 2011-05-16
                                struct bootloader_message boot;
                                memset(&boot, 0, sizeof(boot));
                                set_bootloader_message(&boot);
                                sync();
#endif
                                ui->SetBackground(RecoveryUI::NONE);
                                ui->SetProgressType(RecoveryUI::EMPTY);
                                if (!ui->IsTextVisible()) {
                                    finish_recovery(NULL);
                                    return;  // reboot if logs aren't visible
                                } else {
				#ifndef DISPLAY_CHINESE_IN_RECOVERY
					ui->Print("Restore user data complete.\n");
				#else
					ui->Print("还原用户数据成功\n");
				#endif
                                    finish_recovery(NULL);
                                }
                            }
                        }
                    }
                    break;
#endif
                case Device::APPLY_ADB_SIDELOAD:
                    ensure_path_mounted(CACHE_ROOT);
                    status = apply_from_adb(ui, &wipe_cache, TEMPORARY_INSTALL_FILE);

#if 1 //wschen 2012-07-25
                    if ((status == INSTALL_SUCCESS) && wipe_cache) {
                        struct bootloader_message boot;
                        memset(&boot, 0, sizeof(boot));
                        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
                        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
                        if (wipe_cache == 1) {
                            strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
                            ui->Print("\n-- Wiping cache (at package request)...\n");
                        } else {
                            strlcat(boot.recovery, "--wipe_data\n", sizeof(boot.recovery));
                            ui->Print("\n-- Wiping data (at package request)...\n");
                        }
                        set_bootloader_message(&boot);
                        sync();
                        if (wipe_cache == 1) {
                            if (erase_volume("/cache")) {
                                ui->Print("Cache wipe failed.\n");
                            } else {
                                ui->Print("Cache wipe complete.\n");
                            }
                        } else {
#ifdef SPECIAL_FACTORY_RESET //wschen 2012-07-25 
                            if (special_factory_reset()) {
                                ui->Print("Data wipe failed.\n");
                            } else {
                                ui->Print("Data wipe complete.\n");
                            }
#endif
                        }
                        memset(&boot, 0, sizeof(boot));
                        set_bootloader_message(&boot);
                        sync();
                    }
#endif

                    if (status >= 0) {
                        if (status != INSTALL_SUCCESS) {
                            ui->SetBackground(RecoveryUI::ERROR);
                            ui->Print("Installation aborted.\n");
                        } else if (!ui->IsTextVisible()) {
                            return;  // reboot if logs aren't visible
                        } else {
                            ui->Print("\nInstall from ADB complete.\n");
                        }
                    }
                    break;
            }
        } else {
            ui->SetProgressType(RecoveryUI::EMPTY);

	#ifndef DISPLAY_CHINESE_IN_RECOVERY
		ui->Print("\n Please continue to update your system !\n");
	#else
		ui->Print("\n 请继续完成系统更新  !\n");
	#endif
            int chosen_item = get_menu_selection(headers, device->GetForceMenuItems(), 0, 0, device);

            // device-specific code may take some action here.  It may
            // return one of the core actions handled in the switch
            // statement below.
            chosen_item = device->InvokeForceMenuItem(chosen_item);

            switch (chosen_item)
            {
                case Device::REBOOT:
                    return;

                case Device::FORCE_APPLY_SDCARD_SIDELOAD:
                    {
                    int wipe_cache;
                    //M Removed by QA's resutest.
                    //ui->Print("\n-- Install from sdcard...\n");
                    //set_sdcard_update_bootloader_message();
                    int status = update_directory(SDCARD_ROOT, SDCARD_ROOT, &wipe_cache, device);
                    if ((status == INSTALL_SUCCESS) && wipe_cache) {
                        struct bootloader_message boot;
                        memset(&boot, 0, sizeof(boot));
                        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
                        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
                        if (wipe_cache == 1) {
                            strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
                            ui->Print("\n-- Wiping cache (at package request)...\n");
                        } else {
                            strlcat(boot.recovery, "--wipe_data\n", sizeof(boot.recovery));
                            ui->Print("\n-- Wiping data (at package request)...\n");
                        }
                        set_bootloader_message(&boot);
                        sync();
                        if (wipe_cache == 1) {
#ifndef DISPLAY_CHINESE_IN_RECOVERY
				  if (erase_volume("/cache")) {
	                            ui->Print("Cache wipe failed.\n");
	                        } else {
	                            ui->Print("Cache wipe complete.\n");
	                        }
#else
			          if (erase_volume("/cache")) {
	                            ui->Print("缓存清除失败.\n");
	                        } else {
	                            ui->Print("缓存清除成功.\n");
	                        }
#endif
                        } else {
#ifdef SPECIAL_FACTORY_RESET //wschen 2012-07-25 
                            if (special_factory_reset()) {
                                ui->Print("Data wipe failed.\n");
                            } else {
                                ui->Print("Data wipe complete.\n");
                            }
#endif
                        }
                        memset(&boot, 0, sizeof(boot));
                        set_bootloader_message(&boot);
                        sync();
                    }

                    if (status >= 0) {
                        if (status != INSTALL_SUCCESS) {
                            ui->SetBackground(RecoveryUI::ERROR);
                            prompt_error_message(status);
				#ifndef DISPLAY_CHINESE_IN_RECOVERY
					ui->Print("Installation aborted.\n");
				#else
					ui->Print("安装失败.\n");
				#endif
                        } else if (!ui->IsTextVisible()) {
                            finish_recovery(NULL);
                            return;  // reboot if logs aren't visible
                        } else {
			#ifndef DISPLAY_CHINESE_IN_RECOVERY
				ui->Print("\nInstall from sdcard complete.\n");
			#else
				ui->Print("\n安装完成.\n");
			#endif
                            finish_recovery(NULL);
                        }
                    }
                    }
                    break;
            }
        }
    }
}

static void
print_property(const char *key, const char *name, void *cookie) {
    printf("%s=%s\n", key, name);
}

static void
load_locale_from_cache() {
    FILE* fp = fopen_path(LOCALE_FILE, "r");
    char buffer[80];
    if (fp != NULL) {
        fgets(buffer, sizeof(buffer), fp);
        int j = 0;
        unsigned int i;
        for (i = 0; i < sizeof(buffer) && buffer[i]; ++i) {
            if (!isspace(buffer[i])) {
                buffer[j++] = buffer[i];
            }
        }
        buffer[j] = 0;
        locale = strdup(buffer);
        check_and_fclose(fp, LOCALE_FILE);
    }
}

#if 1 //wschen 2012-07-10
static bool remove_mota_file(const char *file_name)
{
    int  ret = 0;

    //LOG_INFO("[%s] %s\n", __func__, file_name);

    ret = unlink(file_name);

    if (ret == 0)
		return true;

	if (ret < 0 && errno == ENOENT)
		return true;

    return false;
}

static void write_result_file(const char *file_name, int result)
{
#ifdef MTK_EMMC_SUPPORT

    if (INSTALL_SUCCESS == result) {
        set_ota_result(1);
    } else {
        set_ota_result(0);
    }

#else
    char  dir_name[256];

    ensure_path_mounted("/data");

    strcpy(dir_name, file_name);
    char *p = strrchr(dir_name, '/');
    *p = 0;

    fprintf(stdout, "dir_name = %s\n", dir_name);

    if (opendir(dir_name) == NULL)  {
        fprintf(stdout, "dir_name = '%s' does not exist, create it.\n", dir_name);
        if (mkdir(dir_name, 0666))  {
            fprintf(stdout, "can not create '%s' : %s\n", dir_name, strerror(errno));
            return;
        }
    }

    int result_fd = open(file_name, O_RDWR | O_CREAT, 0644);

    if (result_fd < 0) {
        fprintf(stdout, "cannot open '%s' for output : %s\n", file_name, strerror(errno));
        return;
    }

    //LOG_INFO("[%s] %s %d\n", __func__, file_name, result);

    char buf[4];
    if (INSTALL_SUCCESS == result)
        strcpy(buf, "1");
    else
        strcpy(buf, "0");
    write(result_fd, buf, 1);
    close(result_fd);
#endif
}
#endif

//Fangfang.Hui@Prd.PlatSrv.OTA, 2012/06/11, Add for saving running command
static void feed_back(const char *send_intent)
{
    if (send_intent != NULL) {
        FILE *fp = fopen_path(INTENT_FILE, "w");
        if (fp == NULL) {
            LOGE("Can't open %s\n", INTENT_FILE);
        } else {
            fputs(send_intent, fp);
            check_and_fclose(fp, INTENT_FILE);
        }
    }
}

static void save_running_command(const char *running_command)
{
	struct bootloader_message boot;
	memset(&boot, 0, sizeof(boot));

    get_bootloader_message(&boot);

	strlcpy(boot.running_command, running_command, sizeof(boot.running_command));
	set_bootloader_message(&boot);
    //Fangfang.Hui@Prd.PlatSrv.OTA, 2012/06/11, Add for synchronize "/misc" file
    sync();
}

int
main(int argc, char **argv) {
    time_t start = time(NULL);

    // If these fail, there's not really anywhere to complain...
    freopen(TEMPORARY_LOG_FILE, "a", stdout); setbuf(stdout, NULL);
    freopen(TEMPORARY_LOG_FILE, "a", stderr); setbuf(stderr, NULL);

    // If this binary is started with the single argument "--adbd",
    // instead of being the normal recovery binary, it turns into kind
    // of a stripped-down version of adbd that only supports the
    // 'sideload' command.  Note this must be a real argument, not
    // anything in the command file or bootloader control block; the
    // only way recovery should be run with this argument is when it
    // starts a copy of itself from the apply_from_adb() function.
    if (argc == 2 && strcmp(argv[1], "--adbd") == 0) {
        adb_main();
        return 0;
    }

    printf("Starting recovery on %s", ctime(&start));

    load_volume_table();
    get_args(&argc, &argv);

    int previous_runs = 0;
    const char *send_intent = NULL;
    char *update_package[100] = {0};  //MAX_ARGS is 100
    int package_count = 0;
    int i = 0;

    int wipe_data = 0, wipe_cache = 0, show_text = 0;
    bool just_exit = false;

#if 1 //wschen 2012-07-10
#ifdef SUPPORT_FOTA
    const char *fota_delta_path = NULL;
#endif
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-05-16 
    const char *restore_data_path = NULL;
#endif //SUPPORT_DATA_BACKUP_RESTORE
#endif

    int arg;
    while ((arg = getopt_long(argc, argv, "", OPTIONS, NULL)) != -1) {
        switch (arg) {
        case 'p': previous_runs = atoi(optarg); break;
        case 's': send_intent = optarg; break;
        case 'u': 
                update_package[package_count] = strdup(optarg);
                package_count++;
                break;
        case 'w': wipe_data = wipe_cache = 1; break;
        case 'c': wipe_cache = 1; break;
        case 't': show_text = 1; break;
        case 'x': just_exit = true; break;
        case 'l': locale = optarg; break;
#if 1 //wschen 2012-07-10
#ifdef SUPPORT_FOTA
        case 'f': fota_delta_path = optarg; break;
#endif
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-05-16 
        case 'r': restore_data_path = optarg; break;
#endif //SUPPORT_DATA_BACKUP_RESTORE
#endif
        case '?':
            LOGE("Invalid command argument\n");
            continue;
        }
    }
    if (locale == NULL) {
        load_locale_from_cache();
    }
    printf("locale is [%s]\n", locale);

    Device* device = make_device();
    ui = device->GetUI();

    ui->Init();
    ui->SetLocale(locale);
    ui->SetBackground(RecoveryUI::NONE);
    if (show_text) ui->ShowText(true);

#ifdef HAVE_SELINUX
    struct selinux_opt seopts[] = {
      { SELABEL_OPT_PATH, "/file_contexts" }
    };

    sehandle = selabel_open(SELABEL_CTX_FILE, seopts, 1);

    if (!sehandle) {
        fprintf(stderr, "Warning: No file_contexts\n");
        ui->Print("Warning:  No file_contexts\n");
    }
#endif

    device->StartRecovery();

    printf("Command:");
    for (arg = 0; arg < argc; arg++) {
        printf(" \"%s\"", argv[arg]);
    }
    printf("\n");

    /* ----------------------------- */
    /* SECURE BOOT INIT              */    
    /* ----------------------------- */    
#ifdef SUPPORT_SBOOT_UPDATE
    sec_init(false);
#endif

    printf("\n");

    property_list(print_property, NULL);
    printf("\n");

    if (package_count > 0){
        for (i = 0; i < package_count; i++){
            fprintf(stdout, "index %d update_package = %s\n", i, update_package[i]);
        }
    }

#ifdef SUPPORT_FOTA
    fprintf(stdout, "fota_delta_path = %s\n", fota_delta_path ? fota_delta_path : "NULL");
#endif

    int status = INSTALL_SUCCESS;

    if (package_count!= 0 || wipe_data) {
        if (package_count != 0) {
            int copy_success = 0;
    		char former_command[128];
    		int persist_update = 0;
    		struct bootloader_message boot;
			
    		if (ensure_path_mounted("/cache") != 0) {
                    LOGE("Can't mount %s\n", "/cache");
                    return 0;
    		}

              memset(&boot, 0, sizeof(boot));
    		get_bootloader_message(&boot);
    		//if the running_command is not null, maybe we had installed somepackages and been interrupted
    		if (!strncmp(boot.running_command, "--update_package=", strlen("--update_package="))) {
                LOGI("boot.running_command = %s\n", boot.running_command);
    		    persist_update = 1; 
			}
    		
            for (i = 0; i < package_count; i++) {
    			copy_success = 0;
    			send_intent = "0";
    			status = INSTALL_SUCCESS; //reset variables
    			
    			strcpy(former_command, "--update_package=");    			
    			strcat(former_command, update_package[i]);

    			if (persist_update && strncmp(boot.running_command, former_command, strlen(former_command))) {
    				#ifndef DISPLAY_CHINESE_IN_RECOVERY
						ui->Print("%s had been installed, ingore!\n", former_command);
					#else
						ui->Print("%s 已经安装过，忽略!\n", former_command);
					#endif					
    				continue;
    			}
                //ingore package until we find the last package we were to update.
                //if we find the last package, reset persist_update state, or we will can't continue
                persist_update = 0;

    			save_running_command(former_command); //write current running command to /misc, so we can resume update at the last package we were to update.
                #ifndef DISPLAY_CHINESE_IN_RECOVERY
					ui->Print("Start to install package %s", update_package[i]);
				#else
					ui->Print("开始安装更新包 %s\n", update_package[i]);
				#endif
    			#if SUPPORT_SD_HOTPLUG
    			if (!strncmp(update_package[i], "/sdcard", strlen("/sdcard")) &&
    		    	copy_update_package_to_temp(update_package[i])) {
					status = install_package(UPDATE_TEMP_FILE, &wipe_cache, TEMPORARY_INSTALL_FILE);
					copy_success = 1;
    		    } else
    			#endif
    			{
    				status = install_package(update_package[i], &wipe_cache, TEMPORARY_INSTALL_FILE);
    		    }

				if (status == INSTALL_SUCCESS && wipe_cache) {
	                if (erase_volume("/cache")) {
				LOGE("Cache wipe (request by package) failed.");
	                }
	            }

				sync();
				usleep(2000000);
				prompt_error_message(status);
                if (status != INSTALL_SUCCESS) {
					send_intent = "1";
					feed_back(send_intent);
					#ifndef DISPLAY_CHINESE_IN_RECOVERY
						ui->Print("Installation aborted.\n");
					#else
						ui->Print("安装失败.\n");
					#endif
                }
				
				#if SUPPORT_SD_HOTPLUG
			    if (!strncmp(update_package[i], "/sdcard", strlen("/sdcard")))  {
				LOGE("Please re-insert the sd-card and try again\n");
			    }
				
			    if (copy_success) {
			    	unlink(UPDATE_TEMP_FILE);
			    }
				#endif
				
                if (status == INSTALL_SUCCESS) {
                    #ifndef DISPLAY_CHINESE_IN_RECOVERY
						ui->Print("Install package %s successfully!\n\n", update_package[i]);
					#else
						ui->Print("成功安装更新包 %s !\n\n", update_package[i]);
					#endif
                } else {
                    break;   //if any package install error, we should not continue
				}
            }
        }
		
        if (wipe_data){
#if 1 //wschen 2012-04-12
	        struct phone_encrypt_state ps;
	        ps.state = 1;
	        set_phone_encrypt_state(&ps);
#endif
            if (device->WipeData()) {
				status = INSTALL_ERROR;
			}
			
            #ifdef SPECIAL_FACTORY_RESET //wschen 2011-06-16
            if (special_factory_reset()) {
				status = INSTALL_ERROR;
			}
            #else
            if (erase_volume("/data")) {
				status = INSTALL_ERROR;
			}
            #endif //SPECIAL_FACTORY_RESET

            if (wipe_cache) {
				struct bootloader_message boot;
				memset(&boot, 0, sizeof(boot));
				strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
				strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
				strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
				set_bootloader_message(&boot);
				sync();
				if (erase_volume("/cache")) {
					status = INSTALL_ERROR;
				}
			}
			
            if (status != INSTALL_SUCCESS) {
				#ifndef DISPLAY_CHINESE_IN_RECOVERY
					ui->Print("Data wipe failed.\n");
				#else
					ui->Print("数据清除失败.\n");
				#endif
			}
        }
    } else if (wipe_cache) {
#if 1 //wschen 2012-03-21
        struct bootloader_message boot;
        memset(&boot, 0, sizeof(boot));
        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
        strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
        set_bootloader_message(&boot);
        sync();
#endif
        if (erase_volume("/cache")) status = INSTALL_ERROR;
#if 1 //wschen 2012-04-19 write to /dev/misc and reboot soon, it may not really write to flash
        if (status != INSTALL_SUCCESS) {
	#ifndef DISPLAY_CHINESE_IN_RECOVERY
		ui->Print("Cache wipe failed.\n");
	#else
		ui->Print("缓存清除失败.\n");
	#endif
        } else {
            memset(&boot, 0, sizeof(boot));
            set_bootloader_message(&boot);
            sync();
        }
#endif
#ifdef SUPPORT_FOTA
    } else if (fota_delta_path)  {

        struct bootloader_message boot;
        memset(&boot, 0, sizeof(boot));
        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
        strlcat(boot.recovery, "--fota_delta_path_1=", sizeof(boot.recovery));
        strlcat(boot.recovery, fota_delta_path, sizeof(boot.recovery));
        set_bootloader_message(&boot);
        sync();

        status = install_fota_delta_package(fota_delta_path);

        /* ----------------------------- */
        /* SECURE BOOT UPDATE            */    
        /* ----------------------------- */            
#ifdef SUPPORT_SBOOT_UPDATE
        if (INSTALL_SUCCESS == status) {
            sec_update(false);
        }
#endif
#endif
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-05-16 
    } else if (restore_data_path) {
        if (ensure_path_mounted(SDCARD_ROOT) != 0) {
            ui->SetBackground(RecoveryUI::ERROR);
    	#ifndef DISPLAY_CHINESE_IN_RECOVERY
		ui->Print("No SD-Card.\n");
	#else
		ui->Print("未找到SD卡.\n");
	#endif
            status = INSTALL_ERROR;
        } else {
            struct bootloader_message boot;

            status = userdata_restore((char *)restore_data_path, 0);

            memset(&boot, 0, sizeof(boot));
            set_bootloader_message(&boot);
            sync();
            ensure_path_unmounted(SDCARD_ROOT);
        }
#endif //SUPPORT_DATA_BACKUP_RESTORE
    } else if (!just_exit) {
        status = INSTALL_NONE;  // No command specified
    }

    if (status == INSTALL_ERROR || status == INSTALL_CORRUPT) {
        ui->SetBackground(RecoveryUI::ERROR);
    }
    if (status != INSTALL_SUCCESS) {
        ui->ShowText(true);
        prompt_and_wait(device, status);
    }

    // Otherwise, get ready to boot the main system...
    finish_recovery(send_intent);
#ifndef DISPLAY_CHINESE_IN_RECOVERY
	ui->Print("Rebooting...\n");
#else
	ui->Print("正在重启...\n");
#endif
    android_reboot(ANDROID_RB_RESTART, 0, 0);
    return EXIT_SUCCESS;
}

#endif /* VENDOR_EDIT */

