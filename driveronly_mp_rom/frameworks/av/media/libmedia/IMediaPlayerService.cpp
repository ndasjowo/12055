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

#include <stdint.h>
#include <sys/types.h>

#include <binder/Parcel.h>
#include <binder/IMemory.h>
#include <media/ICrypto.h>
#include <media/IHDCP.h>
#include <media/IMediaPlayerService.h>
#include <media/IMediaRecorder.h>
#include <media/IOMX.h>
#include <media/IRemoteDisplay.h>
#include <media/IRemoteDisplayClient.h>
#include <media/IStreamSource.h>

#include <utils/Errors.h>  // for status_t
#include <utils/String8.h>

namespace android {

enum {
    CREATE = IBinder::FIRST_CALL_TRANSACTION,
    DECODE_URL,
    DECODE_FD,
    CREATE_MEDIA_RECORDER,
    CREATE_METADATA_RETRIEVER,
    GET_OMX,
    MAKE_CRYPTO,
    MAKE_HDCP,
    ADD_BATTERY_DATA,
    PULL_BATTERY_DATA,
    LISTEN_FOR_REMOTE_DISPLAY,
#ifndef ANDROID_DEFAULT_CODE
    ENABLE_REMOTE_DISPLAY,
    LISTEN_FOR_FAST_REMOTE_DISPLAY,
    ENABLE_FAST_REMOTE_DISPLAY,
    SEND_GENERIC_MSG,
    SET_BITRATE_CONTROL
#endif
};

class BpMediaPlayerService: public BpInterface<IMediaPlayerService>
{
public:
    BpMediaPlayerService(const sp<IBinder>& impl)
        : BpInterface<IMediaPlayerService>(impl)
    {
    }

    virtual sp<IMediaMetadataRetriever> createMetadataRetriever(pid_t pid)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IMediaPlayerService::getInterfaceDescriptor());
        data.writeInt32(pid);
        remote()->transact(CREATE_METADATA_RETRIEVER, data, &reply);
        return interface_cast<IMediaMetadataRetriever>(reply.readStrongBinder());
    }

    virtual sp<IMediaPlayer> create(
            pid_t pid, const sp<IMediaPlayerClient>& client, int audioSessionId) {
        Parcel data, reply;
        data.writeInterfaceToken(IMediaPlayerService::getInterfaceDescriptor());
        data.writeInt32(pid);
        data.writeStrongBinder(client->asBinder());
        data.writeInt32(audioSessionId);

        remote()->transact(CREATE, data, &reply);
        return interface_cast<IMediaPlayer>(reply.readStrongBinder());
    }

    virtual sp<IMediaRecorder> createMediaRecorder(pid_t pid)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IMediaPlayerService::getInterfaceDescriptor());
        data.writeInt32(pid);
        remote()->transact(CREATE_MEDIA_RECORDER, data, &reply);
        return interface_cast<IMediaRecorder>(reply.readStrongBinder());
    }

    virtual sp<IMemory> decode(const char* url, uint32_t *pSampleRate, int* pNumChannels, audio_format_t* pFormat)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IMediaPlayerService::getInterfaceDescriptor());
        data.writeCString(url);
        remote()->transact(DECODE_URL, data, &reply);
        *pSampleRate = uint32_t(reply.readInt32());
        *pNumChannels = reply.readInt32();
        *pFormat = (audio_format_t) reply.readInt32();
        return interface_cast<IMemory>(reply.readStrongBinder());
    }

    virtual sp<IMemory> decode(int fd, int64_t offset, int64_t length, uint32_t *pSampleRate, int* pNumChannels, audio_format_t* pFormat)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IMediaPlayerService::getInterfaceDescriptor());
        data.writeFileDescriptor(fd);
        data.writeInt64(offset);
        data.writeInt64(length);
        remote()->transact(DECODE_FD, data, &reply);
        *pSampleRate = uint32_t(reply.readInt32());
        *pNumChannels = reply.readInt32();
        *pFormat = (audio_format_t) reply.readInt32();
        return interface_cast<IMemory>(reply.readStrongBinder());
    }

    virtual sp<IOMX> getOMX() {
        Parcel data, reply;
        data.writeInterfaceToken(IMediaPlayerService::getInterfaceDescriptor());
        remote()->transact(GET_OMX, data, &reply);
        return interface_cast<IOMX>(reply.readStrongBinder());
    }

    virtual sp<ICrypto> makeCrypto() {
        Parcel data, reply;
        data.writeInterfaceToken(IMediaPlayerService::getInterfaceDescriptor());
        remote()->transact(MAKE_CRYPTO, data, &reply);
        return interface_cast<ICrypto>(reply.readStrongBinder());
    }

#ifndef ANDROID_DEFAULT_CODE
    virtual status_t enableRemoteDisplay(const char *iface) {
        Parcel data, reply;
        data.writeInterfaceToken(IMediaPlayerService::getInterfaceDescriptor());

        if (iface != NULL) {
            data.writeInt32(1);
            data.writeCString(iface);
        } else {
            data.writeInt32(0);
        }

        remote()->transact(ENABLE_REMOTE_DISPLAY, data, &reply);
        return reply.readInt32();
    }
#endif

    virtual sp<IHDCP> makeHDCP() {
        Parcel data, reply;
        data.writeInterfaceToken(IMediaPlayerService::getInterfaceDescriptor());
        remote()->transact(MAKE_HDCP, data, &reply);
        return interface_cast<IHDCP>(reply.readStrongBinder());
    }


    virtual void addBatteryData(uint32_t params) {
        Parcel data, reply;
        data.writeInterfaceToken(IMediaPlayerService::getInterfaceDescriptor());
        data.writeInt32(params);
        remote()->transact(ADD_BATTERY_DATA, data, &reply);
    }

    virtual status_t pullBatteryData(Parcel* reply) {
        Parcel data;
        data.writeInterfaceToken(IMediaPlayerService::getInterfaceDescriptor());
        return remote()->transact(PULL_BATTERY_DATA, data, reply);
    }

    virtual sp<IRemoteDisplay> listenForRemoteDisplay(const sp<IRemoteDisplayClient>& client,
            const String8& iface)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IMediaPlayerService::getInterfaceDescriptor());
        data.writeStrongBinder(client->asBinder());
        data.writeString8(iface);
        remote()->transact(LISTEN_FOR_REMOTE_DISPLAY, data, &reply);
        return interface_cast<IRemoteDisplay>(reply.readStrongBinder());
    }
    
#ifndef ANDROID_DEFAULT_CODE
///M: MTK WFD added on feature @{
    virtual sp<IRemoteDisplay> listenForRemoteDisplay(const sp<IRemoteDisplayClient>& client,
            const String8& iface, const uint32_t wfdFlags)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IMediaPlayerService::getInterfaceDescriptor());
        data.writeStrongBinder(client->asBinder());
        data.writeString8(iface);
        data.writeInt32(wfdFlags);
        
        remote()->transact(LISTEN_FOR_FAST_REMOTE_DISPLAY, data, &reply);
        return interface_cast<IRemoteDisplay>(reply.readStrongBinder());
    }


    virtual status_t enableRemoteDisplay(const char *iface, const uint32_t wfdFlags) {
        Parcel data, reply;
        data.writeInterfaceToken(IMediaPlayerService::getInterfaceDescriptor());


        if (iface != NULL) {            
            data.writeInt32(1);
            data.writeCString(iface);
        }else{
            data.writeInt32(0);
        }
        data.writeInt32(wfdFlags);

        remote()->transact(ENABLE_FAST_REMOTE_DISPLAY, data, &reply);
        return reply.readInt32();
    }
    
    virtual status_t sendGenericMsg(uint32_t cmd){
        Parcel data, reply;
        data.writeInterfaceToken(IMediaPlayerService::getInterfaceDescriptor());

        data.writeInt32(cmd);
        
        remote()->transact(SEND_GENERIC_MSG, data, &reply);
        return reply.readInt32();
    }

    virtual status_t setBitrateControl(uint32_t level){
        Parcel data, reply;
        data.writeInterfaceToken(IMediaPlayerService::getInterfaceDescriptor());

        data.writeInt32(level);
        
        remote()->transact(SET_BITRATE_CONTROL, data, &reply);
        return reply.readInt32();
    }

/// @}
#endif
};

IMPLEMENT_META_INTERFACE(MediaPlayerService, "android.media.IMediaPlayerService");

// ----------------------------------------------------------------------

status_t BnMediaPlayerService::onTransact(
    uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
{
    switch (code) {
        case CREATE: {
            CHECK_INTERFACE(IMediaPlayerService, data, reply);
            pid_t pid = data.readInt32();
            sp<IMediaPlayerClient> client =
                interface_cast<IMediaPlayerClient>(data.readStrongBinder());
            int audioSessionId = data.readInt32();
            sp<IMediaPlayer> player = create(pid, client, audioSessionId);
            reply->writeStrongBinder(player->asBinder());
            return NO_ERROR;
        } break;
        case DECODE_URL: {
            CHECK_INTERFACE(IMediaPlayerService, data, reply);
            const char* url = data.readCString();
            uint32_t sampleRate;
            int numChannels;
            audio_format_t format;
            sp<IMemory> player = decode(url, &sampleRate, &numChannels, &format);
            reply->writeInt32(sampleRate);
            reply->writeInt32(numChannels);
            reply->writeInt32((int32_t) format);
            reply->writeStrongBinder(player->asBinder());
            return NO_ERROR;
        } break;
        case DECODE_FD: {
            CHECK_INTERFACE(IMediaPlayerService, data, reply);
            int fd = dup(data.readFileDescriptor());
            int64_t offset = data.readInt64();
            int64_t length = data.readInt64();
            uint32_t sampleRate;
            int numChannels;
            audio_format_t format;
            sp<IMemory> player = decode(fd, offset, length, &sampleRate, &numChannels, &format);
            reply->writeInt32(sampleRate);
            reply->writeInt32(numChannels);
            reply->writeInt32((int32_t) format);
            reply->writeStrongBinder(player->asBinder());
            return NO_ERROR;
        } break;
        case CREATE_MEDIA_RECORDER: {
            CHECK_INTERFACE(IMediaPlayerService, data, reply);
            pid_t pid = data.readInt32();
            sp<IMediaRecorder> recorder = createMediaRecorder(pid);
            reply->writeStrongBinder(recorder->asBinder());
            return NO_ERROR;
        } break;
        case CREATE_METADATA_RETRIEVER: {
            CHECK_INTERFACE(IMediaPlayerService, data, reply);
            pid_t pid = data.readInt32();
            sp<IMediaMetadataRetriever> retriever = createMetadataRetriever(pid);
            reply->writeStrongBinder(retriever->asBinder());
            return NO_ERROR;
        } break;
        case GET_OMX: {
            CHECK_INTERFACE(IMediaPlayerService, data, reply);
            sp<IOMX> omx = getOMX();
            reply->writeStrongBinder(omx->asBinder());
            return NO_ERROR;
        } break;
        case MAKE_CRYPTO: {
            CHECK_INTERFACE(IMediaPlayerService, data, reply);
            sp<ICrypto> crypto = makeCrypto();
            reply->writeStrongBinder(crypto->asBinder());
            return NO_ERROR;
        } break;
        case MAKE_HDCP: {
            CHECK_INTERFACE(IMediaPlayerService, data, reply);
            sp<IHDCP> hdcp = makeHDCP();
            reply->writeStrongBinder(hdcp->asBinder());
            return NO_ERROR;
        } break;
        case ADD_BATTERY_DATA: {
            CHECK_INTERFACE(IMediaPlayerService, data, reply);
            uint32_t params = data.readInt32();
            addBatteryData(params);
            return NO_ERROR;
        } break;
        case PULL_BATTERY_DATA: {
            CHECK_INTERFACE(IMediaPlayerService, data, reply);
            pullBatteryData(reply);
            return NO_ERROR;
        } break;
        case LISTEN_FOR_REMOTE_DISPLAY: {
            CHECK_INTERFACE(IMediaPlayerService, data, reply);
            sp<IRemoteDisplayClient> client(
                    interface_cast<IRemoteDisplayClient>(data.readStrongBinder()));
            String8 iface(data.readString8());
            sp<IRemoteDisplay> display(listenForRemoteDisplay(client, iface));
            reply->writeStrongBinder(display->asBinder());
            return NO_ERROR;
        } break;
#ifndef ANDROID_DEFAULT_CODE
        case ENABLE_REMOTE_DISPLAY: {
            CHECK_INTERFACE(IMediaPlayerService, data, reply);
            const char *iface = NULL;
            if (data.readInt32()) {
                iface = data.readCString();
            }
            reply->writeInt32(enableRemoteDisplay(iface));
            return NO_ERROR;
        } break;
        case LISTEN_FOR_FAST_REMOTE_DISPLAY: {
            CHECK_INTERFACE(IMediaPlayerService, data, reply);
            sp<IRemoteDisplayClient> client(
                    interface_cast<IRemoteDisplayClient>(data.readStrongBinder()));
            String8 iface(data.readString8());
            const uint32_t wfdFlags = data.readInt32();
            sp<IRemoteDisplay> display(listenForRemoteDisplay(client, iface, wfdFlags));
            reply->writeStrongBinder(display->asBinder());
            return NO_ERROR;
        } break;            
        case ENABLE_FAST_REMOTE_DISPLAY: {
            CHECK_INTERFACE(IMediaPlayerService, data, reply);
            const char *iface = NULL;

            if (data.readInt32()) {
                iface = data.readCString();
            }            
            const uint32_t wfdFlags = data.readInt32();
            
            reply->writeInt32(enableRemoteDisplay(iface, wfdFlags));
            return NO_ERROR;
        } break;
        case SEND_GENERIC_MSG: {
            CHECK_INTERFACE(IMediaPlayerService, data, reply);
            const uint32_t cmdId = data.readInt32();
            reply->writeInt32(sendGenericMsg(cmdId));
            return NO_ERROR; 
        } break;
        case SET_BITRATE_CONTROL: {
            CHECK_INTERFACE(IMediaPlayerService, data, reply);
            const uint32_t level = data.readInt32();
            reply->writeInt32(setBitrateControl(level));
            return NO_ERROR;
        } break;
#endif
        default:
            return BBinder::onTransact(code, data, reply, flags);
    }
}

// ----------------------------------------------------------------------------

}; // namespace android
