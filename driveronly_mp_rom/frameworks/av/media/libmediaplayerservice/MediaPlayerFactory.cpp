/*
**
** Copyright 2012, The Android Open Source Project
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

#define LOG_TAG "MediaPlayerFactory"
#include <utils/Log.h>

#include <cutils/properties.h>
#include <media/IMediaPlayer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <utils/Errors.h>
#include <utils/misc.h>

#include "MediaPlayerFactory.h"

#include "MidiFile.h"
#include "TestPlayerStub.h"
#include "StagefrightPlayer.h"
#include "nuplayer/NuPlayerDriver.h"

#ifndef ANDROID_DEFAULT_CODE
#ifdef MTK_MATV_ENABLE
#include "mATVAudioPlayer.h"
#endif
#ifdef MTK_FM_ENABLE
#include "FMAudioPlayer.h"
#endif
#ifdef MTK_DRM_APP
#include <drm/DrmManagerClient.h> // OMA DRM v1 implementation
#include <drm/DrmMtkUtil.h>
#endif

#include <MtkRTSPController.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/FileSource.h>
#endif

namespace android {

Mutex MediaPlayerFactory::sLock;
MediaPlayerFactory::tFactoryMap MediaPlayerFactory::sFactoryMap;
bool MediaPlayerFactory::sInitComplete = false;

status_t MediaPlayerFactory::registerFactory_l(IFactory* factory,
                                               player_type type) {
    if (NULL == factory) {
        ALOGE("Failed to register MediaPlayerFactory of type %d, factory is"
              " NULL.", type);
        return BAD_VALUE;
    }

    if (sFactoryMap.indexOfKey(type) >= 0) {
        ALOGE("Failed to register MediaPlayerFactory of type %d, type is"
              " already registered.", type);
        return ALREADY_EXISTS;
    }

    if (sFactoryMap.add(type, factory) < 0) {
        ALOGE("Failed to register MediaPlayerFactory of type %d, failed to add"
              " to map.", type);
        return UNKNOWN_ERROR;
    }

    return OK;
}

player_type MediaPlayerFactory::getDefaultPlayerType() {
    char value[PROPERTY_VALUE_MAX];
    if (property_get("media.stagefright.use-nuplayer", value, NULL)
            && (!strcmp("1", value) || !strcasecmp("true", value))) {
        return NU_PLAYER;
    }

    return STAGEFRIGHT_PLAYER;
}

status_t MediaPlayerFactory::registerFactory(IFactory* factory,
                                             player_type type) {
    Mutex::Autolock lock_(&sLock);
    return registerFactory_l(factory, type);
}

void MediaPlayerFactory::unregisterFactory(player_type type) {
    Mutex::Autolock lock_(&sLock);
    sFactoryMap.removeItem(type);
}

#define GET_PLAYER_TYPE_IMPL(a...)                      \
    Mutex::Autolock lock_(&sLock);                      \
                                                        \
    player_type ret = STAGEFRIGHT_PLAYER;               \
    float bestScore = 0.0;                              \
                                                        \
    for (size_t i = 0; i < sFactoryMap.size(); ++i) {   \
                                                        \
        IFactory* v = sFactoryMap.valueAt(i);           \
        float thisScore;                                \
        CHECK(v != NULL);                               \
        thisScore = v->scoreFactory(a, bestScore);      \
        if (thisScore > bestScore) {                    \
            ret = sFactoryMap.keyAt(i);                 \
            bestScore = thisScore;                      \
        }                                               \
    }                                                   \
                                                        \
    if (0.0 == bestScore) {                             \
        bestScore = getDefaultPlayerType();             \
    }                                                   \
                                                        \
    return ret;

player_type MediaPlayerFactory::getPlayerType(const sp<IMediaPlayer>& client,
                                              const char* url) {
    GET_PLAYER_TYPE_IMPL(client, url);
}

player_type MediaPlayerFactory::getPlayerType(const sp<IMediaPlayer>& client,
                                              int fd,
                                              int64_t offset,
                                              int64_t length) {
    GET_PLAYER_TYPE_IMPL(client, fd, offset, length);
}

player_type MediaPlayerFactory::getPlayerType(const sp<IMediaPlayer>& client,
                                              const sp<IStreamSource> &source) {
    GET_PLAYER_TYPE_IMPL(client, source);
}

#undef GET_PLAYER_TYPE_IMPL

sp<MediaPlayerBase> MediaPlayerFactory::createPlayer(
        player_type playerType,
        void* cookie,
        notify_callback_f notifyFunc) {
    sp<MediaPlayerBase> p;
    IFactory* factory;
    status_t init_result;
    Mutex::Autolock lock_(&sLock);

    if (sFactoryMap.indexOfKey(playerType) < 0) {
        ALOGE("Failed to create player object of type %d, no registered"
              " factory", playerType);
        return p;
    }

    factory = sFactoryMap.valueFor(playerType);
    CHECK(NULL != factory);
    p = factory->createPlayer();

    if (p == NULL) {
        ALOGE("Failed to create player object of type %d, create failed",
               playerType);
        return p;
    }

    init_result = p->initCheck();
    if (init_result == NO_ERROR) {
        p->setNotifyCallback(cookie, notifyFunc);
    } else {
        ALOGE("Failed to create player object of type %d, initCheck failed"
              " (res = %d)", playerType, init_result);
        p.clear();
    }

    return p;
}

/*****************************************************************************
 *                                                                           *
 *                     Built-In Factory Implementations                      *
 *                                                                           *
 *****************************************************************************/

class StagefrightPlayerFactory :
    public MediaPlayerFactory::IFactory {
  public:
    virtual float scoreFactory(const sp<IMediaPlayer>& client,
                               int fd,
                               int64_t offset,
                               int64_t length,
                               float curScore) {
        char buf[20];
        lseek(fd, offset, SEEK_SET);
        read(fd, buf, sizeof(buf));
        lseek(fd, offset, SEEK_SET);

        long ident = *((long*)buf);

        // Ogg vorbis?
        if (ident == 0x5367674f) // 'OggS'
            return 1.0;

        return 0.0;
    }

    virtual sp<MediaPlayerBase> createPlayer() {
        ALOGV(" create StagefrightPlayer");
        return new StagefrightPlayer();
    }
};

class NuPlayerFactory : public MediaPlayerFactory::IFactory {
  public:
    virtual float scoreFactory(const sp<IMediaPlayer>& client,
                               const char* url,
                               float curScore) {
        static const float kOurScore = 0.8;

        if (kOurScore <= curScore)
            return 0.0;

        if (!strncasecmp("http://", url, 7)
                || !strncasecmp("https://", url, 8)) {
            size_t len = strlen(url);
            if (len >= 5 && !strcasecmp(".m3u8", &url[len - 5])) {
                return kOurScore;
            }

            if (strstr(url,"m3u8")) {
                return kOurScore;
            }
#ifndef ANDROID_DEFAULT_CODE
	    char value[PROPERTY_VALUE_MAX];
	    if (property_get("mtk.streaming.oldrtsp", value, NULL) &&
			    !strcmp("1", value) || !strcasecmp("true", value)) {}
	    else {
/*		    ALOGD("Before connect");
		    sp<DataSource> dataSource = DataSource::CreateFromURI(url, NULL); //can't stop during connecting 
		    ALOGD("After connect");
		    //CHECK(dataSource != NULL);
		    if(dataSource == NULL){
			    //client->notify(MEDIA_ERROR, MEDIA_ERROR_UNKNOWN,ERROR_CANNOT_CONNECT);
			    //(MediaPlayerService::Client*)(client.get())->notify(client,MEDIA_ERROR, MEDIA_ERROR_UNKNOWN,ERROR_CANNOT_CONNECT);
			    ALOGE("connect fail");
			    return 0.0; //need other message?
		    }

		    String8 mimeType;
		    float confidence = 0.0;
		    bool ret=false;
		    ALOGD("Before sniff sdp");
		    ret = SniffSDP(dataSource,&mimeType,&confidence,NULL);
		    ALOGD("After sniff sdp");
		    if(ret){
			    const char* mime = mimeType.string();
			    ALOGI("is sdp,mime=%s",mime);
			    if (!strcasecmp(mime, MEDIA_MIMETYPE_APPLICATION_SDP)) {
				    return kOurScore;
			    }
		    }*/
		
		if (len >= 5 && !strncasecmp(".smil", &url[len - 5], 5))
			return kOurScore;
		if (len >= 4 && !strncasecmp(".sdp", &url[len - 4], 4))
			return kOurScore;
		if (strstr(url, ".sdp?"))
			return kOurScore;
	    }
#endif
	}
#ifndef ANDROID_DEFAULT_CODE
	char value[PROPERTY_VALUE_MAX];
	if (property_get("mtk.streaming.oldrtsp", value, NULL) &&
			!strcmp("1", value) || !strcasecmp("true", value)) {}
	else if (!strncasecmp("rtsp://", url, 7)) {
		return kOurScore;
	}
#else
	if (!strncasecmp("rtsp://", url, 7)) {
		return kOurScore;
	}
#endif
	return 0.0;
    }

    virtual float scoreFactory(const sp<IMediaPlayer>& client,
                               const sp<IStreamSource> &source,
                               float curScore) {
        return 1.0;
    }
#ifndef ANDROID_DEFAULT_CODE
virtual float scoreFactory(const sp<IMediaPlayer>& client,
                               int fd,
                               int64_t offset,
                               int64_t length,
                               float curScore) {

	static const float kOurScore = 0.8; 

	if (kOurScore <= curScore)
		return 0.0;

	char value[PROPERTY_VALUE_MAX];
	if (property_get("mtk.streaming.oldrtsp", value, NULL) &&
			!strcmp("1", value) || !strcasecmp("true", value)) {}
	else {
		sp<DataSource> dataSource = new FileSource(dup(fd), offset, length);
		if(!dataSource.get()){
			ALOGE("FileSource create fail");
        		return 0.0;
		}
		String8 mimeType;
		float confidence = 0.0;
		bool ret=false;
		ALOGD("Before sniff local sdp");
		ret = SniffSDP(dataSource,&mimeType,&confidence,NULL);
		ALOGD("After sniff local sdp");
		if(ret){
			const char* mime = mimeType.string();
			ALOGI("is sdp,mime=%s",mime);
			if (!strcasecmp(mime, MEDIA_MIMETYPE_APPLICATION_SDP)) {
				return kOurScore;
			}
		}
	}
        return 0.0;
    }

#endif
    virtual sp<MediaPlayerBase> createPlayer() {
        ALOGV(" create NuPlayer");
        return new NuPlayerDriver;
    }
};

class SonivoxPlayerFactory : public MediaPlayerFactory::IFactory {
  public:
    virtual float scoreFactory(const sp<IMediaPlayer>& client,
                               const char* url,
                               float curScore) {
        static const float kOurScore = 0.4;
        static const char* const FILE_EXTS[] = { ".mid",
                                                 ".midi",
                                                 ".smf",
                                                 ".xmf",
                                                 ".mxmf",
                                                 ".imy",
                                                 ".rtttl",
                                                 ".rtx",
                                                 ".ota"};
        if (kOurScore <= curScore)
            return 0.0;

        // use MidiFile for MIDI extensions
        int lenURL = strlen(url);
        for (int i = 0; i < NELEM(FILE_EXTS); ++i) {
            int len = strlen(FILE_EXTS[i]);
            int start = lenURL - len;
            if (start > 0) {
                if (!strncasecmp(url + start, FILE_EXTS[i], len)) {
                    return kOurScore;
                }
            }
        }
#ifndef ANDROID_DEFAULT_CODE
#ifdef MTK_DRM_APP
    // the url shall be a file path type.
    ALOGD("getPlayerType(): using url, check for DRM protected midi.");
    String8 path(url);
    String8 dcf_mime = DrmMtkUtil::getDcfMime(path);
    if (!dcf_mime.isEmpty()) {
        ALOGD("getPlayerType(): dcf file mime [%s]", dcf_mime.string());
        // if it's a OMA DRM v1 .dcf file
        // the original mime type can be retrieved successfully.
            String8 m = DrmMtkUtil::toCommonMime(dcf_mime.string());
            if (0 == strcasecmp(m.string(), "audio/midi")
                || 0 == strcasecmp(m.string(), "audio/sp-midi")
                || 0 == strcasecmp(m.string(), "audio/imelody")) {
                return kOurScore; // sonivox case
            } else {
                return 0.0;
            }
    }
#endif
#endif
        return 0.0;
    }

    virtual float scoreFactory(const sp<IMediaPlayer>& client,
                               int fd,
                               int64_t offset,
                               int64_t length,
                               float curScore) {
        static const float kOurScore = 0.8;

        if (kOurScore <= curScore)
            return 0.0;

        // Some kind of MIDI?
        EAS_DATA_HANDLE easdata;
        if (EAS_Init(&easdata) == EAS_SUCCESS) {
            EAS_FILE locator;
            locator.path = NULL;
            locator.fd = fd;
            locator.offset = offset;
            locator.length = length;
            EAS_HANDLE  eashandle;
            if (EAS_OpenFile(easdata, &locator, &eashandle) == EAS_SUCCESS) {
                EAS_CloseFile(easdata, eashandle);
                EAS_Shutdown(easdata);
                return kOurScore;
            }
            EAS_Shutdown(easdata);
        }
#ifndef ANDROID_DEFAULT_CODE
#ifdef MTK_DRM_APP
    ALOGD("getPlayerType(): using fd, check for DRM protected midi.");
    String8 dcf_mime = DrmMtkUtil::getDcfMime(fd);
    if (!dcf_mime.isEmpty()) {
        ALOGD("getPlayerType(): dcf file mime [%s]", dcf_mime.string());
        // if it's a OMA DRM v1 .dcf file
        // the original mime type can be retrieved successfully.
        String8 m = DrmMtkUtil::toCommonMime(dcf_mime.string());
        if (0 == strcasecmp(m.string(), "audio/midi")
            || 0 == strcasecmp(m.string(), "audio/sp-midi")
            || 0 == strcasecmp(m.string(), "audio/imelody")) {
            return kOurScore; // sonivox case
        } else {
             return 0.0;
        }
    }
#endif
#endif  
        return 0.0;
    }

    virtual sp<MediaPlayerBase> createPlayer() {
        ALOGV(" create MidiFile");
        return new MidiFile();
    }
};

class TestPlayerFactory : public MediaPlayerFactory::IFactory {
  public:
    virtual float scoreFactory(const sp<IMediaPlayer>& client,
                               const char* url,
                               float curScore) {
        if (TestPlayerStub::canBeUsed(url)) {
            return 1.0;
        }

        return 0.0;
    }

    virtual sp<MediaPlayerBase> createPlayer() {
        ALOGV("Create Test Player stub");
        return new TestPlayerStub();
    }
};
#ifndef ANDROID_DEFAULT_CODE
class FMPlayerFactory : public MediaPlayerFactory::IFactory {
  public:
    virtual float scoreFactory(const sp<IMediaPlayer>& client,
                               const char* url,
                               float curScore) {
        if(strncmp(url, "MEDIATEK://MEDIAPLAYER_PLAYERTYPE_FM", 36) == 0)
           return 1.0;
        return 0.0;
    }

    virtual sp<MediaPlayerBase> createPlayer() {
#ifdef MTK_FM_ENABLE
        ALOGV("Create FM Player");
        return new FMAudioPlayer();
#endif
        return NULL;
    }
};
class ATVPlayerFactory : public MediaPlayerFactory::IFactory {
  public:
    virtual float scoreFactory(const sp<IMediaPlayer>& client,
                               const char* url,
                               float curScore) {
        if(strncmp(url, "MEDIATEK://MEDIAPLAYER_PLAYERTYPE_MATV", 38) == 0)
           return 1.0;
        return 0.0;
    }

    virtual sp<MediaPlayerBase> createPlayer() {
#ifdef MTK_MATV_ENABLE 
        ALOGV("Create ATV Player");
        return new mATVAudioPlayer();
#endif
        return NULL;
    }
};
#ifdef MTK_CMMB_ENABLE
class CMMBPlayerFactory : public MediaPlayerFactory::IFactory {
  public:
    virtual float scoreFactory(const sp<IMediaPlayer>& client,
                               const char* url,
                               float curScore) {
        if(strncmp(url, "CMMB", 4) == 0)
           return 1.0;
        return 0.0;
    }

    virtual sp<MediaPlayerBase> createPlayer() {
        ALOGV("Create CMMB Player");
        return new StagefrightPlayer();
    }
};
#endif
#endif

void MediaPlayerFactory::registerBuiltinFactories() {
    Mutex::Autolock lock_(&sLock);

    if (sInitComplete)
        return;

    registerFactory_l(new StagefrightPlayerFactory(), STAGEFRIGHT_PLAYER);
    registerFactory_l(new NuPlayerFactory(), NU_PLAYER);
    registerFactory_l(new SonivoxPlayerFactory(), SONIVOX_PLAYER);
    registerFactory_l(new TestPlayerFactory(), TEST_PLAYER);
#ifndef ANDROID_DEFAULT_CODE
    registerFactory_l(new FMPlayerFactory(), FM_AUDIO_PLAYER);
    registerFactory_l(new ATVPlayerFactory(), MATV_AUDIO_PLAYER);
#ifdef MTK_CMMB_ENABLE
    registerFactory_l(new CMMBPlayerFactory(), CMMB_PLAYER);
#endif
#endif
    sInitComplete = true;
}

}  // namespace android
