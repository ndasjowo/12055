/*
**
** Copyright (C) 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#define LOG_TAG "CameraService"
//#define LOG_NDEBUG 0

#include <stdio.h>
#include <sys/types.h>
#include <pthread.h>

#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <cutils/atomic.h>
#include <cutils/properties.h>
#include <gui/SurfaceTextureClient.h>
#include <gui/Surface.h>
#include <hardware/hardware.h>
#include <media/AudioSystem.h>
#include <media/mediaplayer.h>
#include <utils/Errors.h>
#include <utils/Log.h>
#include <utils/String16.h>

#include "CameraService.h"
#include "CameraClient.h"
#include "Camera2Client.h"

//!++
#ifdef  MTK_CAMERAPROFILE_SUPPORT
    #include "CameraProfile.h"
#endif 
#ifdef  MTK_CAMERA_BSP_SUPPORT
    #include <camera/MtkCameraParameters.h>
#endif
//!--

namespace android {

// ----------------------------------------------------------------------------
// Logging support -- this is for debugging only
// Use "adb shell dumpsys media.camera -v 1" to change it.
//!++
//#ifdef  MTK_CAMERA_BSP_SUPPORT
volatile int32_t gLogLevel = 1;
//#else
//volatile int32_t gLogLevel = 0;
//#endif
//!--

#define LOG1(...) ALOGD_IF(gLogLevel >= 1, __VA_ARGS__);
#define LOG2(...) ALOGD_IF(gLogLevel >= 2, __VA_ARGS__);

static void setLogLevel(int level) {
    android_atomic_write(level, &gLogLevel);
}

// ----------------------------------------------------------------------------

static int getCallingPid() {
    return IPCThreadState::self()->getCallingPid();
}

static int getCallingUid() {
    return IPCThreadState::self()->getCallingUid();
}

// ----------------------------------------------------------------------------

// This is ugly and only safe if we never re-create the CameraService, but
// should be ok for now.
static CameraService *gCameraService;

CameraService::CameraService()
:mSoundRef(0), mModule(0)
{
    ALOGI("CameraService started (pid=%d)", getpid());
    gCameraService = this;
#ifdef VENDOR_EDIT
//LiChen@CameraApp, 2013/03/07, Add for IntelligentSleep
#ifdef OPPO_INTELLIGENT_SLEEP
    mIntelSleepId = -1;
#endif
#endif /* VENDOR_EDIT */

}

void CameraService::onFirstRef()
{
    BnCameraService::onFirstRef();

    if (hw_get_module(CAMERA_HARDWARE_MODULE_ID,
                (const hw_module_t **)&mModule) < 0) {
        ALOGE("Could not load camera HAL module");
        mNumberOfCameras = 0;
    }
    else {
        mNumberOfCameras = mModule->get_number_of_cameras();
        if (mNumberOfCameras > MAX_CAMERAS) {
            ALOGE("Number of cameras(%d) > MAX_CAMERAS(%d).",
                    mNumberOfCameras, MAX_CAMERAS);
            mNumberOfCameras = MAX_CAMERAS;
        }
        for (int i = 0; i < mNumberOfCameras; i++) {
            setCameraFree(i);
        }
    }
}

CameraService::~CameraService() {
    for (int i = 0; i < mNumberOfCameras; i++) {
        if (mBusy[i]) {
            ALOGE("camera %d is still in use in destructor!", i);
        }
    }

    gCameraService = NULL;
}

int32_t CameraService::getNumberOfCameras() {
//!++
#if defined(ATVCHIP_MTK_ENABLE)
    //Exclude mATV
    LOG1("[getNumberOfCameras] NumberOfCameras:%d except for atv \n", mNumberOfCameras - 1);
    return mNumberOfCameras - 1;
#else
    LOG1("[getNumberOfCameras] NumberOfCameras:%d \n", mNumberOfCameras);
    return mNumberOfCameras;
#endif
//!--
}

status_t CameraService::getCameraInfo(int cameraId,
                                      struct CameraInfo* cameraInfo) {
    if (!mModule) {
        return NO_INIT;
    }
//!++
#if defined(ATVCHIP_MTK_ENABLE)
    //Exclude mATV
    LOG1("[getCameraInfo] id:%d NumberOfCameras:%d except for atv \n", cameraId, mNumberOfCameras - 1);
    if (cameraId < 0 || cameraId >= (mNumberOfCameras - 1)) {
        return BAD_VALUE;
    }
#else
    LOG1("[getCameraInfo] id:%d NumberOfCameras(%d) \n", cameraId, mNumberOfCameras);
    if (cameraId < 0 || cameraId >= mNumberOfCameras) {
        return BAD_VALUE;
    }
#endif
//!--
    struct camera_info info;
    status_t rc = mModule->get_camera_info(cameraId, &info);
    cameraInfo->facing = info.facing;
    cameraInfo->orientation = info.orientation;
    return rc;
}

sp<ICamera> CameraService::connect(
        const sp<ICameraClient>& cameraClient, int cameraId) {
#ifdef  MTK_CAMERAPROFILE_SUPPORT
    initCameraProfile(); 
    AutoCPTLog cptlog(Event_CS_connect);
#endif
    int callingPid = getCallingPid();

#ifdef VENDOR_EDIT
//LiChen@CameraApp, 2013/03/07, Add for IntelligentSleep
#ifdef OPPO_INTELLIGENT_SLEEP
    LOG1("CameraService::connect Intelligent Sleep (pid %d, id %d)", callingPid, cameraId);
    LOG1("CameraService::connect Intelligent mIntelSleepId %d", mIntelSleepId);
    switch (cameraId) {
        case CAMERA_INTELLIGENT_SLEEP:
        
            for(int i = 0 ; i < mNumberOfCameras; i ++) {
                if (mBusy[i]) {
                    return NULL;
                }
            }

            cameraId = 1;
            mIntelSleepId = CAMERA_INTELLIGENT_SLEEP;
            
            break;
        default:
            if (mIntelSleepId == CAMERA_INTELLIGENT_SLEEP) {
				int cnt = 0;
                while (mClient[1].promote() == NULL && cnt < 10) {
                    usleep(50000);
					cnt++;
                }
                
                if (mClient[1].promote() != NULL) {
                    mClient[1].promote()->disconnect();
                }
            }
            mIntelSleepId = -1;
            break;
    }
#endif
#endif /* VENDOR_EDIT */


    LOG1("CameraService::connect E (pid %d, id %d)", callingPid, cameraId);

    if (!mModule) {
        ALOGE("Camera HAL module not loaded");
        return NULL;
    }

    sp<Client> client;
//!++
#if defined(ATVCHIP_MTK_ENABLE)
    if (cameraId == 0xFF) {
        // It's atv, the last cameraId is atv
        cameraId = mNumberOfCameras - 1;
        status_t status = setProperty(
            String8(MtkCameraParameters::PROPERTY_KEY_CLIENT_APPMODE), 
            String8(MtkCameraParameters::APP_MODE_NAME_MTK_ATV)
        );
        ALOGD("connected from atv - cameraId(%d) status(%d) \n", cameraId, status);
    }
    else if (cameraId < 0 || cameraId >= (mNumberOfCameras - 1)) {
        ALOGE("CameraService::connect X (pid %d) rejected (invalid cameraId %d), (mNumberOfCameras-1=%d).",
        callingPid, cameraId, (mNumberOfCameras-1));
        return NULL;
    }
#else
    if (cameraId < 0 || cameraId >= mNumberOfCameras) {
        ALOGE("CameraService::connect X (pid %d) rejected (invalid cameraId %d).",
            callingPid, cameraId);
        return NULL;
    }
#endif
//!--

    char value[PROPERTY_VALUE_MAX];
    property_get("sys.secpolicy.camera.disabled", value, "0");
    if (strcmp(value, "1") == 0) {
        // Camera is disabled by DevicePolicyManager.
        ALOGI("Camera is disabled. connect X (pid %d) rejected", callingPid);
        return NULL;
    }

    Mutex::Autolock lock(mServiceLock);
    if (mClient[cameraId] != 0) {
        client = mClient[cameraId].promote();
        if (client != 0) {
            if (cameraClient->asBinder() == client->getCameraClient()->asBinder()) {
                LOG1("CameraService::connect X (pid %d) (the same client)",
                     callingPid);
                return client;
            } else {
                ALOGW("CameraService::connect X (pid %d) rejected (existing client).",
                      callingPid);
                return NULL;
            }
        }
        mClient[cameraId].clear();
    }

    if (mBusy[cameraId]) {
        ALOGW("CameraService::connect X (pid %d) rejected"
                " (camera %d is still busy).", callingPid, cameraId);
        return NULL;
    }

    struct camera_info info;
    if (mModule->get_camera_info(cameraId, &info) != OK) {
        ALOGE("Invalid camera id %d", cameraId);
        return NULL;
    }

    int deviceVersion;
    if (mModule->common.module_api_version == CAMERA_MODULE_API_VERSION_2_0) {
        deviceVersion = info.device_version;
    } else {
        deviceVersion = CAMERA_DEVICE_API_VERSION_1_0;
    }

    switch(deviceVersion) {
      case CAMERA_DEVICE_API_VERSION_1_0:
        client = new CameraClient(this, cameraClient, cameraId,
                info.facing, callingPid, getpid());
        break;
      case CAMERA_DEVICE_API_VERSION_2_0:
        client = new Camera2Client(this, cameraClient, cameraId,
                info.facing, callingPid, getpid());
        break;
      default:
        ALOGE("Unknown camera device HAL version: %d", deviceVersion);
        return NULL;
    }

#ifdef  MTK_CAMERA_BSP_SUPPORT
    // To avoid release/new MediaPlayer when switching between main/sub sensor, and it will reduce the switch time.
#ifdef VENDOR_EDIT
//LiChen@CameraApp, 2013/04/08, Remove for speeding up the opening camera
    loadSound();
#endif /* VENDOR_EDIT */

#endif  
 
#ifdef  MTK_CAMERAPROFILE_SUPPORT
    CPTLog(Event_CS_newCamHwIF, CPTFlagStart);
#endif

    if (client->initialize(mModule) != OK) {
#ifdef  MTK_CAMERAPROFILE_SUPPORT
        CPTLogStr(Event_CS_newCamHwIF, CPTFlagEnd,  "new CameraHardwareInterface failed");
#endif  
#ifdef  MTK_CAMERA_BSP_SUPPORT
        // To avoid release/new MediaPlayer when switching between main/sub sensor, and it will reduce the switch time.
        releaseSound();
#endif
        return NULL;
    }

#ifdef  MTK_CAMERAPROFILE_SUPPORT
    CPTLog(Event_CS_newCamHwIF, CPTFlagEnd);
#endif

    cameraClient->asBinder()->linkToDeath(this);

    mClient[cameraId] = client;
    LOG1("CameraService::connect X (id %d, this pid is %d)", cameraId, getpid());
    return client;
}

void CameraService::removeClient(const sp<ICameraClient>& cameraClient) {
    int callingPid = getCallingPid();
    LOG1("CameraService::removeClient E (pid %d)", callingPid);

    // Declare this before the lock to make absolutely sure the
    // destructor won't be called with the lock held.
    Mutex::Autolock lock(mServiceLock);

    int outIndex;
    sp<Client> client = findClientUnsafe(cameraClient->asBinder(), outIndex);

    if (client != 0) {
        // Found our camera, clear and leave.
        LOG1("removeClient: clear camera %d", outIndex);
        mClient[outIndex].clear();

        client->unlinkToDeath(this);
    }

    LOG1("CameraService::removeClient X (pid %d)", callingPid);
}

sp<CameraService::Client> CameraService::findClientUnsafe(
                        const wp<IBinder>& cameraClient, int& outIndex) {
    sp<Client> client;

    for (int i = 0; i < mNumberOfCameras; i++) {

        // This happens when we have already disconnected (or this is
        // just another unused camera).
        if (mClient[i] == 0) continue;

        // Promote mClient. It can fail if we are called from this path:
        // Client::~Client() -> disconnect() -> removeClient().
        client = mClient[i].promote();

        // Clean up stale client entry
        if (client == NULL) {
            mClient[i].clear();
            continue;
        }

        if (cameraClient == client->getCameraClient()->asBinder()) {
            // Found our camera
            outIndex = i;
            return client;
        }
    }

    outIndex = -1;
    return NULL;
}

CameraService::Client* CameraService::getClientByIdUnsafe(int cameraId) {
    if (cameraId < 0 || cameraId >= mNumberOfCameras) return NULL;
    return mClient[cameraId].unsafe_get();
}

Mutex* CameraService::getClientLockById(int cameraId) {
    if (cameraId < 0 || cameraId >= mNumberOfCameras) return NULL;
    return &mClientLock[cameraId];
}

sp<CameraService::Client> CameraService::getClientByRemote(
                                const wp<IBinder>& cameraClient) {

    // Declare this before the lock to make absolutely sure the
    // destructor won't be called with the lock held.
    sp<Client> client;

    Mutex::Autolock lock(mServiceLock);

    int outIndex;
    client = findClientUnsafe(cameraClient, outIndex);

    return client;
}

status_t CameraService::onTransact(
    uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags) {
    // Permission checks
    switch (code) {
//!++
#ifdef  MTK_CAMERA_BSP_SUPPORT
        case BnCameraService::GET_PROPERTY:
            {
                CHECK_INTERFACE(ICameraService, data, reply);
                String8 const key = data.readString8();
                String8 value;
                status_t const status = getProperty(key, value);
                reply->writeString8(value);
                reply->writeInt32(status);
                ALOGD("[GET_PROPERTY] - pid=%d, uid=%d (%s)=(%s) \n", getCallingPid(), getCallingUid(), key.string(), value.string());
                return OK;
            }
        case BnCameraService::SET_PROPERTY:
            {
                CHECK_INTERFACE(ICameraService, data, reply);
                String8 const key = data.readString8();
                String8 const value = data.readString8();
                status_t const status = setProperty(key, value);
                reply->writeInt32(status);
                ALOGD("[SET_PROPERTY] - pid=%d, uid=%d (%s)=(%s) \n", getCallingPid(), getCallingUid(), key.string(), value.string());
                return OK;
            }
#endif  //MTK_CAMERA_BSP_SUPPORT
//!--
        case BnCameraService::CONNECT:
            const int pid = getCallingPid();
            const int self_pid = getpid();
            if (pid != self_pid) {
                // we're called from a different process, do the real check
                if (!checkCallingPermission(
                        String16("android.permission.CAMERA"))) {
                    const int uid = getCallingUid();
                    ALOGE("Permission Denial: "
                         "can't use the camera pid=%d, uid=%d", pid, uid);
                    return PERMISSION_DENIED;
                }
            }
            break;
    }

    return BnCameraService::onTransact(code, data, reply, flags);
}

// The reason we need this busy bit is a new CameraService::connect() request
// may come in while the previous Client's destructor has not been run or is
// still running. If the last strong reference of the previous Client is gone
// but the destructor has not been finished, we should not allow the new Client
// to be created because we need to wait for the previous Client to tear down
// the hardware first.
void CameraService::setCameraBusy(int cameraId) {
    android_atomic_write(1, &mBusy[cameraId]);

    ALOGV("setCameraBusy cameraId=%d", cameraId);
}

void CameraService::setCameraFree(int cameraId) {
    android_atomic_write(0, &mBusy[cameraId]);

    ALOGV("setCameraFree cameraId=%d", cameraId);
}

// We share the media players for shutter and recording sound for all clients.
// A reference count is kept to determine when we will actually release the
// media players.

MediaPlayer* CameraService::newMediaPlayer(const char *file) {
#ifdef  MTK_CAMERAPROFILE_SUPPORT
    AutoCPTLog cptlog(Event_CS_newMediaPlayer);
#endif
    
#ifdef  MTK_CAMERA_BSP_SUPPORT    
    LOG1("[CameraService::newMediaPlayer] + (%s)\r\n", file);
#endif
    MediaPlayer* mp = new MediaPlayer();
    if (mp->setDataSource(file, NULL) == NO_ERROR) {
        mp->setAudioStreamType(AUDIO_STREAM_ENFORCED_AUDIBLE);
        mp->prepare();
    } else {
        ALOGE("Failed to load CameraService sounds: %s", file);
        return NULL;
    }
#ifdef  MTK_CAMERA_BSP_SUPPORT
    LOG1("[CameraService::newMediaPlayer] -\r\n");
#endif
    return mp;
}

void CameraService::loadSound() {
#ifdef  MTK_CAMERA_BSP_SUPPORT
    LOG1("[CameraService::loadSound] + tid(%d) mSoundLock - ref=%d\r\n", ::gettid(), mSoundRef);
#endif
    Mutex::Autolock lock(mSoundLock);
    LOG1("CameraService::loadSound ref=%d", mSoundRef);
    if (mSoundRef++) return;

    mSoundPlayer[SOUND_SHUTTER] = newMediaPlayer("/system/media/audio/ui/camera_click.ogg");
    mSoundPlayer[SOUND_RECORDING] = newMediaPlayer("/system/media/audio/ui/VideoRecord.ogg");
}

void CameraService::releaseSound() {
    Mutex::Autolock lock(mSoundLock);
    LOG1("CameraService::releaseSound ref=%d", mSoundRef);
    if (--mSoundRef) return;

    for (int i = 0; i < NUM_SOUNDS; i++) {
        if (mSoundPlayer[i] != 0) {
            mSoundPlayer[i]->disconnect();
            mSoundPlayer[i].clear();
        }
    }
}

void CameraService::playSound(sound_kind kind) {
#ifdef  MTK_CAMERAPROFILE_SUPPORT
    AutoCPTLog cptlog(Event_CS_playSound);
#endif
    LOG1("playSound(%d)", kind);
    Mutex::Autolock lock(mSoundLock);
    sp<MediaPlayer> player = mSoundPlayer[kind];
    if (player != 0) {
        player->seekTo(0);
        player->start();
    }
#ifdef  MTK_CAMERA_BSP_SUPPORT
    LOG1("playSound(%d) - tid(%d)", kind, ::gettid());
#endif
}

// ----------------------------------------------------------------------------

CameraService::Client::Client(const sp<CameraService>& cameraService,
        const sp<ICameraClient>& cameraClient,
        int cameraId, int cameraFacing, int clientPid, int servicePid) {
#ifdef  MTK_CAMERAPROFILE_SUPPORT
    AutoCPTLog cptlog(Event_CS_newClient);
#endif
    int callingPid = getCallingPid();
    LOG1("Client::Client E (pid %d, id %d)", callingPid, cameraId);

    mCameraService = cameraService;
    mCameraClient = cameraClient;
    mCameraId = cameraId;
    mCameraFacing = cameraFacing;
    mClientPid = clientPid;
    mServicePid = servicePid;
    mDestructionStarted = false;

    cameraService->setCameraBusy(cameraId);
#ifndef  MTK_CAMERA_BSP_SUPPORT
    // To avoid release/new MediaPlayer when switching between main/sub sensor, and it will reduce the switch time.
#ifdef VENDOR_EDIT
//LiChen@CameraApp, 2013/04/08, Remove for speeding up the opening camera
    cameraService->loadSound();
#endif /* VENDOR_EDIT */

#endif
    LOG1("Client::Client X (pid %d, id %d)", callingPid, cameraId);
}

// tear down the client
CameraService::Client::~Client() {
    mCameraService->releaseSound();

    // unconditionally disconnect. function is idempotent
    Client::disconnect();
}

// ----------------------------------------------------------------------------

Mutex* CameraService::Client::getClientLockFromCookie(void* user) {
    return gCameraService->getClientLockById((int) user);
}

// Provide client pointer for callbacks. Client lock returned from getClientLockFromCookie should
// be acquired for this to be safe
CameraService::Client* CameraService::Client::getClientFromCookie(void* user) {
    Client* client = gCameraService->getClientByIdUnsafe((int) user);

    // This could happen if the Client is in the process of shutting down (the
    // last strong reference is gone, but the destructor hasn't finished
    // stopping the hardware).
    if (client == NULL) return NULL;

    // destruction already started, so should not be accessed
    if (client->mDestructionStarted) return NULL;

    return client;
}

// NOTE: function is idempotent
void CameraService::Client::disconnect() {
    mCameraService->removeClient(mCameraClient);
    mCameraService->setCameraFree(mCameraId);

//#ifdef VENDOR_EDIT
//LiChen@CameraApp, 2013/04/07, Add for 
    if (mCameraService->mIntelSleepId == CAMERA_INTELLIGENT_SLEEP) {
        mCameraService->mIntelSleepId = -1;
    }
//#endif /* VENDOR_EDIT */
    
}


#ifdef VENDOR_EDIT
//LiChen@CameraApp, 2013/03/08, Add for 
int CameraService::Client::getIntellgetCameraId() {
    return mCameraService->mIntelSleepId;
}
#endif /* VENDOR_EDIT */


// ----------------------------------------------------------------------------

static const int kDumpLockRetries = 50;
static const int kDumpLockSleep = 60000;

static bool tryLock(Mutex& mutex)
{
    bool locked = false;
    for (int i = 0; i < kDumpLockRetries; ++i) {
        if (mutex.tryLock() == NO_ERROR) {
            locked = true;
            break;
        }
        usleep(kDumpLockSleep);
    }
    return locked;
}

status_t CameraService::dump(int fd, const Vector<String16>& args) {
    String8 result;
    if (checkCallingPermission(String16("android.permission.DUMP")) == false) {
        result.appendFormat("Permission Denial: "
                "can't dump CameraService from pid=%d, uid=%d\n",
                getCallingPid(),
                getCallingUid());
        write(fd, result.string(), result.size());
    } else {
        bool locked = tryLock(mServiceLock);
        // failed to lock - CameraService is probably deadlocked
        if (!locked) {
            result.append("CameraService may be deadlocked\n");
            write(fd, result.string(), result.size());
        }

        bool hasClient = false;
        if (!mModule) {
            result = String8::format("No camera module available!\n");
            write(fd, result.string(), result.size());
            return NO_ERROR;
        }

        result = String8::format("Camera module HAL API version: 0x%x\n",
                mModule->common.hal_api_version);
        result.appendFormat("Camera module API version: 0x%x\n",
                mModule->common.module_api_version);
        result.appendFormat("Camera module name: %s\n",
                mModule->common.name);
        result.appendFormat("Camera module author: %s\n",
                mModule->common.author);
        result.appendFormat("Number of camera devices: %d\n\n", mNumberOfCameras);
        write(fd, result.string(), result.size());
        for (int i = 0; i < mNumberOfCameras; i++) {
            result = String8::format("Camera %d static information:\n", i);
            camera_info info;

            status_t rc = mModule->get_camera_info(i, &info);
            if (rc != OK) {
                result.appendFormat("  Error reading static information!\n");
                write(fd, result.string(), result.size());
            } else {
                result.appendFormat("  Facing: %s\n",
                        info.facing == CAMERA_FACING_BACK ? "BACK" : "FRONT");
                result.appendFormat("  Orientation: %d\n", info.orientation);
                int deviceVersion;
                if (mModule->common.module_api_version <
                        CAMERA_MODULE_API_VERSION_2_0) {
                    deviceVersion = CAMERA_DEVICE_API_VERSION_1_0;
                } else {
                    deviceVersion = info.device_version;
                }
                result.appendFormat("  Device version: 0x%x\n", deviceVersion);
                if (deviceVersion >= CAMERA_DEVICE_API_VERSION_2_0) {
                    result.appendFormat("  Device static metadata:\n");
                    write(fd, result.string(), result.size());
                    dump_indented_camera_metadata(info.static_camera_characteristics,
                            fd, 2, 4);
                } else {
                    write(fd, result.string(), result.size());
                }
            }

            sp<Client> client = mClient[i].promote();
            if (client == 0) {
                result = String8::format("  Device is closed, no client instance\n");
                write(fd, result.string(), result.size());
                continue;
            }
            hasClient = true;
            result = String8::format("  Device is open. Client instance dump:\n");
            write(fd, result.string(), result.size());
            client->dump(fd, args);
        }
        if (!hasClient) {
            result = String8::format("\nNo active camera clients yet.\n");
            write(fd, result.string(), result.size());
        }

        if (locked) mServiceLock.unlock();

        // change logging level
        int n = args.size();
        for (int i = 0; i + 1 < n; i++) {
            String16 verboseOption("-v");
            if (args[i] == verboseOption) {
                String8 levelStr(args[i+1]);
                int level = atoi(levelStr.string());
                result = String8::format("\nSetting log level to %d.\n", level);
                setLogLevel(level);
                write(fd, result.string(), result.size());
            }
        }

    }
    return NO_ERROR;
}

/*virtual*/void CameraService::binderDied(
    const wp<IBinder> &who) {

    /**
      * While tempting to promote the wp<IBinder> into a sp,
      * it's actually not supported by the binder driver
      */

    ALOGV("java clients' binder died");

    sp<Client> cameraClient = getClientByRemote(who);

    if (cameraClient == 0) {
        ALOGV("java clients' binder death already cleaned up (normal case)");
        return;
    }

    ALOGW("Disconnecting camera client %p since the binder for it "
          "died (this pid %d)", cameraClient.get(), getCallingPid());

    cameraClient->disconnect();

}

}; // namespace android
