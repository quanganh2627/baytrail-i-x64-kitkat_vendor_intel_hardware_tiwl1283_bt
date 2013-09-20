LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

ifeq ($(BUILD_WITH_SECURITY_FRAMEWORK),txei)
LOCAL_C_INCLUDES := \
	$(TARGET_OUT_HEADERS)/libtxei
LOCAL_CFLAGS:= -DBUILD_WITH_TXEI_SUPPORT
LOCAL_STATIC_LIBRARIES := \
	CC6_TXEI_UMIP_ACCESS CC6_ALL_BASIC_LIB
else ifeq ($(BUILD_WITH_SECURITY_FRAMEWORK),chaabi_token)
LOCAL_C_INCLUDES := \
	$(TARGET_OUT_HEADERS)/libdx_cc7
LOCAL_CFLAGS:= -DBUILD_WITH_TOKEN_SUPPORT
LOCAL_STATIC_LIBRARIES := \
	libdx_cc7_static
else ifeq ($(BUILD_WITH_SECURITY_FRAMEWORK),chaabi_legacy)
LOCAL_C_INCLUDES := \
	$(TARGET_OUT_HEADERS)/chaabi
LOCAL_CFLAGS:= -DBUILD_WITH_CHAABI_SUPPORT
LOCAL_STATIC_LIBRARIES := \
	CC6_UMIP_ACCESS CC6_ALL_BASIC_LIB
endif

LOCAL_SRC_FILES:= \
	bd_provisioning.c
LOCAL_CFLAGS += -m32
LOCAL_SHARED_LIBRARIES := \
	libc libcutils libcrypto

LOCAL_MODULE:= bd_prov
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)

