/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "MediaProfilesJNI"
#include <utils/Log.h>

#include <stdio.h>
#include <utils/threads.h>

#include "jni.h"
#include "JNIHelp.h"
#include "android_runtime/AndroidRuntime.h"
#include <media/MediaProfiles.h>

using namespace android;

static Mutex sLock;
MediaProfiles *sProfiles = NULL;

// This function is called from a static block in MediaProfiles.java class,
// which won't run until the first time an instance of this class is used.
static void
android_media_MediaProfiles_native_init(JNIEnv *env)
{
    ALOGV("native_init");
    Mutex::Autolock lock(sLock);

    if (sProfiles == NULL) {
        sProfiles = MediaProfiles::getInstance();
    }
}

static jint
android_media_MediaProfiles_native_get_num_file_formats(JNIEnv *env, jobject thiz)
{
    ALOGV("native_get_num_file_formats");
    return sProfiles->getOutputFileFormats().size();
}

static jint
android_media_MediaProfiles_native_get_file_format(JNIEnv *env, jobject thiz, jint index)
{
    ALOGV("native_get_file_format: %d", index);
    Vector<output_format> formats = sProfiles->getOutputFileFormats();
    int nSize = formats.size();
    if (index < 0 || index >= nSize) {
        jniThrowException(env, "java/lang/IllegalArgumentException", "out of array boundary");
        return -1;
    }
    return static_cast<jint>(formats[index]);
}

static jint
android_media_MediaProfiles_native_get_num_video_encoders(JNIEnv *env, jobject thiz)
{
    ALOGV("native_get_num_video_encoders");
    return sProfiles->getVideoEncoders().size();
}

static jobject
android_media_MediaProfiles_native_get_video_encoder_cap(JNIEnv *env, jobject thiz, jint index)
{
    ALOGV("native_get_video_encoder_cap: %d", index);
    Vector<video_encoder> encoders = sProfiles->getVideoEncoders();
    int nSize = encoders.size();
    if (index < 0 || index >= nSize) {
        jniThrowException(env, "java/lang/IllegalArgumentException", "out of array boundary");
        return NULL;
    }

    video_encoder encoder = encoders[index];
    int minBitRate = sProfiles->getVideoEncoderParamByName("enc.vid.bps.min", encoder);
    int maxBitRate = sProfiles->getVideoEncoderParamByName("enc.vid.bps.max", encoder);
    int minFrameRate = sProfiles->getVideoEncoderParamByName("enc.vid.fps.min", encoder);
    int maxFrameRate = sProfiles->getVideoEncoderParamByName("enc.vid.fps.max", encoder);
    int minFrameWidth = sProfiles->getVideoEncoderParamByName("enc.vid.width.min", encoder);
    int maxFrameWidth = sProfiles->getVideoEncoderParamByName("enc.vid.width.max", encoder);
    int minFrameHeight = sProfiles->getVideoEncoderParamByName("enc.vid.height.min", encoder);
    int maxFrameHeight = sProfiles->getVideoEncoderParamByName("enc.vid.height.max", encoder);

    // Check on the values retrieved
    if ((minBitRate == -1 || maxBitRate == -1) ||
        (minFrameRate == -1 || maxFrameRate == -1) ||
        (minFrameWidth == -1 || maxFrameWidth == -1) ||
        (minFrameHeight == -1 || maxFrameHeight == -1)) {

        jniThrowException(env, "java/lang/RuntimeException", "Error retrieving video encoder capability params");
        return NULL;
    }

    // Construct an instance of the VideoEncoderCap and set its member variables
    jclass videoEncoderCapClazz = env->FindClass("android/media/EncoderCapabilities$VideoEncoderCap");
    jmethodID videoEncoderCapConstructorMethodID = env->GetMethodID(videoEncoderCapClazz, "<init>", "(IIIIIIIII)V");
    jobject cap = env->NewObject(videoEncoderCapClazz,
                                 videoEncoderCapConstructorMethodID,
                                 static_cast<int>(encoder),
                                 minBitRate, maxBitRate,
                                 minFrameRate, maxFrameRate,
                                 minFrameWidth, maxFrameWidth,
                                 minFrameHeight, maxFrameHeight);
    return cap;
}

static jint
android_media_MediaProfiles_native_get_num_audio_encoders(JNIEnv *env, jobject thiz)
{
    ALOGV("native_get_num_audio_encoders");
    return sProfiles->getAudioEncoders().size();
}

static jobject
android_media_MediaProfiles_native_get_audio_encoder_cap(JNIEnv *env, jobject thiz, jint index)
{
    ALOGV("native_get_audio_encoder_cap: %d", index);
    Vector<audio_encoder> encoders = sProfiles->getAudioEncoders();
    int nSize = encoders.size();
    if (index < 0 || index >= nSize) {
        jniThrowException(env, "java/lang/IllegalArgumentException", "out of array boundary");
        return NULL;
    }

    audio_encoder encoder = encoders[index];
    int minBitRate = sProfiles->getAudioEncoderParamByName("enc.aud.bps.min", encoder);
    int maxBitRate = sProfiles->getAudioEncoderParamByName("enc.aud.bps.max", encoder);
    int minSampleRate = sProfiles->getAudioEncoderParamByName("enc.aud.hz.min", encoder);
    int maxSampleRate = sProfiles->getAudioEncoderParamByName("enc.aud.hz.max", encoder);
    int minChannels = sProfiles->getAudioEncoderParamByName("enc.aud.ch.min", encoder);
    int maxChannels = sProfiles->getAudioEncoderParamByName("enc.aud.ch.max", encoder);

    // Check on the values retrieved
    if ((minBitRate == -1 || maxBitRate == -1) ||
        (minSampleRate == -1 || maxSampleRate == -1) ||
        (minChannels == -1 || maxChannels == -1)) {

        jniThrowException(env, "java/lang/RuntimeException", "Error retrieving video encoder capability params");
        return NULL;
    }

    jclass audioEncoderCapClazz = env->FindClass("android/media/EncoderCapabilities$AudioEncoderCap");
    jmethodID audioEncoderCapConstructorMethodID = env->GetMethodID(audioEncoderCapClazz, "<init>", "(IIIIIII)V");
    jobject cap = env->NewObject(audioEncoderCapClazz,
                                 audioEncoderCapConstructorMethodID,
                                 static_cast<int>(encoder),
                                 minBitRate, maxBitRate,
                                 minSampleRate, maxSampleRate,
                                 minChannels, maxChannels);
    return cap;
}

#ifdef VENDOR_EDIT
//Xianfen.Fu@Prd.Video.VideoCodec,2012/06/05,Add for camcordprofile
static jstring
android_media_MediaProfiles_native_get_camcorder_profile_caps(JNIEnv *env, jobject thiz, jint cameraId)
{
#ifndef ANDROID_DEFAULT_CODE
	ALOGV("native_get_camcorder_profile_num, cameraId = %d", cameraId);
	char buff[256];
	memset(buff, 0, 256);

	jstring ret = NULL;

	if(sProfiles == NULL) {
		strcat(buff, " ");
		ret = env->NewStringUTF(buff);
		return ret;
	}

	String8 aaa = sProfiles->getCamcorderProfilesCaps(cameraId);
	ret = env->NewStringUTF(aaa.string());
	return ret;
#else
    return env->NewStringUTF("0,1,2,3");
#endif	
}
#endif /*VENDOR_EDIT*/

static bool isCamcorderQualityKnown(int quality)
{
    return ((quality >= CAMCORDER_QUALITY_LIST_START &&
             quality <= CAMCORDER_QUALITY_LIST_END) ||
            (quality >= CAMCORDER_QUALITY_TIME_LAPSE_LIST_START &&
             quality <= CAMCORDER_QUALITY_TIME_LAPSE_LIST_END));
}

static jobject
android_media_MediaProfiles_native_get_camcorder_profile(JNIEnv *env, jobject thiz, jint id, jint quality)
{
    ALOGV("native_get_camcorder_profile: %d %d", id, quality);
    if (!isCamcorderQualityKnown(quality)) {
        jniThrowException(env, "java/lang/RuntimeException", "Unknown camcorder profile quality");
        return NULL;
    }

    camcorder_quality q = static_cast<camcorder_quality>(quality);
    int duration         = sProfiles->getCamcorderProfileParamByName("duration",    id, q);
    int fileFormat       = sProfiles->getCamcorderProfileParamByName("file.format", id, q);
    int videoCodec       = sProfiles->getCamcorderProfileParamByName("vid.codec",   id, q);
    int videoBitRate     = sProfiles->getCamcorderProfileParamByName("vid.bps",     id, q);
    int videoFrameRate   = sProfiles->getCamcorderProfileParamByName("vid.fps",     id, q);
    int videoFrameWidth  = sProfiles->getCamcorderProfileParamByName("vid.width",   id, q);
    int videoFrameHeight = sProfiles->getCamcorderProfileParamByName("vid.height",  id, q);
    int audioCodec       = sProfiles->getCamcorderProfileParamByName("aud.codec",   id, q);
    int audioBitRate     = sProfiles->getCamcorderProfileParamByName("aud.bps",     id, q);
    int audioSampleRate  = sProfiles->getCamcorderProfileParamByName("aud.hz",      id, q);
    int audioChannels    = sProfiles->getCamcorderProfileParamByName("aud.ch",      id, q);

    // Check on the values retrieved
    if (duration == -1 || fileFormat == -1 || videoCodec == -1 || audioCodec == -1 ||
        videoBitRate == -1 || videoFrameRate == -1 || videoFrameWidth == -1 || videoFrameHeight == -1 ||
        audioBitRate == -1 || audioSampleRate == -1 || audioChannels == -1) {

        jniThrowException(env, "java/lang/RuntimeException", "Error retrieving camcorder profile params");
        return NULL;
    }

    jclass camcorderProfileClazz = env->FindClass("android/media/CamcorderProfile");
    jmethodID camcorderProfileConstructorMethodID = env->GetMethodID(camcorderProfileClazz, "<init>", "(IIIIIIIIIIII)V");
    return env->NewObject(camcorderProfileClazz,
                          camcorderProfileConstructorMethodID,
                          duration,
                          quality,
                          fileFormat,
                          videoCodec,
                          videoBitRate,
                          videoFrameRate,
                          videoFrameWidth,
                          videoFrameHeight,
                          audioCodec,
                          audioBitRate,
                          audioSampleRate,
                          audioChannels);
}

static jboolean
android_media_MediaProfiles_native_has_camcorder_profile(JNIEnv *env, jobject thiz, jint id, jint quality)
{
    ALOGV("native_has_camcorder_profile: %d %d", id, quality);
    if (!isCamcorderQualityKnown(quality)) {
        return false;
    }

    camcorder_quality q = static_cast<camcorder_quality>(quality);
    return sProfiles->hasCamcorderProfile(id, q);
}

static jint
android_media_MediaProfiles_native_get_num_video_decoders(JNIEnv *env, jobject thiz)
{
    ALOGV("native_get_num_video_decoders");
    return sProfiles->getVideoDecoders().size();
}

static jint
android_media_MediaProfiles_native_get_video_decoder_type(JNIEnv *env, jobject thiz, jint index)
{
    ALOGV("native_get_video_decoder_type: %d", index);
    Vector<video_decoder> decoders = sProfiles->getVideoDecoders();
    int nSize = decoders.size();
    if (index < 0 || index >= nSize) {
        jniThrowException(env, "java/lang/IllegalArgumentException", "out of array boundary");
        return -1;
    }

    return static_cast<jint>(decoders[index]);
}

static jint
android_media_MediaProfiles_native_get_num_audio_decoders(JNIEnv *env, jobject thiz)
{
    ALOGV("native_get_num_audio_decoders");
    return sProfiles->getAudioDecoders().size();
}

static jint
android_media_MediaProfiles_native_get_audio_decoder_type(JNIEnv *env, jobject thiz, jint index)
{
    ALOGV("native_get_audio_decoder_type: %d", index);
    Vector<audio_decoder> decoders = sProfiles->getAudioDecoders();
    int nSize = decoders.size();
    if (index < 0 || index >= nSize) {
        jniThrowException(env, "java/lang/IllegalArgumentException", "out of array boundary");
        return -1;
    }

    return static_cast<jint>(decoders[index]);
}

static jint
android_media_MediaProfiles_native_get_num_image_encoding_quality_levels(JNIEnv *env, jobject thiz, jint cameraId)
{
    ALOGV("native_get_num_image_encoding_quality_levels");
    return sProfiles->getImageEncodingQualityLevels(cameraId).size();
}

static jint
android_media_MediaProfiles_native_get_image_encoding_quality_level(JNIEnv *env, jobject thiz, jint cameraId, jint index)
{
    ALOGV("native_get_image_encoding_quality_level");
    Vector<int> levels = sProfiles->getImageEncodingQualityLevels(cameraId);
    if (index < 0 || index >= levels.size()) {
        jniThrowException(env, "java/lang/IllegalArgumentException", "out of array boundary");
        return -1;
    }
    return static_cast<jint>(levels[index]);
}
static jobject
android_media_MediaProfiles_native_get_videoeditor_profile(JNIEnv *env, jobject thiz)
{
    ALOGV("native_get_videoeditor_profile");

    int maxInputFrameWidth =
            sProfiles->getVideoEditorCapParamByName("videoeditor.input.width.max");
    int maxInputFrameHeight =
            sProfiles->getVideoEditorCapParamByName("videoeditor.input.height.max");
    int maxOutputFrameWidth =
            sProfiles->getVideoEditorCapParamByName("videoeditor.output.width.max");
    int maxOutputFrameHeight =
            sProfiles->getVideoEditorCapParamByName("videoeditor.output.height.max");

    // Check on the values retrieved
    if (maxInputFrameWidth == -1 || maxInputFrameHeight == -1 ||
        maxOutputFrameWidth == -1 || maxOutputFrameHeight == -1) {

        jniThrowException(env, "java/lang/RuntimeException",\
            "Error retrieving videoeditor profile params");
        return NULL;
    }
    ALOGV("native_get_videoeditor_profile \
        inWidth:%d inHeight:%d,outWidth:%d, outHeight:%d",\
        maxInputFrameWidth,maxInputFrameHeight,\
        maxOutputFrameWidth,maxOutputFrameHeight);

    jclass VideoEditorProfileClazz =
        env->FindClass("android/media/videoeditor/VideoEditorProfile");
    jmethodID VideoEditorProfileConstructorMethodID =
        env->GetMethodID(VideoEditorProfileClazz, "<init>", "(IIII)V");
    return env->NewObject(VideoEditorProfileClazz,
                          VideoEditorProfileConstructorMethodID,
                          maxInputFrameWidth,
                          maxInputFrameHeight,
                          maxOutputFrameWidth,
                          maxOutputFrameHeight);
}
static jint
android_media_MediaProfiles_native_get_videoeditor_export_profile(
    JNIEnv *env, jobject thiz, jint codec)
{
    ALOGV("android_media_MediaProfiles_native_get_export_profile index ");
    int profile =0;
    profile = sProfiles->getVideoEditorExportParamByName("videoeditor.export.profile", codec);
    // Check the values retrieved
    if (profile == -1) {
        jniThrowException(env, "java/lang/RuntimeException",\
            "Error retrieving videoeditor export profile params");
        return -1;
    }
    return static_cast<jint>(profile);
}

static jint
android_media_MediaProfiles_native_get_videoeditor_export_level(
    JNIEnv *env, jobject thiz, jint codec)
{
    ALOGV("android_media_MediaProfiles_native_get_export_level");
    int level =0;
    level = sProfiles->getVideoEditorExportParamByName("videoeditor.export.level", codec);
    // Check the values retrieved
    if (level == -1) {
        jniThrowException(env, "java/lang/RuntimeException",\
            "Error retrieving videoeditor export level params");
        return -1;
    }
    return static_cast<jint>(level);
}
static JNINativeMethod gMethodsForEncoderCapabilitiesClass[] = {
    {"native_init",                            "()V",                    (void *)android_media_MediaProfiles_native_init},
    {"native_get_num_file_formats",            "()I",                    (void *)android_media_MediaProfiles_native_get_num_file_formats},
    {"native_get_file_format",                 "(I)I",                   (void *)android_media_MediaProfiles_native_get_file_format},
    {"native_get_num_video_encoders",          "()I",                    (void *)android_media_MediaProfiles_native_get_num_video_encoders},
    {"native_get_num_audio_encoders",          "()I",                    (void *)android_media_MediaProfiles_native_get_num_audio_encoders},

    {"native_get_video_encoder_cap",           "(I)Landroid/media/EncoderCapabilities$VideoEncoderCap;",
                                                                         (void *)android_media_MediaProfiles_native_get_video_encoder_cap},

    {"native_get_audio_encoder_cap",           "(I)Landroid/media/EncoderCapabilities$AudioEncoderCap;",
                                                                         (void *)android_media_MediaProfiles_native_get_audio_encoder_cap},
};

static JNINativeMethod gMethodsForCamcorderProfileClass[] = {
    {"native_init",                            "()V",                    (void *)android_media_MediaProfiles_native_init},
    {"native_get_camcorder_profile",           "(II)Landroid/media/CamcorderProfile;",
                                                                         (void *)android_media_MediaProfiles_native_get_camcorder_profile},
    {"native_has_camcorder_profile",           "(II)Z",
                                                                         (void *)android_media_MediaProfiles_native_has_camcorder_profile},
#ifdef VENDOR_EDIT
//Xianfen.Fu@Prd.Video.VideoCodec,2012/06/05,Add for camcordprofile
// work around for Camera build error, open below MTK API
//#ifndef ANDROID_DEFAULT_CODE
    {"native_get_camcorder_profile_caps", "(I)Ljava/lang/String;", (void *)android_media_MediaProfiles_native_get_camcorder_profile_caps}, 
//#endif 
#endif /*VENDOR_EDIT*/
};

static JNINativeMethod gMethodsForDecoderCapabilitiesClass[] = {
    {"native_init",                            "()V",                    (void *)android_media_MediaProfiles_native_init},
    {"native_get_num_video_decoders",          "()I",                    (void *)android_media_MediaProfiles_native_get_num_video_decoders},
    {"native_get_num_audio_decoders",          "()I",                    (void *)android_media_MediaProfiles_native_get_num_audio_decoders},
    {"native_get_video_decoder_type",          "(I)I",                   (void *)android_media_MediaProfiles_native_get_video_decoder_type},
    {"native_get_audio_decoder_type",          "(I)I",                   (void *)android_media_MediaProfiles_native_get_audio_decoder_type},
};

static JNINativeMethod gMethodsForCameraProfileClass[] = {
    {"native_init",                            "()V",                    (void *)android_media_MediaProfiles_native_init},
    {"native_get_num_image_encoding_quality_levels",
                                               "(I)I",                   (void *)android_media_MediaProfiles_native_get_num_image_encoding_quality_levels},
    {"native_get_image_encoding_quality_level","(II)I",                   (void *)android_media_MediaProfiles_native_get_image_encoding_quality_level},
};

static JNINativeMethod gMethodsForVideoEditorProfileClass[] = {
    {"native_init",                            "()V",         (void *)android_media_MediaProfiles_native_init},
    {"native_get_videoeditor_profile", "()Landroid/media/videoeditor/VideoEditorProfile;", (void *)android_media_MediaProfiles_native_get_videoeditor_profile},
    {"native_get_videoeditor_export_profile", "(I)I", (void *)android_media_MediaProfiles_native_get_videoeditor_export_profile},
    {"native_get_videoeditor_export_level", "(I)I", (void *)android_media_MediaProfiles_native_get_videoeditor_export_level},
};

static const char* const kEncoderCapabilitiesClassPathName = "android/media/EncoderCapabilities";
static const char* const kDecoderCapabilitiesClassPathName = "android/media/DecoderCapabilities";
static const char* const kCamcorderProfileClassPathName = "android/media/CamcorderProfile";
static const char* const kCameraProfileClassPathName = "android/media/CameraProfile";
static const char* const kVideoEditorProfileClassPathName =
    "android/media/videoeditor/VideoEditorProfile";

// This function only registers the native methods, and is called from
// JNI_OnLoad in android_media_MediaPlayer.cpp
int register_android_media_MediaProfiles(JNIEnv *env)
{
    int ret1 = AndroidRuntime::registerNativeMethods(env,
               kEncoderCapabilitiesClassPathName,
               gMethodsForEncoderCapabilitiesClass,
               NELEM(gMethodsForEncoderCapabilitiesClass));

    int ret2 = AndroidRuntime::registerNativeMethods(env,
               kCamcorderProfileClassPathName,
               gMethodsForCamcorderProfileClass,
               NELEM(gMethodsForCamcorderProfileClass));

    int ret3 = AndroidRuntime::registerNativeMethods(env,
               kDecoderCapabilitiesClassPathName,
               gMethodsForDecoderCapabilitiesClass,
               NELEM(gMethodsForDecoderCapabilitiesClass));

    int ret4 = AndroidRuntime::registerNativeMethods(env,
               kCameraProfileClassPathName,
               gMethodsForCameraProfileClass,
               NELEM(gMethodsForCameraProfileClass));

    int ret5 = AndroidRuntime::registerNativeMethods(env,
               kVideoEditorProfileClassPathName,
               gMethodsForVideoEditorProfileClass,
               NELEM(gMethodsForVideoEditorProfileClass));

    // Success if all return values from above are 0
    return (ret1 || ret2 || ret3 || ret4 || ret5);
}
