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
CFLAGS      := -g -fPIC -D_REENTRANT -Wall -std=c++17
LIBNAME     := RCEC
LIBNAMEFULL := lib$(LIBNAME).so

OBJS:= CECFrame.o \
	Connection.o \
	Driver.o \
	MessageDecoder.o \
	Bus.o \
	DriverImpl.o \
	DriverAidlImpl.o \
	LibCCEC.o \
	OpCode.o \
	Util.o \

# The AIDL/Binder back-end is built into libRCEC unconditionally, for every SOC
# vendor, so its include roots and link edges are not conditional on anything.
# Both are derived from the same two staging prefixes configure takes, which the
# RDK build orchestrator exports exactly as it already exports OPENSOURCE_BASE,
# GLIB_LIBRARY_PATH and GLIBS below:
#
#   HALIF_PREFIX             root of the staged rdk-halif-aidl tree
#   BINDER_SDK_DIR           root of the staged linux_binder_idl Binder SDK
#   BINDER_SDK_INCLUDE_DIR   optional override, for a split staging layout whose
#                            headers do not sit under BINDER_SDK_DIR
#   HALIF_LIB_DIR            optional override, for a layout that stages the stub
#                            libraries outside the rdk-halif-aidl tree
#   BINDER_LIB_DIR           optional override, likewise for libbinder/libutils
#
# A deployment that does not export them fails at compile time on a missing
# generated header, which is the intended outcome: the alternative is silently
# shipping a library that carries only one back-end.
BINDER_SDK_INCLUDE_DIR ?= $(BINDER_SDK_DIR)
HALIF_LIB_DIR ?= $(HALIF_PREFIX)/lib/halif
BINDER_LIB_DIR ?= $(BINDER_SDK_DIR)/lib/binder

AIDL_INCLUDE = -I$(HALIF_PREFIX)/hdmicec/0.1.0.0/include	\
	-I$(HALIF_PREFIX)/common/0.2.0.0/include	\
	-I$(HALIF_PREFIX)/common/current	\
	-I$(BINDER_SDK_INCLUDE_DIR)/include/binder_sdk	\

# The four direct link edges, in dependency order.  libbinder's own transitive
# closure - liblog, libbase, libcutils, libcutils_sockets - is staged and made
# loadable rather than named here, because nothing in the middleware references
# it.
AIDL_LIBS = -lhdmicec-v0.1.0.0-cpp -lcommon-v0.2.0.0-cpp -lbinder -lutils
AIDL_LDFLAGS = -L$(HALIF_LIB_DIR) -L$(BINDER_LIB_DIR)

INCLUDE = -I.\
	-I../include	\
	-I../../osal/include	\
	-I../drivers/include	\
	$(AIDL_INCLUDE)


CFLAGS+= $(INCLUDE)

LDFLAGS+= -L$(OPENSOURCE_BASE)/lib
LDFLAGS+=-L$(GLIB_LIBRARY_PATH)/
LDFLAGS+=$(GLIBS)
LDFLAGS += -L. -lpthread

all: clean library
	@echo "Build Finished...."

library: $(OBJS)
	@echo "Building $(LIBNAMEFULL) ...."
	mkdir -p install/lib
	$(CXX) $(OBJS) $(CFLAGS) -shared -o install/lib/$(LIBNAMEFULL) $(LDFLAGS) $(AIDL_LDFLAGS) $(AIDL_LIBS)

%.o: %.cpp
	@echo "Building $@ ...."
	$(CXX) -c $<  $(CFLAGS) -o $@

clean:
	@echo "Cleaning the directory..."
	@$(RM) $(OBJS) install
