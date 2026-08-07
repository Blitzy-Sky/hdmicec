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
    ccec/test_Operand.cpp \
    ccec/test_Exception.cpp \
    $(top_srcdir)/mocks/hdmicec/hdmi_cec_driver_mock.cpp

# ORDER IS SIGNIFICANT IN THIS LIST, and the last two entries are placed at the END on
# purpose.  GoogleTest registers each TEST_F during static initialisation, and the order in
# which those initialisers run follows the link order of the objects, which follows this
# list; the order in which the suites then RUN is the order they were registered.  This suite
# contains pre-existing order-fragile tests that pass in the established order and must not
# be modified on this pass, so inserting a new translation unit in the MIDDLE of this list
# reorders every suite after it and turns a green run red - measured: adding
# ccec/test_Operand.cpp and ccec/test_Exception.cpp after ccec/test_Operands.cpp moved the
# Bus and Driver suites and produced a crash in DriverTest.MultipleClose, while appending the
# same two files here left all 519 tests green.  Append new translation units; do not
# interleave them.

run_L1Tests_LDADD = \
    $(top_builddir)/ccec/src/libRCEC.la \
    $(top_builddir)/osal/src/libRCECOSHal.la \
    -lgtest -lgmock

run_L1Tests_LDFLAGS = -pthread

# Run tests on 'make check'
TESTS = run_L1Tests

# Clean up test artifacts
CLEANFILES = *.log *.trs
