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
VHAL_LIBNAMEFULL := libvHAL.so

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

VHAL_OBJ := factoryImpl/vHAL.o

# Calculate include directory path explicitly
CCEC_INCLUDE_DIR := $(shell cd .. && pwd)/include

INCLUDE = -I.\
	-I$(CCEC_INCLUDE_DIR)	\
	-I../../osal/include	\
	-I../drivers/include	\
	-IfactoryImpl	\


CFLAGS+= $(INCLUDE)

LDFLAGS+= -L$(OPENSOURCE_BASE)/lib
LDFLAGS+=-L$(GLIB_LIBRARY_PATH)/
LDFLAGS+=$(GLIBS)
LDFLAGS += -L. -lpthread

all: clean library
	@echo "Build Finished...."

library: $(VHAL_LIBNAMEFULL) $(OBJS)
	@echo "Building $(LIBNAMEFULL) ...."
	mkdir -p install/lib
	$(CXX) $(OBJS) $(CFLAGS) -Linstall/lib -lvHAL $(LDFLAGS) -shared -o install/lib/$(LIBNAMEFULL)

$(VHAL_LIBNAMEFULL): $(VHAL_OBJ)
	@echo "Building $(VHAL_LIBNAMEFULL) ...."
	mkdir -p install/lib
	$(CXX) $(VHAL_OBJ) $(CFLAGS) $(LDFLAGS) -shared -o install/lib/$(VHAL_LIBNAMEFULL)

%.o: %.cpp
	@echo "Building $@ ...."
	$(CXX) -c $<  $(CFLAGS) -o $@

clean:
	@echo "Cleaning the directory..."
	@$(RM) $(OBJS) $(VHAL_OBJ) install

