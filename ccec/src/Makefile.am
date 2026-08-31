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
# deliberately no --enable switch and no Automake conditional guarding it.
#
# @AIDL_CPPFLAGS@ carries the four include roots the generated stubs need, and
# @AIDL_CXXFLAGS@ the C++17 dialect flag configure settled on.  Both are resolved
# by configure from the HALIF and Binder SDK prefixes.
#
# -std=c++17 IS SPELLED LITERALLY HERE, IN ADDITION TO @AIDL_CXXFLAGS@, and the
# duplication is deliberate.  The generated AIDL headers include <optional> and
# declare std::optional parameters, so the dialect is a requirement of this
# library and not of the host that happens to build it; a substitution is the
# wrong place to keep the only copy of it, because a host whose compiler already
# defaults to gnu++17 would leave that substitution empty and the requirement
# would be invisible until a SOC toolchain with an older default reached
# DriverAidlImpl.cpp.  The other three consumers - the hand-written
# ccec/src/Makefile, which has no configure step at all, and the two test
# Makefile.am files - already name it the same way, so this is the fourth of
# four rather than a new convention.
#
# THIS IS SAFE ONLY BECAUSE CONFIGURE REFUSES A COMPILER THAT REJECTS THE FLAG:
# the C++17 probe in configure.ac tests -std=c++17 first and errors out when it
# is not accepted, so no configured build can reach this line with a compiler
# that would reject it.  @AIDL_CXXFLAGS@ is kept as well - it is the same value,
# repeating an identical -std= is a no-op, and dropping it would silently break
# the substituted contract the two test Makefile.am files also consume.
AM_CXXFLAGS = -pthread -Wall -std=c++17 @AIDL_CXXFLAGS@ -I${top_srcdir}/osal/include -I${top_srcdir}/ccec/include @AIDL_CPPFLAGS@

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

# THE FOUR AIDL/BINDER EDGES ARE FORCED INTO DT_NEEDED FROM HERE, AND THE REASON
# IS MEASURED RATHER THAN STYLISTIC.
#
# All four have to be direct DT_NEEDED entries of the produced libRCEC: AAP 0.4.2
# names four unconditional edges and AAP 0.9.4 checks the ELF against the
# expected NEEDED set.  --as-needed is the effective default in these toolchains
# and it drops the edge for any library that no symbol in the output references.
# libcommon-v0.2.0.0-cpp is that library - PropertyValue is reached only through
# the hdmicec stub library's own interface - so it was absent from DT_NEEDED and
# deleting its token from AIDL_LIBS would have changed nothing observable.
#
# Closing that needs --no-as-needed to take effect on those -l options, and
# neither of the two obvious spellings does.  Measured on this tree:
#
#   * a bare "-Wl,--no-as-needed" is classified by libtool into $compiler_flags,
#     and archive_cmds is
#         $CC -shared $pic_flag $libobjs $deplibs $compiler_flags $wl-soname ...
#     so it lands AFTER the whole $deplibs list, where it cannot affect it;
#   * and Automake rejects it in libRCEC_la_LIBADD outright ("linker flags such
#     as ... belong in libRCEC_la_LDFLAGS").
#
# One comma-joined -Wl group carries its own scope with it: libtool splits it on
# commas and re-emits one -Wl, option per element IN ORDER, so --no-as-needed,
# the four libraries and the restoring --as-needed stay together wherever the
# group as a whole is placed.  That makes the wrapping independent of libtool's
# argument classification, which is what the two failures above turn on.
#
# --as-needed is restored immediately after the four, so the libraries the
# compiler driver appends of its own accord keep the disposition they have today.
# @AIDL_LIBS@ below is kept as well, so configure stays the one place that
# decides the list and libRCEC.la's dependency_libs still records the four edges
# the ordinary way; both forms name the same four libraries, ld sees each name
# twice and emits one DT_NEEDED per library.  The literal names here must
# therefore track AIDL_LIBS in configure.ac - a divergence fails the link on a
# library it cannot find, by name, rather than quietly dropping an edge.
libRCEC_la_LDFLAGS = -lpthread \
                     -Wl,--no-as-needed,-lhdmicec-v0.1.0.0-cpp,-lcommon-v0.2.0.0-cpp,-lbinder,-lutils,--as-needed
# The four direct AIDL/Binder link edges, in dependency order, follow the OSAL
# edge.  They are required whether or not a given platform has an AIDL HAL: the
# back-end's symbols must resolve at library link time, and the runtime query is
# what decides whether they are ever called.  @AIDL_LDFLAGS@ supplies the -L
# search paths both this list and the forcing group above depend on.
libRCEC_la_LIBADD = -lRCECOSHal -L${top_builddir}/osal/src/.libs @AIDL_LDFLAGS@ @AIDL_LIBS@
