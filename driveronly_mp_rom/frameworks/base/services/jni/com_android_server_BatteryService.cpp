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

#define LOG_TAG "BatteryService"

#include "JNIHelp.h"
#include "jni.h"
#include <utils/Log.h>
#include <utils/misc.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <linux/ioctl.h>

namespace android {

#define POWER_SUPPLY_PATH "/sys/class/power_supply"

struct FieldIds {
    // members
    jfieldID mAcOnline;
    jfieldID mUsbOnline;
    jfieldID mWirelessOnline;
    jfieldID mBatteryStatus;
    jfieldID mBatteryHealth;
    jfieldID mBatteryPresent;
    jfieldID mBatteryLevel;
    jfieldID mBatteryVoltage;
    jfieldID mBatteryTemperature;
    jfieldID mBatteryTechnology;
	#ifdef VENDOR_EDIT
	//Zhanhong.Hu@Prd.SystemSrv.Charge 2011/07/21 Add
    jfieldID mChargerVoltage;
    jfieldID mChargingCurrent;
    //Jiangtao.Guo@Prd.SystemSrv.Charge, 2012/03/08, Add for [shutdown for low battery], {
    jfieldID mBatteryRequestPoweroff;
    //} add end for [].    
	#endif /* VENDOR_EDIT*/
};
static FieldIds gFieldIds;

struct BatteryManagerConstants {
    jint statusUnknown;
    jint statusCharging;
    jint statusDischarging;
    jint statusNotCharging;
    jint statusFull;
    jint healthUnknown;
    jint healthGood;
    jint healthOverheat;
    jint healthDead;
    jint healthOverVoltage;
    jint healthUnspecifiedFailure;
    jint healthCold;
};
static BatteryManagerConstants gConstants;

struct PowerSupplyPaths {
    char* acOnlinePath;
    char* usbOnlinePath;
    char* wirelessOnlinePath;
    char* batteryStatusPath;
    char* batteryHealthPath;
    char* batteryPresentPath;
    char* batteryCapacityPath;
    char* batteryVoltagePath;
    char* batteryTemperaturePath;
    char* batteryTechnologyPath;
	#ifdef VENDOR_EDIT
    //Zhanhong.Hu@Prd.SystemSrv.Charge 2011/07/21 Add
    char* chargerVoltagePath;
    char* chargingCurrentPath;
    //Jiangtao.Guo@Prd.SystemSrv.Charge, 2012/03/08, Add for [shutdown for low battery], {
    char* batteryRequestPoweroffPath;
    //} add end for [].    
    #endif /* VENDOR_EDIT*/
};
static PowerSupplyPaths gPaths;

static int gVoltageDivisor = 1;

static jint getBatteryStatus(const char* status)
{
    switch (status[0]) {
        case 'C': return gConstants.statusCharging;         // Charging
        case 'D': return gConstants.statusDischarging;      // Discharging
        case 'F': return gConstants.statusFull;             // Full
        case 'N': return gConstants.statusNotCharging;      // Not charging
        case 'U': return gConstants.statusUnknown;          // Unknown
            
        default: {
            ALOGW("Unknown battery status '%s'", status);
            return gConstants.statusUnknown;
        }
    }
}

static jint getBatteryHealth(const char* status)
{
    switch (status[0]) {
        case 'C': return gConstants.healthCold;         // Cold
        case 'D': return gConstants.healthDead;         // Dead
        case 'G': return gConstants.healthGood;         // Good
        case 'O': {
            if (strcmp(status, "Overheat") == 0) {
                return gConstants.healthOverheat;
            } else if (strcmp(status, "Over voltage") == 0) {
                return gConstants.healthOverVoltage;
            }
            ALOGW("Unknown battery health[1] '%s'", status);
            return gConstants.healthUnknown;
        }
        
        case 'U': {
            if (strcmp(status, "Unspecified failure") == 0) {
                return gConstants.healthUnspecifiedFailure;
            } else if (strcmp(status, "Unknown") == 0) {
                return gConstants.healthUnknown;
            }
            // fall through
        }
            
        default: {
            ALOGW("Unknown battery health[2] '%s'", status);
            return gConstants.healthUnknown;
        }
    }
}

static int readFromFile(const char* path, char* buf, size_t size)
{
    if (!path)
        return -1;
    int fd = open(path, O_RDONLY, 0);
    if (fd == -1) {
        ALOGE("Could not open '%s'", path);
        return -1;
    }
    
    ssize_t count = read(fd, buf, size);
    if (count > 0) {
        while (count > 0 && buf[count-1] == '\n')
            count--;
        buf[count] = '\0';
    } else {
        buf[0] = '\0';
    } 

    close(fd);
    return count;
}

static void setBooleanField(JNIEnv* env, jobject obj, const char* path, jfieldID fieldID)
{
    const int SIZE = 16;
    char buf[SIZE];
    
    jboolean value = false;
    if (readFromFile(path, buf, SIZE) > 0) {
        if (buf[0] != '0') {
            value = true;
        }
    }
    env->SetBooleanField(obj, fieldID, value);
}

static void setIntField(JNIEnv* env, jobject obj, const char* path, jfieldID fieldID)
{
    const int SIZE = 128;
    char buf[SIZE];
    
    jint value = 0;
    if (readFromFile(path, buf, SIZE) > 0) {
        value = atoi(buf);
    }
    env->SetIntField(obj, fieldID, value);
}

static void setVoltageField(JNIEnv* env, jobject obj, const char* path, jfieldID fieldID)
{
    const int SIZE = 128;
    char buf[SIZE];

    jint value = 0;
    if (readFromFile(path, buf, SIZE) > 0) {
        value = atoi(buf);
        value /= gVoltageDivisor;
    }
    env->SetIntField(obj, fieldID, value);
}


static void android_server_BatteryService_update(JNIEnv* env, jobject obj)
{
    setBooleanField(env, obj, gPaths.acOnlinePath, gFieldIds.mAcOnline);
    setBooleanField(env, obj, gPaths.usbOnlinePath, gFieldIds.mUsbOnline);
    setBooleanField(env, obj, gPaths.wirelessOnlinePath, gFieldIds.mWirelessOnline);
    setBooleanField(env, obj, gPaths.batteryPresentPath, gFieldIds.mBatteryPresent);
    
    setIntField(env, obj, gPaths.batteryCapacityPath, gFieldIds.mBatteryLevel);
    setVoltageField(env, obj, gPaths.batteryVoltagePath, gFieldIds.mBatteryVoltage);
    setIntField(env, obj, gPaths.batteryTemperaturePath, gFieldIds.mBatteryTemperature);

    #ifdef VENDOR_EDIT
	//Zhanhong.Hu@Prd.SystemSrv.Charge 2011/07/21 Add	
	setVoltageField(env, obj, gPaths.chargerVoltagePath, gFieldIds.mChargerVoltage);
	setVoltageField(env, obj, gPaths.chargingCurrentPath, gFieldIds.mChargingCurrent);
	//Jiangtao.Guo@Prd.SystemSrv.Charge, 2012/03/08, Add for [shutdown for low battery], {
    setIntField(env, obj, gPaths.batteryRequestPoweroffPath, gFieldIds.mBatteryRequestPoweroff);	    
	//} add end for [].	
	#endif /* VENDOR_EDIT */
	
    const int SIZE = 128;
    char buf[SIZE];
    
    if (readFromFile(gPaths.batteryStatusPath, buf, SIZE) > 0)
        env->SetIntField(obj, gFieldIds.mBatteryStatus, getBatteryStatus(buf));
    else
        env->SetIntField(obj, gFieldIds.mBatteryStatus,
                         gConstants.statusUnknown);
    
    if (readFromFile(gPaths.batteryHealthPath, buf, SIZE) > 0)
        env->SetIntField(obj, gFieldIds.mBatteryHealth, getBatteryHealth(buf));

    if (readFromFile(gPaths.batteryTechnologyPath, buf, SIZE) > 0)
        env->SetObjectField(obj, gFieldIds.mBatteryTechnology, env->NewStringUTF(buf));
}

static JNINativeMethod sMethods[] = {
     /* name, signature, funcPtr */
	{"native_update", "()V", (void*)android_server_BatteryService_update},
};

int register_android_server_BatteryService(JNIEnv* env)
{
    char    path[PATH_MAX];
    struct dirent* entry;

    DIR* dir = opendir(POWER_SUPPLY_PATH);
    if (dir == NULL) {
        ALOGE("Could not open %s\n", POWER_SUPPLY_PATH);
    } else {
        while ((entry = readdir(dir))) {
            const char* name = entry->d_name;

            // ignore "." and ".."
            if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) {
                continue;
            }

            char buf[20];
            // Look for "type" file in each subdirectory
            snprintf(path, sizeof(path), "%s/%s/type", POWER_SUPPLY_PATH, name);
            int length = readFromFile(path, buf, sizeof(buf));
            if (length > 0) {
                if (buf[length - 1] == '\n')
                    buf[length - 1] = 0;

                if (strcmp(buf, "Mains") == 0) {
                    snprintf(path, sizeof(path), "%s/%s/online", POWER_SUPPLY_PATH, name);
                    if (access(path, R_OK) == 0)
                        gPaths.acOnlinePath = strdup(path);
                }
                else if (strcmp(buf, "USB") == 0) {
                    snprintf(path, sizeof(path), "%s/%s/online", POWER_SUPPLY_PATH, name);
                    if (access(path, R_OK) == 0)
                        gPaths.usbOnlinePath = strdup(path);
                }
                else if (strcmp(buf, "Wireless") == 0) {
                    snprintf(path, sizeof(path), "%s/%s/online", POWER_SUPPLY_PATH, name);
                    if (access(path, R_OK) == 0)
                        gPaths.wirelessOnlinePath = strdup(path);
                }
                else if (strcmp(buf, "Battery") == 0) {
                    snprintf(path, sizeof(path), "%s/%s/status", POWER_SUPPLY_PATH, name);
                    if (access(path, R_OK) == 0)
                        gPaths.batteryStatusPath = strdup(path);
                    snprintf(path, sizeof(path), "%s/%s/health", POWER_SUPPLY_PATH, name);
                    if (access(path, R_OK) == 0)
                        gPaths.batteryHealthPath = strdup(path);
                    snprintf(path, sizeof(path), "%s/%s/present", POWER_SUPPLY_PATH, name);
                    if (access(path, R_OK) == 0)
                        gPaths.batteryPresentPath = strdup(path);
                    snprintf(path, sizeof(path), "%s/%s/capacity", POWER_SUPPLY_PATH, name);
                    if (access(path, R_OK) == 0)
                        gPaths.batteryCapacityPath = strdup(path);

                    snprintf(path, sizeof(path), "%s/%s/voltage_now", POWER_SUPPLY_PATH, name);
                    if (access(path, R_OK) == 0) {
                        gPaths.batteryVoltagePath = strdup(path);
                        // voltage_now is in microvolts, not millivolts
                        gVoltageDivisor = 1000;
                    } else {
                        snprintf(path, sizeof(path), "%s/%s/batt_vol", POWER_SUPPLY_PATH, name);
                        if (access(path, R_OK) == 0)
                            gPaths.batteryVoltagePath = strdup(path);
                    }

                    snprintf(path, sizeof(path), "%s/%s/temp", POWER_SUPPLY_PATH, name);
                    if (access(path, R_OK) == 0) {
                        gPaths.batteryTemperaturePath = strdup(path);
                    } else {
                        snprintf(path, sizeof(path), "%s/%s/batt_temp", POWER_SUPPLY_PATH, name);
                        if (access(path, R_OK) == 0)
                            gPaths.batteryTemperaturePath = strdup(path);
                    }
    				#ifdef VENDOR_EDIT
    				//Zhanhong.Hu@Prd.SystemSrv.Charge 2011/07/21 Add				
                    snprintf(path, sizeof(path), "%s/%s/ChargerVoltage", POWER_SUPPLY_PATH, name);
                    if (access(path, R_OK) == 0) 
    				{
                        gPaths.chargerVoltagePath = strdup(path);
                    }
                    snprintf(path, sizeof(path), "%s/%s/BatteryAverageCurrent", POWER_SUPPLY_PATH, name);
                    if (access(path, R_OK) == 0) 
    				{
                        gPaths.chargingCurrentPath = strdup(path);
                    } 
                    //Jiangtao.Guo@Prd.SystemSrv.Charge, 2012/03/08, Add for [shutdown for low battery], {
                    snprintf(path, sizeof(path), "%s/%s/BatteryRequestPoweroff", POWER_SUPPLY_PATH, name);
                    if (access(path, R_OK) == 0) 
    				{
                        gPaths.batteryRequestPoweroffPath = strdup(path);
                    }
                    //} add end for [].				
                    #endif /* VENDOR_EDIT */
                    snprintf(path, sizeof(path), "%s/%s/technology", POWER_SUPPLY_PATH, name);
                    if (access(path, R_OK) == 0)
                        gPaths.batteryTechnologyPath = strdup(path);
                }
            }
        }
        closedir(dir);
    }

    if (!gPaths.acOnlinePath)
        ALOGE("acOnlinePath not found");
    if (!gPaths.usbOnlinePath)
        ALOGE("usbOnlinePath not found");
    if (!gPaths.wirelessOnlinePath)
        ALOGE("wirelessOnlinePath not found");
    if (!gPaths.batteryStatusPath)
        ALOGE("batteryStatusPath not found");
    if (!gPaths.batteryHealthPath)
        ALOGE("batteryHealthPath not found");
    if (!gPaths.batteryPresentPath)
        ALOGE("batteryPresentPath not found");
    if (!gPaths.batteryCapacityPath)
        ALOGE("batteryCapacityPath not found");
    if (!gPaths.batteryVoltagePath)
        ALOGE("batteryVoltagePath not found");
    if (!gPaths.batteryTemperaturePath)
        ALOGE("batteryTemperaturePath not found");
    if (!gPaths.batteryTechnologyPath)
        ALOGE("batteryTechnologyPath not found");
	#ifdef VENDOR_EDIT  
	//Zhanhong.Hu@Prd.SystemSrv.Charge 2011/07/21 Add	
    if (!gPaths.chargerVoltagePath)
        ALOGE("chargerVoltagePath not found");
    if (!gPaths.chargingCurrentPath)
        ALOGE("chargingCurrentPath not found");
    //Jiangtao.Guo@Prd.SystemSrv.Charge, 2012/03/08, Add for [shutdown for low battery], {
    if (!gPaths.batteryRequestPoweroffPath)
        ALOGE("batteryRequestPoweroffPath not found");      
    //} add end for [].	
    #endif /* VENDOR_EDIT */

    jclass clazz = env->FindClass("com/android/server/BatteryService");

    if (clazz == NULL) {
        ALOGE("Can't find com/android/server/BatteryService");
        return -1;
    }
    
    gFieldIds.mAcOnline = env->GetFieldID(clazz, "mAcOnline", "Z");
    gFieldIds.mUsbOnline = env->GetFieldID(clazz, "mUsbOnline", "Z");
    gFieldIds.mWirelessOnline = env->GetFieldID(clazz, "mWirelessOnline", "Z");
    gFieldIds.mBatteryStatus = env->GetFieldID(clazz, "mBatteryStatus", "I");
    gFieldIds.mBatteryHealth = env->GetFieldID(clazz, "mBatteryHealth", "I");
    gFieldIds.mBatteryPresent = env->GetFieldID(clazz, "mBatteryPresent", "Z");
    gFieldIds.mBatteryLevel = env->GetFieldID(clazz, "mBatteryLevel", "I");
    gFieldIds.mBatteryTechnology = env->GetFieldID(clazz, "mBatteryTechnology", "Ljava/lang/String;");
    gFieldIds.mBatteryVoltage = env->GetFieldID(clazz, "mBatteryVoltage", "I");
    gFieldIds.mBatteryTemperature = env->GetFieldID(clazz, "mBatteryTemperature", "I");
	#ifdef VENDOR_EDIT
	//Zhanhong.Hu@Prd.SystemSrv.Charge 2011/07/21 Add	
	gFieldIds.mChargerVoltage = env->GetFieldID(clazz, "mChargerVoltage", "I");
	gFieldIds.mChargingCurrent = env->GetFieldID(clazz, "mChargingCurrent", "I");
	//Jiangtao.Guo@Prd.SystemSrv.Charge, 2012/03/08, Add for [shutdown for low battery], {
	gFieldIds.mBatteryRequestPoweroff = env->GetFieldID(clazz, "mBatteryRequestPoweroff", "I");    
	//} add end for [].	
	#endif /* VENDOR_EDIT */

    LOG_FATAL_IF(gFieldIds.mAcOnline == NULL, "Unable to find BatteryService.AC_ONLINE_PATH");
    LOG_FATAL_IF(gFieldIds.mUsbOnline == NULL, "Unable to find BatteryService.USB_ONLINE_PATH");
    LOG_FATAL_IF(gFieldIds.mWirelessOnline == NULL, "Unable to find BatteryService.WIRELESS_ONLINE_PATH");
    LOG_FATAL_IF(gFieldIds.mBatteryStatus == NULL, "Unable to find BatteryService.BATTERY_STATUS_PATH");
    LOG_FATAL_IF(gFieldIds.mBatteryHealth == NULL, "Unable to find BatteryService.BATTERY_HEALTH_PATH");
    LOG_FATAL_IF(gFieldIds.mBatteryPresent == NULL, "Unable to find BatteryService.BATTERY_PRESENT_PATH");
    LOG_FATAL_IF(gFieldIds.mBatteryLevel == NULL, "Unable to find BatteryService.BATTERY_CAPACITY_PATH");
    LOG_FATAL_IF(gFieldIds.mBatteryVoltage == NULL, "Unable to find BatteryService.BATTERY_VOLTAGE_PATH");
    LOG_FATAL_IF(gFieldIds.mBatteryTemperature == NULL, "Unable to find BatteryService.BATTERY_TEMPERATURE_PATH");
    LOG_FATAL_IF(gFieldIds.mBatteryTechnology == NULL, "Unable to find BatteryService.BATTERY_TECHNOLOGY_PATH");
    
    #ifdef VENDOR_EDIT
	//Zhanhong.Hu@Prd.SystemSrv.Charge 2011/07/21 Add	
    LOG_FATAL_IF(gFieldIds.mChargerVoltage == NULL, "Unable to find BatteryService.CHARGER_VOLTAGE_PATH");
    LOG_FATAL_IF(gFieldIds.mChargingCurrent == NULL, "Unable to find BatteryService.CHARGING_CURRENT_PATH"); 	
	//Jiangtao.Guo@Prd.SystemSrv.Charge, 2012/03/08, Add for [shutdown for low battery], {
    LOG_FATAL_IF(gFieldIds.mBatteryRequestPoweroff == NULL, "Unable to find BatteryService.BATTERY_REQUEST_POWEROFF_PATH");  
	//} add end for []. 	
	#endif
	
    clazz = env->FindClass("android/os/BatteryManager");
    
    if (clazz == NULL) {
        ALOGE("Can't find android/os/BatteryManager");
        return -1;
    }
    
    gConstants.statusUnknown = env->GetStaticIntField(clazz, 
            env->GetStaticFieldID(clazz, "BATTERY_STATUS_UNKNOWN", "I"));
            
    gConstants.statusCharging = env->GetStaticIntField(clazz, 
            env->GetStaticFieldID(clazz, "BATTERY_STATUS_CHARGING", "I"));
            
    gConstants.statusDischarging = env->GetStaticIntField(clazz, 
            env->GetStaticFieldID(clazz, "BATTERY_STATUS_DISCHARGING", "I"));
    
    gConstants.statusNotCharging = env->GetStaticIntField(clazz, 
            env->GetStaticFieldID(clazz, "BATTERY_STATUS_NOT_CHARGING", "I"));
    
    gConstants.statusFull = env->GetStaticIntField(clazz, 
            env->GetStaticFieldID(clazz, "BATTERY_STATUS_FULL", "I"));

    gConstants.healthUnknown = env->GetStaticIntField(clazz, 
            env->GetStaticFieldID(clazz, "BATTERY_HEALTH_UNKNOWN", "I"));

    gConstants.healthGood = env->GetStaticIntField(clazz, 
            env->GetStaticFieldID(clazz, "BATTERY_HEALTH_GOOD", "I"));

    gConstants.healthOverheat = env->GetStaticIntField(clazz, 
            env->GetStaticFieldID(clazz, "BATTERY_HEALTH_OVERHEAT", "I"));

    gConstants.healthDead = env->GetStaticIntField(clazz, 
            env->GetStaticFieldID(clazz, "BATTERY_HEALTH_DEAD", "I"));

    gConstants.healthOverVoltage = env->GetStaticIntField(clazz, 
            env->GetStaticFieldID(clazz, "BATTERY_HEALTH_OVER_VOLTAGE", "I"));
            
    gConstants.healthUnspecifiedFailure = env->GetStaticIntField(clazz, 
            env->GetStaticFieldID(clazz, "BATTERY_HEALTH_UNSPECIFIED_FAILURE", "I"));
    
    gConstants.healthCold = env->GetStaticIntField(clazz,
            env->GetStaticFieldID(clazz, "BATTERY_HEALTH_COLD", "I"));

    return jniRegisterNativeMethods(env, "com/android/server/BatteryService", sMethods, NELEM(sMethods));
}

} /* namespace android */
