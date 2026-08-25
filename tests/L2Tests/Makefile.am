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

# L2 (integration) test tier for the CCEC middleware.
#
# This directory holds the tier that exercises a full CCEC round trip against
# each of the two HAL back-ends over REAL transport: the legacy in-process C
# ABI through the existing mock, and the out-of-process com.rdk.hal.hdmicec
# AIDL HAL through a separately hosted test-scope fake service.  The two must
# run in different processes, because libbinder resolves a service name that
# was registered in the calling process to the local BBinder - no proxy, no
# transaction across the driver and no client threadpool involvement - so an
# in-process fake cannot prove the transport at all.
#
# THIS FILE IS A PLACEHOLDER AND BUILDS NOTHING YET.  configure.ac already
# lists tests/L2Tests/Makefile in AC_CONFIG_FILES, and automake requires a
# Makefile.am for every such entry, so this file has to exist for the tree to
# autoreconf and configure at all.  Deliberately empty of targets until the
# runner, its integration case and the fake-service host land:
#
#     noinst_PROGRAMS = run_L2Tests fake_hdmi_cec_aidl_host
#     TESTS           = run_L2Tests
#
#     run_L2Tests_SOURCES = test_main.cpp ccec/test_DualPathIntegration.cpp \
#         $(top_srcdir)/mocks/hdmicec/hdmi_cec_driver_mock.cpp
#     fake_hdmi_cec_aidl_host_SOURCES = \
#         $(top_srcdir)/mocks/hdmicec/fake_hdmi_cec_aidl_service.cpp \
#         $(top_srcdir)/mocks/hdmicec/fake_hdmi_cec_aidl_service_host.cpp
#
# The legacy mock belongs to the RUNNER, because the legacy arm drives the
# legacy back-end through it and without it that arm has no HAL.  The fake
# belongs to the HOST and never to the runner, because the two are required to
# be separate processes.  Declaring a target whose sources do not exist yet
# would make `make` fail here rather than in the commit that adds them, which
# is why nothing is declared.
#
# Nothing recurses into this directory yet either: tests/Makefile.am still
# reads `SUBDIRS = L1Tests` under `if ENABLE_L1TESTS`, so the Makefile that
# configure generates from this file is inert until that line gains L2Tests.
#
# Compilation contract for the targets above, when they arrive: -std=c++17
# (the generated AIDL headers include <optional>), the six L1 include roots
# plus the four AIDL/Binder roots that configure resolves into AIDL_CPPFLAGS,
# and the four link edges in AIDL_LIBS - libhdmicec-v0.1.0.0-cpp,
# libcommon-v0.2.0.0-cpp, libbinder and libutils - in that dependency order.
# Executing the AIDL arm additionally needs a Binder-capable kernel and a
# running servicemanager, which a hosted CI runner does not provide.
