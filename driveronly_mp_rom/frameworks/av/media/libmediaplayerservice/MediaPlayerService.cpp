/*
**
** Copyright 2008, The Android Open Source Project
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

// Proxy for media player implementations

//#define LOG_NDEBUG 0
#define LOG_TAG "MediaPlayerService"
#include <utils/Log.h>
#include <cutils/xlog.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <unistd.h>

#include <string.h>

#include <cutils/atomic.h>
#include <cutils/properties.h> // for property_get

#include <utils/misc.h>

#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/MemoryHeapBase.h>
#include <binder/MemoryBase.h>
#include <binder/Parcel.h>
#include <gui/SurfaceTextureClient.h>
#include <utils/Errors.h>  // for status_t
#include <utils/String8.h>
#include <utils/SystemClock.h>
#include <utils/Vector.h>

#include <media/IRemoteDisplay.h>
#include <media/IRemoteDisplayClient.h>
#include <media/MediaPlayerInterface.h>
#include <media/mediarecorder.h>
#include <media/MediaMetadataRetrieverInterface.h>
#include <media/Metadata.h>
#include <media/AudioTrack.h>
#include <media/MemoryLeakTrackUtil.h>
#include <media/stagefright/MediaErrors.h>

#include <system/audio.h>

#include <private/android_filesystem_config.h>

#include "ActivityManager.h"
#include "MediaRecorderClient.h"
#include "MediaPlayerService.h"
#include "MetadataRetrieverClient.h"
#include "MediaPlayerFactory.h"

#include "MidiFile.h"
#include "TestPlayerStub.h"
#include "StagefrightPlayer.h"
#include "nuplayer/NuPlayerDriver.h"

#ifndef ANDROID_DEFAULT_CODE
#ifdef NOTIFYSENDER_ENABLE
#include "NotifySender.h"
#endif
#ifdef MTK_DRM_APP
#include <drm/DrmManagerClient.h> // OMA DRM v1 implementation
#include <drm/DrmMtkUtil.h>
#endif
#endif

#include <OMX.h>

#include "Crypto.h"
#include "HDCP.h"
#include "RemoteDisplay.h"

namespace {
using android::media::Metadata;
using android::status_t;
using android::OK;
using android::BAD_VALUE;
using android::NOT_ENOUGH_DATA;
using android::Parcel;

// Max number of entries in the filter.
const int kMaxFilterSize = 64;  // I pulled that out of thin air.

// FIXME: Move all the metadata related function in the Metadata.cpp


// Unmarshall a filter from a Parcel.
// Filter format in a parcel:
//
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                       number of entries (n)                   |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                       metadata type 1                         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                       metadata type 2                         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  ....
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                       metadata type n                         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// @param p Parcel that should start with a filter.
// @param[out] filter On exit contains the list of metadata type to be
//                    filtered.
// @param[out] status On exit contains the status code to be returned.
// @return true if the parcel starts with a valid filter.
bool unmarshallFilter(const Parcel& p,
                      Metadata::Filter *filter,
                      status_t *status)
{
    int32_t val;
    if (p.readInt32(&val) != OK)
    {
        ALOGE("Failed to read filter's length");
        *status = NOT_ENOUGH_DATA;
        return false;
    }

    if( val > kMaxFilterSize || val < 0)
    {
        ALOGE("Invalid filter len %d", val);
        *status = BAD_VALUE;
        return false;
    }

    const size_t num = val;

    filter->clear();
    filter->setCapacity(num);

    size_t size = num * sizeof(Metadata::Type);


    if (p.dataAvail() < size)
    {
        ALOGE("Filter too short expected %d but got %d", size, p.dataAvail());
        *status = NOT_ENOUGH_DATA;
        return false;
    }

    const Metadata::Type *data =
            static_cast<const Metadata::Type*>(p.readInplace(size));

    if (NULL == data)
    {
        ALOGE("Filter had no data");
        *status = BAD_VALUE;
        return false;
    }

    // TODO: The stl impl of vector would be more efficient here
    // because it degenerates into a memcpy on pod types. Try to
    // replace later or use stl::set.
    for (size_t i = 0; i < num; ++i)
    {
        filter->add(*data);
        ++data;
    }
    *status = OK;
    return true;
}

// @param filter Of metadata type.
// @param val To be searched.
// @return true if a match was found.
bool findMetadata(const Metadata::Filter& filter, const int32_t val)
{
    // Deal with empty and ANY right away
    if (filter.isEmpty()) return false;
    if (filter[0] == Metadata::kAny) return true;

    return filter.indexOf(val) >= 0;
}

}  // anonymous namespace


namespace android {
	
#ifndef ANDROID_DEFAULT_CODE
#ifdef NOTIFYSENDER_ENABLE
static  sp<NotifySender> GetNotifySender()
{
  static sp<NotifySender> gNotifySender = new NotifySender();
	if(gNotifySender.get()==0)
	  SXLOGE("Create NotifySender fail");
	return gNotifySender;
}
#endif
#endif

static bool checkPermission(const char* permissionString) {
#ifndef HAVE_ANDROID_OS
    return true;
#endif
    if (getpid() == IPCThreadState::self()->getCallingPid()) return true;
    bool ok = checkCallingPermission(String16(permissionString));
    if (!ok) ALOGE("Request requires %s", permissionString);
    return ok;
}

// TODO: Find real cause of Audio/Video delay in PV framework and remove this workaround
/* static */ int MediaPlayerService::AudioOutput::mMinBufferCount = 4;
/* static */ bool MediaPlayerService::AudioOutput::mIsOnEmulator = false;

void MediaPlayerService::instantiate() {
    defaultServiceManager()->addService(
            String16("media.player"), new MediaPlayerService());
}

MediaPlayerService::MediaPlayerService()
{
    ALOGV("MediaPlayerService created");
#ifndef ANDROID_DEFAULT_CODE	
    SXLOGD("MediaPlayerService created");
#endif
    mNextConnId = 1;

    mBatteryAudio.refCount = 0;
    for (int i = 0; i < NUM_AUDIO_DEVICES; i++) {
        mBatteryAudio.deviceOn[i] = 0;
        mBatteryAudio.lastTime[i] = 0;
        mBatteryAudio.totalTime[i] = 0;
    }
    // speaker is on by default
    mBatteryAudio.deviceOn[SPEAKER] = 1;

    MediaPlayerFactory::registerBuiltinFactories();
}

MediaPlayerService::~MediaPlayerService()
{
    ALOGV("MediaPlayerService destroyed");
}

sp<IMediaRecorder> MediaPlayerService::createMediaRecorder(pid_t pid)
{
    sp<MediaRecorderClient> recorder = new MediaRecorderClient(this, pid);
    wp<MediaRecorderClient> w = recorder;
    Mutex::Autolock lock(mLock);
    mMediaRecorderClients.add(w);
    ALOGV("Create new media recorder client from pid %d", pid);
    return recorder;
}

void MediaPlayerService::removeMediaRecorderClient(wp<MediaRecorderClient> client)
{
    Mutex::Autolock lock(mLock);
    mMediaRecorderClients.remove(client);
    ALOGV("Delete media recorder client");
}

sp<IMediaMetadataRetriever> MediaPlayerService::createMetadataRetriever(pid_t pid)
{
    sp<MetadataRetrieverClient> retriever = new MetadataRetrieverClient(pid);
    ALOGV("Create new media retriever from pid %d", pid);
    return retriever;
}

sp<IMediaPlayer> MediaPlayerService::create(pid_t pid, const sp<IMediaPlayerClient>& client,
        int audioSessionId)
{
    int32_t connId = android_atomic_inc(&mNextConnId);

    sp<Client> c = new Client(
            this, pid, connId, client, audioSessionId,
            IPCThreadState::self()->getCallingUid());

    ALOGV("Create new client(%d) from pid %d, uid %d, ", connId, pid,
         IPCThreadState::self()->getCallingUid());
#ifndef ANDROID_DEFAULT_CODE
		client->notify(MEDIA_SET_PLAYERID,0,0,connId,NULL);
#endif
    wp<Client> w = c;
    {
        Mutex::Autolock lock(mLock);
        mClients.add(w);
    }
    return c;
}

sp<IOMX> MediaPlayerService::getOMX() {
    Mutex::Autolock autoLock(mLock);

    if (mOMX.get() == NULL) {
        mOMX = new OMX;
    }

    return mOMX;
}

sp<ICrypto> MediaPlayerService::makeCrypto() {
    return new Crypto;
}

sp<IHDCP> MediaPlayerService::makeHDCP() {
    return new HDCP;
}

sp<IRemoteDisplay> MediaPlayerService::listenForRemoteDisplay(
        const sp<IRemoteDisplayClient>& client, const String8& iface) {
    if (!checkPermission("android.permission.CONTROL_WIFI_DISPLAY")) {
        return NULL;
    }

    return new RemoteDisplay(client, iface.string());
}


#ifndef ANDROID_DEFAULT_CODE
sp<IRemoteDisplay> MediaPlayerService::listenForRemoteDisplay(
        const sp<IRemoteDisplayClient>& client, const String8& iface, const uint32_t wfdFlags) {
    //if (!checkPermission("android.permission.CONTROL_WIFI_DISPLAY")) {
    //    return NULL;
    //}

        return new RemoteDisplay(client, iface.string(), wfdFlags);

}

status_t MediaPlayerService::enableRemoteDisplay(const char *iface) {

        if (!checkPermission("android.permission.CONTROL_WIFI_DISPLAY")) {
            return PERMISSION_DENIED;
        }
    
        
        Mutex::Autolock autoLock(mLock);
    
        if (iface != NULL) {
            if (mRemoteDisplay != NULL) {
                return INVALID_OPERATION;
            }
    
            mRemoteDisplay = new RemoteDisplay(NULL /* client */, iface);
            return OK;
        }
    
        if (mRemoteDisplay != NULL) {
            mRemoteDisplay->dispose();
            mRemoteDisplay.clear();
        }


    return OK;
}

status_t MediaPlayerService::enableRemoteDisplay(const char *iface, const uint32_t wfdFlags) {

        if (!checkPermission("android.permission.CONTROL_WIFI_DISPLAY")) {
            return PERMISSION_DENIED;
        }
    
        Mutex::Autolock autoLock(mLock);
    
        if (iface != NULL) {
            if (mRemoteDisplay != NULL) {
                return INVALID_OPERATION;
            }
    
            mRemoteDisplay = new RemoteDisplay(NULL /* client */, iface, wfdFlags);
            return OK;
        }
    
        if (mRemoteDisplay != NULL) {
            mRemoteDisplay->dispose();
            mRemoteDisplay.clear();
        }

    return OK;
}


status_t MediaPlayerService::sendGenericMsg(uint32_t cmd){
    Mutex::Autolock autoLock(mLock);

    if (mRemoteDisplay != NULL) {
        mRemoteDisplay->sendGenericMsg(cmd);
    }

    return OK;
}

status_t MediaPlayerService::setBitrateControl(uint32_t level){
    Mutex::Autolock autoLock(mLock);
        
    if (mRemoteDisplay != NULL) {
        mRemoteDisplay->setBitrateControl(level);
    }

    return OK;    
}

#endif

status_t MediaPlayerService::AudioCache::dump(int fd, const Vector<String16>& args) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;

    result.append(" AudioCache\n");
    if (mHeap != 0) {
        snprintf(buffer, 255, "  heap base(%p), size(%d), flags(%d), device(%s)\n",
                mHeap->getBase(), mHeap->getSize(), mHeap->getFlags(), mHeap->getDevice());
        result.append(buffer);
    }
    snprintf(buffer, 255, "  msec per frame(%f), channel count(%d), format(%d), frame count(%ld)\n",
            mMsecsPerFrame, mChannelCount, mFormat, mFrameCount);
    result.append(buffer);
    snprintf(buffer, 255, "  sample rate(%d), size(%d), error(%d), command complete(%s)\n",
            mSampleRate, mSize, mError, mCommandComplete?"true":"false");
    result.append(buffer);
    ::write(fd, result.string(), result.size());
    return NO_ERROR;
}

status_t MediaPlayerService::AudioOutput::dump(int fd, const Vector<String16>& args) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;

    result.append(" AudioOutput\n");
    snprintf(buffer, 255, "  stream type(%d), left - right volume(%f, %f)\n",
            mStreamType, mLeftVolume, mRightVolume);
    result.append(buffer);
    snprintf(buffer, 255, "  msec per frame(%f), latency (%d)\n",
            mMsecsPerFrame, (mTrack != 0) ? mTrack->latency() : -1);
    result.append(buffer);
    snprintf(buffer, 255, "  aux effect id(%d), send level (%f)\n",
            mAuxEffectId, mSendLevel);
    result.append(buffer);

    ::write(fd, result.string(), result.size());
    if (mTrack != 0) {
        mTrack->dump(fd, args);
    }
    return NO_ERROR;
}

status_t MediaPlayerService::Client::dump(int fd, const Vector<String16>& args) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    result.append(" Client\n");
    snprintf(buffer, 255, "  pid(%d), connId(%d), status(%d), looping(%s)\n",
            mPid, mConnId, mStatus, mLoop?"true": "false");
    result.append(buffer);
    write(fd, result.string(), result.size());
    if (mPlayer != NULL) {
        mPlayer->dump(fd, args);
    }
    if (mAudioOutput != 0) {
        mAudioOutput->dump(fd, args);
    }
    write(fd, "\n", 1);
    return NO_ERROR;
}

status_t MediaPlayerService::dump(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    if (checkCallingPermission(String16("android.permission.DUMP")) == false) {
        snprintf(buffer, SIZE, "Permission Denial: "
                "can't dump MediaPlayerService from pid=%d, uid=%d\n",
                IPCThreadState::self()->getCallingPid(),
                IPCThreadState::self()->getCallingUid());
        result.append(buffer);
    } else {
        Mutex::Autolock lock(mLock);
        for (int i = 0, n = mClients.size(); i < n; ++i) {
            sp<Client> c = mClients[i].promote();
            if (c != 0) c->dump(fd, args);
        }
        if (mMediaRecorderClients.size() == 0) {
                result.append(" No media recorder client\n\n");
        } else {
            for (int i = 0, n = mMediaRecorderClients.size(); i < n; ++i) {
                sp<MediaRecorderClient> c = mMediaRecorderClients[i].promote();
                if (c != 0) {
                    snprintf(buffer, 255, " MediaRecorderClient pid(%d)\n", c->mPid);
                    result.append(buffer);
                    write(fd, result.string(), result.size());
                    result = "\n";
                    c->dump(fd, args);
                }
            }
        }

        result.append(" Files opened and/or mapped:\n");
        snprintf(buffer, SIZE, "/proc/%d/maps", gettid());
        FILE *f = fopen(buffer, "r");
        if (f) {
            while (!feof(f)) {
                fgets(buffer, SIZE, f);
                if (strstr(buffer, " /storage/") ||
                    strstr(buffer, " /system/sounds/") ||
                    strstr(buffer, " /data/") ||
                    strstr(buffer, " /system/media/")) {
                    result.append("  ");
                    result.append(buffer);
                }
            }
            fclose(f);
        } else {
            result.append("couldn't open ");
            result.append(buffer);
            result.append("\n");
        }

        snprintf(buffer, SIZE, "/proc/%d/fd", gettid());
        DIR *d = opendir(buffer);
        if (d) {
            struct dirent *ent;
            while((ent = readdir(d)) != NULL) {
                if (strcmp(ent->d_name,".") && strcmp(ent->d_name,"..")) {
                    snprintf(buffer, SIZE, "/proc/%d/fd/%s", gettid(), ent->d_name);
                    struct stat s;
                    if (lstat(buffer, &s) == 0) {
                        if ((s.st_mode & S_IFMT) == S_IFLNK) {
                            char linkto[256];
                            int len = readlink(buffer, linkto, sizeof(linkto));
                            if(len > 0) {
                                if(len > 255) {
                                    linkto[252] = '.';
                                    linkto[253] = '.';
                                    linkto[254] = '.';
                                    linkto[255] = 0;
                                } else {
                                    linkto[len] = 0;
                                }
                                if (strstr(linkto, "/storage/") == linkto ||
                                    strstr(linkto, "/system/sounds/") == linkto ||
                                    strstr(linkto, "/data/") == linkto ||
                                    strstr(linkto, "/system/media/") == linkto) {
                                    result.append("  ");
                                    result.append(buffer);
                                    result.append(" -> ");
                                    result.append(linkto);
                                    result.append("\n");
                                }
                            }
                        } else {
                            result.append("  unexpected type for ");
                            result.append(buffer);
                            result.append("\n");
                        }
                    }
                }
            }
            closedir(d);
        } else {
            result.append("couldn't open ");
            result.append(buffer);
            result.append("\n");
        }

        bool dumpMem = false;
        for (size_t i = 0; i < args.size(); i++) {
            if (args[i] == String16("-m")) {
                dumpMem = true;
            }
        }
        if (dumpMem) {
            dumpMemoryAddresses(fd);
        }
    }
    write(fd, result.string(), result.size());
    return NO_ERROR;
}

void MediaPlayerService::removeClient(wp<Client> client)
{
    Mutex::Autolock lock(mLock);
    mClients.remove(client);
}

MediaPlayerService::Client::Client(
        const sp<MediaPlayerService>& service, pid_t pid,
        int32_t connId, const sp<IMediaPlayerClient>& client,
        int audioSessionId, uid_t uid)
{
    ALOGV("Client(%d) constructor", connId);
    mPid = pid;
    mConnId = connId;
    mService = service;
    mClient = client;
    mLoop = false;
    mStatus = NO_INIT;
    mAudioSessionId = audioSessionId;
    mUID = uid;
    mRetransmitEndpointValid = false;

#if CALLBACK_ANTAGONIZER
    ALOGD("create Antagonizer");
    mAntagonizer = new Antagonizer(notify, this);
#endif
}

MediaPlayerService::Client::~Client()
{
    ALOGV("Client(%d) destructor pid = %d", mConnId, mPid);
    mAudioOutput.clear();
    wp<Client> client(this);
    disconnect();
    mService->removeClient(client);
#ifndef ANDROID_DEFAULT_CODE
    SXLOGD("[%d]~Client", mConnId);  
#endif
}

void MediaPlayerService::Client::disconnect()
{
    ALOGV("disconnect(%d) from pid %d", mConnId, mPid);
#ifndef ANDROID_DEFAULT_CODE
    SXLOGD("disconnect(%d) from pid %d", mConnId, mPid);
#endif
    // grab local reference and clear main reference to prevent future
    // access to object
    sp<MediaPlayerBase> p;
    {
        Mutex::Autolock l(mLock);
        p = mPlayer;
    }
#ifdef ANDROID_DEFAULT_CODE
/**ALPS00266733, mClient maybe is used by Notify() function such as **
**OnBufferingUpdate event when being cleared **/
    mClient.clear();
#endif

    mPlayer.clear();

    // clear the notification to prevent callbacks to dead client
    // and reset the player. We assume the player will serialize
    // access to itself if necessary.
    if (p != 0) {
        p->setNotifyCallback(0, 0);
#if CALLBACK_ANTAGONIZER
        ALOGD("kill Antagonizer");
        mAntagonizer->kill();
#endif
        p->reset();
    }
#ifndef ANDROID_DEFAULT_CODE
    mClient.clear();
#endif
    disconnectNativeWindow();

    IPCThreadState::self()->flushCommands();
}

sp<MediaPlayerBase> MediaPlayerService::Client::createPlayer(player_type playerType)
{
    // determine if we have the right player type
    sp<MediaPlayerBase> p = mPlayer;
    if ((p != NULL) && (p->playerType() != playerType)) {
        ALOGV("delete player");
        p.clear();
    }
    if (p == NULL) {
        p = MediaPlayerFactory::createPlayer(playerType, this, notify);
    }

    if (p != NULL) {
        p->setUID(mUID);
    }

    return p;
}

sp<MediaPlayerBase> MediaPlayerService::Client::setDataSource_pre(
        player_type playerType)
{
    ALOGV("player type = %d", playerType);

    // create the right type of player
    sp<MediaPlayerBase> p = createPlayer(playerType);
    if (p == NULL) {
        return p;
    }

    if (!p->hardwareOutput()) {
        mAudioOutput = new AudioOutput(mAudioSessionId);
        static_cast<MediaPlayerInterface*>(p.get())->setAudioSink(mAudioOutput);
    }

    return p;
}

void MediaPlayerService::Client::setDataSource_post(
        const sp<MediaPlayerBase>& p,
        status_t status)
{
    ALOGV(" setDataSource");
    mStatus = status;
    if (mStatus != OK) {
        ALOGE("  error: %d", mStatus);
        return;
    }

    // Set the re-transmission endpoint if one was chosen.
    if (mRetransmitEndpointValid) {
        mStatus = p->setRetransmitEndpoint(&mRetransmitEndpoint);
        if (mStatus != NO_ERROR) {
            ALOGE("setRetransmitEndpoint error: %d", mStatus);
        }
    }

    if (mStatus == OK) {
        mPlayer = p;
    }
}

status_t MediaPlayerService::Client::setDataSource(
        const char *url, const KeyedVector<String8, String8> *headers)
{
    ALOGV("setDataSource(%s)", url);
#ifndef ANDROID_DEFAULT_CODE
    SXLOGD("setDataSource(%s)", url);
#endif

    if (url == NULL)
        return UNKNOWN_ERROR;

    if ((strncmp(url, "http://", 7) == 0) ||
        (strncmp(url, "https://", 8) == 0) ||
        (strncmp(url, "rtsp://", 7) == 0)) {
        if (!checkPermission("android.permission.INTERNET")) {
            return PERMISSION_DENIED;
        }
    }
#ifndef ANDROID_DEFAULT_CODE
    if (strncmp(url, "rtsp://file://", 14) == 0) {
        url = url + 14;
    }
#endif

    if (strncmp(url, "content://", 10) == 0) {
        // get a filedescriptor for the content Uri and
        // pass it to the setDataSource(fd) method

        String16 url16(url);
        int fd = android::openContentProviderFile(url16);
        if (fd < 0)
        {
            ALOGE("Couldn't open fd for %s", url);
            return UNKNOWN_ERROR;
        }
        setDataSource(fd, 0, 0x7fffffffffLL); // this sets mStatus
        close(fd);
        return mStatus;
    } else {
        player_type playerType = MediaPlayerFactory::getPlayerType(this, url);
#ifndef ANDROID_DEFAULT_CODE
        SXLOGD("player type = %d", playerType);
	// mtk80902: rtsp via NuPlayer or AwesomePlayer switcher - for debug
	if (strncmp(url, "rtsp://", 7) == 0) {
		char value[PROPERTY_VALUE_MAX];
		if (property_get("media.stagefright.oldrtsp", value, NULL) &&
			!strcmp("1", value) || !strcasecmp("true", value))
			playerType = STAGEFRIGHT_PLAYER;
		else
			playerType = NU_PLAYER;
	}

#ifdef MTK_CMMB_ENABLE
        if((playerType == MATV_AUDIO_PLAYER) || (playerType == FM_AUDIO_PLAYER) ||(playerType == CMMB_PLAYER))
#else
        if((playerType == MATV_AUDIO_PLAYER) || (playerType == FM_AUDIO_PLAYER))
#endif
        {
        	  sp<MediaPlayerBase> p = createPlayer(playerType);
        	  if (p == NULL) return NO_INIT;

            if (!p->hardwareOutput()) {
              mAudioOutput = new AudioOutput(mAudioSessionId);
              static_cast<MediaPlayerInterface*>(p.get())->setAudioSink(mAudioOutput);
            }
        
            mStatus = p->setDataSource(url, headers);
            if (mStatus == NO_ERROR) mPlayer = p;
            SXLOGD("setDataSource(%s) done", url);
		        return mStatus;
        }
#ifdef MTK_DRM_APP
        // the url is "file://" case, check if it is a DCF file.
        // and check if it's a trusted audio / video playback client for drm
        String8 path(url);
        bool isOMADrm = DrmMtkUtil::isDcf(path);
        String8 drmProc;
        if (isOMADrm) {
            drmProc = DrmMtkUtil::getProcessName(mPid); // current client process
            if (!DrmMtkUtil::isTrustedClient(drmProc)) {
                ALOGW("setDataSource with url: untrusted client [%d][%s], denied to access drm source [%s]",
                        mPid, drmProc.string(), path.string());
                return UNKNOWN_ERROR;
            }
        }
#endif
#endif
        sp<MediaPlayerBase> p = setDataSource_pre(playerType);
        if (p == NULL) {
            return NO_INIT;
        }

        setDataSource_post(p, p->setDataSource(url, headers));
#ifndef ANDROID_DEFAULT_CODE
#ifdef MTK_DRM_APP
        if (isOMADrm && mStatus == NO_ERROR) {
            ALOGD("setDataSource with url: save process info: [%s]", drmProc.string());
            Parcel request;
            request.writeString8(drmProc);
            request.setDataPosition(0);
            setParameter(KEY_PARAMETER_DRM_CLIENT_PROC, request);
        }
#endif
        SXLOGD("setDataSource(%s) done", url);
#endif
        return mStatus;
    }
}

status_t MediaPlayerService::Client::setDataSource(int fd, int64_t offset, int64_t length)
{
    ALOGV("setDataSource fd=%d, offset=%lld, length=%lld", fd, offset, length);
#ifndef ANDROID_DEFAULT_CODE
    SXLOGD("setDataSource fd=%d, offset=%lld, length=%lld", fd, offset, length);
#endif
    struct stat sb;
    int ret = fstat(fd, &sb);
    if (ret != 0) {
        ALOGE("fstat(%d) failed: %d, %s", fd, ret, strerror(errno));
        return UNKNOWN_ERROR;
    }

    ALOGV("st_dev  = %llu", sb.st_dev);
    ALOGV("st_mode = %u", sb.st_mode);
    ALOGV("st_uid  = %lu", sb.st_uid);
    ALOGV("st_gid  = %lu", sb.st_gid);
    ALOGV("st_size = %llu", sb.st_size);

    if (offset >= sb.st_size) {
        ALOGE("offset error");
        ::close(fd);
        return UNKNOWN_ERROR;
    }
    if (offset + length > sb.st_size) {
        length = sb.st_size - offset;
        ALOGV("calculated length = %lld", length);
    }
#ifndef ANDROID_DEFAULT_CODE
#ifdef MTK_DRM_APP
    // file descriptor case, check if it's a DCF file
    // and check if it's a trusted audio / video playback client for drm
    bool isOMADrm = DrmMtkUtil::isDcf(fd);
    String8 drmProc;
    if (isOMADrm) {
        drmProc = DrmMtkUtil::getProcessName(mPid); // current client process
        if (!DrmMtkUtil::isTrustedClient(drmProc)) {
            ALOGW("setDataSource with fd: untrusted client [%d][%s], denied to access drm fd [%d]",
                    mPid, drmProc.string(), fd);
            return UNKNOWN_ERROR;
        }
    }
#endif
#endif

    player_type playerType = MediaPlayerFactory::getPlayerType(this,
                                                               fd,
                                                               offset,
                                                               length);
#ifndef ANDROID_DEFAULT_CODE
    SXLOGD("player type = %d", playerType);
#endif                                                              
    sp<MediaPlayerBase> p = setDataSource_pre(playerType);
    if (p == NULL) {
        return NO_INIT;
    }

    // now set data source
    setDataSource_post(p, p->setDataSource(fd, offset, length));
#ifndef ANDROID_DEFAULT_CODE
#ifdef MTK_DRM_APP
    if (isOMADrm && mStatus == NO_ERROR) {
        ALOGD("setDataSource with fd: save process info: [%s]", drmProc.string());
        Parcel request;
        request.writeString8(drmProc);
        request.setDataPosition(0);

        setParameter(KEY_PARAMETER_DRM_CLIENT_PROC, request);
    }
#endif
    SXLOGD("setDataSource fd=%d, offset=%lld, length=%lld done", fd, offset, length);
#endif

    return mStatus;
}

status_t MediaPlayerService::Client::setDataSource(
        const sp<IStreamSource> &source) {
    // create the right type of player
    player_type playerType = MediaPlayerFactory::getPlayerType(this, source);
    sp<MediaPlayerBase> p = setDataSource_pre(playerType);
    if (p == NULL) {
        return NO_INIT;
    }

    // now set data source
    setDataSource_post(p, p->setDataSource(source));
    return mStatus;
}

void MediaPlayerService::Client::disconnectNativeWindow() {
    if (mConnectedWindow != NULL) {
        status_t err = native_window_api_disconnect(mConnectedWindow.get(),
                NATIVE_WINDOW_API_MEDIA);

        if (err != OK) {
            ALOGW("native_window_api_disconnect returned an error: %s (%d)",
                    strerror(-err), err);
        }
    }
    mConnectedWindow.clear();
}

status_t MediaPlayerService::Client::setVideoSurfaceTexture(
        const sp<ISurfaceTexture>& surfaceTexture)
{
    ALOGV("[%d] setVideoSurfaceTexture(%p)", mConnId, surfaceTexture.get());
    sp<MediaPlayerBase> p = getPlayer();
    if (p == 0) return UNKNOWN_ERROR;

    sp<IBinder> binder(surfaceTexture == NULL ? NULL :
            surfaceTexture->asBinder());
    if (mConnectedWindowBinder == binder) {
        return OK;
    }

    sp<ANativeWindow> anw;
    if (surfaceTexture != NULL) {
        anw = new SurfaceTextureClient(surfaceTexture);
        status_t err = native_window_api_connect(anw.get(),
                NATIVE_WINDOW_API_MEDIA);
#ifndef ANDROID_DEFAULT_CODE
        if(err == -EEXIST)
        {
          native_window_api_disconnect(anw.get(),NATIVE_WINDOW_API_MEDIA);
          err = native_window_api_connect(anw.get(),NATIVE_WINDOW_API_MEDIA);
        }
#endif
        if (err != OK) {
            ALOGE("setVideoSurfaceTexture failed: %d", err);
            // Note that we must do the reset before disconnecting from the ANW.
            // Otherwise queue/dequeue calls could be made on the disconnected
            // ANW, which may result in errors.
            reset();

            disconnectNativeWindow();

            return err;
        }
    }

    // Note that we must set the player's new SurfaceTexture before
    // disconnecting the old one.  Otherwise queue/dequeue calls could be made
    // on the disconnected ANW, which may result in errors.
    status_t err = p->setVideoSurfaceTexture(surfaceTexture);

    disconnectNativeWindow();

    mConnectedWindow = anw;

    if (err == OK) {
        mConnectedWindowBinder = binder;
    } else {
        disconnectNativeWindow();
    }

    return err;
}

status_t MediaPlayerService::Client::invoke(const Parcel& request,
                                            Parcel *reply)
{
    sp<MediaPlayerBase> p = getPlayer();
    if (p == NULL) return UNKNOWN_ERROR;
    return p->invoke(request, reply);
}

// This call doesn't need to access the native player.
status_t MediaPlayerService::Client::setMetadataFilter(const Parcel& filter)
{
    status_t status;
    media::Metadata::Filter allow, drop;

    if (unmarshallFilter(filter, &allow, &status) &&
        unmarshallFilter(filter, &drop, &status)) {
        Mutex::Autolock lock(mLock);

        mMetadataAllow = allow;
        mMetadataDrop = drop;
    }
    return status;
}

status_t MediaPlayerService::Client::getMetadata(
        bool update_only, bool apply_filter, Parcel *reply)
{
    sp<MediaPlayerBase> player = getPlayer();
    if (player == 0) return UNKNOWN_ERROR;

    status_t status;
    // Placeholder for the return code, updated by the caller.
    reply->writeInt32(-1);

    media::Metadata::Filter ids;

    // We don't block notifications while we fetch the data. We clear
    // mMetadataUpdated first so we don't lose notifications happening
    // during the rest of this call.
    {
        Mutex::Autolock lock(mLock);
        if (update_only) {
            ids = mMetadataUpdated;
        }
        mMetadataUpdated.clear();
    }

    media::Metadata metadata(reply);

    metadata.appendHeader();
    status = player->getMetadata(ids, reply);

    if (status != OK) {
        metadata.resetParcel();
        ALOGE("getMetadata failed %d", status);
        return status;
    }

    // FIXME: Implement filtering on the result. Not critical since
    // filtering takes place on the update notifications already. This
    // would be when all the metadata are fetch and a filter is set.

    // Everything is fine, update the metadata length.
    metadata.updateLength();
    return OK;
}

status_t MediaPlayerService::Client::prepareAsync()
{
    ALOGV("[%d] prepareAsync", mConnId);
#ifndef ANDROID_DEFAULT_CODE	
    SXLOGD("[%d] prepareAsync", mConnId);
#endif
    sp<MediaPlayerBase> p = getPlayer();
    if (p == 0) return UNKNOWN_ERROR;
    status_t ret = p->prepareAsync();
#if CALLBACK_ANTAGONIZER
    ALOGD("start Antagonizer");
    if (ret == NO_ERROR) mAntagonizer->start();
#endif
    return ret;
}

status_t MediaPlayerService::Client::start()
{
    ALOGV("[%d] start", mConnId);
    sp<MediaPlayerBase> p = getPlayer();
    if (p == 0) return UNKNOWN_ERROR;
    p->setLooping(mLoop);
#ifndef ANDROID_DEFAULT_CODE	
    SXLOGD("[%d] start", mConnId);   
#endif	
    return p->start();
}

status_t MediaPlayerService::Client::stop()
{
    ALOGV("[%d] stop", mConnId);
    sp<MediaPlayerBase> p = getPlayer();
    if (p == 0) return UNKNOWN_ERROR;
#ifndef ANDROID_DEFAULT_CODE	
    SXLOGD("[%d] stop", mConnId);   
#endif
    return p->stop();
}

status_t MediaPlayerService::Client::pause()
{
    ALOGV("[%d] pause", mConnId);
    sp<MediaPlayerBase> p = getPlayer();
    if (p == 0) return UNKNOWN_ERROR;
#ifndef ANDROID_DEFAULT_CODE	
    SXLOGD("[%d] pause", mConnId);   
#endif
    return p->pause();
}

status_t MediaPlayerService::Client::isPlaying(bool* state)
{
    *state = false;
    sp<MediaPlayerBase> p = getPlayer();
    if (p == 0) return UNKNOWN_ERROR;
    *state = p->isPlaying();
    ALOGV("[%d] isPlaying: %d", mConnId, *state);
    return NO_ERROR;
}

status_t MediaPlayerService::Client::getCurrentPosition(int *msec)
{
    ALOGV("getCurrentPosition");
    sp<MediaPlayerBase> p = getPlayer();
    if (p == 0) return UNKNOWN_ERROR;
    status_t ret = p->getCurrentPosition(msec);
    if (ret == NO_ERROR) {
        ALOGV("[%d] getCurrentPosition = %d", mConnId, *msec);
    } else {
        ALOGE("getCurrentPosition returned %d", ret);
    }
    return ret;
}

status_t MediaPlayerService::Client::getDuration(int *msec)
{
    ALOGV("getDuration");
    sp<MediaPlayerBase> p = getPlayer();
    if (p == 0) return UNKNOWN_ERROR;
    status_t ret = p->getDuration(msec);
    if (ret == NO_ERROR) {
        ALOGV("[%d] getDuration = %d", mConnId, *msec);
#ifndef ANDROID_DEFAULT_CODE	
        SXLOGD("[%d] getDuration = %d", mConnId, *msec);
#endif
    } else {
        ALOGE("getDuration returned %d", ret);
    }
    return ret;
}

status_t MediaPlayerService::Client::setNextPlayer(const sp<IMediaPlayer>& player) {
    ALOGV("setNextPlayer");
    Mutex::Autolock l(mLock);
    sp<Client> c = static_cast<Client*>(player.get());
    mNextClient = c;

    if (c != NULL) {
#ifndef ANDROID_DEFAULT_CODE	
        SXLOGD("[%ld]setNextPlayer:[%ld]",mConnId,mNextClient->mConnId);
#endif
        if (mAudioOutput != NULL) {
            mAudioOutput->setNextOutput(c->mAudioOutput);
        } else if ((mPlayer != NULL) && !mPlayer->hardwareOutput()) {
            ALOGE("no current audio output");
        }

        if ((mPlayer != NULL) && (mNextClient->getPlayer() != NULL)) {
            mPlayer->setNextPlayer(mNextClient->getPlayer());
        }
    }

    return OK;
}

status_t MediaPlayerService::Client::seekTo(int msec)
{
    ALOGV("[%d] seekTo(%d)", mConnId, msec);
#ifndef ANDROID_DEFAULT_CODE	
    SXLOGD("[%d] seekTo(%d)", mConnId, msec);
#endif
    sp<MediaPlayerBase> p = getPlayer();
    if (p == 0) return UNKNOWN_ERROR;
    return p->seekTo(msec);
}

status_t MediaPlayerService::Client::reset()
{
    ALOGV("[%d] reset", mConnId);
    mRetransmitEndpointValid = false;
    sp<MediaPlayerBase> p = getPlayer();
    if (p == 0) return UNKNOWN_ERROR;
#ifndef ANDROID_DEFAULT_CODE	
    SXLOGD("[%d] reset", mConnId);   
#endif
    return p->reset();
}

status_t MediaPlayerService::Client::setAudioStreamType(audio_stream_type_t type)
{
    ALOGV("[%d] setAudioStreamType(%d)", mConnId, type);
    // TODO: for hardware output, call player instead
    Mutex::Autolock l(mLock);
    if (mAudioOutput != 0) mAudioOutput->setAudioStreamType(type);
    return NO_ERROR;
}

status_t MediaPlayerService::Client::setLooping(int loop)
{
    ALOGV("[%d] setLooping(%d)", mConnId, loop);
    mLoop = loop;
    sp<MediaPlayerBase> p = getPlayer();
    if (p != 0) return p->setLooping(loop);
    return NO_ERROR;
}

status_t MediaPlayerService::Client::setVolume(float leftVolume, float rightVolume)
{
    ALOGV("[%d] setVolume(%f, %f)", mConnId, leftVolume, rightVolume);

    // for hardware output, call player instead
    sp<MediaPlayerBase> p = getPlayer();
    {
      Mutex::Autolock l(mLock);
      if (p != 0 && p->hardwareOutput()) {
          MediaPlayerHWInterface* hwp =
                  reinterpret_cast<MediaPlayerHWInterface*>(p.get());
          return hwp->setVolume(leftVolume, rightVolume);
      } else {
          if (mAudioOutput != 0) mAudioOutput->setVolume(leftVolume, rightVolume);
          return NO_ERROR;
      }
    }

    return NO_ERROR;
}

status_t MediaPlayerService::Client::setAuxEffectSendLevel(float level)
{
    ALOGV("[%d] setAuxEffectSendLevel(%f)", mConnId, level);
    Mutex::Autolock l(mLock);
    if (mAudioOutput != 0) return mAudioOutput->setAuxEffectSendLevel(level);
    return NO_ERROR;
}

status_t MediaPlayerService::Client::attachAuxEffect(int effectId)
{
    ALOGV("[%d] attachAuxEffect(%d)", mConnId, effectId);
    Mutex::Autolock l(mLock);
    if (mAudioOutput != 0) return mAudioOutput->attachAuxEffect(effectId);
    return NO_ERROR;
}

status_t MediaPlayerService::Client::setParameter(int key, const Parcel &request) {
    ALOGV("[%d] setParameter(%d)", mConnId, key);
    sp<MediaPlayerBase> p = getPlayer();
    if (p == 0) return UNKNOWN_ERROR;
#ifdef VENDOR_EDIT 
//liyajun@MediaSrv.Group.module, 2012/09/21, Add for capture video frame
	if (key == KEY_PARAMETER_CAPTURE_FRAME) {
		return p->captureFrame(String8(request.readString16()));
	} else	
#endif /* VENDOR_EDIT */
    return p->setParameter(key, request);
}

status_t MediaPlayerService::Client::getParameter(int key, Parcel *reply) {
    ALOGV("[%d] getParameter(%d)", mConnId, key);
    sp<MediaPlayerBase> p = getPlayer();
    if (p == 0) return UNKNOWN_ERROR;
    return p->getParameter(key, reply);
}

status_t MediaPlayerService::Client::setRetransmitEndpoint(
        const struct sockaddr_in* endpoint) {

    if (NULL != endpoint) {
        uint32_t a = ntohl(endpoint->sin_addr.s_addr);
        uint16_t p = ntohs(endpoint->sin_port);
        ALOGV("[%d] setRetransmitEndpoint(%u.%u.%u.%u:%hu)", mConnId,
                (a >> 24), (a >> 16) & 0xFF, (a >> 8) & 0xFF, (a & 0xFF), p);
    } else {
        ALOGV("[%d] setRetransmitEndpoint = <none>", mConnId);
    }

    sp<MediaPlayerBase> p = getPlayer();

    // Right now, the only valid time to set a retransmit endpoint is before
    // player selection has been made (since the presence or absence of a
    // retransmit endpoint is going to determine which player is selected during
    // setDataSource).
    if (p != 0) return INVALID_OPERATION;

    if (NULL != endpoint) {
        mRetransmitEndpoint = *endpoint;
        mRetransmitEndpointValid = true;
    } else {
        mRetransmitEndpointValid = false;
    }

    return NO_ERROR;
}

status_t MediaPlayerService::Client::getRetransmitEndpoint(
        struct sockaddr_in* endpoint)
{
    if (NULL == endpoint)
        return BAD_VALUE;

    sp<MediaPlayerBase> p = getPlayer();

    if (p != NULL)
        return p->getRetransmitEndpoint(endpoint);

    if (!mRetransmitEndpointValid)
        return NO_INIT;

    *endpoint = mRetransmitEndpoint;

    return NO_ERROR;
}

void MediaPlayerService::Client::notify(
        void* cookie, int msg, int ext1, int ext2, const Parcel *obj)
{
    Client* client = static_cast<Client*>(cookie);
    if (client == NULL) {
        return;
    }

    sp<IMediaPlayerClient> c;
    {
        Mutex::Autolock l(client->mLock);
        c = client->mClient;
        if (msg == MEDIA_PLAYBACK_COMPLETE && client->mNextClient != NULL) {
            if (client->mAudioOutput != NULL)
                client->mAudioOutput->switchToNextOutput();
            client->mNextClient->start();
            client->mNextClient->mClient->notify(MEDIA_INFO, MEDIA_INFO_STARTED_AS_NEXT, 0, obj);
        }
    }

    if (MEDIA_INFO == msg &&
        MEDIA_INFO_METADATA_UPDATE == ext1) {
        const media::Metadata::Type metadata_type = ext2;

        if(client->shouldDropMetadata(metadata_type)) {
            return;
        }

        // Update the list of metadata that have changed. getMetadata
        // also access mMetadataUpdated and clears it.
        client->addNewMetadataUpdate(metadata_type);
    }
#ifndef ANDROID_DEFAULT_CODE
    if(c == NULL)	
    	return;
    SXLOGD("[%d] notify (%p, %d, %d, %d)", client->mConnId, cookie, msg, ext1, ext2);
#ifdef NOTIFYSENDER_ENABLE
	  sp<NotifySender>  p = GetNotifySender();
    if(p.get())
        p->sendMessage(c,msg,ext1,ext2,client->mConnId,obj);
	  else
        c->notify(msg, ext1, ext2,client->mConnId,obj);
#else
    c->notify(msg, ext1, ext2,client->mConnId,obj);
#endif
#else
    if (c != NULL) {
        ALOGV("[%d] notify (%p, %d, %d, %d)", client->mConnId, cookie, msg, ext1, ext2);
        c->notify(msg, ext1, ext2, obj);
    }
#endif
}


bool MediaPlayerService::Client::shouldDropMetadata(media::Metadata::Type code) const
{
    Mutex::Autolock lock(mLock);

    if (findMetadata(mMetadataDrop, code)) {
        return true;
    }

    if (mMetadataAllow.isEmpty() || findMetadata(mMetadataAllow, code)) {
        return false;
    } else {
        return true;
    }
}

#ifndef ANDROID_DEFAULT_CODE
#ifdef MTK_CMMB_ENABLE
//CMMB
status_t MediaPlayerService::Client::capture(const char* uri) {
    SXLOGD("[%d] cmmb Capture(%s)", mConnId, uri);
    sp<MediaPlayerBase> p = getPlayer();

	if (p == 0) {
		return UNKNOWN_ERROR;
	}

	return p->capture(uri);
}
#endif
#endif

void MediaPlayerService::Client::addNewMetadataUpdate(media::Metadata::Type metadata_type) {
    Mutex::Autolock lock(mLock);
    if (mMetadataUpdated.indexOf(metadata_type) < 0) {
        mMetadataUpdated.add(metadata_type);
    }
}

#if CALLBACK_ANTAGONIZER
const int Antagonizer::interval = 10000; // 10 msecs

Antagonizer::Antagonizer(notify_callback_f cb, void* client) :
    mExit(false), mActive(false), mClient(client), mCb(cb)
{
    createThread(callbackThread, this);
}

void Antagonizer::kill()
{
    Mutex::Autolock _l(mLock);
    mActive = false;
    mExit = true;
    mCondition.wait(mLock);
}

int Antagonizer::callbackThread(void* user)
{
    ALOGD("Antagonizer started");
    Antagonizer* p = reinterpret_cast<Antagonizer*>(user);
    while (!p->mExit) {
        if (p->mActive) {
            ALOGV("send event");
            p->mCb(p->mClient, 0, 0, 0);
        }
        usleep(interval);
    }
    Mutex::Autolock _l(p->mLock);
    p->mCondition.signal();
    ALOGD("Antagonizer stopped");
    return 0;
}
#endif

static size_t kDefaultHeapSize = 1024 * 1024; // 1MB

sp<IMemory> MediaPlayerService::decode(const char* url, uint32_t *pSampleRate, int* pNumChannels, audio_format_t* pFormat)
{
    ALOGV("decode(%s)", url);
#ifndef ANDROID_DEFAULT_CODE
    SXLOGD("decode(%s)", url);
#endif
    sp<MemoryBase> mem;
    sp<MediaPlayerBase> player;

    // Protect our precious, precious DRMd ringtones by only allowing
    // decoding of http, but not filesystem paths or content Uris.
    // If the application wants to decode those, it should open a
    // filedescriptor for them and use that.
    if (url != NULL && strncmp(url, "http://", 7) != 0) {
        ALOGD("Can't decode %s by path, use filedescriptor instead", url);
        return mem;
    }

    player_type playerType =
        MediaPlayerFactory::getPlayerType(NULL /* client */, url);
    ALOGV("player type = %d", playerType);

    // create the right type of player
    sp<AudioCache> cache = new AudioCache(url);
    player = MediaPlayerFactory::createPlayer(playerType, cache.get(), cache->notify);
    if (player == NULL) goto Exit;
    if (player->hardwareOutput()) goto Exit;

    static_cast<MediaPlayerInterface*>(player.get())->setAudioSink(cache);

    // set data source
    if (player->setDataSource(url) != NO_ERROR) goto Exit;

    ALOGV("prepare");
    player->prepareAsync();

    ALOGV("wait for prepare");
    if (cache->wait() != NO_ERROR) goto Exit;

    ALOGV("start");
    player->start();

    ALOGV("wait for playback complete");
    cache->wait();
    // in case of error, return what was successfully decoded.
    if (cache->size() == 0) {
        goto Exit;
    }

    mem = new MemoryBase(cache->getHeap(), 0, cache->size());
    *pSampleRate = cache->sampleRate();
    *pNumChannels = cache->channelCount();
    *pFormat = cache->format();
    ALOGV("return memory @ %p, sampleRate=%u, channelCount = %d, format = %d", mem->pointer(), *pSampleRate, *pNumChannels, *pFormat);
#ifndef ANDROID_DEFAULT_CODE
    SXLOGD("return memory @ %p, sampleRate=%u, channelCount = %d, format = %d", mem->pointer(), *pSampleRate, *pNumChannels, *pFormat);
#endif

Exit:
    if (player != 0) player->reset();
    return mem;
}

sp<IMemory> MediaPlayerService::decode(int fd, int64_t offset, int64_t length, uint32_t *pSampleRate, int* pNumChannels, audio_format_t* pFormat)
{
    ALOGV("decode(%d, %lld, %lld)", fd, offset, length);
#ifndef ANDROID_DEFAULT_CODE
    SXLOGD("decode(%d, %lld, %lld)", fd, offset, length);
#endif
    sp<MemoryBase> mem;
    sp<MediaPlayerBase> player;

    player_type playerType = MediaPlayerFactory::getPlayerType(NULL /* client */,
                                                               fd,
                                                               offset,
                                                               length);
    ALOGV("player type = %d", playerType);

    // create the right type of player
    sp<AudioCache> cache = new AudioCache("decode_fd");
    player = MediaPlayerFactory::createPlayer(playerType, cache.get(), cache->notify);
    if (player == NULL) goto Exit;
    if (player->hardwareOutput()) goto Exit;

    static_cast<MediaPlayerInterface*>(player.get())->setAudioSink(cache);

    // set data source
    if (player->setDataSource(fd, offset, length) != NO_ERROR) goto Exit;

    ALOGV("prepare");
    player->prepareAsync();

    ALOGV("wait for prepare");
    if (cache->wait() != NO_ERROR) goto Exit;

    ALOGV("start");
    player->start();

    ALOGV("wait for playback complete");
    cache->wait();
    // in case of error, return what was successfully decoded.
    if (cache->size() == 0) {
        goto Exit;
    }

    mem = new MemoryBase(cache->getHeap(), 0, cache->size());
    *pSampleRate = cache->sampleRate();
    *pNumChannels = cache->channelCount();
    *pFormat = cache->format();
    ALOGV("return memory @ %p, sampleRate=%u, channelCount = %d, format = %d", mem->pointer(), *pSampleRate, *pNumChannels, *pFormat);
#ifndef ANDROID_DEFAULT_CODE
    SXLOGD("return memory @ %p, sampleRate=%u, channelCount = %d, format = %d", mem->pointer(), *pSampleRate, *pNumChannels, *pFormat);
#endif
Exit:
    if (player != 0) player->reset();
    ::close(fd);
    return mem;
}


#undef LOG_TAG
#define LOG_TAG "AudioSink"
MediaPlayerService::AudioOutput::AudioOutput(int sessionId)
    : mCallback(NULL),
      mCallbackCookie(NULL),
      mCallbackData(NULL),
      mBytesWritten(0),
      mSessionId(sessionId),
      mFlags(AUDIO_OUTPUT_FLAG_NONE) {
    ALOGV("AudioOutput(%d)", sessionId);
#ifndef ANDROID_DEFAULT_CODE
    SXLOGD("AudioOutput(%d)", sessionId);
#endif
    mTrack = 0;
    mRecycledTrack = 0;
    mStreamType = AUDIO_STREAM_MUSIC;
    mLeftVolume = 1.0;
    mRightVolume = 1.0;
    mPlaybackRatePermille = 1000;
    mSampleRateHz = 0;
    mMsecsPerFrame = 0;
    mAuxEffectId = 0;
    mSendLevel = 0.0;
    setMinBufferCount();
}

MediaPlayerService::AudioOutput::~AudioOutput()
{
    close();
    delete mRecycledTrack;
    delete mCallbackData;
}

void MediaPlayerService::AudioOutput::setMinBufferCount()
{
    char value[PROPERTY_VALUE_MAX];
    if (property_get("ro.kernel.qemu", value, 0)) {
        mIsOnEmulator = true;
        mMinBufferCount = 12;  // to prevent systematic buffer underrun for emulator
    }
}

bool MediaPlayerService::AudioOutput::isOnEmulator()
{
    setMinBufferCount();
    return mIsOnEmulator;
}

int MediaPlayerService::AudioOutput::getMinBufferCount()
{
    setMinBufferCount();
    return mMinBufferCount;
}

ssize_t MediaPlayerService::AudioOutput::bufferSize() const
{
    if (mTrack == 0) return NO_INIT;
    return mTrack->frameCount() * frameSize();
}

ssize_t MediaPlayerService::AudioOutput::frameCount() const
{
    if (mTrack == 0) return NO_INIT;
    return mTrack->frameCount();
}

ssize_t MediaPlayerService::AudioOutput::channelCount() const
{
    if (mTrack == 0) return NO_INIT;
    return mTrack->channelCount();
}

ssize_t MediaPlayerService::AudioOutput::frameSize() const
{
    if (mTrack == 0) return NO_INIT;
    return mTrack->frameSize();
}

uint32_t MediaPlayerService::AudioOutput::latency () const
{
    if (mTrack == 0) return 0;
    return mTrack->latency();
}

float MediaPlayerService::AudioOutput::msecsPerFrame() const
{
    return mMsecsPerFrame;
}

status_t MediaPlayerService::AudioOutput::getPosition(uint32_t *position) const
{
    if (mTrack == 0) return NO_INIT;
    return mTrack->getPosition(position);
}

status_t MediaPlayerService::AudioOutput::getFramesWritten(uint32_t *frameswritten) const
{
    if (mTrack == 0) return NO_INIT;
    *frameswritten = mBytesWritten / frameSize();
    return OK;
}

status_t MediaPlayerService::AudioOutput::open(
        uint32_t sampleRate, int channelCount, audio_channel_mask_t channelMask,
        audio_format_t format, int bufferCount,
        AudioCallback cb, void *cookie,
        audio_output_flags_t flags)
{
    mCallback = cb;
    mCallbackCookie = cookie;

    // Check argument "bufferCount" against the mininum buffer count
    if (bufferCount < mMinBufferCount) {
        ALOGD("bufferCount (%d) is too small and increased to %d", bufferCount, mMinBufferCount);
        bufferCount = mMinBufferCount;

    }
    ALOGV("open(%u, %d, 0x%x, %d, %d, %d)", sampleRate, channelCount, channelMask,
            format, bufferCount, mSessionId);
    int afSampleRate;
    int afFrameCount;
    uint32_t frameCount;

    if (AudioSystem::getOutputFrameCount(&afFrameCount, mStreamType) != NO_ERROR) {
        return NO_INIT;
    }
    if (AudioSystem::getOutputSamplingRate(&afSampleRate, mStreamType) != NO_ERROR) {
        return NO_INIT;
    }

    frameCount = (sampleRate*afFrameCount*bufferCount)/afSampleRate;
#ifndef ANDROID_DEFAULT_CODE
    SXLOGD("open(%u, %d, %d, %d, %d)", sampleRate, channelCount, format, bufferCount,mSessionId);
#endif

    if (channelMask == CHANNEL_MASK_USE_CHANNEL_ORDER) {
        channelMask = audio_channel_out_mask_from_count(channelCount);
        if (0 == channelMask) {
            ALOGE("open() error, can\'t derive mask for %d audio channels", channelCount);
            return NO_INIT;
        }
    }

    AudioTrack *t;
    CallbackData *newcbd = NULL;
    if (mCallback != NULL) {
        newcbd = new CallbackData(this);
        t = new AudioTrack(
                mStreamType,
                sampleRate,
                format,
                channelMask,
                frameCount,
                flags,
                CallbackWrapper,
                newcbd,
                0,  // notification frames
                mSessionId);
    } else {
        t = new AudioTrack(
                mStreamType,
                sampleRate,
                format,
                channelMask,
                frameCount,
                flags,
                NULL,
                NULL,
                0,
                mSessionId);
    }

    if ((t == 0) || (t->initCheck() != NO_ERROR)) {
        ALOGE("Unable to create audio track");
        delete t;
        delete newcbd;
        return NO_INIT;
    }


    if (mRecycledTrack) {
        // check if the existing track can be reused as-is, or if a new track needs to be created.

        bool reuse = true;
        if ((mCallbackData == NULL && mCallback != NULL) ||
                (mCallbackData != NULL && mCallback == NULL)) {
            // recycled track uses callbacks but the caller wants to use writes, or vice versa
            ALOGV("can't chain callback and write");
            reuse = false;
        } else if ((mRecycledTrack->getSampleRate() != sampleRate) ||
                (mRecycledTrack->channelCount() != channelCount) ||
                (mRecycledTrack->frameCount() != t->frameCount())) {
            ALOGV("samplerate, channelcount or framecount differ: %d/%d Hz, %d/%d ch, %d/%d frames",
                  mRecycledTrack->getSampleRate(), sampleRate,
                  mRecycledTrack->channelCount(), channelCount,
                  mRecycledTrack->frameCount(), t->frameCount());
            reuse = false;
        } else if (flags != mFlags) {
            ALOGV("output flags differ %08x/%08x", flags, mFlags);
            reuse = false;
        }
        if (reuse) {
            ALOGV("chaining to next output");
#ifndef ANDROID_DEFAULT_CODE
            SXLOGD("chaining to next output");
#endif
            close();
            mTrack = mRecycledTrack;
            mRecycledTrack = NULL;
            if (mCallbackData != NULL) {
                mCallbackData->setOutput(this);
            }
            delete t;
            delete newcbd;
            return OK;
        }

        // if we're not going to reuse the track, unblock and flush it
        if (mCallbackData != NULL) {
            mCallbackData->setOutput(NULL);
            mCallbackData->endTrackSwitch();
        }
        mRecycledTrack->flush();
        delete mRecycledTrack;
        mRecycledTrack = NULL;
        delete mCallbackData;
        mCallbackData = NULL;
        close();
    }

    mCallbackData = newcbd;
    ALOGV("setVolume");
    t->setVolume(mLeftVolume, mRightVolume);

    mSampleRateHz = sampleRate;
    mFlags = flags;
    mMsecsPerFrame = mPlaybackRatePermille / (float) sampleRate;
    uint32_t pos;
    if (t->getPosition(&pos) == OK) {
        mBytesWritten = uint64_t(pos) * t->frameSize();
    }
    mTrack = t;

    status_t res = t->setSampleRate(mPlaybackRatePermille * mSampleRateHz / 1000);
    if (res != NO_ERROR) {
        return res;
    }
    t->setAuxEffectSendLevel(mSendLevel);
    return t->attachAuxEffect(mAuxEffectId);;
}

void MediaPlayerService::AudioOutput::start()
{
    ALOGV("start");
#ifndef ANDROID_DEFAULT_CODE
    SXLOGD("start");
#endif
    if (mCallbackData != NULL) {
        mCallbackData->endTrackSwitch();
    }
    if (mTrack) {
        mTrack->setVolume(mLeftVolume, mRightVolume);
        mTrack->setAuxEffectSendLevel(mSendLevel);
        mTrack->start();
    }
}

void MediaPlayerService::AudioOutput::setNextOutput(const sp<AudioOutput>& nextOutput) {
    mNextOutput = nextOutput;
}


void MediaPlayerService::AudioOutput::switchToNextOutput() {
    ALOGV("switchToNextOutput");
#ifndef ANDROID_DEFAULT_CODE    
    SXLOGD("switchToNextOutput");
#endif    
    if (mNextOutput != NULL) {
        if (mCallbackData != NULL) {
            mCallbackData->beginTrackSwitch();
        }
        delete mNextOutput->mCallbackData;
        mNextOutput->mCallbackData = mCallbackData;
        mCallbackData = NULL;
        mNextOutput->mRecycledTrack = mTrack;
        mTrack = NULL;
        mNextOutput->mSampleRateHz = mSampleRateHz;
        mNextOutput->mMsecsPerFrame = mMsecsPerFrame;
        mNextOutput->mBytesWritten = mBytesWritten;
        mNextOutput->mFlags = mFlags;
    }
}

ssize_t MediaPlayerService::AudioOutput::write(const void* buffer, size_t size)
{
    LOG_FATAL_IF(mCallback != NULL, "Don't call write if supplying a callback.");

    //ALOGV("write(%p, %u)", buffer, size);
    if (mTrack) {
        ssize_t ret = mTrack->write(buffer, size);
        mBytesWritten += ret;
        return ret;
    }
    return NO_INIT;
}

void MediaPlayerService::AudioOutput::stop()
{
    ALOGV("stop");
#ifndef ANDROID_DEFAULT_CODE
    SXLOGD("stop");
#endif
    if (mTrack) mTrack->stop();
}

void MediaPlayerService::AudioOutput::flush()
{
    ALOGV("flush");
#ifndef ANDROID_DEFAULT_CODE
    SXLOGD("flush");
#endif
    if (mTrack) mTrack->flush();
}

void MediaPlayerService::AudioOutput::pause()
{
    ALOGV("pause");
#ifndef ANDROID_DEFAULT_CODE
    SXLOGD("pause");
#endif
    if (mTrack) mTrack->pause();
}

void MediaPlayerService::AudioOutput::close()
{
    ALOGV("close");
#ifndef ANDROID_DEFAULT_CODE
    SXLOGD("close");
#endif
    delete mTrack;
    mTrack = 0;
}

void MediaPlayerService::AudioOutput::setVolume(float left, float right)
{
    ALOGV("setVolume(%f, %f)", left, right);
    mLeftVolume = left;
    mRightVolume = right;
    if (mTrack) {
        mTrack->setVolume(left, right);
    }
}

status_t MediaPlayerService::AudioOutput::setPlaybackRatePermille(int32_t ratePermille)
{
    ALOGV("setPlaybackRatePermille(%d)", ratePermille);
    status_t res = NO_ERROR;
    if (mTrack) {
        res = mTrack->setSampleRate(ratePermille * mSampleRateHz / 1000);
    } else {
        res = NO_INIT;
    }
    mPlaybackRatePermille = ratePermille;
    if (mSampleRateHz != 0) {
        mMsecsPerFrame = mPlaybackRatePermille / (float) mSampleRateHz;
    }
    return res;
}

status_t MediaPlayerService::AudioOutput::setAuxEffectSendLevel(float level)
{
    ALOGV("setAuxEffectSendLevel(%f)", level);
    mSendLevel = level;
    if (mTrack) {
        return mTrack->setAuxEffectSendLevel(level);
    }
    return NO_ERROR;
}

status_t MediaPlayerService::AudioOutput::attachAuxEffect(int effectId)
{
    ALOGV("attachAuxEffect(%d)", effectId);
    mAuxEffectId = effectId;
    if (mTrack) {
        return mTrack->attachAuxEffect(effectId);
    }
    return NO_ERROR;
}

// static
void MediaPlayerService::AudioOutput::CallbackWrapper(
        int event, void *cookie, void *info) {
    //ALOGV("callbackwrapper");
    if (event != AudioTrack::EVENT_MORE_DATA) {
        return;
    }

    CallbackData *data = (CallbackData*)cookie;
    data->lock();
    AudioOutput *me = data->getOutput();
    AudioTrack::Buffer *buffer = (AudioTrack::Buffer *)info;
    if (me == NULL) {
        // no output set, likely because the track was scheduled to be reused
        // by another player, but the format turned out to be incompatible.
        data->unlock();
        buffer->size = 0;
        return;
    }

    size_t actualSize = (*me->mCallback)(
            me, buffer->raw, buffer->size, me->mCallbackCookie);

    if (actualSize == 0 && buffer->size > 0 && me->mNextOutput == NULL) {
        // We've reached EOS but the audio track is not stopped yet,
        // keep playing silence.
        memset(buffer->raw, 0, buffer->size);
        actualSize = buffer->size;
    }

    buffer->size = actualSize;
    data->unlock();
}

int MediaPlayerService::AudioOutput::getSessionId() const
{
    return mSessionId;
}

#undef LOG_TAG
#define LOG_TAG "AudioCache"
MediaPlayerService::AudioCache::AudioCache(const char* name) :
    mChannelCount(0), mFrameCount(1024), mSampleRate(0), mSize(0),
    mError(NO_ERROR), mCommandComplete(false)
{
    // create ashmem heap
    mHeap = new MemoryHeapBase(kDefaultHeapSize, 0, name);
}

uint32_t MediaPlayerService::AudioCache::latency () const
{
    return 0;
}

float MediaPlayerService::AudioCache::msecsPerFrame() const
{
    return mMsecsPerFrame;
}

status_t MediaPlayerService::AudioCache::getPosition(uint32_t *position) const
{
    if (position == 0) return BAD_VALUE;
    *position = mSize;
    return NO_ERROR;
}

status_t MediaPlayerService::AudioCache::getFramesWritten(uint32_t *written) const
{
    if (written == 0) return BAD_VALUE;
    *written = mSize;
    return NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////

struct CallbackThread : public Thread {
    CallbackThread(const wp<MediaPlayerBase::AudioSink> &sink,
                   MediaPlayerBase::AudioSink::AudioCallback cb,
                   void *cookie);

protected:
    virtual ~CallbackThread();

    virtual bool threadLoop();

private:
    wp<MediaPlayerBase::AudioSink> mSink;
    MediaPlayerBase::AudioSink::AudioCallback mCallback;
    void *mCookie;
    void *mBuffer;
    size_t mBufferSize;

    CallbackThread(const CallbackThread &);
    CallbackThread &operator=(const CallbackThread &);
};

CallbackThread::CallbackThread(
        const wp<MediaPlayerBase::AudioSink> &sink,
        MediaPlayerBase::AudioSink::AudioCallback cb,
        void *cookie)
    : mSink(sink),
      mCallback(cb),
      mCookie(cookie),
      mBuffer(NULL),
      mBufferSize(0) {
}

CallbackThread::~CallbackThread() {
    if (mBuffer) {
        free(mBuffer);
        mBuffer = NULL;
    }
}

bool CallbackThread::threadLoop() {
    sp<MediaPlayerBase::AudioSink> sink = mSink.promote();
    if (sink == NULL) {
        return false;
    }

    if (mBuffer == NULL) {
        mBufferSize = sink->bufferSize();
        mBuffer = malloc(mBufferSize);
    }

    size_t actualSize =
        (*mCallback)(sink.get(), mBuffer, mBufferSize, mCookie);

    if (actualSize > 0) {
        sink->write(mBuffer, actualSize);
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////

status_t MediaPlayerService::AudioCache::open(
        uint32_t sampleRate, int channelCount, audio_channel_mask_t channelMask,
        audio_format_t format, int bufferCount,
        AudioCallback cb, void *cookie, audio_output_flags_t flags)
{
    ALOGV("open(%u, %d, 0x%x, %d, %d)", sampleRate, channelCount, channelMask, format, bufferCount);
#ifndef ANDROID_DEFAULT_CODE
    SXLOGD("open(%u, %d, 0x%x, %d, %d)", sampleRate, channelCount, channelMask, format, bufferCount);
#endif
    if (mHeap->getHeapID() < 0) {
        return NO_INIT;
    }

    mSampleRate = sampleRate;
    mChannelCount = (uint16_t)channelCount;
    mFormat = format;
    mMsecsPerFrame = 1.e3 / (float) sampleRate;

    if (cb != NULL) {
        mCallbackThread = new CallbackThread(this, cb, cookie);
    }
    return NO_ERROR;
}

void MediaPlayerService::AudioCache::start() {
    if (mCallbackThread != NULL) {
        mCallbackThread->run("AudioCache callback");
    }
}

void MediaPlayerService::AudioCache::stop() {
    if (mCallbackThread != NULL) {
        mCallbackThread->requestExitAndWait();
    }
}

ssize_t MediaPlayerService::AudioCache::write(const void* buffer, size_t size)
{
    ALOGV("write(%p, %u)", buffer, size);
    if ((buffer == 0) || (size == 0)) return size;

    uint8_t* p = static_cast<uint8_t*>(mHeap->getBase());
    if (p == NULL) return NO_INIT;
    p += mSize;
    ALOGV("memcpy(%p, %p, %u)", p, buffer, size);
    if (mSize + size > mHeap->getSize()) {
        ALOGE("Heap size overflow! req size: %d, max size: %d", (mSize + size), mHeap->getSize());
        size = mHeap->getSize() - mSize;
    }
    memcpy(p, buffer, size);
    mSize += size;
    return size;
}

// call with lock held
status_t MediaPlayerService::AudioCache::wait()
{
    Mutex::Autolock lock(mLock);
    while (!mCommandComplete) {
        mSignal.wait(mLock);
    }
    mCommandComplete = false;

    if (mError == NO_ERROR) {
        ALOGV("wait - success");
    } else {
        ALOGV("wait - error");
    }
    return mError;
}

void MediaPlayerService::AudioCache::notify(
        void* cookie, int msg, int ext1, int ext2, const Parcel *obj)
{
    ALOGV("notify(%p, %d, %d, %d)", cookie, msg, ext1, ext2);
#ifndef ANDROID_DEFAULT_CODE
    SXLOGD("notify(%p, %d, %d, %d)", cookie, msg, ext1, ext2);
#endif
    AudioCache* p = static_cast<AudioCache*>(cookie);

    // ignore buffering messages
    switch (msg)
    {
    case MEDIA_ERROR:
        ALOGE("Error %d, %d occurred", ext1, ext2);
        p->mError = ext1;
        break;
    case MEDIA_PREPARED:
        ALOGV("prepared");
        break;
    case MEDIA_PLAYBACK_COMPLETE:
        ALOGV("playback complete");
        break;
    default:
        ALOGV("ignored");
        return;
    }

    // wake up thread
    Mutex::Autolock lock(p->mLock);
    p->mCommandComplete = true;
    p->mSignal.signal();
}

int MediaPlayerService::AudioCache::getSessionId() const
{
    return 0;
}

void MediaPlayerService::addBatteryData(uint32_t params)
{
    Mutex::Autolock lock(mLock);

    int32_t time = systemTime() / 1000000L;

    // change audio output devices. This notification comes from AudioFlinger
    if ((params & kBatteryDataSpeakerOn)
            || (params & kBatteryDataOtherAudioDeviceOn)) {

        int deviceOn[NUM_AUDIO_DEVICES];
        for (int i = 0; i < NUM_AUDIO_DEVICES; i++) {
            deviceOn[i] = 0;
        }

        if ((params & kBatteryDataSpeakerOn)
                && (params & kBatteryDataOtherAudioDeviceOn)) {
            deviceOn[SPEAKER_AND_OTHER] = 1;
        } else if (params & kBatteryDataSpeakerOn) {
            deviceOn[SPEAKER] = 1;
        } else {
            deviceOn[OTHER_AUDIO_DEVICE] = 1;
        }

        for (int i = 0; i < NUM_AUDIO_DEVICES; i++) {
            if (mBatteryAudio.deviceOn[i] != deviceOn[i]){

                if (mBatteryAudio.refCount > 0) { // if playing audio
                    if (!deviceOn[i]) {
                        mBatteryAudio.lastTime[i] += time;
                        mBatteryAudio.totalTime[i] += mBatteryAudio.lastTime[i];
                        mBatteryAudio.lastTime[i] = 0;
                    } else {
                        mBatteryAudio.lastTime[i] = 0 - time;
                    }
                }

                mBatteryAudio.deviceOn[i] = deviceOn[i];
            }
        }
        return;
    }

    // an sudio stream is started
    if (params & kBatteryDataAudioFlingerStart) {
        // record the start time only if currently no other audio
        // is being played
        if (mBatteryAudio.refCount == 0) {
            for (int i = 0; i < NUM_AUDIO_DEVICES; i++) {
                if (mBatteryAudio.deviceOn[i]) {
                    mBatteryAudio.lastTime[i] -= time;
                }
            }
        }

        mBatteryAudio.refCount ++;
        return;

    } else if (params & kBatteryDataAudioFlingerStop) {
        if (mBatteryAudio.refCount <= 0) {
            ALOGW("Battery track warning: refCount is <= 0");
            return;
        }

        // record the stop time only if currently this is the only
        // audio being played
        if (mBatteryAudio.refCount == 1) {
            for (int i = 0; i < NUM_AUDIO_DEVICES; i++) {
                if (mBatteryAudio.deviceOn[i]) {
                    mBatteryAudio.lastTime[i] += time;
                    mBatteryAudio.totalTime[i] += mBatteryAudio.lastTime[i];
                    mBatteryAudio.lastTime[i] = 0;
                }
            }
        }

        mBatteryAudio.refCount --;
        return;
    }

    int uid = IPCThreadState::self()->getCallingUid();
    if (uid == AID_MEDIA) {
        return;
    }
    int index = mBatteryData.indexOfKey(uid);

    if (index < 0) { // create a new entry for this UID
        BatteryUsageInfo info;
        info.audioTotalTime = 0;
        info.videoTotalTime = 0;
        info.audioLastTime = 0;
        info.videoLastTime = 0;
        info.refCount = 0;

        if (mBatteryData.add(uid, info) == NO_MEMORY) {
            ALOGE("Battery track error: no memory for new app");
            return;
        }
    }

    BatteryUsageInfo &info = mBatteryData.editValueFor(uid);

    if (params & kBatteryDataCodecStarted) {
        if (params & kBatteryDataTrackAudio) {
            info.audioLastTime -= time;
            info.refCount ++;
        }
        if (params & kBatteryDataTrackVideo) {
            info.videoLastTime -= time;
            info.refCount ++;
        }
    } else {
        if (info.refCount == 0) {
            ALOGW("Battery track warning: refCount is already 0");
            return;
        } else if (info.refCount < 0) {
            ALOGE("Battery track error: refCount < 0");
            mBatteryData.removeItem(uid);
            return;
        }

        if (params & kBatteryDataTrackAudio) {
            info.audioLastTime += time;
            info.refCount --;
        }
        if (params & kBatteryDataTrackVideo) {
            info.videoLastTime += time;
            info.refCount --;
        }

        // no stream is being played by this UID
        if (info.refCount == 0) {
            info.audioTotalTime += info.audioLastTime;
            info.audioLastTime = 0;
            info.videoTotalTime += info.videoLastTime;
            info.videoLastTime = 0;
        }
    }
}

status_t MediaPlayerService::pullBatteryData(Parcel* reply) {
    Mutex::Autolock lock(mLock);

    // audio output devices usage
    int32_t time = systemTime() / 1000000L; //in ms
    int32_t totalTime;

    for (int i = 0; i < NUM_AUDIO_DEVICES; i++) {
        totalTime = mBatteryAudio.totalTime[i];

        if (mBatteryAudio.deviceOn[i]
            && (mBatteryAudio.lastTime[i] != 0)) {
                int32_t tmpTime = mBatteryAudio.lastTime[i] + time;
                totalTime += tmpTime;
        }

        reply->writeInt32(totalTime);
        // reset the total time
        mBatteryAudio.totalTime[i] = 0;
   }

    // codec usage
    BatteryUsageInfo info;
    int size = mBatteryData.size();

    reply->writeInt32(size);
    int i = 0;

    while (i < size) {
        info = mBatteryData.valueAt(i);

        reply->writeInt32(mBatteryData.keyAt(i)); //UID
        reply->writeInt32(info.audioTotalTime);
        reply->writeInt32(info.videoTotalTime);

        info.audioTotalTime = 0;
        info.videoTotalTime = 0;

        // remove the UID entry where no stream is being played
        if (info.refCount <= 0) {
            mBatteryData.removeItemsAt(i);
            size --;
            i --;
        }
        i++;
    }
    return NO_ERROR;
}
} // namespace android
