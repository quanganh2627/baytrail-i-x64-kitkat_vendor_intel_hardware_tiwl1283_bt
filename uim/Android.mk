ifeq ($(BOARD_HAVE_TI12XX),true)
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

#
# UIM Application
#

ifeq ($(BUILD_WITH_CHAABI_SUPPORT),true)
LOCAL_C_INCLUDES:= uim.h \
$(TOP)/external/bluetooth/bluez/ \
$(TARGET_OUT_HEADERS)/chaabi
else
LOCAL_C_INCLUDES:= uim.h \
$(TOP)/external/bluetooth/bluez/
endif

LOCAL_SRC_FILES:= \
	uim.c
LOCAL_SHARED_LIBRARIES:= libnetutils

ifeq ($(BUILD_WITH_CHAABI_SUPPORT),true)
LOCAL_CFLAGS:= -DBUILD_WITH_CHAABI_SUPPORT
LOCAL_STATIC_LIBRARIES:= CC6_UMIP_ACCESS CC6_ALL_BASIC_LIB
endif
LOCAL_CFLAGS += -m32
LOCAL_MODULE:=uim
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

endif
