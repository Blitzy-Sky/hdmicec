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
SUBDIRS =
AM_CXXFLAGS = -pthread -Wall -I${top_srcdir}/osal/include -I${top_srcdir}/ccec/include

lib_LTLIBRARIES = librdkvHAL.la libAidlHAL.la libRCEC.la

AM_LDFLAGS = -ltelemetry_msgsender

# rdkvHAL library
librdkvHAL_la_SOURCES = factoryImpl/vHAL.cpp
librdkvHAL_la_CXXFLAGS = $(AM_CXXFLAGS)
librdkvHAL_la_LDFLAGS = -lpthread
librdkvHAL_la_LIBADD = -lRCECOSHal -L${top_builddir}/osal/src/.libs

# AidlHAL library
libAidlHAL_la_SOURCES = factoryImpl/AidlHAL.cpp
libAidlHAL_la_CXXFLAGS = $(AM_CXXFLAGS)
libAidlHAL_la_LDFLAGS = -lpthread

# Main RCEC library
libRCEC_la_SOURCES = CECFrame.cpp \
                     Util.cpp \
                     DriverImpl.cpp \
                     LibCCEC.cpp \
                     Bus.cpp \
                     OpCode.cpp \
                     Connection.cpp \
                     Driver.cpp \
                     MessageDecoder.cpp \
                     factoryImpl/HDMICecHalFactory.cpp

libRCEC_la_LDFLAGS = -lpthread
libRCEC_la_LIBADD = librdkvHAL.la libAidlHAL.la -lRCECOSHal -L${top_builddir}/osal/src/.libs



