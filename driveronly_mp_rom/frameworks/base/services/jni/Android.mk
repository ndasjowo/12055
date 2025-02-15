LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
#ifdef VENDOR_EDIT
#@OppoHook
#Add com_android_server_OppoService.cpp for oppo_service
LOCAL_SRC_FILES:= \
    com_android_server_AlarmManagerService.cpp \
    com_android_server_BatteryService.cpp \
    com_android_server_input_InputApplicationHandle.cpp \
    com_android_server_input_InputManagerService.cpp \
    com_android_server_input_InputWindowHandle.cpp \
    com_android_server_LightsService.cpp \
    com_android_server_power_PowerManagerService.cpp \
    com_android_server_SerialService.cpp \
    com_android_server_SystemServer.cpp \
    com_android_server_UsbDeviceManager.cpp \
    com_android_server_UsbHostManager.cpp \
    com_android_server_VibratorService.cpp \
    com_android_server_location_GpsLocationProvider.cpp \
    com_android_server_connectivity_Vpn.cpp \
    com_android_server_OppoBatteryService.cpp \
    ../../../../$(MTK_PATH_SOURCE)/frameworks-ext/base/services/jni/com_android_server_PerfService.cpp \
    onload.cpp
#endif /* VENDOR_EDIT */

LOCAL_C_INCLUDES += \
    $(JNI_H_INCLUDE) \
    frameworks/base/services \
    frameworks/base/core/jni \
    external/skia/include/core \
    libcore/include \
    libcore/include/libsuspend \
	$(call include-path-for, libhardware)/hardware \
	$(call include-path-for, libhardware_legacy)/hardware_legacy \

LOCAL_SHARED_LIBRARIES := \
    libandroid_runtime \
    libandroidfw \
    libcutils \
    libhardware \
    libhardware_legacy \
    libnativehelper \
    libsystem_server \
    libutils \
    libui \
    libinput \
    libskia \
    libgui \
    libusbhost \
    libsuspend \
    libdl

ifeq ($(MTK_AAL_SUPPORT),yes)
    LOCAL_C_INCLUDES += \
        $(MTK_PATH_PLATFORM)/hardware/aal/inc

    LOCAL_SHARED_LIBRARIES += \
        libaal
endif

ifeq ($(WITH_MALLOC_LEAK_CHECK),true)
    LOCAL_CFLAGS += -DMALLOC_LEAK_CHECK
endif

LOCAL_MODULE:= libandroid_servers

include $(BUILD_SHARED_LIBRARY)
