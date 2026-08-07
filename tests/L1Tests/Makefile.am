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

AM_CPPFLAGS = -I$(top_srcdir)/ccec/include \
              -I$(top_srcdir)/osal/include \
              -I$(top_srcdir)/mocks \
              -I$(top_srcdir)/mocks/hdmicec \
              -I$(GTEST_INCLUDE_DIR) \
              -I$(GMOCK_INCLUDE_DIR)

AM_CXXFLAGS = -Wall -std=c++14 -g

# Define test programs
noinst_PROGRAMS = run_L1Tests

# Main test runner
run_L1Tests_SOURCES = \
    test_main.cpp \
    ccec/test_CECFrame.cpp \
    ccec/test_Connection.cpp \
    ccec/test_Bus.cpp \
    ccec/test_LibCCEC.cpp \
    ccec/test_MessageEncoder.cpp \
    ccec/test_MessageDecoder.cpp \
    ccec/test_OpCode.cpp \
    ccec/test_Operands.cpp \
    ccec/test_Driver_Mock.cpp \
    ccec/test_Driver.cpp \
    ccec/test_DriverImpl_Async.cpp \
    ccec/test_Util.cpp \
    osal/test_ConditionVariable.cpp \
    osal/test_Mutex.cpp \
    osal/test_Thread.cpp \
    $(top_srcdir)/mocks/hdmicec/hdmi_cec_driver_mock.cpp

# ORDER IS SIGNIFICANT IN THIS LIST.  GoogleTest registers each TEST_F during static
# initialisation; those initialisers run in the link order of the objects, which follows this
# list, and the order in which the suites then RUN is the order they were registered.  This
# suite contains pre-existing order-fragile tests that pass in the established order and must
# not be modified on this pass, so the RELATIVE ORDER OF THE PRE-EXISTING ENTRIES ABOVE IS
# UNCHANGED from before this pass: the four units added here - ccec/test_DriverImpl_Async.cpp,
# ccec/test_Util.cpp, osal/test_Mutex.cpp and osal/test_Thread.cpp - are placed after the last
# pre-existing entry of their own layer, never between two pre-existing ones.  Keep that
# property when adding a translation unit: append it within its layer, and re-run the whole
# suite afterwards, because a reordering shows up as a failure in Bus/Connection/Driver rather
# than in the unit that was added.

# -lgcov is required by ccec/test_Util.cpp, which runs each of its cases in a forked child
# so that the non-atomic production log level is never written in the process the Bus
# threads live in, and calls __gcov_reset()/__gcov_dump() in that child so the production
# lines it drove are still counted - a child leaving through _exit() writes no coverage data
# otherwise.  Those two entry points live in their own members of libgcov.a
# (_gcov_reset.o, _gcov_dump.o) and are pulled in only by a direct reference, which is why
# naming the library here is not redundant with the -fprofile-arcs the coverage build already
# passes: that flag links libgcov but does not select those members.  Listing it
# unconditionally also keeps a build configured WITHOUT coverage instrumentation linkable,
# where the same two calls become no-ops over an empty counter list.
run_L1Tests_LDADD = \
    $(top_builddir)/ccec/src/libRCEC.la \
    $(top_builddir)/osal/src/libRCECOSHal.la \
    -lgtest -lgmock -lgcov

run_L1Tests_LDFLAGS = -pthread

# Run tests on 'make check'
TESTS = run_L1Tests

# Clean up test artifacts
CLEANFILES = *.log *.trs
