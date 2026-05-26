##########################################################################
# If not stated otherwise in this file or this component's LICENSE
# file the following copyright and licenses apply:
#
# Copyright 2016 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##########################################################################
RM          := rm -rf 
CFLAGS      := -g -fPIC -D_REENTRANT -Wall
LIBNAME     := RCEC
LIBNAMEFULL := lib$(LIBNAME).so
AIDLHAL_LIBNAMEFULL := libAidlHAL.so

OBJS:= CECFrame.o \
	Connection.o \
	Driver.o \
	MessageDecoder.o \
	Bus.o \
	DriverImpl.o \
	LibCCEC.o \
	OpCode.o \
	Util.o \
	factoryImpl/HDMICecHalFactory.o \

AIDLHAL_OBJ := factoryImpl/AidlHAL.o

# Calculate AIDL include path relative to workspace
AIDL_GEN_DIR := $(shell cd ../../.. && pwd)/aidl/rdk-halif-aidl/gen/hdmicec/current
AIDL_H_DIR := $(AIDL_GEN_DIR)/h
BINDER_IDL_DIR := $(shell cd ../../.. && pwd)/aidl/rdk-halif-aidl/build-tools/linux_binder_idl
BINDER_INCLUDE := $(BINDER_IDL_DIR)/android/native/libs/binder/include
BINDER_NDK_INCLUDE := $(BINDER_IDL_DIR)/android/native/libs/binder/ndk/include_cpp
BINDER_UTILS_INCLUDE := $(BINDER_IDL_DIR)/android/core/libutils/include
BINDER_CUTILS_INCLUDE := $(BINDER_IDL_DIR)/android/core/libcutils/include
BINDER_LOG_INCLUDE := $(BINDER_IDL_DIR)/android/logging/liblog/include
BINDER_BASE_INCLUDE := $(BINDER_IDL_DIR)/android/libbase/include
BINDER_BUILD_DIR := $(BINDER_IDL_DIR)/aidl-generator/out

# Calculate include directory path explicitly
CCEC_INCLUDE_DIR := $(shell cd .. && pwd)/include

INCLUDE = -I.\
	-I$(CCEC_INCLUDE_DIR)	\
	-I../../osal/include	\
	-I../drivers/include	\
	-IfactoryImpl	\
	-I$(AIDL_H_DIR)	\
	-I$(BINDER_INCLUDE)	\
	-I$(BINDER_NDK_INCLUDE)	\
	-I$(BINDER_UTILS_INCLUDE)	\
	-I$(BINDER_CUTILS_INCLUDE)	\
	-I$(BINDER_LOG_INCLUDE)	\
	-I$(BINDER_BASE_INCLUDE)	\
	-I$(BINDER_IDL_DIR)/android/native/include	\


CFLAGS+= $(INCLUDE)

COMMON_LDFLAGS+= -L$(OPENSOURCE_BASE)/lib
COMMON_LDFLAGS+=-L$(GLIB_LIBRARY_PATH)/
COMMON_LDFLAGS+=$(GLIBS)
COMMON_LDFLAGS += -L. -lpthread

AIDLHAL_LDFLAGS := $(COMMON_LDFLAGS)
# Binder libraries - link if they exist
ifneq ($(wildcard $(BINDER_BUILD_DIR)/libbinder.a),)
AIDLHAL_LDFLAGS += -L$(BINDER_BUILD_DIR) -lbinder
endif
ifneq ($(wildcard $(BINDER_BUILD_DIR)/libutils.a),)
AIDLHAL_LDFLAGS += -L$(BINDER_BUILD_DIR) -lutils
endif
ifneq ($(wildcard $(BINDER_BUILD_DIR)/liblog.a),)
AIDLHAL_LDFLAGS += -L$(BINDER_BUILD_DIR) -llog
endif

all: clean library
	@echo "Build Finished...."

library: $(AIDLHAL_LIBNAMEFULL) $(OBJS)
	@echo "Building $(LIBNAMEFULL) ...."
	mkdir -p install/lib
	$(CXX) $(OBJS) $(CFLAGS) -Linstall/lib -lAidlHAL $(COMMON_LDFLAGS) -shared -o install/lib/$(LIBNAMEFULL)

$(AIDLHAL_LIBNAMEFULL): $(AIDLHAL_OBJ)
	@echo "Building $(AIDLHAL_LIBNAMEFULL) ...."
	mkdir -p install/lib
	$(CXX) $(AIDLHAL_OBJ) $(CFLAGS) $(AIDLHAL_LDFLAGS) -shared -o install/lib/$(AIDLHAL_LIBNAMEFULL)

%.o: %.cpp
	@echo "Building $@ ...."
	$(CXX) -c $<  $(CFLAGS) -o $@

clean:
	@echo "Cleaning the directory..."
	@$(RM) $(OBJS) $(AIDLHAL_OBJ) install

