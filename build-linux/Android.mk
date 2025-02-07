#
# Copyright (C) YuqiaoZhang(HanetakaChou)
# 
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published
# by the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
# 
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#

# https://developer.android.com/ndk/guides/android_mk

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := OpenCL

LOCAL_SRC_FILES := \
	$(LOCAL_PATH)/../loader/linux/icd_linux_envvars.c \
	$(LOCAL_PATH)/../loader/linux/icd_linux.c \
	$(LOCAL_PATH)/../loader/icd.c \
	$(LOCAL_PATH)/../loader/icd_dispatch_generated.c \
	$(LOCAL_PATH)/../loader/icd_dispatch.c

LOCAL_CFLAGS :=

ifeq (armeabi-v7a,$(TARGET_ARCH_ABI))
LOCAL_ARM_MODE := arm
LOCAL_ARM_NEON := true
else ifeq (arm64-v8a,$(TARGET_ARCH_ABI))
LOCAL_CFLAGS +=
else ifeq (x86,$(TARGET_ARCH_ABI))
LOCAL_CFLAGS += -mf16c
LOCAL_CFLAGS += -mfma
LOCAL_CFLAGS += -mavx2
else ifeq (x86_64,$(TARGET_ARCH_ABI))
LOCAL_CFLAGS += -mf16c
LOCAL_CFLAGS += -mfma
LOCAL_CFLAGS += -mavx2
else
LOCAL_CFLAGS +=
endif

LOCAL_CFLAGS += -Wall
LOCAL_CFLAGS += -Werror=return-type

LOCAL_CFLAGS += -DCL_NO_NON_ICD_DISPATCH_EXTENSION_PROTOTYPES
LOCAL_CFLAGS += -DCL_TARGET_OPENCL_VERSION=300
LOCAL_CFLAGS += -DOPENCL_ICD_LOADER_VERSION_MAJOR=3 
LOCAL_CFLAGS += -DOPENCL_ICD_LOADER_VERSION_MINOR=0 
LOCAL_CFLAGS += -DOPENCL_ICD_LOADER_VERSION_REV=6 

LOCAL_C_INCLUDES :=
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../OpenCL-Headers
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../loader

LOCAL_CPPFLAGS := 
LOCAL_CPPFLAGS += -std=c++20

LOCAL_LDFLAGS :=
LOCAL_LDFLAGS += -Wl,--enable-new-dtags
LOCAL_LDFLAGS += -Wl,-rpath,\$$ORIGIN
LOCAL_LDFLAGS += -Wl,--version-script,$(LOCAL_PATH)/../loader/linux/icd_exports.map

LOCAL_LDFLAGS += -ldl

LOCAL_STATIC_LIBRARIES :=

include $(BUILD_SHARED_LIBRARY)
