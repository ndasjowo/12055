/*
**
** Copyright 2007, The Android Open Source Project
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

#define LOG_TAG "IAudioFlinger"
//#define LOG_NDEBUG 0
#include <utils/Log.h>

#include <stdint.h>
#include <sys/types.h>

#include <binder/Parcel.h>

#include <media/IAudioFlinger.h>

namespace android {

enum {
    CREATE_TRACK = IBinder::FIRST_CALL_TRANSACTION,
    OPEN_RECORD,
    SAMPLE_RATE,
    CHANNEL_COUNT,
    FORMAT,
    FRAME_COUNT,
    LATENCY,
    SET_MASTER_VOLUME,
    SET_MASTER_MUTE,
    MASTER_VOLUME,
    MASTER_MUTE,
    SET_STREAM_VOLUME,
    SET_STREAM_MUTE,
    STREAM_VOLUME,
    STREAM_MUTE,
    SET_MODE,

    //#ifdef VENDOR_EDIT
    //OppoHook
	//property=android.annotation.OppoHook.OppoRomType.OPPO
    //Chenps ZhiYong.Lin@Plf.Framework modify for BPM 2013/01/21
    GET_AUDIOTRACK_PIDS,
    //#endif /* VENDOR_EDIT */
    
    SET_MIC_MUTE,
    GET_MIC_MUTE,
    SET_PARAMETERS,
    GET_PARAMETERS,
    REGISTER_CLIENT,
    GET_INPUTBUFFERSIZE,
    OPEN_OUTPUT,
    OPEN_DUPLICATE_OUTPUT,
    CLOSE_OUTPUT,
    SUSPEND_OUTPUT,
    RESTORE_OUTPUT,
    OPEN_INPUT,
    CLOSE_INPUT,
    SET_STREAM_OUTPUT,
    SET_VOICE_VOLUME,
    GET_RENDER_POSITION,
    GET_INPUT_FRAMES_LOST,
    NEW_AUDIO_SESSION_ID,
    ACQUIRE_AUDIO_SESSION_ID,
    RELEASE_AUDIO_SESSION_ID,
    QUERY_NUM_EFFECTS,
    QUERY_EFFECT,
    GET_EFFECT_DESCRIPTOR,
    CREATE_EFFECT,
    MOVE_EFFECTS,
    LOAD_HW_MODULE,
    GET_PRIMARY_OUTPUT_SAMPLING_RATE,
    GET_PRIMARY_OUTPUT_FRAME_COUNT,
#ifdef MTK_AUDIO
    GET_EM_PARAMETER,
    SET_EM_PARAMETER,
    SET_AUDIO_COMMAND,
    GET_AUDIO_COMMAND,
    SET_AUDIO_DATA,
    GET_AUDIO_DATA,
    SET_ACF_PREVIEW_PARAMETER,
    SET_HCF_PREVIEW_PARAMETER,
    PCM_PLAY_START,
    PCM_PLAY_STOP,
    PCM_PLAY_WEITE,
    PCM_PLAY_GET_FREE_BUFFER_SIZE,
    PCM_RECORD_START,
    PCM_RECORD_STOP,
    PCM_RECORD_READ,
    Read_Ref_FromRing,
    Get_Voice_Unlock_ULTime,
    Set_Voice_Unlock_SRC,
    start_Voice_Unlock_DL,
    stop_Voice_Unlock_DL,
    free_Voice_Unlock_DL_Instance,
    get_Voice_Unlock_DL_Instance,
    Get_Voice_Unlock_DL_Latency
#endif
};

class BpAudioFlinger : public BpInterface<IAudioFlinger>
{
public:
    BpAudioFlinger(const sp<IBinder>& impl)
        : BpInterface<IAudioFlinger>(impl)
    {
    }

    virtual sp<IAudioTrack> createTrack(
                                pid_t pid,
                                audio_stream_type_t streamType,
                                uint32_t sampleRate,
                                audio_format_t format,
                                audio_channel_mask_t channelMask,
                                int frameCount,
                                track_flags_t flags,
                                const sp<IMemory>& sharedBuffer,
                                audio_io_handle_t output,
                                pid_t tid,
                                int *sessionId,
                                status_t *status)
    {
        Parcel data, reply;
        sp<IAudioTrack> track;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(pid);
        data.writeInt32((int32_t) streamType);
        data.writeInt32(sampleRate);
        data.writeInt32(format);
        data.writeInt32(channelMask);
        data.writeInt32(frameCount);
        data.writeInt32((int32_t) flags);
        data.writeStrongBinder(sharedBuffer->asBinder());
        data.writeInt32((int32_t) output);
        data.writeInt32((int32_t) tid);
        int lSessionId = 0;
        if (sessionId != NULL) {
            lSessionId = *sessionId;
        }
        data.writeInt32(lSessionId);
        status_t lStatus = remote()->transact(CREATE_TRACK, data, &reply);
        if (lStatus != NO_ERROR) {
            ALOGE("createTrack error: %s", strerror(-lStatus));
        } else {
            lSessionId = reply.readInt32();
            if (sessionId != NULL) {
                *sessionId = lSessionId;
            }
            lStatus = reply.readInt32();
            track = interface_cast<IAudioTrack>(reply.readStrongBinder());
        }
        if (status) {
            *status = lStatus;
        }
        return track;
    }

    virtual sp<IAudioRecord> openRecord(
                                pid_t pid,
                                audio_io_handle_t input,
                                uint32_t sampleRate,
                                audio_format_t format,
                                audio_channel_mask_t channelMask,
                                int frameCount,
                                track_flags_t flags,
                                pid_t tid,
                                int *sessionId,
                                status_t *status)
    {
        Parcel data, reply;
        sp<IAudioRecord> record;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(pid);
        data.writeInt32((int32_t) input);
        data.writeInt32(sampleRate);
        data.writeInt32(format);
        data.writeInt32(channelMask);
        data.writeInt32(frameCount);
        data.writeInt32(flags);
        data.writeInt32((int32_t) tid);
        int lSessionId = 0;
        if (sessionId != NULL) {
            lSessionId = *sessionId;
        }
        data.writeInt32(lSessionId);
        status_t lStatus = remote()->transact(OPEN_RECORD, data, &reply);
        if (lStatus != NO_ERROR) {
            ALOGE("openRecord error: %s", strerror(-lStatus));
        } else {
            lSessionId = reply.readInt32();
            if (sessionId != NULL) {
                *sessionId = lSessionId;
            }
            lStatus = reply.readInt32();
            record = interface_cast<IAudioRecord>(reply.readStrongBinder());
        }
        if (status) {
            *status = lStatus;
        }
        return record;
    }

    virtual uint32_t sampleRate(audio_io_handle_t output) const
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32((int32_t) output);
        remote()->transact(SAMPLE_RATE, data, &reply);
        return reply.readInt32();
    }

#if 0
    virtual int channelCount(audio_io_handle_t output) const
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32((int32_t) output);
        remote()->transact(CHANNEL_COUNT, data, &reply);
        return reply.readInt32();
    }
#endif

    virtual audio_format_t format(audio_io_handle_t output) const
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32((int32_t) output);
        remote()->transact(FORMAT, data, &reply);
        return (audio_format_t) reply.readInt32();
    }

    virtual size_t frameCount(audio_io_handle_t output) const
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32((int32_t) output);
        remote()->transact(FRAME_COUNT, data, &reply);
        return reply.readInt32();
    }

    virtual uint32_t latency(audio_io_handle_t output) const
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32((int32_t) output);
        remote()->transact(LATENCY, data, &reply);
        return reply.readInt32();
    }

    virtual status_t setMasterVolume(float value)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeFloat(value);
        remote()->transact(SET_MASTER_VOLUME, data, &reply);
        return reply.readInt32();
    }

    virtual status_t setMasterMute(bool muted)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(muted);
        remote()->transact(SET_MASTER_MUTE, data, &reply);
        return reply.readInt32();
    }

    virtual float masterVolume() const
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        remote()->transact(MASTER_VOLUME, data, &reply);
        return reply.readFloat();
    }

    virtual bool masterMute() const
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        remote()->transact(MASTER_MUTE, data, &reply);
        return reply.readInt32();
    }

    virtual status_t setStreamVolume(audio_stream_type_t stream, float value,
            audio_io_handle_t output)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32((int32_t) stream);
        data.writeFloat(value);
        data.writeInt32((int32_t) output);
        remote()->transact(SET_STREAM_VOLUME, data, &reply);
        return reply.readInt32();
    }

    virtual status_t setStreamMute(audio_stream_type_t stream, bool muted)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32((int32_t) stream);
        data.writeInt32(muted);
        remote()->transact(SET_STREAM_MUTE, data, &reply);
        return reply.readInt32();
    }

    virtual float streamVolume(audio_stream_type_t stream, audio_io_handle_t output) const
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32((int32_t) stream);
        data.writeInt32((int32_t) output);
        remote()->transact(STREAM_VOLUME, data, &reply);
        return reply.readFloat();
    }

    virtual bool streamMute(audio_stream_type_t stream) const
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32((int32_t) stream);
        remote()->transact(STREAM_MUTE, data, &reply);
        return reply.readInt32();
    }

    virtual status_t setMode(audio_mode_t mode)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(mode);
        remote()->transact(SET_MODE, data, &reply);
        return reply.readInt32();
    }

    virtual status_t setMicMute(bool state)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(state);
        remote()->transact(SET_MIC_MUTE, data, &reply);
        return reply.readInt32();
    }

    virtual bool getMicMute() const
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        remote()->transact(GET_MIC_MUTE, data, &reply);
        return reply.readInt32();
    }

    virtual status_t setParameters(audio_io_handle_t ioHandle, const String8& keyValuePairs)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32((int32_t) ioHandle);
        data.writeString8(keyValuePairs);
        remote()->transact(SET_PARAMETERS, data, &reply);
        return reply.readInt32();
    }

    virtual String8 getParameters(audio_io_handle_t ioHandle, const String8& keys) const
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32((int32_t) ioHandle);
        data.writeString8(keys);
        remote()->transact(GET_PARAMETERS, data, &reply);
        return reply.readString8();
    }

    virtual void registerClient(const sp<IAudioFlingerClient>& client)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeStrongBinder(client->asBinder());
        remote()->transact(REGISTER_CLIENT, data, &reply);
    }

    virtual size_t getInputBufferSize(uint32_t sampleRate, audio_format_t format,
            audio_channel_mask_t channelMask) const
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(sampleRate);
        data.writeInt32(format);
        data.writeInt32(channelMask);
        remote()->transact(GET_INPUTBUFFERSIZE, data, &reply);
        return reply.readInt32();
    }

    virtual audio_io_handle_t openOutput(audio_module_handle_t module,
                                         audio_devices_t *pDevices,
                                         uint32_t *pSamplingRate,
                                         audio_format_t *pFormat,
                                         audio_channel_mask_t *pChannelMask,
                                         uint32_t *pLatencyMs,
                                         audio_output_flags_t flags)
    {
        Parcel data, reply;
        audio_devices_t devices = pDevices ? *pDevices : (audio_devices_t)0;
        uint32_t samplingRate = pSamplingRate ? *pSamplingRate : 0;
        audio_format_t format = pFormat ? *pFormat : AUDIO_FORMAT_DEFAULT;
        audio_channel_mask_t channelMask = pChannelMask ? *pChannelMask : (audio_channel_mask_t)0;
        uint32_t latency = pLatencyMs ? *pLatencyMs : 0;

        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(module);
        data.writeInt32(devices);
        data.writeInt32(samplingRate);
        data.writeInt32(format);
        data.writeInt32(channelMask);
        data.writeInt32(latency);
        data.writeInt32((int32_t) flags);
        remote()->transact(OPEN_OUTPUT, data, &reply);
        audio_io_handle_t output = (audio_io_handle_t) reply.readInt32();
        ALOGV("openOutput() returned output, %d", output);
        devices = (audio_devices_t)reply.readInt32();
        if (pDevices) *pDevices = devices;
        samplingRate = reply.readInt32();
        if (pSamplingRate) *pSamplingRate = samplingRate;
        format = (audio_format_t) reply.readInt32();
        if (pFormat) *pFormat = format;
        channelMask = (audio_channel_mask_t)reply.readInt32();
        if (pChannelMask) *pChannelMask = channelMask;
        latency = reply.readInt32();
        if (pLatencyMs) *pLatencyMs = latency;
        return output;
    }

    virtual audio_io_handle_t openDuplicateOutput(audio_io_handle_t output1,
            audio_io_handle_t output2)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32((int32_t) output1);
        data.writeInt32((int32_t) output2);
        remote()->transact(OPEN_DUPLICATE_OUTPUT, data, &reply);
        return (audio_io_handle_t) reply.readInt32();
    }

    virtual status_t closeOutput(audio_io_handle_t output)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32((int32_t) output);
        remote()->transact(CLOSE_OUTPUT, data, &reply);
        return reply.readInt32();
    }

    virtual status_t suspendOutput(audio_io_handle_t output)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32((int32_t) output);
        remote()->transact(SUSPEND_OUTPUT, data, &reply);
        return reply.readInt32();
    }

    virtual status_t restoreOutput(audio_io_handle_t output)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32((int32_t) output);
        remote()->transact(RESTORE_OUTPUT, data, &reply);
        return reply.readInt32();
    }

    virtual audio_io_handle_t openInput(audio_module_handle_t module,
                                        audio_devices_t *pDevices,
                                        uint32_t *pSamplingRate,
                                        audio_format_t *pFormat,
                                        audio_channel_mask_t *pChannelMask)
    {
        Parcel data, reply;
        audio_devices_t devices = pDevices ? *pDevices : (audio_devices_t)0;
        uint32_t samplingRate = pSamplingRate ? *pSamplingRate : 0;
        audio_format_t format = pFormat ? *pFormat : AUDIO_FORMAT_DEFAULT;
        audio_channel_mask_t channelMask = pChannelMask ? *pChannelMask : (audio_channel_mask_t)0;

        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(module);
        data.writeInt32(devices);
        data.writeInt32(samplingRate);
        data.writeInt32(format);
        data.writeInt32(channelMask);
        remote()->transact(OPEN_INPUT, data, &reply);
        audio_io_handle_t input = (audio_io_handle_t) reply.readInt32();
        devices = (audio_devices_t)reply.readInt32();
        if (pDevices) *pDevices = devices;
        samplingRate = reply.readInt32();
        if (pSamplingRate) *pSamplingRate = samplingRate;
        format = (audio_format_t) reply.readInt32();
        if (pFormat) *pFormat = format;
        channelMask = (audio_channel_mask_t)reply.readInt32();
        if (pChannelMask) *pChannelMask = channelMask;
        return input;
    }

    virtual status_t closeInput(int input)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(input);
        remote()->transact(CLOSE_INPUT, data, &reply);
        return reply.readInt32();
    }

    virtual status_t setStreamOutput(audio_stream_type_t stream, audio_io_handle_t output)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32((int32_t) stream);
        data.writeInt32((int32_t) output);
        remote()->transact(SET_STREAM_OUTPUT, data, &reply);
        return reply.readInt32();
    }

    virtual status_t setVoiceVolume(float volume)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeFloat(volume);
        remote()->transact(SET_VOICE_VOLUME, data, &reply);
        return reply.readInt32();
    }

    virtual status_t getRenderPosition(uint32_t *halFrames, uint32_t *dspFrames,
            audio_io_handle_t output) const
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32((int32_t) output);
        remote()->transact(GET_RENDER_POSITION, data, &reply);
        status_t status = reply.readInt32();
        if (status == NO_ERROR) {
            uint32_t tmp = reply.readInt32();
            if (halFrames) {
                *halFrames = tmp;
            }
            tmp = reply.readInt32();
            if (dspFrames) {
                *dspFrames = tmp;
            }
        }
        return status;
    }

    virtual unsigned int getInputFramesLost(audio_io_handle_t ioHandle) const
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32((int32_t) ioHandle);
        remote()->transact(GET_INPUT_FRAMES_LOST, data, &reply);
        return reply.readInt32();
    }

    virtual int newAudioSessionId()
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        status_t status = remote()->transact(NEW_AUDIO_SESSION_ID, data, &reply);
        int id = 0;
        if (status == NO_ERROR) {
            id = reply.readInt32();
        }
        return id;
    }

    virtual void acquireAudioSessionId(int audioSession)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(audioSession);
        remote()->transact(ACQUIRE_AUDIO_SESSION_ID, data, &reply);
    }

    virtual void releaseAudioSessionId(int audioSession)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(audioSession);
        remote()->transact(RELEASE_AUDIO_SESSION_ID, data, &reply);
    }

    virtual status_t queryNumberEffects(uint32_t *numEffects) const
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        status_t status = remote()->transact(QUERY_NUM_EFFECTS, data, &reply);
        if (status != NO_ERROR) {
            return status;
        }
        status = reply.readInt32();
        if (status != NO_ERROR) {
            return status;
        }
        if (numEffects != NULL) {
            *numEffects = (uint32_t)reply.readInt32();
        }
        return NO_ERROR;
    }

    virtual status_t queryEffect(uint32_t index, effect_descriptor_t *pDescriptor) const
    {
        if (pDescriptor == NULL) {
            return BAD_VALUE;
        }
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(index);
        status_t status = remote()->transact(QUERY_EFFECT, data, &reply);
        if (status != NO_ERROR) {
            return status;
        }
        status = reply.readInt32();
        if (status != NO_ERROR) {
            return status;
        }
        reply.read(pDescriptor, sizeof(effect_descriptor_t));
        return NO_ERROR;
    }

    virtual status_t getEffectDescriptor(const effect_uuid_t *pUuid,
            effect_descriptor_t *pDescriptor) const
    {
        if (pUuid == NULL || pDescriptor == NULL) {
            return BAD_VALUE;
        }
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.write(pUuid, sizeof(effect_uuid_t));
        status_t status = remote()->transact(GET_EFFECT_DESCRIPTOR, data, &reply);
        if (status != NO_ERROR) {
            return status;
        }
        status = reply.readInt32();
        if (status != NO_ERROR) {
            return status;
        }
        reply.read(pDescriptor, sizeof(effect_descriptor_t));
        return NO_ERROR;
    }

    //#ifdef VENDOR_EDIT
    //OppoHook
	//property=android.annotation.OppoHook.OppoRomType.OPPO
    //Chenps ZhiYong.Lin@Plf.Framework modify for BPM 2013/01/21
    virtual status_t getActiveAudioPids(Vector<int32_t>& outPids)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        status_t status = remote()->transact(GET_AUDIOTRACK_PIDS, data, &reply);
        if (status != NO_ERROR) {
            return status;
        }
		
		outPids.clear();
		int32_t totalSize = reply.readInt32();
		int32_t tmp;
		for(int i = 0; i < totalSize; i++) {
		    tmp = reply.readInt32();
			outPids.add(tmp);
		}
        return status;
    }
    //#endif /* VENDOR_EDIT */
    virtual sp<IEffect> createEffect(pid_t pid,
                                    effect_descriptor_t *pDesc,
                                    const sp<IEffectClient>& client,
                                    int32_t priority,
                                    audio_io_handle_t output,
                                    int sessionId,
                                    status_t *status,
                                    int *id,
                                    int *enabled)
    {
        Parcel data, reply;
        sp<IEffect> effect;

        if (pDesc == NULL) {
            return effect;
            if (status) {
                *status = BAD_VALUE;
            }
        }

        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(pid);
        data.write(pDesc, sizeof(effect_descriptor_t));
        data.writeStrongBinder(client->asBinder());
        data.writeInt32(priority);
        data.writeInt32((int32_t) output);
        data.writeInt32(sessionId);

        status_t lStatus = remote()->transact(CREATE_EFFECT, data, &reply);
        if (lStatus != NO_ERROR) {
            ALOGE("createEffect error: %s", strerror(-lStatus));
        } else {
            lStatus = reply.readInt32();
            int tmp = reply.readInt32();
            if (id) {
                *id = tmp;
            }
            tmp = reply.readInt32();
            if (enabled != NULL) {
                *enabled = tmp;
            }
            effect = interface_cast<IEffect>(reply.readStrongBinder());
            reply.read(pDesc, sizeof(effect_descriptor_t));
        }
        if (status) {
            *status = lStatus;
        }

        return effect;
    }

    virtual status_t moveEffects(int session, audio_io_handle_t srcOutput,
            audio_io_handle_t dstOutput)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(session);
        data.writeInt32((int32_t) srcOutput);
        data.writeInt32((int32_t) dstOutput);
        remote()->transact(MOVE_EFFECTS, data, &reply);
        return reply.readInt32();
    }

    virtual audio_module_handle_t loadHwModule(const char *name)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeCString(name);
        remote()->transact(LOAD_HW_MODULE, data, &reply);
        return (audio_module_handle_t) reply.readInt32();
    }

    virtual int32_t getPrimaryOutputSamplingRate()
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        remote()->transact(GET_PRIMARY_OUTPUT_SAMPLING_RATE, data, &reply);
        return reply.readInt32();
    }

    virtual int32_t getPrimaryOutputFrameCount()
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        remote()->transact(GET_PRIMARY_OUTPUT_FRAME_COUNT, data, &reply);
        return reply.readInt32();
    }
#ifdef MTK_AUDIO
    //   Interfaces mtk added 
    virtual int xWayPlay_Start(int sample_rate)
    {
        ALOGV("xWayPlay_Start");
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(sample_rate);
        remote()->transact(PCM_PLAY_START, data, &reply);
        int ret = reply.readInt32();
        return ret;
    }
    virtual int xWayPlay_Stop()
    {
        ALOGV("xWayPlay_Start");
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        remote()->transact(PCM_PLAY_STOP, data, &reply);
        int ret = reply.readInt32();
        return ret;
    }
    virtual int xWayPlay_Write(void *buffer, int size_bytes)
    {
        ALOGV("xWayPlay_Write");
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(size_bytes);
        data.write(buffer,size_bytes);
        remote()->transact(PCM_PLAY_WEITE, data, &reply);
        int ret = reply.readInt32();
        return ret;
    }
    virtual int xWayPlay_GetFreeBufferCount(void)
    {
        ALOGV("xWayPlay_GetFreeBufferCount");
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        remote()->transact(PCM_PLAY_GET_FREE_BUFFER_SIZE, data, &reply);
        int ret = reply.readInt32();
        return ret;
    }
    virtual int xWayRec_Start(int sample_rate)
    {
        ALOGV("xWayRec_Start");
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(sample_rate);
        remote()->transact(PCM_RECORD_START, data, &reply);
        int ret = reply.readInt32();
        return ret;
    }
    virtual int xWayRec_Stop()
    {
        ALOGV("xWayRec_Stop");
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        remote()->transact(PCM_RECORD_STOP, data, &reply);
        int ret = reply.readInt32();
        return ret;
    }
    virtual int xWayRec_Read(void *buffer, int size_bytes)
    {
        ALOGV("xWayRec_Read");
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(size_bytes);
        data.write(buffer,size_bytes);
        remote()->transact(PCM_RECORD_READ, data, &reply);
        reply.read(buffer, size_bytes);
        int ret = reply.readInt32();
        return ret;
    }

    virtual status_t GetEMParameter(void *ptr, size_t len)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(len);
        data.write(ptr,len);
        remote()->transact(GET_EM_PARAMETER, data, &reply);
        reply.read(ptr, len);
        return OK;
    }
    virtual status_t SetEMParameter(void *ptr, size_t len)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(len);
        data.write(ptr,len);
        remote()->transact(SET_EM_PARAMETER, data, &reply);
        reply.read(ptr, len);
        return OK;
    }
    virtual status_t SetAudioCommand(int var1,int var2)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(var1);
        data.writeInt32(var2);
        remote()->transact(SET_AUDIO_COMMAND, data, &reply);
        return  reply.readInt32();
    }
    virtual status_t GetAudioCommand(int var1)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(var1);
        remote()->transact(GET_AUDIO_COMMAND, data, &reply);
        return  reply.readInt32();
    }
    virtual status_t SetAudioData(int par1, size_t len,void *ptr)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(par1);
        data.writeInt32(len);
        data.write(ptr,len);
        remote()->transact(SET_AUDIO_DATA, data, &reply);
        reply.read(ptr, len);
        return OK;
    }
    virtual status_t GetAudioData(int par1,size_t len,void *ptr)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(par1);
        data.writeInt32(len);
        data.write(ptr,len);
        remote()->transact(GET_AUDIO_DATA, data, &reply);
        reply.read(ptr, len);
        return OK;
    }
    virtual status_t SetACFPreviewParameter(void *ptr, size_t len)
    {
        Parcel data, reply;

        ALOGD("IAudioFlinger::SetACFPreviewParameter!!");
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(len);
        data.write(ptr,len);
        remote()->transact(SET_ACF_PREVIEW_PARAMETER, data, &reply);
        reply.read(ptr, len);
        return OK;

    }

    virtual status_t SetHCFPreviewParameter(void *ptr, size_t len)
    {
        Parcel data, reply;

        ALOGD("IAudioFlinger::SetHCFPreviewParameter!! 0");
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        ALOGD("IAudioFlinger::SetHCFPreviewParameter!! 1");
        data.writeInt32(len);
        ALOGD("IAudioFlinger::SetHCFPreviewParameter!! 2");
        data.write(ptr,len);
        ALOGD("IAudioFlinger::SetHCFPreviewParameter!! 3");
        remote()->transact(SET_HCF_PREVIEW_PARAMETER, data, &reply);
        ALOGD("IAudioFlinger::SetHCFPreviewParameter!! 4 remote()->transact done");
        reply.read(ptr, len);
        ALOGD("IAudioFlinger::SetHCFPreviewParameter!! 5 return OK");
        return OK;

    }
    //added by wendy    
    virtual int ReadRefFromRing(void*buf, uint32_t datasz,void* DLtime)
    {
        ALOGV("ReadRefFromRing");
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(datasz);
        remote()->transact(Read_Ref_FromRing, data, &reply);
        reply.read(buf, datasz);
        reply.read(DLtime, 16);
        int ret = reply.readInt32();
        ALOGW("ReadRefFromRing %d", ret);
        return ret;
    }
    
    virtual int GetVoiceUnlockULTime(void* DLtime)
    {
        ALOGV("GetVoiceUnlockULTime");
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.write(DLtime,16);
        remote()->transact(Get_Voice_Unlock_ULTime, data, &reply);
        reply.read(DLtime, 16);
        int ret = reply.readInt32();
        return ret;
    }
    virtual int SetVoiceUnlockSRC(uint outSR, uint outCH)
    {
        ALOGV("SetVoiceUnlockSRC");
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        data.writeInt32(outSR);        
        data.writeInt32(outCH);
        remote()->transact(Set_Voice_Unlock_SRC, data, &reply);
        int ret = reply.readInt32();
        return ret;
    }
    
    virtual bool startVoiceUnlockDL()
    {        
        ALOGV("startVoiceUnlockDL");
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        remote()->transact(start_Voice_Unlock_DL, data, &reply);
        int ret = reply.readInt32();
        return ret;
    }    
    virtual bool stopVoiceUnlockDL()
    {    
        ALOGV("stopVoiceUnlockDL");
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        remote()->transact(stop_Voice_Unlock_DL, data, &reply);
        int ret = reply.readInt32();
        return ret;
    }
    
    virtual void freeVoiceUnlockDLInstance ()
    {    
        ALOGV("freeVoiceUnlockDLInstance ");
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        remote()->transact(free_Voice_Unlock_DL_Instance , data, &reply);
        return ;
    }
    
    virtual bool getVoiceUnlockDLInstance()
    {
        ALOGV("getVoiceUnlockDLInstance");
        Parcel data, reply;
        data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
        remote()->transact(get_Voice_Unlock_DL_Instance, data, &reply);
        bool ret = reply.readInt32();
        return ret;
    }
    virtual int GetVoiceUnlockDLLatency()
    {
         ALOGV("GetVoiceUnlockDLLatency");
         Parcel data, reply;
         data.writeInterfaceToken(IAudioFlinger::getInterfaceDescriptor());
         remote()->transact(Get_Voice_Unlock_DL_Latency, data, &reply);
         int ret = reply.readInt32();
         return ret;
    }
#endif
};

IMPLEMENT_META_INTERFACE(AudioFlinger, "android.media.IAudioFlinger");

// ----------------------------------------------------------------------

status_t BnAudioFlinger::onTransact(
    uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
{
    switch (code) {
        case CREATE_TRACK: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            pid_t pid = data.readInt32();
            int streamType = data.readInt32();
            uint32_t sampleRate = data.readInt32();
            audio_format_t format = (audio_format_t) data.readInt32();
            audio_channel_mask_t channelMask = data.readInt32();
            size_t bufferCount = data.readInt32();
            track_flags_t flags = (track_flags_t) data.readInt32();
            sp<IMemory> buffer = interface_cast<IMemory>(data.readStrongBinder());
            audio_io_handle_t output = (audio_io_handle_t) data.readInt32();
            pid_t tid = (pid_t) data.readInt32();
            int sessionId = data.readInt32();
            status_t status;
            sp<IAudioTrack> track = createTrack(pid,
                    (audio_stream_type_t) streamType, sampleRate, format,
                    channelMask, bufferCount, flags, buffer, output, tid, &sessionId, &status);
            reply->writeInt32(sessionId);
            reply->writeInt32(status);
            reply->writeStrongBinder(track->asBinder());
            return NO_ERROR;
        } break;
        case OPEN_RECORD: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            pid_t pid = data.readInt32();
            audio_io_handle_t input = (audio_io_handle_t) data.readInt32();
            uint32_t sampleRate = data.readInt32();
            audio_format_t format = (audio_format_t) data.readInt32();
            audio_channel_mask_t channelMask = data.readInt32();
            size_t bufferCount = data.readInt32();
            track_flags_t flags = (track_flags_t) data.readInt32();
            pid_t tid = (pid_t) data.readInt32();
            int sessionId = data.readInt32();
            status_t status;
            sp<IAudioRecord> record = openRecord(pid, input,
                    sampleRate, format, channelMask, bufferCount, flags, tid, &sessionId, &status);
            reply->writeInt32(sessionId);
            reply->writeInt32(status);
            reply->writeStrongBinder(record->asBinder());
            return NO_ERROR;
        } break;
        case SAMPLE_RATE: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            reply->writeInt32( sampleRate((audio_io_handle_t) data.readInt32()) );
            return NO_ERROR;
        } break;
#if 0
        case CHANNEL_COUNT: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            reply->writeInt32( channelCount((audio_io_handle_t) data.readInt32()) );
            return NO_ERROR;
        } break;
#endif
        case FORMAT: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            reply->writeInt32( format((audio_io_handle_t) data.readInt32()) );
            return NO_ERROR;
        } break;
        case FRAME_COUNT: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            reply->writeInt32( frameCount((audio_io_handle_t) data.readInt32()) );
            return NO_ERROR;
        } break;
        case LATENCY: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            reply->writeInt32( latency((audio_io_handle_t) data.readInt32()) );
            return NO_ERROR;
        } break;
        case SET_MASTER_VOLUME: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            reply->writeInt32( setMasterVolume(data.readFloat()) );
            return NO_ERROR;
        } break;
        case SET_MASTER_MUTE: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            reply->writeInt32( setMasterMute(data.readInt32()) );
            return NO_ERROR;
        } break;
        case MASTER_VOLUME: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            reply->writeFloat( masterVolume() );
            return NO_ERROR;
        } break;
        case MASTER_MUTE: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            reply->writeInt32( masterMute() );
            return NO_ERROR;
        } break;
        case SET_STREAM_VOLUME: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            int stream = data.readInt32();
            float volume = data.readFloat();
            audio_io_handle_t output = (audio_io_handle_t) data.readInt32();
            reply->writeInt32( setStreamVolume((audio_stream_type_t) stream, volume, output) );
            return NO_ERROR;
        } break;
        case SET_STREAM_MUTE: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            int stream = data.readInt32();
            reply->writeInt32( setStreamMute((audio_stream_type_t) stream, data.readInt32()) );
            return NO_ERROR;
        } break;
        case STREAM_VOLUME: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            int stream = data.readInt32();
            int output = data.readInt32();
            reply->writeFloat( streamVolume((audio_stream_type_t) stream, output) );
            return NO_ERROR;
        } break;
        case STREAM_MUTE: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            int stream = data.readInt32();
            reply->writeInt32( streamMute((audio_stream_type_t) stream) );
            return NO_ERROR;
        } break;
        case SET_MODE: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            audio_mode_t mode = (audio_mode_t) data.readInt32();
            reply->writeInt32( setMode(mode) );
            return NO_ERROR;
        } break;
        case SET_MIC_MUTE: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            int state = data.readInt32();
            reply->writeInt32( setMicMute(state) );
            return NO_ERROR;
        } break;
        case GET_MIC_MUTE: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            reply->writeInt32( getMicMute() );
            return NO_ERROR;
        } break;
        case SET_PARAMETERS: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            audio_io_handle_t ioHandle = (audio_io_handle_t) data.readInt32();
            String8 keyValuePairs(data.readString8());
            reply->writeInt32(setParameters(ioHandle, keyValuePairs));
            return NO_ERROR;
        } break;
        case GET_PARAMETERS: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            audio_io_handle_t ioHandle = (audio_io_handle_t) data.readInt32();
            String8 keys(data.readString8());
            reply->writeString8(getParameters(ioHandle, keys));
            return NO_ERROR;
        } break;

        case REGISTER_CLIENT: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            sp<IAudioFlingerClient> client = interface_cast<IAudioFlingerClient>(data.readStrongBinder());
            registerClient(client);
            return NO_ERROR;
        } break;
        case GET_INPUTBUFFERSIZE: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            uint32_t sampleRate = data.readInt32();
            audio_format_t format = (audio_format_t) data.readInt32();
            audio_channel_mask_t channelMask = data.readInt32();
            reply->writeInt32( getInputBufferSize(sampleRate, format, channelMask) );
            return NO_ERROR;
        } break;
        case OPEN_OUTPUT: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            audio_module_handle_t module = (audio_module_handle_t)data.readInt32();
            audio_devices_t devices = (audio_devices_t)data.readInt32();
            uint32_t samplingRate = data.readInt32();
            audio_format_t format = (audio_format_t) data.readInt32();
            audio_channel_mask_t channelMask = (audio_channel_mask_t)data.readInt32();
            uint32_t latency = data.readInt32();
            audio_output_flags_t flags = (audio_output_flags_t) data.readInt32();
            audio_io_handle_t output = openOutput(module,
                                                 &devices,
                                                 &samplingRate,
                                                 &format,
                                                 &channelMask,
                                                 &latency,
                                                 flags);
            ALOGV("OPEN_OUTPUT output, %p", output);
            reply->writeInt32((int32_t) output);
            reply->writeInt32(devices);
            reply->writeInt32(samplingRate);
            reply->writeInt32(format);
            reply->writeInt32(channelMask);
            reply->writeInt32(latency);
            return NO_ERROR;
        } break;
        case OPEN_DUPLICATE_OUTPUT: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            audio_io_handle_t output1 = (audio_io_handle_t) data.readInt32();
            audio_io_handle_t output2 = (audio_io_handle_t) data.readInt32();
            reply->writeInt32((int32_t) openDuplicateOutput(output1, output2));
            return NO_ERROR;
        } break;
        case CLOSE_OUTPUT: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            reply->writeInt32(closeOutput((audio_io_handle_t) data.readInt32()));
            return NO_ERROR;
        } break;
        case SUSPEND_OUTPUT: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            reply->writeInt32(suspendOutput((audio_io_handle_t) data.readInt32()));
            return NO_ERROR;
        } break;
        case RESTORE_OUTPUT: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            reply->writeInt32(restoreOutput((audio_io_handle_t) data.readInt32()));
            return NO_ERROR;
        } break;
        case OPEN_INPUT: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            audio_module_handle_t module = (audio_module_handle_t)data.readInt32();
            audio_devices_t devices = (audio_devices_t)data.readInt32();
            uint32_t samplingRate = data.readInt32();
            audio_format_t format = (audio_format_t) data.readInt32();
            audio_channel_mask_t channelMask = (audio_channel_mask_t)data.readInt32();

            audio_io_handle_t input = openInput(module,
                                             &devices,
                                             &samplingRate,
                                             &format,
                                             &channelMask);
            reply->writeInt32((int32_t) input);
            reply->writeInt32(devices);
            reply->writeInt32(samplingRate);
            reply->writeInt32(format);
            reply->writeInt32(channelMask);
            return NO_ERROR;
        } break;
        case CLOSE_INPUT: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            reply->writeInt32(closeInput((audio_io_handle_t) data.readInt32()));
            return NO_ERROR;
        } break;
        case SET_STREAM_OUTPUT: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            uint32_t stream = data.readInt32();
            audio_io_handle_t output = (audio_io_handle_t) data.readInt32();
            reply->writeInt32(setStreamOutput((audio_stream_type_t) stream, output));
            return NO_ERROR;
        } break;
        case SET_VOICE_VOLUME: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            float volume = data.readFloat();
            reply->writeInt32( setVoiceVolume(volume) );
            return NO_ERROR;
        } break;
        case GET_RENDER_POSITION: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            audio_io_handle_t output = (audio_io_handle_t) data.readInt32();
            uint32_t halFrames;
            uint32_t dspFrames;
            status_t status = getRenderPosition(&halFrames, &dspFrames, output);
            reply->writeInt32(status);
            if (status == NO_ERROR) {
                reply->writeInt32(halFrames);
                reply->writeInt32(dspFrames);
            }
            return NO_ERROR;
        }
        case GET_INPUT_FRAMES_LOST: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            audio_io_handle_t ioHandle = (audio_io_handle_t) data.readInt32();
            reply->writeInt32(getInputFramesLost(ioHandle));
            return NO_ERROR;
        } break;
        case NEW_AUDIO_SESSION_ID: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            reply->writeInt32(newAudioSessionId());
            return NO_ERROR;
        } break;
        case ACQUIRE_AUDIO_SESSION_ID: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            int audioSession = data.readInt32();
            acquireAudioSessionId(audioSession);
            return NO_ERROR;
        } break;
        case RELEASE_AUDIO_SESSION_ID: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            int audioSession = data.readInt32();
            releaseAudioSessionId(audioSession);
            return NO_ERROR;
        } break;
        case QUERY_NUM_EFFECTS: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            uint32_t numEffects;
            status_t status = queryNumberEffects(&numEffects);
            reply->writeInt32(status);
            if (status == NO_ERROR) {
                reply->writeInt32((int32_t)numEffects);
            }
            return NO_ERROR;
        }
        case QUERY_EFFECT: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            effect_descriptor_t desc;
            status_t status = queryEffect(data.readInt32(), &desc);
            reply->writeInt32(status);
            if (status == NO_ERROR) {
                reply->write(&desc, sizeof(effect_descriptor_t));
            }
            return NO_ERROR;
        }
        case GET_EFFECT_DESCRIPTOR: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            effect_uuid_t uuid;
            data.read(&uuid, sizeof(effect_uuid_t));
            effect_descriptor_t desc;
            status_t status = getEffectDescriptor(&uuid, &desc);
            reply->writeInt32(status);
            if (status == NO_ERROR) {
                reply->write(&desc, sizeof(effect_descriptor_t));
            }
            return NO_ERROR;
        }

        //#ifdef VENDOR_EDIT
        //OppoHook
		//property=android.annotation.OppoHook.OppoRomType.OPPO
    	//Chenps ZhiYong.Lin@Plf.Framework modify for BPM 2013/01/21
        case GET_AUDIOTRACK_PIDS: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
			
            Vector<int32_t> pids;
            status_t status = getActiveAudioPids(pids);
			
			if (status != NO_ERROR) {
			    return status;
			}
			int totalSize = pids.size();
			reply->writeInt32(totalSize);
			for (int i = 0; i < totalSize; i++) {
			    reply->writeInt32(pids[i]);
			}  			
            return NO_ERROR;
        } break;
        //#endif /* VENDOR_EDIT */
        case CREATE_EFFECT: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            pid_t pid = data.readInt32();
            effect_descriptor_t desc;
            data.read(&desc, sizeof(effect_descriptor_t));
            sp<IEffectClient> client = interface_cast<IEffectClient>(data.readStrongBinder());
            int32_t priority = data.readInt32();
            audio_io_handle_t output = (audio_io_handle_t) data.readInt32();
            int sessionId = data.readInt32();
            status_t status;
            int id;
            int enabled;

            sp<IEffect> effect = createEffect(pid, &desc, client, priority, output, sessionId, &status, &id, &enabled);
            reply->writeInt32(status);
            reply->writeInt32(id);
            reply->writeInt32(enabled);
            reply->writeStrongBinder(effect->asBinder());
            reply->write(&desc, sizeof(effect_descriptor_t));
            return NO_ERROR;
        } break;
        case MOVE_EFFECTS: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            int session = data.readInt32();
            audio_io_handle_t srcOutput = (audio_io_handle_t) data.readInt32();
            audio_io_handle_t dstOutput = (audio_io_handle_t) data.readInt32();
            reply->writeInt32(moveEffects(session, srcOutput, dstOutput));
            return NO_ERROR;
        } break;
        case LOAD_HW_MODULE: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            reply->writeInt32(loadHwModule(data.readCString()));
            return NO_ERROR;
        } break;
        case GET_PRIMARY_OUTPUT_SAMPLING_RATE: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            reply->writeInt32(getPrimaryOutputSamplingRate());
            return NO_ERROR;
        } break;
        case GET_PRIMARY_OUTPUT_FRAME_COUNT: {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            reply->writeInt32(getPrimaryOutputFrameCount());
            return NO_ERROR;
        } break;
#ifdef MTK_AUDIO
        // add by chipeng for EM mode Setting
        case GET_EM_PARAMETER:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            size_t size = data.readInt32();
            void *params = malloc(size);
            data.read(params, size);
            status_t err = GetEMParameter(params,size);
            reply->write( params,size);
            free(params);
            return NO_ERROR;
        }break;
        case SET_EM_PARAMETER:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            size_t size = data.readInt32();
            void *params = malloc(size);
            data.read(params, size);
            status_t err = SetEMParameter(params,size);
            reply->write(params,size);
            free(params);
            return NO_ERROR;
        }break;
        case SET_AUDIO_COMMAND:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            int32_t command1 = data.readInt32();
            int32_t command2 = data.readInt32();
            reply->writeInt32(SetAudioCommand(command1,command2));
            return NO_ERROR;
        }break;
        case GET_AUDIO_COMMAND:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            int32_t command1 = data.readInt32();
            reply->writeInt32(GetAudioCommand(command1));
            return NO_ERROR;
        }break;
        case SET_AUDIO_DATA:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            size_t command = data.readInt32();
            size_t buf_size = data.readInt32();
            void *params = malloc(buf_size);
            data.read(params, buf_size);
            status_t err = SetAudioData(command,buf_size,params);
            reply->write(params,buf_size);
            free(params);
            return NO_ERROR;
        }break;
        case GET_AUDIO_DATA:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            size_t command = data.readInt32();
            size_t buf_size = data.readInt32();
            void *params = malloc(buf_size);
            data.read(params, buf_size);
            status_t err = GetAudioData(command,buf_size,params);
            reply->write(params,buf_size);
            free(params);
            return NO_ERROR;
        }break;
        case SET_ACF_PREVIEW_PARAMETER:
        {
            ALOGD("IAudioFlinger  case SET_ACF_PREVIEW_PARAMETER SetACFPreviewParameter!! ");
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            size_t size = data.readInt32();
            void *params = malloc(size);
            data.read(params, size);
            status_t err = SetACFPreviewParameter(params,size);
            reply->write( params,size);
            free(params);
            return NO_ERROR;
        }break;
        case SET_HCF_PREVIEW_PARAMETER:
        {
            ALOGD("IAudioFlinger  case SET_HCF_PREVIEW_PARAMETER SetHCFPreviewParameter!! ");
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            size_t size = data.readInt32();
            void *params = malloc(size);
            data.read(params, size);
            status_t err = SetHCFPreviewParameter(params,size);
            reply->write( params,size);
            free(params);
            return NO_ERROR;
        }break;

        /////////////////////////////////////////////////////////////////////////
        //    for PCMxWay Interface API ...   
        /////////////////////////////////////////////////////////////////////////
        case PCM_PLAY_START:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            size_t sample_rate = data.readInt32();
            status_t err = xWayPlay_Start(sample_rate);
            reply->writeInt32(err);
            return NO_ERROR;
        }break;
        case PCM_PLAY_STOP:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            status_t err = xWayPlay_Stop();
            reply->writeInt32(err);
            return NO_ERROR;
        }break;
        case PCM_PLAY_WEITE:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            size_t size_bytes = data.readInt32();
            void *buffer = malloc(size_bytes);
            data.read(buffer, size_bytes);
            status_t err = xWayPlay_Write(buffer,size_bytes);
            reply->writeInt32(err);
            free(buffer);
            return NO_ERROR;
        }
        break;
        case PCM_PLAY_GET_FREE_BUFFER_SIZE:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            status_t err = xWayPlay_GetFreeBufferCount();
            reply->writeInt32(err);
            return NO_ERROR;
        }
        break;
        case PCM_RECORD_START:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            size_t sample_rate = data.readInt32();
            status_t err = xWayRec_Start(sample_rate);
            reply->writeInt32(err);
            return NO_ERROR;
        }
        break;
        case PCM_RECORD_STOP:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            status_t err = xWayRec_Stop();
            reply->writeInt32(err);
            return NO_ERROR;
        }break;
        case PCM_RECORD_READ:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            size_t size_bytes = data.readInt32();
            void *buffer = malloc(size_bytes);
            data.read(buffer, size_bytes);
            status_t err = xWayRec_Read(buffer,size_bytes);
            reply->write(buffer,size_bytes);
            reply->writeInt32(err);
            free(buffer);
            return NO_ERROR;
        }break;
        case Read_Ref_FromRing:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            size_t size_bytes = data.readInt32();
            void *buffer = malloc(size_bytes);
            void *DLtime = malloc(16);
            status_t err = ReadRefFromRing(buffer,size_bytes, DLtime);
            reply->write(buffer,size_bytes);            
            reply->write(DLtime,16);
            reply->writeInt32(err);
            free(buffer);
            free(DLtime);
            return NO_ERROR;
        }break;
        case Get_Voice_Unlock_ULTime:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            void *DLtime = malloc(16);
            data.read(DLtime, 16);
            status_t err = GetVoiceUnlockULTime(DLtime);
            reply->write(DLtime,16);
            reply->writeInt32(err);
            free(DLtime);
            return NO_ERROR;
        }break;
        case Set_Voice_Unlock_SRC:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            size_t outSR = data.readInt32();        
            size_t outCH = data.readInt32();
            status_t err = SetVoiceUnlockSRC(outSR, outCH);
            reply->writeInt32(err);
            return NO_ERROR;
        }
        break;
        case start_Voice_Unlock_DL:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            status_t err = startVoiceUnlockDL();
            reply->writeInt32(err);
            return NO_ERROR;
        }
        break;
        case stop_Voice_Unlock_DL:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            status_t err = stopVoiceUnlockDL();
            reply->writeInt32(err);
            return NO_ERROR;
        }
        break;
        case free_Voice_Unlock_DL_Instance:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            freeVoiceUnlockDLInstance();
            return NO_ERROR;
        }
        break;
        case get_Voice_Unlock_DL_Instance:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            bool err = getVoiceUnlockDLInstance();
            reply->writeInt32(err);
            return NO_ERROR;
        }
        break;
        case Get_Voice_Unlock_DL_Latency:
        {
            CHECK_INTERFACE(IAudioFlinger, data, reply);
            int err = GetVoiceUnlockDLLatency();
            reply->writeInt32(err);
            return NO_ERROR;
        }
        break;
#endif
        default:
            return BBinder::onTransact(code, data, reply, flags);
    }
}

// ----------------------------------------------------------------------------

}; // namespace android
