/*
 * Copyright (C) 2008 The Android Open Source Project
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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <linux/capability.h>
#include <linux/prctl.h>

#include <cutils/properties.h>

#include "private/android_filesystem_config.h"

#define LOG_TAG "dumpstate"
#include <utils/Log.h>

#include "dumpstate.h"

/* read before root is shed */
static char cmdline_buf[16384] = "(unknown)";
static const char *dump_traces_path = NULL;

static char screenshot_path[PATH_MAX] = "";

#ifdef VENDOR_EDIT
//licx@OnLineRD.framework, 2012-08-13 modify
#define NORMAL_TYPE_STRING "normal"
#define ASSERT_TYPE_STRING "assert"
#define FEEDBACK_TYPE_STRING "feedback"
#define ALL_TYPE_STRING "all"

#define NORMAL_TYPE 0
#define ASSERT_TYPE 1
#define FEEDBACK_TYPE 2
#define ALL_TYPE 3

#define _parse_type(str, type) \
            ({ \
                strcmp(str, type##_STRING) == 0 ? type : NORMAL_TYPE; \
            })

static int parse_type(char *type, int length)
{
    if (type == NULL)
        return NORMAL_TYPE;
    switch(length) {
        case 3:
            return _parse_type(type, ALL_TYPE);
        case 6:
            return _parse_type(type, ASSERT_TYPE);
        case 8:
            return _parse_type(type, FEEDBACK_TYPE);
        default:
            return NORMAL_TYPE;
    }
}
#endif /* VENDOR_EDIT */

/* dumps the current system state to stdout */
#ifndef VENDOR_EDIT
//licx@OnLineRD.framework, 2012-08-13 modifiy
// android code
static void dumpstate() {
#else
static void dumpstate(const int type) {
#endif /* VENDOR_EDIT */
    time_t now = time(NULL);
    char build[PROPERTY_VALUE_MAX], fingerprint[PROPERTY_VALUE_MAX];
    char radio[PROPERTY_VALUE_MAX], bootloader[PROPERTY_VALUE_MAX];
    char network[PROPERTY_VALUE_MAX], date[80];
    char build_type[PROPERTY_VALUE_MAX];

    property_get("ro.build.display.id", build, "(unknown)");
    property_get("ro.build.fingerprint", fingerprint, "(unknown)");
    property_get("ro.build.type", build_type, "(unknown)");
    property_get("ro.baseband", radio, "(unknown)");
    property_get("ro.bootloader", bootloader, "(unknown)");
    property_get("gsm.operator.alpha", network, "(unknown)");
    strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", localtime(&now));

    printf("========================================================\n");
    printf("== dumpstate: %s\n", date);
    printf("========================================================\n");

    printf("\n");
    printf("Build: %s\n", build);
    printf("Build fingerprint: '%s'\n", fingerprint); /* format is important for other tools */
    printf("Bootloader: %s\n", bootloader);
    printf("Radio: %s\n", radio);
    printf("Network: %s\n", network);

    printf("Kernel: ");
    dump_file(NULL, "/proc/version");
    printf("Command line: %s\n", strtok(cmdline_buf, "\n"));
    printf("\n");

#ifndef VENDOR_EDIT
//licx@OnLineRD.framework, 2012-08-13 modifiy
    // android code
    run_command("UPTIME", 10, "uptime", NULL);
    dump_file("MEMORY INFO", "/proc/meminfo");
    run_command("CPU INFO", 10, "top", "-n", "1", "-d", "1", "-m", "30", "-t", NULL);
    run_command("PROCRANK", 20, "procrank", NULL);
    dump_file("VIRTUAL MEMORY STATS", "/proc/vmstat");
    dump_file("VMALLOC INFO", "/proc/vmallocinfo");
    dump_file("SLAB INFO", "/proc/slabinfo");
    dump_file("ZONEINFO", "/proc/zoneinfo");
    dump_file("PAGETYPEINFO", "/proc/pagetypeinfo");
    dump_file("BUDDYINFO", "/proc/buddyinfo");
    dump_file("FRAGMENTATION INFO", "/d/extfrag/unusable_index");


    dump_file("KERNEL WAKELOCKS", "/proc/wakelocks");
    dump_file("KERNEL CPUFREQ", "/sys/devices/system/cpu/cpu0/cpufreq/stats/time_in_state");

    run_command("PROCESSES", 10, "ps", "-P", NULL);
    run_command("PROCESSES AND THREADS", 10, "ps", "-t", "-p", "-P", NULL);
    run_command("LIBRANK", 10, "librank", NULL);

    do_dmesg();

    run_command("LIST OF OPEN FILES", 10, SU_PATH, "root", "lsof", NULL);

    for_each_pid(do_showmap, "SMAPS OF ALL PROCESSES");
    for_each_pid(show_wchan, "BLOCKED PROCESS WAIT-CHANNELS");

#else
    switch (type) {
        case ASSERT_TYPE: {
            dump_file("MEMORY INFO", "/proc/meminfo");
            run_command("CPU INFO", 10, "top", "-n", "1", "-d", "1", NULL);
            dump_file("VMALLOC INFO", "/proc/vmallocinfo");
			
            run_command("PROCESSES", 10, "ps", "-P", NULL);
            run_command("PROCESSES AND THREADS", 10, "ps", "-t", "-p", "-P", NULL);
			
            run_command("LIST OF OPEN FILES", 10, SU_PATH, "root", "lsof", NULL);
        }
        break;
        case FEEDBACK_TYPE: {
            dump_file("MEMORY INFO", "/proc/meminfo");
            run_command("CPU INFO", 10, "top", "-n", "1", "-d", "1", "-m", "30", "-t", NULL);
			
            dump_file("VIRTUAL MEMORY STATS", "/proc/vmstat");
            dump_file("VMALLOC INFO", "/proc/vmallocinfo");
			
            dump_file("KERNEL WAKELOCKS", "/proc/wakelocks");
            dump_file("KERNEL CPUFREQ", "/sys/devices/system/cpu/cpu0/cpufreq/stats/time_in_state");
			
            run_command("PROCESSES", 10, "ps", "-P", NULL);
            run_command("PROCESSES AND THREADS", 10, "ps", "-t", "-p", "-P", NULL);
			
            do_dmesg();
        }
        break;
        default: {
            run_command("UPTIME", 10, "uptime", NULL);
            dump_file("MEMORY INFO", "/proc/meminfo");
            run_command("CPU INFO", 10, "top", "-n", "1", "-d", "1", "-m", "30", "-t", NULL);
            run_command("PROCRANK", 20, "procrank", NULL);
            dump_file("VIRTUAL MEMORY STATS", "/proc/vmstat");
            dump_file("VMALLOC INFO", "/proc/vmallocinfo");
            dump_file("SLAB INFO", "/proc/slabinfo");
            dump_file("ZONEINFO", "/proc/zoneinfo");
            dump_file("PAGETYPEINFO", "/proc/pagetypeinfo");
            dump_file("BUDDYINFO", "/proc/buddyinfo");

            dump_file("KERNEL WAKELOCKS", "/proc/wakelocks");
            dump_file("KERNEL CPUFREQ", "/sys/devices/system/cpu/cpu0/cpufreq/stats/time_in_state");

            run_command("PROCESSES", 10, "ps", "-P", NULL);
            run_command("PROCESSES AND THREADS", 10, "ps", "-t", "-p", "-P", NULL);
            run_command("LIBRANK", 10, "librank", NULL);

            do_dmesg();

            run_command("LIST OF OPEN FILES", 10, SU_PATH, "root", "lsof", NULL);

            for_each_pid(do_showmap, "SMAPS OF ALL PROCESSES");
            for_each_pid(show_wchan, "BLOCKED PROCESS WAIT-CHANNELS");
        }
        break;
    }
#endif /* VENDOR_EDIT */

    // dump_file("EVENT LOG TAGS", "/etc/event-log-tags");
    run_command("SYSTEM LOG", 20, "logcat", "-v", "threadtime", "-d", "*:v", NULL);
    run_command("EVENT LOG", 20, "logcat", "-b", "events", "-v", "threadtime", "-d", "*:v", NULL);
    run_command("RADIO LOG", 20, "logcat", "-b", "radio", "-v", "threadtime", "-d", "*:v", NULL);


    /* show the traces we collected in main(), if that was done */
    if (dump_traces_path != NULL) {
        dump_file("VM TRACES JUST NOW", dump_traces_path);
    }

    /* only show ANR traces if they're less than 15 minutes old */
    struct stat st;
    char anr_traces_path[PATH_MAX];
    property_get("dalvik.vm.stack-trace-file", anr_traces_path, "");
    if (!anr_traces_path[0]) {
        printf("*** NO VM TRACES FILE DEFINED (dalvik.vm.stack-trace-file)\n\n");
    } else if (stat(anr_traces_path, &st)) {
        printf("*** NO ANR VM TRACES FILE (%s): %s\n\n", anr_traces_path, strerror(errno));
    } else {
        dump_file("VM TRACES AT LAST ANR", anr_traces_path);
    }

    /* slow traces for slow operations */
    if (anr_traces_path[0] != 0) {
        int tail = strlen(anr_traces_path)-1;
        while (tail > 0 && anr_traces_path[tail] != '/') {
            tail--;
        }
        int i = 0;
        while (1) {
            sprintf(anr_traces_path+tail+1, "slow%02d.txt", i);
            if (stat(anr_traces_path, &st)) {
                // No traces file at this index, done with the files.
                break;
            }
            dump_file("VM TRACES WHEN SLOW", anr_traces_path);
            i++;
        }
    }

#ifdef VENDOR_EDIT
//licx@OnLineRD.framework, 2012-08-13 modifiy
    if (type == ALL_TYPE || type == NORMAL_TYPE) {
#endif /* VENDOR_EDIT */
    dump_file("NETWORK DEV INFO", "/proc/net/dev");
    dump_file("QTAGUID NETWORK INTERFACES INFO", "/proc/net/xt_qtaguid/iface_stat_all");
    dump_file("QTAGUID NETWORK INTERFACES INFO (xt)", "/proc/net/xt_qtaguid/iface_stat_fmt");
    dump_file("QTAGUID CTRL INFO", "/proc/net/xt_qtaguid/ctrl");
    dump_file("QTAGUID STATS INFO", "/proc/net/xt_qtaguid/stats");

    dump_file("NETWORK ROUTES", "/proc/net/route");
    dump_file("NETWORK ROUTES IPV6", "/proc/net/ipv6_route");

    /* TODO: Make last_kmsg CAP_SYSLOG protected. b/5555691 */
    dump_file("LAST KMSG", "/proc/last_kmsg");
    dump_file("LAST PANIC CONSOLE", "/data/dontpanic/apanic_console");
    dump_file("LAST PANIC THREADS", "/data/dontpanic/apanic_threads");

    if (screenshot_path[0]) {
        ALOGI("taking screenshot\n");
        run_command(NULL, 5, SU_PATH, "root", "screenshot", screenshot_path, NULL);
        ALOGI("wrote screenshot: %s\n", screenshot_path);
    }

    run_command("SYSTEM SETTINGS", 20, SU_PATH, "root", "sqlite3",
            "/data/data/com.android.providers.settings/databases/settings.db",
            "pragma user_version; select * from system; select * from secure; select * from global;", NULL);

    /* The following have a tendency to get wedged when wifi drivers/fw goes belly-up. */
    run_command("NETWORK INTERFACES", 10, SU_PATH, "root", "netcfg", NULL);
    run_command("IP RULES", 10, "ip", "rule", "show", NULL);
    run_command("IP RULES v6", 10, "ip", "-6", "rule", "show", NULL);
    run_command("ROUTE TABLE 60", 10, "ip", "route", "show", "table", "60", NULL);
    run_command("ROUTE TABLE 61 v6", 10, "ip", "-6", "route", "show", "table", "60", NULL);
    run_command("ROUTE TABLE 61", 10, "ip", "route", "show", "table", "61", NULL);
    run_command("ROUTE TABLE 61 v6", 10, "ip", "-6", "route", "show", "table", "61", NULL);
    dump_file("ARP CACHE", "/proc/net/arp");
    run_command("IPTABLES", 10, SU_PATH, "root", "iptables", "-L", "-nvx", NULL);
    run_command("IP6TABLES", 10, SU_PATH, "root", "ip6tables", "-L", "-nvx", NULL);
    run_command("IPTABLE NAT", 10, SU_PATH, "root", "iptables", "-t", "nat", "-L", "-nvx", NULL);
    /* no ip6 nat */
    run_command("IPTABLE RAW", 10, SU_PATH, "root", "iptables", "-t", "raw", "-L", "-nvx", NULL);
    run_command("IP6TABLE RAW", 10, SU_PATH, "root", "ip6tables", "-t", "raw", "-L", "-nvx", NULL);

    run_command("WIFI NETWORKS", 20,
            SU_PATH, "root", "wpa_cli", "list_networks", NULL);

    property_get("dhcp.wlan0.gateway", network, "");
    if (network[0])
        run_command("PING GATEWAY", 10, SU_PATH, "root", "ping", "-c", "3", "-i", ".5", network, NULL);
    property_get("dhcp.wlan0.dns1", network, "");
    if (network[0])
        run_command("PING DNS1", 10, SU_PATH, "root", "ping", "-c", "3", "-i", ".5", network, NULL);
    property_get("dhcp.wlan0.dns2", network, "");
    if (network[0])
        run_command("PING DNS2", 10, SU_PATH, "root", "ping", "-c", "3", "-i", ".5", network, NULL);
#ifdef VENDOR_EDIT
//licx@OnLineRD.framework, 2012-08-13 modifiy
    }
#endif /* VENDOR_EDIT */
#ifdef FWDUMP_bcm4329
    run_command("DUMP WIFI STATUS", 20,
            SU_PATH, "root", "dhdutil", "-i", "wlan0", "dump", NULL);
    run_command("DUMP WIFI INTERNAL COUNTERS", 20,
            SU_PATH, "root", "wlutil", "counters", NULL);
#endif

#ifndef VENDOR_EDIT
//licx@OnLineRD.framework, 2012-08-13 modifiy
    // android code
    print_properties();

    run_command("VOLD DUMP", 10, "vdc", "dump", NULL);
    run_command("SECURE CONTAINERS", 10, "vdc", "asec", "list", NULL);

    run_command("FILESYSTEMS & FREE SPACE", 10, SU_PATH, "root", "df", NULL);

    run_command("PACKAGE SETTINGS", 20, SU_PATH, "root", "cat", "/data/system/packages.xml", NULL);
    dump_file("PACKAGE UID ERRORS", "/data/system/uiderrors.txt");

    run_command("LAST RADIO LOG", 10, "parse_radio_log", "/proc/last_radio_log", NULL);

    printf("------ BACKLIGHTS ------\n");
    printf("LCD brightness=");
    dump_file(NULL, "/sys/class/leds/lcd-backlight/brightness");
    printf("Button brightness=");
    dump_file(NULL, "/sys/class/leds/button-backlight/brightness");
    printf("Keyboard brightness=");
    dump_file(NULL, "/sys/class/leds/keyboard-backlight/brightness");
    printf("ALS mode=");
    dump_file(NULL, "/sys/class/leds/lcd-backlight/als");
    printf("LCD driver registers:\n");
    dump_file(NULL, "/sys/class/leds/lcd-backlight/registers");
    printf("\n");

    /* Binder state is expensive to look at as it uses a lot of memory. */
    dump_file("BINDER FAILED TRANSACTION LOG", "/sys/kernel/debug/binder/failed_transaction_log");
    dump_file("BINDER TRANSACTION LOG", "/sys/kernel/debug/binder/transaction_log");
    dump_file("BINDER TRANSACTIONS", "/sys/kernel/debug/binder/transactions");
    dump_file("BINDER STATS", "/sys/kernel/debug/binder/stats");
    dump_file("BINDER STATE", "/sys/kernel/debug/binder/state");

#ifdef BOARD_HAS_DUMPSTATE
    printf("========================================================\n");
    printf("== Board\n");
    printf("========================================================\n");

    dumpstate_board();
    printf("\n");
#endif

    /* Migrate the ril_dumpstate to a dumpstate_board()? */
    char ril_dumpstate_timeout[PROPERTY_VALUE_MAX] = {0};
    property_get("ril.dumpstate.timeout", ril_dumpstate_timeout, "30");
    if (strnlen(ril_dumpstate_timeout, PROPERTY_VALUE_MAX - 1) > 0) {
        if (0 == strncmp(build_type, "user", PROPERTY_VALUE_MAX - 1)) {
            // su does not exist on user builds, so try running without it.
            // This way any implementations of vril-dump that do not require
            // root can run on user builds.
            run_command("DUMP VENDOR RIL LOGS", atoi(ril_dumpstate_timeout),
                    "vril-dump", NULL);
        } else {
            run_command("DUMP VENDOR RIL LOGS", atoi(ril_dumpstate_timeout),
                    SU_PATH, "root", "vril-dump", NULL);
        }
    }
#else
    switch (type) {
        case ASSERT_TYPE: {
            run_command("FILESYSTEMS & FREE SPACE", 10, "su", "root", "df", NULL);
        }
        break;
        case FEEDBACK_TYPE: {
            print_properties();

            run_command("VOLD DUMP", 10, "vdc", "dump", NULL);
            
            dump_file("PACKAGE SETTINGS", "/data/system/packages.xml");
            dump_file("PACKAGE UID ERRORS", "/data/system/uiderrors.txt");
            #ifdef BOARD_HAS_DUMPSTATE
                printf("========================================================\n");
                printf("== Board\n");
                printf("========================================================\n");

                dumpstate_board();
                printf("\n");
            #endif
        }
        break;
        default: {
            print_properties();

            run_command("VOLD DUMP", 10, "vdc", "dump", NULL);
            run_command("SECURE CONTAINERS", 10, "vdc", "asec", "list", NULL);

            run_command("FILESYSTEMS & FREE SPACE", 10, SU_PATH, "root", "df", NULL);

            run_command("PACKAGE SETTINGS", 20, SU_PATH, "root", "cat", "/data/system/packages.xml", NULL);
            dump_file("PACKAGE UID ERRORS", "/data/system/uiderrors.txt");

            run_command("LAST RADIO LOG", 10, "parse_radio_log", "/proc/last_radio_log", NULL);

            printf("------ BACKLIGHTS ------\n");
            printf("LCD brightness=");
            dump_file(NULL, "/sys/class/leds/lcd-backlight/brightness");
            printf("Button brightness=");
            dump_file(NULL, "/sys/class/leds/button-backlight/brightness");
            printf("Keyboard brightness=");
            dump_file(NULL, "/sys/class/leds/keyboard-backlight/brightness");
            printf("ALS mode=");
            dump_file(NULL, "/sys/class/leds/lcd-backlight/als");
            printf("LCD driver registers:\n");
            dump_file(NULL, "/sys/class/leds/lcd-backlight/registers");
            printf("\n");

            /* Binder state is expensive to look at as it uses a lot of memory. */
            dump_file("BINDER FAILED TRANSACTION LOG", "/sys/kernel/debug/binder/failed_transaction_log");
            dump_file("BINDER TRANSACTION LOG", "/sys/kernel/debug/binder/transaction_log");
            dump_file("BINDER TRANSACTIONS", "/sys/kernel/debug/binder/transactions");
            dump_file("BINDER STATS", "/sys/kernel/debug/binder/stats");
            dump_file("BINDER STATE", "/sys/kernel/debug/binder/state");
			#ifdef BOARD_HAS_DUMPSTATE
                printf("========================================================\n");
                printf("== Board\n");
                printf("========================================================\n");

                dumpstate_board();
                printf("\n");
            #endif

            /* Migrate the ril_dumpstate to a dumpstate_board()? */
            char ril_dumpstate_timeout[PROPERTY_VALUE_MAX] = {0};
            property_get("ril.dumpstate.timeout", ril_dumpstate_timeout, "30");
            if (strnlen(ril_dumpstate_timeout, PROPERTY_VALUE_MAX - 1) > 0) {
                if (0 == strncmp(build_type, "user", PROPERTY_VALUE_MAX - 1)) {
                    // su does not exist on user builds, so try running without it.
                    // This way any implementations of vril-dump that do not require
                    // root can run on user builds.
                    run_command("DUMP VENDOR RIL LOGS", atoi(ril_dumpstate_timeout),
                            "vril-dump", NULL);
                } else {
                    run_command("DUMP VENDOR RIL LOGS", atoi(ril_dumpstate_timeout),
                            SU_PATH, "root", "vril-dump", NULL);
                }
            }
        }
        break;
    }
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
//licx@OnLineRD.framework, 2012-08-13 modifiy
    if (type != FEEDBACK_TYPE) {
#endif /* VENDOR_EDIT */
    printf("========================================================\n");
    printf("== Android Framework Services\n");
    printf("========================================================\n");

    /* the full dumpsys is starting to take a long time, so we need
       to increase its timeout.  we really need to do the timeouts in
       dumpsys itself... */
    run_command("DUMPSYS", 60, "dumpsys", NULL);
#ifdef VENDOR_EDIT
//licx@OnLineRD.framework, 2012-08-13 modifiy
    }
#endif /* VENDOR_EDIT */

    printf("========================================================\n");
    printf("== Running Application Activities\n");
    printf("========================================================\n");

    run_command("APP ACTIVITIES", 30, "dumpsys", "activity", "all", NULL);

    printf("========================================================\n");
    printf("== Running Application Services\n");
    printf("========================================================\n");

    run_command("APP SERVICES", 30, "dumpsys", "activity", "service", "all", NULL);
#ifdef VENDOR_EDIT
//licx@OnLineRD.framework, 2012-08-13 modifiy
    if (type == FEEDBACK_TYPE) {
        printf("========================================================\n");
        printf("== Running usb Services\n");
        printf("========================================================\n");
    
        run_command("USB SERVICES", 30, "dumpsys", "usb", NULL);
    
        printf("========================================================\n");
        printf("== Running mount Services\n");
        printf("========================================================\n");
    
        run_command("MOUNT SERVICES", 30, "dumpsys", "mount", NULL);
    
        printf("========================================================\n");
        printf("== Running throttle Services\n");
        printf("========================================================\n");
    
        run_command("THROTTLE SERVICES", 30, "dumpsys", "throttle", NULL);
    
        printf("========================================================\n");
        printf("== Running connectivity Services\n");
        printf("========================================================\n");
    
        run_command("CONNECTIVITY SERVICES", 30, "dumpsys", "connectivity", NULL);
        
        printf("========================================================\n");
        printf("== Running wifi Services\n");
        printf("========================================================\n");
    
        run_command("WIFI SERVICES", 30, "dumpsys", "wifi", NULL);
    
    
        printf("========================================================\n");
        printf("== Running statusbar Services\n");
        printf("========================================================\n");
    
        run_command("SATUSBAR SERVICES", 30, "dumpsys", "statusbar", NULL);
    
    
        printf("========================================================\n");
        printf("== Running window Services\n");
        printf("========================================================\n");
    
        run_command("WINDOW SERVICES", 30, "dumpsys", "window", NULL);
    
        printf("========================================================\n");
        printf("== Running alarm Services\n");
        printf("========================================================\n");
    
        run_command("ALARM SERVICES", 30, "dumpsys", "alarm", NULL);
        
        printf("========================================================\n");
        printf("== Running power Services\n");
        printf("========================================================\n");
    
        run_command("POWER SERVICES", 30, "dumpsys", "power", NULL);
    
        printf("========================================================\n");
        printf("== Running SurfaceFlinger Services\n");
        printf("========================================================\n");
    
        run_command("SURFACEFLINGER SERVICES", 30, "dumpsys", "SurfaceFlinger", NULL);
    }
#endif /* VENDOR_EDIT */

    printf("========================================================\n");
    printf("== Running Application Providers\n");
    printf("========================================================\n");

    run_command("APP SERVICES", 30, "dumpsys", "activity", "provider", "all", NULL);


    printf("========================================================\n");
    printf("== dumpstate: done\n");
    printf("========================================================\n");
}

static void usage() {
#ifndef VENDOR_EDIT
//licx@OnLineRD.framework, 2012-08-13 modify
    // android code
    fprintf(stderr, "usage: dumpstate [-b soundfile] [-e soundfile] [-o file [-d] [-p] [-z]] [-s]\n"
            "  -o: write to file (instead of stdout)\n"
            "  -d: append date to filename (requires -o)\n"
            "  -z: gzip output (requires -o)\n"
            "  -p: capture screenshot to filename.png (requires -o)\n"
            "  -s: write output to control socket (for init)\n"
            "  -b: play sound file instead of vibrate, at beginning of job\n"
            "  -e: play sound file instead of vibrate, at end of job\n"
		);
#else
    fprintf(stderr, "usage: dumpstate [-b soundfile] [-e soundfile] [-o file [-d] [-p] [-z]] [-s] [-t type]\n"
            "  -o: write to file (instead of stdout)\n"
            "  -d: append date to filename (requires -o)\n"
            "  -z: gzip output (requires -o)\n"
            "  -p: capture screenshot to filename.png (requires -o)\n"
            "  -s: write output to control socket (for init)\n"
            "  -b: play sound file instead of vibrate, at beginning of job\n"
            "  -e: play sound file instead of vibrate, at end of job\n"
            "  -t: type given to choose what & how to dump\n"
        );
#endif /* VENDOR_EDIT */
}

int main(int argc, char *argv[]) {
    int do_add_date = 0;
    int do_compress = 0;
    int do_vibrate = 1;
    char* use_outfile = 0;
    char* begin_sound = 0;
    char* end_sound = 0;
    int use_socket = 0;
    int do_fb = 0;
#ifdef VENDOR_EDIT
//licx@OnLineRD.framework, 2012-08-13 add
    int dump_type = NORMAL_TYPE;
#endif /* VENDOR_EDIT */
    if (getuid() != 0) {
        // Old versions of the adb client would call the
        // dumpstate command directly. Newer clients
        // call /system/bin/bugreport instead. If we detect
        // we're being called incorrectly, then exec the
        // correct program.
        return execl("/system/bin/bugreport", "/system/bin/bugreport", NULL);
    }
    ALOGI("begin\n");

    signal(SIGPIPE, SIG_IGN);

    /* set as high priority, and protect from OOM killer */
    setpriority(PRIO_PROCESS, 0, -20);
    FILE *oom_adj = fopen("/proc/self/oom_adj", "w");
    if (oom_adj) {
        fputs("-17", oom_adj);
        fclose(oom_adj);
    }

    /* very first thing, collect stack traces from Dalvik and native processes (needs root) */
    dump_traces_path = dump_traces();

    int c;
#ifndef VENDOR_EDIT
//licx@OnLineRD.framework, 2012-08-13 modifiy for providing type
    // android code
    while ((c = getopt(argc, argv, "b:de:ho:svzp")) != -1) {
#else
    while ((c = getopt(argc, argv, "b:de:ho:svzpt:")) != -1) {
#endif /* VENDOR_EDIT */
        switch (c) {
            case 'b': begin_sound = optarg;  break;
            case 'd': do_add_date = 1;       break;
            case 'e': end_sound = optarg;    break;
            case 'o': use_outfile = optarg;  break;
            case 's': use_socket = 1;        break;
            case 'v': break;  // compatibility no-op
            case 'q': do_vibrate = 0;        break;
            case 'z': do_compress = 6;       break;
            case 'p': do_fb = 1;             break;
#ifdef VENDOR_EDIT
//licx@OnLineRD.framework, 2012-08-13 add
            case 't': dump_type = parse_type(optarg, strlen(optarg));         break;
#endif /* VENDOR_EDIT */
            case '?': printf("\n");
            case 'h':
                usage();
                exit(1);
        }
    }

    FILE *vibrator = 0;
    if (do_vibrate) {
        /* open the vibrator before dropping root */
        vibrator = fopen("/sys/class/timed_output/vibrator/enable", "w");
        if (vibrator) fcntl(fileno(vibrator), F_SETFD, FD_CLOEXEC);
    }

    /* read /proc/cmdline before dropping root */
    FILE *cmdline = fopen("/proc/cmdline", "r");
    if (cmdline != NULL) {
        fgets(cmdline_buf, sizeof(cmdline_buf), cmdline);
        fclose(cmdline);
    }

    if (prctl(PR_SET_KEEPCAPS, 1) < 0) {
        ALOGE("prctl(PR_SET_KEEPCAPS) failed: %s\n", strerror(errno));
        return -1;
    }

    /* switch to non-root user and group */
    gid_t groups[] = { AID_LOG, AID_SDCARD_R, AID_SDCARD_RW,
            AID_MOUNT, AID_INET, AID_NET_BW_STATS };
    if (setgroups(sizeof(groups)/sizeof(groups[0]), groups) != 0) {
        ALOGE("Unable to setgroups, aborting: %s\n", strerror(errno));
        return -1;
    }
    if (setgid(AID_SHELL) != 0) {
        ALOGE("Unable to setgid, aborting: %s\n", strerror(errno));
        return -1;
    }
    if (setuid(AID_SHELL) != 0) {
        ALOGE("Unable to setuid, aborting: %s\n", strerror(errno));
        return -1;
    }

    struct __user_cap_header_struct capheader;
    struct __user_cap_data_struct capdata[2];
    memset(&capheader, 0, sizeof(capheader));
    memset(&capdata, 0, sizeof(capdata));
    capheader.version = _LINUX_CAPABILITY_VERSION_3;
    capheader.pid = 0;

    capdata[CAP_TO_INDEX(CAP_SYSLOG)].permitted = CAP_TO_MASK(CAP_SYSLOG);
    capdata[CAP_TO_INDEX(CAP_SYSLOG)].effective = CAP_TO_MASK(CAP_SYSLOG);
    capdata[0].inheritable = 0;
    capdata[1].inheritable = 0;

    if (capset(&capheader, &capdata[0]) < 0) {
        ALOGE("capset failed: %s\n", strerror(errno));
        return -1;
    }

    char path[PATH_MAX], tmp_path[PATH_MAX];
    pid_t gzip_pid = -1;

#ifndef VENDOR_EDIT
//licx@OnLineRD.framework, 2012-08-13 modifiy
    // android code
    if (use_socket) {
#else
    if (use_socket || (dump_type == FEEDBACK_TYPE)) {
#endif /* VENDOR_EDIT */
        redirect_to_socket(stdout, "dumpstate");
    } else if (use_outfile) {
        strlcpy(path, use_outfile, sizeof(path));
        if (do_add_date) {
            char date[80];
            time_t now = time(NULL);
            strftime(date, sizeof(date), "-%Y-%m-%d-%H-%M-%S", localtime(&now));
            strlcat(path, date, sizeof(path));
        }
        if (do_fb) {
            strlcpy(screenshot_path, path, sizeof(screenshot_path));
            strlcat(screenshot_path, ".png", sizeof(screenshot_path));
        }
        strlcat(path, ".txt", sizeof(path));
        if (do_compress) strlcat(path, ".gz", sizeof(path));
        strlcpy(tmp_path, path, sizeof(tmp_path));
        strlcat(tmp_path, ".tmp", sizeof(tmp_path));
        gzip_pid = redirect_to_file(stdout, tmp_path, do_compress);
    }

    if (begin_sound) {
        play_sound(begin_sound);
    } else if (vibrator) {
        fputs("150", vibrator);
        fflush(vibrator);
    }

#ifndef VENDOR_EDIT
//licx@OnLineRD.framework, 2012-08-13 modifiy
    // android code
    dumpstate();
#else
    dumpstate(dump_type);
#endif /* VENDOR_EDIT */


    if (end_sound) {
        play_sound(end_sound);
    } else if (vibrator) {
        int i;
        for (i = 0; i < 3; i++) {
            fputs("75\n", vibrator);
            fflush(vibrator);
            usleep((75 + 50) * 1000);
        }
        fclose(vibrator);
    }

    /* wait for gzip to finish, otherwise it might get killed when we exit */
    if (gzip_pid > 0) {
        fclose(stdout);
        waitpid(gzip_pid, NULL, 0);
    }

    /* rename the (now complete) .tmp file to its final location */
    if (use_outfile && rename(tmp_path, path)) {
        fprintf(stderr, "rename(%s, %s): %s\n", tmp_path, path, strerror(errno));
    }

    ALOGI("done\n");

    return 0;
}
