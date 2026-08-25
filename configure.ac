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
dnl                                               -*- Autoconf -*-
dnl Process this file with autoconf to produce a configure script.

dnl AC_PREREQ([2.69])
AC_INIT(hdmicec, 1.0, balaji_punnuru@cable.comcast.com,rmfwebkitifc)
AC_CONFIG_SRCDIR([osal/src/ConditionVariable.cpp])
AM_CONFIG_HEADER(cfg/config.h)
AC_CONFIG_MACRO_DIR([cfg])
AC_CONFIG_AUX_DIR([cfg])
AM_INIT_AUTOMAKE([foreign no-dist-gzip dist-bzip2 1.9])
LT_INIT

dnl subdirectories.

dnl Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_CPP
AC_PROG_CXXCPP

dnl use pretty build output with automake >= 1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],
  [AM_DEFAULT_VERBOSITY=1
   AC_SUBST(AM_DEFAULT_VERBOSITY)])




dnl **********************************
dnl checks for dependencies
dnl **********************************
AC_HEADER_STDC

dnl Checks for header files.
AC_CHECK_HEADERS([limits.h memory.h stdlib.h string.h sys/socket.h unistd.h])

dnl Checks for typedefs, structures, and compiler characteristics.
dnl AC_CHECK_HEADER_STDBOOL
AC_TYPE_UINT8_T

dnl Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([memset strdup strerror])

PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 0.10.28])

dnl **********************************
dnl HDMI CEC AIDL / Binder HAL back-end
dnl **********************************
dnl libRCEC carries BOTH HAL back-ends in every build for every SOC vendor:
dnl the legacy in-process C ABI and the out-of-process com.rdk.hal.hdmicec
dnl AIDL/Binder HAL.  Which one is used is decided at RUN TIME, exactly once,
dnl by Driver::getInstance in ccec/src/Driver.cpp on the basis of a query for
dnl the "HdmiCec" Binder service.  There is therefore deliberately no
dnl --enable/--disable switch and no AM_CONDITIONAL for the AIDL back-end
dnl here: a configure-time toggle would make the linkage conditional on
dnl vendor, variant or configuration, which is precisely what this migration
dnl forbids.  The dependency is as mandatory as GLIB above, so - following the
dnl bare PKG_CHECK_MODULES call on the preceding line, which supplies no
dnl action-if-not-found and therefore aborts configure - every check below is
dnl hard and fails configure outright when the dependency is missing.
dnl
dnl rdk-halif-aidl ships no pkg-config metadata: it contains no .pc file, no
dnl .pc.in template and no pkgconfig directory anywhere.  PKG_CHECK_MODULES
dnl cannot be used for these dependencies, so the headers and the libraries
dnl are probed directly instead.
dnl
dnl Two staging prefixes are the whole of the orchestrator contract.  Every
dnl include root and library directory below is derived from one of them by a
dnl fixed relative path, so the Autotools build and the hand-written RDK build
dnl in ccec/src/Makefile cannot end up configured with different sets.
AC_ARG_VAR([HALIF_PREFIX],
    [Root of the staged rdk-halif-aidl tree supplying the AIDL stub headers and libraries])
AC_ARG_VAR([HALIF_LIB_DIR],
    [AIDL stub library directory; overrides HALIF_PREFIX/lib/halif for split staging layouts])
AC_ARG_VAR([BINDER_SDK_DIR],
    [Root of the staged linux_binder_idl 2.6.0 Binder SDK, holding lib/binder and include/binder_sdk])
AC_ARG_VAR([BINDER_SDK_INCLUDE_DIR],
    [Binder SDK header prefix; overrides BINDER_SDK_DIR for split staging layouts])

AC_ARG_WITH([halif-prefix],
    AS_HELP_STRING([--with-halif-prefix=DIR],
        [root of the staged rdk-halif-aidl tree supplying the AIDL stub headers
         and libraries; defaults to the sibling rdk-halif-aidl checkout. Same as
         the HALIF_PREFIX variable]),
    [if test "x$withval" = xyes || test "x$withval" = xno || test -z "$withval"; then
         AC_MSG_ERROR([--with-halif-prefix requires a directory argument])
     fi
     HALIF_PREFIX=$withval])

AC_ARG_WITH([binder-sdk],
    AS_HELP_STRING([--with-binder-sdk=DIR],
        [root of the staged linux_binder_idl Binder SDK supplying libbinder and
         libutils. Same as the BINDER_SDK_DIR variable]),
    [if test "x$withval" = xyes || test "x$withval" = xno || test -z "$withval"; then
         AC_MSG_ERROR([--with-binder-sdk requires a directory argument])
     fi
     BINDER_SDK_DIR=$withval])

dnl Fall back to the sibling rdk-halif-aidl checkout when no prefix is given.
dnl srcdir may be relative while the substituted flags are consumed from build
dnl subdirectories, so the value is made absolute here.
if test -z "$HALIF_PREFIX"; then
    if test -d "$srcdir/../rdk-halif-aidl"; then
        HALIF_PREFIX=`cd "$srcdir/../rdk-halif-aidl" && pwd`
    fi
fi

dnl Four include roots are required to compile ccec/src/DriverAidlImpl.cpp:
dnl   1. HALIF_PREFIX/hdmicec/0.1.0.0/include  the generated com/rdk/hal/hdmicec
dnl                                            client stubs and server bases
dnl   2. HALIF_PREFIX/common/0.2.0.0/include   com/rdk/hal/PropertyValue.h, the
dnl                                            frozen import the hdmicec 0.1.0.0
dnl                                            snapshot was generated and pinned
dnl                                            against - 0.2.0.0, deliberately
dnl                                            NOT the newer common release
dnl   3. HALIF_PREFIX/common/current           halcompat.h, which is NOT inside
dnl                                            root 2, so the two snapshot roots
dnl                                            alone do not reach it
dnl   4. the Binder SDK header root            binder/, utils/ and android-base/
dnl                                            pulled in by the generated headers
dnl A root present in one consumer and absent from another is the failure this
dnl single derived set exists to prevent, so all four are resolved once here.
dnl
dnl CI hazard: roots 2 and 3 live under common/, outside the hdmicec/-only
dnl sparse checkout that rdk-halif-aidl is documented with.  A fresh sparse
dnl clone fails the halcompat.h probe below until common/ is materialised.  The
dnl fix belongs in the workflow files, not here; this note exists so that the
dnl failure is diagnosable rather than mysterious.
AIDL_CPPFLAGS=""
hdmicec_aidl_missing=""

AC_MSG_CHECKING([for the generated AIDL stub include root])
if test -n "$HALIF_PREFIX" && test -d "$HALIF_PREFIX/hdmicec/0.1.0.0/include"; then
    AIDL_CPPFLAGS="$AIDL_CPPFLAGS -I$HALIF_PREFIX/hdmicec/0.1.0.0/include"
    AC_MSG_RESULT([$HALIF_PREFIX/hdmicec/0.1.0.0/include])
else
    hdmicec_aidl_missing="$hdmicec_aidl_missing $HALIF_PREFIX/hdmicec/0.1.0.0/include"
    AC_MSG_RESULT([not found])
fi

AC_MSG_CHECKING([for the frozen common 0.2.0.0 include root])
if test -n "$HALIF_PREFIX" && test -d "$HALIF_PREFIX/common/0.2.0.0/include"; then
    AIDL_CPPFLAGS="$AIDL_CPPFLAGS -I$HALIF_PREFIX/common/0.2.0.0/include"
    AC_MSG_RESULT([$HALIF_PREFIX/common/0.2.0.0/include])
else
    hdmicec_aidl_missing="$hdmicec_aidl_missing $HALIF_PREFIX/common/0.2.0.0/include"
    AC_MSG_RESULT([not found])
fi

AC_MSG_CHECKING([for the halcompat.h include root])
if test -n "$HALIF_PREFIX" && test -d "$HALIF_PREFIX/common/current"; then
    AIDL_CPPFLAGS="$AIDL_CPPFLAGS -I$HALIF_PREFIX/common/current"
    AC_MSG_RESULT([$HALIF_PREFIX/common/current])
else
    hdmicec_aidl_missing="$hdmicec_aidl_missing $HALIF_PREFIX/common/current"
    AC_MSG_RESULT([not found])
fi

dnl The Binder header root differs between a local development layout and a
dnl Yocto or sysroot staging layout, so the candidates are tried in the same
dnl order the AIDL snapshot's own CMake build resolves them: the development
dnl layout first, then the split-staging override, then a flat include
dnl directory.  A candidate whose base variable is unset is skipped, so that it
dnl cannot expand to an absolute /include/... path that happens to match some
dnl unrelated host directory.
AC_MSG_CHECKING([for the Binder SDK header root])
if test -n "$BINDER_SDK_DIR" && test -d "$BINDER_SDK_DIR/include/binder_sdk"; then
    BINDER_INCLUDE_DIR="$BINDER_SDK_DIR/include/binder_sdk"
elif test -n "$BINDER_SDK_INCLUDE_DIR" && test -d "$BINDER_SDK_INCLUDE_DIR/include/binder_sdk"; then
    BINDER_INCLUDE_DIR="$BINDER_SDK_INCLUDE_DIR/include/binder_sdk"
elif test -n "$BINDER_SDK_INCLUDE_DIR" && test -d "$BINDER_SDK_INCLUDE_DIR/include"; then
    BINDER_INCLUDE_DIR="$BINDER_SDK_INCLUDE_DIR/include"
elif test -n "$BINDER_SDK_DIR" && test -d "$BINDER_SDK_DIR/include"; then
    BINDER_INCLUDE_DIR="$BINDER_SDK_DIR/include"
else
    BINDER_INCLUDE_DIR=""
fi
if test -n "$BINDER_INCLUDE_DIR"; then
    AIDL_CPPFLAGS="$AIDL_CPPFLAGS -I$BINDER_INCLUDE_DIR"
    AC_MSG_RESULT([$BINDER_INCLUDE_DIR])
else
    hdmicec_aidl_missing="$hdmicec_aidl_missing BINDER_SDK_DIR/include/binder_sdk"
    AC_MSG_RESULT([not found; relying on the ambient CPPFLAGS])
fi

dnl Library directories, derived from the same two prefixes.  Each falls back
dnl to the ambient LDFLAGS when it cannot be resolved, so a sysroot that stages
dnl these libraries on the default search path still configures cleanly; the
dnl link probe further down is what decides whether the result actually works.
AC_MSG_CHECKING([for the AIDL stub library directory])
if test -z "$HALIF_LIB_DIR" && test -n "$HALIF_PREFIX" && test -d "$HALIF_PREFIX/lib/halif"; then
    HALIF_LIB_DIR="$HALIF_PREFIX/lib/halif"
fi
if test -n "$HALIF_LIB_DIR"; then
    AC_MSG_RESULT([$HALIF_LIB_DIR])
else
    AC_MSG_RESULT([not found; relying on the ambient LDFLAGS])
fi

AC_MSG_CHECKING([for the Binder SDK library directory])
if test -n "$BINDER_SDK_DIR" && test -d "$BINDER_SDK_DIR/lib/binder"; then
    BINDER_LIB_DIR="$BINDER_SDK_DIR/lib/binder"
elif test -n "$BINDER_SDK_DIR" && test -d "$BINDER_SDK_DIR/lib"; then
    BINDER_LIB_DIR="$BINDER_SDK_DIR/lib"
else
    BINDER_LIB_DIR=""
fi
if test -n "$BINDER_LIB_DIR"; then
    AC_MSG_RESULT([$BINDER_LIB_DIR])
else
    AC_MSG_RESULT([not found; relying on the ambient LDFLAGS])
fi

AIDL_LDFLAGS=""
if test -n "$HALIF_LIB_DIR"; then
    AIDL_LDFLAGS="$AIDL_LDFLAGS -L$HALIF_LIB_DIR"
fi
if test -n "$BINDER_LIB_DIR"; then
    AIDL_LDFLAGS="$AIDL_LDFLAGS -L$BINDER_LIB_DIR"
fi

dnl The direct link edges, in dependency order.  libbinder's own transitive
dnl closure - liblog, libbase, libcutils and libcutils_sockets - is staged and
dnl made loadable rather than named here, because nothing in the middleware
dnl references it; a link failure caused by that closure being absent from the
dnl loader path is a staging problem, not a missing direct edge.
AIDL_LIBS="-lhdmicec-v0.1.0.0-cpp -lcommon-v0.2.0.0-cpp -lbinder -lutils"

dnl Every probe below runs against saved copies of the four flag variables and
dnl restores them afterwards, so that no probe flag leaks into the substituted
dnl values or into any later check.
hdmicec_saved_CPPFLAGS=$CPPFLAGS
hdmicec_saved_CXXFLAGS=$CXXFLAGS
hdmicec_saved_LDFLAGS=$LDFLAGS
hdmicec_saved_LIBS=$LIBS

AC_LANG_PUSH([C++])

dnl The C++17 requirement is pinned explicitly rather than inherited from the
dnl compiler default: the generated AIDL headers include <optional> and declare
dnl std::optional parameters, and halcompat.h opens a nested-namespace
dnl definition.  A host whose default dialect already covers C++17 hides the
dnl requirement, so a SOC toolchain with an older default has to fail here, at
dnl configure time, rather than part-way through compiling DriverAidlImpl.cpp.
dnl AX_CXX_COMPILE_STDCXX_17 is deliberately not used: autoconf-archive is not
dnl vendored and AC_CONFIG_MACRO_DIR points at cfg/, which carries no .m4 file,
dnl so that macro would survive unexpanded into configure.  The probe below is
dnl self-contained and exercises both constructs the real dependencies use.
m4_define([HDMICEC_CXX17_TESTPROGRAM],
    [AC_LANG_PROGRAM([[
#include <optional>
namespace hdmicec_cxx17_probe::inner { typedef int probe_type; }
]], [[
std::optional<hdmicec_cxx17_probe::inner::probe_type> probe(17);
return probe.value() == 17 ? 0 : 1;
]])])

AC_MSG_CHECKING([whether $CXX supports the C++17 constructs the AIDL headers require])
AIDL_CXXFLAGS=""
AC_COMPILE_IFELSE([HDMICEC_CXX17_TESTPROGRAM],
    [AC_MSG_RESULT([yes, with the ambient CXXFLAGS])],
    [CXXFLAGS="$hdmicec_saved_CXXFLAGS -std=c++17"
     AC_COMPILE_IFELSE([HDMICEC_CXX17_TESTPROGRAM],
         [AIDL_CXXFLAGS="-std=c++17"
          AC_MSG_RESULT([yes, with -std=c++17])],
         [AC_MSG_RESULT([no])
          AC_MSG_ERROR([a C++17 compiler is required to build the AIDL HDMI CEC
back-end, which is present in every build.  $CXX rejected both a
nested-namespace definition and std::optional with the ambient CXXFLAGS and
with -std=c++17 added.  Use a newer C++ compiler or set CXX accordingly.])])])

dnl Probes from here on use the dialect flag settled above.  The flag itself is
dnl substituted for the consuming Makefile.am files rather than injected into
dnl CXXFLAGS, so that the library and the test suites carry it explicitly.
CXXFLAGS="$hdmicec_saved_CXXFLAGS $AIDL_CXXFLAGS"
CPPFLAGS="$hdmicec_saved_CPPFLAGS $AIDL_CPPFLAGS"

dnl Every probe from here on begins by undefining VERSION.  AM_INIT_AUTOMAKE
dnl puts a package-version string into the confdefs prologue that autoconf
dnl prepends to each test program, and both halcompat.h and the generated
dnl headers spell the interface version as the identifier VERSION - as a static
dnl member on IHdmiCec and as I::VERSION at halcompat.h:171 - so the macro
dnl would expand inside them and the probe would fail for a reason that has
dnl nothing to do with the dependency being present.  This is confined to the
dnl probes: no source file under hdmicec includes cfg/config.h, so the shipped
dnl build never sees the macro.  A translation unit that came to include both
dnl cfg/config.h and halcompat.h would need the same undef.
dnl
dnl One include of halcompat.h proves three of the four roots at once: the
dnl header itself lives under common/current, and it pulls in
dnl binder/IServiceManager.h, binder/IInterface.h and utils/String16.h from the
dnl Binder SDK root.
AC_MSG_CHECKING([for halcompat.h and the Binder SDK headers])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#undef VERSION
#include <halcompat.h>
]], [[
return 0;
]])],
    [AC_MSG_RESULT([yes])],
    [AC_MSG_RESULT([no])
     AC_MSG_ERROR([the HAL interface headers required by the AIDL HDMI CEC
back-end were not found.  halcompat.h and the Binder SDK headers it includes
are mandatory: libRCEC always contains both HAL back-ends and chooses between
them at run time, so this dependency cannot be disabled.
  include roots tried:$AIDL_CPPFLAGS
  roots that do not exist:$hdmicec_aidl_missing
Set the two staging prefixes with --with-halif-prefix=DIR and
--with-binder-sdk=DIR, or through the HALIF_PREFIX and BINDER_SDK_DIR
variables.  When headers and libraries are staged separately, point
BINDER_SDK_INCLUDE_DIR at the Binder header prefix.  Note that halcompat.h
lives at HALIF_PREFIX/common/current, outside the hdmicec/-only sparse checkout
rdk-halif-aidl is documented with, so a sparse clone must materialise common/
as well.])])

dnl The second include proves the remaining root and the frozen common 0.2.0.0
dnl import that the generated header pulls in as com/rdk/hal/PropertyValue.h.
AC_MSG_CHECKING([for the generated com.rdk.hal.hdmicec AIDL client headers])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#undef VERSION
#include <com/rdk/hal/hdmicec/IHdmiCec.h>
]], [[
return ::com::rdk::hal::hdmicec::IHdmiCec::VERSION > 0 ? 0 : 1;
]])],
    [AC_MSG_RESULT([yes])],
    [AC_MSG_RESULT([no])
     AC_MSG_ERROR([the generated com.rdk.hal.hdmicec AIDL client headers were
not found, or they could not be compiled.  The C++ libbinder backend is
expected, so com/rdk/hal/hdmicec/IHdmiCec.h must declare the IHdmiCec
interface, and com/rdk/hal/PropertyValue.h must come from the frozen common
0.2.0.0 snapshot the hdmicec 0.1.0.0 snapshot was generated against.
  include roots tried:$AIDL_CPPFLAGS
  roots that do not exist:$hdmicec_aidl_missing
Set --with-halif-prefix=DIR or HALIF_PREFIX to the staged rdk-halif-aidl
tree.])])

LDFLAGS="$hdmicec_saved_LDFLAGS $AIDL_LDFLAGS"
LIBS="$AIDL_LIBS $hdmicec_saved_LIBS"

dnl The link probe resolves one real symbol from each layer: serviceName from
dnl the hdmicec stub library, defaultServiceManager from libbinder and the
dnl String16 constructor from libutils.  Naming libcommon-v0.2.0.0-cpp on the
dnl same command line makes the linker locate that file too, which is what
dnl confirms the pinned 0.2.0.0 ABI is the one actually staged.  The probe is
dnl only linked and never run, so it is safe on a host with no Binder driver.
AC_MSG_CHECKING([for the AIDL stub and Binder client libraries])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#undef VERSION
#include <com/rdk/hal/hdmicec/IHdmiCec.h>
#include <binder/IServiceManager.h>
#include <utils/String16.h>
]], [[
::android::sp< ::android::IServiceManager> sm = ::android::defaultServiceManager();
::android::String16 name(::com::rdk::hal::hdmicec::IHdmiCec::serviceName().c_str());
return sm != nullptr && name.size() > 0 ? 0 : 1;
]])],
    [AC_MSG_RESULT([yes])],
    [AC_MSG_RESULT([no])
     AC_MSG_ERROR([the AIDL stub and Binder client libraries could not be
linked.  All four are unconditional link edges of libRCEC, in this order:
  $AIDL_LIBS
searched in:$AIDL_LDFLAGS
libcommon-v0.2.0.0-cpp is required at exactly 0.2.0.0, the ABI the hdmicec
0.1.0.0 snapshot was generated and pinned against; a newer common release is
not a substitute.  Set --with-halif-prefix=DIR and --with-binder-sdk=DIR, or
the HALIF_PREFIX and BINDER_SDK_DIR variables, and use HALIF_LIB_DIR when the
stub libraries are staged away from the headers.  If the libraries are present
but the link still fails, check that libbinder's own closure - liblog, libbase,
libcutils and libcutils_sockets - is staged and on the loader path; those are
not direct edges and must not be added as such.])])

AC_LANG_POP([C++])

CPPFLAGS=$hdmicec_saved_CPPFLAGS
CXXFLAGS=$hdmicec_saved_CXXFLAGS
LDFLAGS=$hdmicec_saved_LDFLAGS
LIBS=$hdmicec_saved_LIBS

dnl The substituted contract consumed by ccec/src/Makefile.am,
dnl tests/L1Tests/Makefile.am and tests/L2Tests/Makefile.am:
dnl   AIDL_CPPFLAGS  the four -I include roots
dnl   AIDL_CXXFLAGS  the C++17 dialect flag this compiler needs, if any
dnl   AIDL_LDFLAGS   the -L stub and Binder library directories
dnl   AIDL_LIBS      the four direct -l edges, in dependency order
dnl The resolved directories are substituted individually as well, together
dnl with the two prefixes by way of AC_ARG_VAR above, so that a consumer can
dnl derive anything else it needs from the same inputs.
AC_SUBST([AIDL_CPPFLAGS])
AC_SUBST([AIDL_CXXFLAGS])
AC_SUBST([AIDL_LDFLAGS])
AC_SUBST([AIDL_LIBS])
AC_SUBST([BINDER_INCLUDE_DIR])
AC_SUBST([BINDER_LIB_DIR])

dnl A staging path that resolves to the wrong place must be visible in the
dnl configure output, because the failure it would otherwise produce is a
dnl library that silently carries only one back-end.
AC_MSG_NOTICE([AIDL HDMI CEC back-end: HALIF prefix .......... $HALIF_PREFIX])
AC_MSG_NOTICE([AIDL HDMI CEC back-end: AIDL stub libraries ... $HALIF_LIB_DIR])
AC_MSG_NOTICE([AIDL HDMI CEC back-end: Binder SDK prefix ..... $BINDER_SDK_DIR])
AC_MSG_NOTICE([AIDL HDMI CEC back-end: Binder header root .... $BINDER_INCLUDE_DIR])
AC_MSG_NOTICE([AIDL HDMI CEC back-end: Binder libraries ...... $BINDER_LIB_DIR])
AC_MSG_NOTICE([AIDL HDMI CEC back-end: C++17 flag ............ ${AIDL_CXXFLAGS:-none required}])

dnl Optional L1 unit tests support
AC_ARG_ENABLE([l1tests],
    AS_HELP_STRING([--enable-l1tests], [Enable L1 unit tests (requires gtest)]),
    [enable_l1tests=$enableval],
    [enable_l1tests=no])

AM_CONDITIONAL([ENABLE_L1TESTS], [test "x$enable_l1tests" = "xyes"])

if test "x$enable_l1tests" = "xyes"; then
    PKG_CHECK_MODULES([GTEST], [gtest >= 1.10.0], [],
        [AC_MSG_ERROR([Google Test not found. Install libgtest-dev or use --disable-l1tests])])
    AC_SUBST([GTEST_CFLAGS])
    AC_SUBST([GTEST_LIBS])
fi

AC_CONFIG_FILES([Makefile
		 cfg/Makefile
                 osal/Makefile
                 osal/src/Makefile
                 ccec/Makefile
                 ccec/src/Makefile
                 tests/Makefile
                 tests/L1Tests/Makefile
                 tests/L2Tests/Makefile])
AC_OUTPUT
