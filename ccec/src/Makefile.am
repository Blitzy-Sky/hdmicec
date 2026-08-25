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

# The AIDL/Binder HDMI CEC back-end is compiled into libRCEC unconditionally, so
# that both back-ends are present in every build for every SOC vendor and the
# choice between them is made at runtime rather than at build time.  There is
# deliberately no --enable switch and no AM_CONDITIONAL guarding it.
#
# @AIDL_CPPFLAGS@ carries the four include roots the generated stubs need, and
# @AIDL_CXXFLAGS@ the C++17 dialect flag this compiler requires: the generated
# headers use <optional>, so the dialect is pinned explicitly here rather than
# inherited from whatever the SOC toolchain happens to default to.  Both are
# resolved by configure from the HALIF and Binder SDK prefixes.
AM_CXXFLAGS = -pthread -Wall @AIDL_CXXFLAGS@ -I${top_srcdir}/osal/include -I${top_srcdir}/ccec/include @AIDL_CPPFLAGS@

lib_LTLIBRARIES = libRCEC.la

AM_LDFLAGS = -ltelemetry_msgsender

libRCEC_la_SOURCES = CECFrame.cpp \
                     Util.cpp \
                     DriverImpl.cpp \
                     DriverAidlImpl.cpp \
                     LibCCEC.cpp \
                     Bus.cpp \
                     OpCode.cpp \
                     Connection.cpp \
                     Driver.cpp \
                     MessageDecoder.cpp

libRCEC_la_LDFLAGS = -lpthread
# The four direct AIDL/Binder link edges, in dependency order, follow the OSAL
# edge.  They are required whether or not a given platform has an AIDL HAL: the
# back-end's symbols must resolve at library link time, and the runtime query is
# what decides whether they are ever called.
libRCEC_la_LIBADD = -lRCECOSHal -L${top_builddir}/osal/src/.libs @AIDL_LDFLAGS@ @AIDL_LIBS@
