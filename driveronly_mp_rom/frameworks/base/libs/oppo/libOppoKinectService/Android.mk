# Copyright (C) 2009 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_PRELINK_MODULE:=false
LOCAL_MODULE_TAGS := optional


ifeq ($(strip $(TARGET_BUILD_VARIANT)), user)
	LOCAL_CFLAGS += -DMTK_USER_BUILD
endif


LOCAL_MODULE    := libOppoKinectService
LOCAL_SRC_FILES := NativeOppoKinect.cpp\
                   NativeActionTurn.cpp\
                   NativeActionPickUp.cpp\
                   NativeActionLean.cpp\
                   NativeActionBringToEar.cpp\
                   NativeActionStatic.cpp\
                   NativeActionCartWheel.cpp\
				   NativeActionSwing.cpp
                   
LOCAL_LDLIBS := -llog
LOCAL_SHARED_LIBRARIES += \
libcutils libutils
include $(BUILD_SHARED_LIBRARY)
