LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

GSTREAMER_SDK_ROOT_ANDROID := ../../gstreamer-0.10-release

LIB_PATH := $(LOCAL_PATH)/../../libs/armeabi

SPICE_CLIENT_ANDROID_DEPS   := $(LOCAL_PATH)/../libs/deps

spice_objs := \

LOCAL_MODULE := webrtc_apm_test

LOCAL_SRC_FILES := \
    common/ring_buffer.c \
    common/resample.c \
	common/audio_process_util.c \
	w_log.c	\
    webrtc_apm_test.c

LOCAL_LDLIBS += $(spice_objs) \
                -ljnigraphics -llog -ldl -lstdc++ -lz -lc \
                -landroid -lOpenSLES -lEGL -lGLESv2

LOCAL_C_INCLUDES += $(LOCAL_PATH) \
                    $(LOCAL_PATH)/common \
                    $(SPICE_CLIENT_ANDROID_DEPS)/include \
                    $(LOCAL_PATH)/../extra/celt/include \
                    $(LOCAL_PATH)/../extra/opus-1.3.1/include \
                    $(LOCAL_PATH)/../extra/webrtc-android-apm-master/webrtc \
                    $(LOCAL_PATH)/../extra/webrtc-android-apm-master/webrtc_wrapper \
					$(LOCAL_PATH)/../extra/webrtc-android-apm-master \

SPICEFLAGS := \
	-DG_LOG_DOMAIN=\"GSpice\" \
	-DSW_CANVAS_CACHE \
	-DSPICE_GTK_LOCALEDIR=\"/usr/local/share/locale\" \
	-DHAVE_CONFIG_H \
	-DHAVE_CELT051 \
	-DHAVE_OPUS \
	-D_REENTRANT \
	-UHAVE_SYS_SHM_H \

LOCAL_CPPFLAGS := $(SPICEFLAGS) -std=c11 -O2 -Wall

LOCAL_CFLAGS := $(SPICEFLAGS) \
                -std=gnu99 -O2 -Wall -Wno-sign-compare \
                -Wno-deprecated-declarations -Wl,--no-undefined \
                -fPIC -lpthread -mfpu=neon -mfloat-abi=softfp -DNOLOGF -DRK3188

LOCAL_EXPORT_CFLAGS += $(LOCAL_CFLAGS)
LOCAL_EXPORT_LDLIBS += $(LOCAL_LDLIBS)

LOCAL_SHARED_LIBRARIES := celt051 opus webrtc_audio_preprocessing webrtc_wrapper
LOCAL_ARM_MODE := arm

# include $(BUILD_SHARED_LIBRARY)
include $(BUILD_EXECUTABLE)

