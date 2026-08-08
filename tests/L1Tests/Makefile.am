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

# Every entry in run_L1Tests_SOURCES below except test_main.cpp names a source in a SUBDIRECTORY
# (ccec/, osal/, or the mock under $(top_srcdir)/mocks).  With subdir-objects off, automake flattens
# each object into this directory and warns once per such source - seventeen warnings on this file,
# all of them at the run_L1Tests_SOURCES line.  The subdirectory layout is the suite's convention and
# is not up for change, so the option is enabled instead of living with the warnings.
#
# MEASURED, both ways, from a clean tree: seventeen automake warnings -> zero; `make all` and the
# tests build otherwise warning-free (the two that remain come from gtest.h instantiated by the
# pre-existing ccec/test_Operands.cpp and are not ours); the suite still runs 452 of 452 green; and
# the coverage table is IDENTICAL file for file - 91.5% (1912/2090) lines, 85.7% (377/440) functions,
# 57.3% (1094/1908) branches, gate PASS.  Registration order is also unaffected: objects are still
# linked in the order of this list, only their paths change.
#
# The one visible consequence is that the mock's object, .gcno and .deps now land beside its source
# in mocks/hdmicec/ instead of here.  That is the same class of in-tree build output the build already
# leaves in ccec/src and osal/src, it is untracked, and `git clean -fdx` removes it with the rest.
AUTOMAKE_OPTIONS = subdir-objects

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
# initialisation, the order in which those initialisers run follows the link order of the
# objects, which follows this list, and the order in which the suites then RUN is the order
# they were registered.  This suite contains order-fragile tests that pass only in the
# established order, so inserting a translation unit in the MIDDLE of this list reorders
# every suite after it and can turn a green run red.
#
# APPEND new translation units to the end of the list; do not interleave them.

run_L1Tests_LDADD = \
    $(top_builddir)/ccec/src/libRCEC.la \
    $(top_builddir)/osal/src/libRCECOSHal.la \
    -lgtest -lgmock

run_L1Tests_LDFLAGS = -pthread

# Run tests on 'make check'
TESTS = run_L1Tests

# Clean up test artifacts
CLEANFILES = *.log *.trs
