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
#                            headers do not sit under BINDER_SDK_DIR.  It is not
#                            defaulted to BINDER_SDK_DIR here: BINDER_SDK_DIR
#                            supplies two of the four candidate header layouts
#                            below on its own, so a default would only hide
#                            which prefix a resolved root actually came from
#   HALIF_LIB_DIR            optional override, for a layout that stages the stub
#                            libraries outside the rdk-halif-aidl tree
#   BINDER_LIB_DIR           optional override, likewise for libbinder/libutils
#
# A deployment that does not export them fails at compile time on a missing
# generated header, or - for the Binder headers, which are probed below - with
# the named error this file raises, which is the intended outcome: the
# alternative is silently shipping a library that carries only one back-end.
HALIF_LIB_DIR ?= $(HALIF_PREFIX)/lib/halif

# THE BINDER LIBRARY DIRECTORY IS PROBED, FOR THE SAME REASON THE HEADER ROOT
# BELOW IS.  configure.ac accepts two layouts for it - lib/binder, as the SDK's
# own build stages it, and then a flat lib/, as a Yocto-style sysroot presents it
# - so a hand build that assumed only the first would fail to link on a staging
# tree the Autotools build configures happily, which is the same asymmetry the
# header probe exists to remove.  The candidates and their order are exactly
# configure.ac's; each is skipped when BINDER_SDK_DIR is unset, so an unset
# prefix cannot expand to an absolute /lib/binder belonging to the host; and each
# is probed by libbinder.so itself rather than by the bare directory, because
# $(wildcard) cannot distinguish a directory from a file and a directory without
# libbinder.so cannot satisfy the -lbinder edge anyway.
#
# The probe is skipped entirely when BINDER_LIB_DIR was supplied from the
# environment or the command line, which is exactly what the ?= this replaces
# guaranteed - $(origin) is undefined only when nothing has set it.
ifeq ($(origin BINDER_LIB_DIR),undefined)
BINDER_LIB_CANDIDATES := \
	$(if $(strip $(BINDER_SDK_DIR)),$(BINDER_SDK_DIR)/lib/binder) \
	$(if $(strip $(BINDER_SDK_DIR)),$(BINDER_SDK_DIR)/lib)

BINDER_LIB_DIR := $(firstword \
	$(patsubst %/libbinder.so,%, \
		$(wildcard $(addsuffix /libbinder.so,$(BINDER_LIB_CANDIDATES)))))
endif

# THE BINDER HEADER ROOT IS PROBED, NOT ASSUMED, AND THE CANDIDATE LIST IS THE
# SAME FOUR LAYOUTS IN THE SAME ORDER configure.ac accepts.  It has to be: this
# hand-written makefile is the build every SOC vendor actually runs through
# rdk_build.sh, so a staging layout that satisfies Autotools and not this file
# would configure cleanly and then fail here - or, worse, be "fixed" by dropping
# the back-end.  The order is the one the AIDL snapshot's own CMake build
# resolves in: the local development layout first, then the split-staging
# override in both its spellings, then a flat include directory.
#
#   1  $(BINDER_SDK_DIR)/include/binder_sdk           development layout
#   2  $(BINDER_SDK_INCLUDE_DIR)/include/binder_sdk   split staging
#   3  $(BINDER_SDK_INCLUDE_DIR)/include              split staging, flat
#   4  $(BINDER_SDK_DIR)/include                      Yocto or sysroot, flat
#
# A CANDIDATE WHOSE PREFIX IS UNSET CONTRIBUTES NOTHING, which is why each one
# is wrapped in $(if ...).  Without that guard an unset prefix would expand to
# the absolute path /include/binder_sdk, and on a host that happens to have such
# a directory the probe would resolve to it and the build would compile against
# headers nobody staged.  configure.ac guards its four the same way, with
# `test -n`, for the same reason.
#
# EACH CANDIDATE IS PROBED BY THE HEADER THE GENERATED STUBS ACTUALLY INCLUDE,
# binder/IBinder.h, rather than by the bare directory.  $(wildcard) is an
# existence test that does not distinguish a directory from a file, so a
# directory probe would accept a stray file of the same name; and a root that
# does not carry binder/IBinder.h cannot work anyway, because every generated
# header reaches it as <binder/IBinder.h> through this -I.  So this test is
# never weaker than configure's `test -d` and turns one class of unstaged tree
# into the named error below instead of a confusing header-not-found later.
# $(wildcard) preserves the order of its arguments and $(firstword) then takes
# the first surviving candidate, so precedence is exactly the list above.
BINDER_INCLUDE_CANDIDATES := \
	$(if $(strip $(BINDER_SDK_DIR)),$(BINDER_SDK_DIR)/include/binder_sdk) \
	$(if $(strip $(BINDER_SDK_INCLUDE_DIR)),$(BINDER_SDK_INCLUDE_DIR)/include/binder_sdk) \
	$(if $(strip $(BINDER_SDK_INCLUDE_DIR)),$(BINDER_SDK_INCLUDE_DIR)/include) \
	$(if $(strip $(BINDER_SDK_DIR)),$(BINDER_SDK_DIR)/include)

BINDER_INCLUDE_DIR := $(firstword \
	$(patsubst %/binder/IBinder.h,%, \
		$(wildcard $(addsuffix /binder/IBinder.h,$(BINDER_INCLUDE_CANDIDATES)))))

# The check is skipped when the only goal is `clean`, so that removing objects
# still works on a host that never staged the SDK.  Every other goal compiles or
# links, and none of those can be honestly attempted without these headers.
BINDER_HEADER_GOALS := $(filter-out clean,$(MAKECMDGOALS))
ifeq ($(strip $(MAKECMDGOALS)),)
BINDER_HEADER_GOALS := all
endif

define BINDER_INCLUDE_NOT_FOUND
none of the four Binder SDK header layouts resolved, so libRCEC cannot be built
with the AIDL HDMI CEC back-end, and the back-end is not optional.  Export the
staging prefixes this makefile derives its paths from - HALIF_PREFIX for the
rdk-halif-aidl tree, BINDER_SDK_DIR for the linux_binder_idl Binder SDK, and
BINDER_SDK_INCLUDE_DIR only if that SDK's headers are staged apart from it -
exactly as the RDK build orchestrator already exports OPENSOURCE_BASE and
GLIB_LIBRARY_PATH.  Tried, in order, each skipped when its prefix is unset and
each probed for binder/IBinder.h beneath it:
  1  $$(BINDER_SDK_DIR)/include/binder_sdk
  2  $$(BINDER_SDK_INCLUDE_DIR)/include/binder_sdk
  3  $$(BINDER_SDK_INCLUDE_DIR)/include
  4  $$(BINDER_SDK_DIR)/include
with BINDER_SDK_DIR=[$(BINDER_SDK_DIR)] and BINDER_SDK_INCLUDE_DIR=[$(BINDER_SDK_INCLUDE_DIR)],
which left these candidates to probe: [$(strip $(BINDER_INCLUDE_CANDIDATES))]
endef

define BINDER_LIB_NOT_FOUND
neither Binder SDK library layout resolved, so libRCEC cannot be linked against
libbinder and libutils, and those edges are not optional.  Export BINDER_SDK_DIR
to the linux_binder_idl Binder SDK staging prefix, or set BINDER_LIB_DIR
directly when the libraries are staged apart from it.  Tried, in order, each
skipped when BINDER_SDK_DIR is unset and each probed for libbinder.so beneath it:
  1  $$(BINDER_SDK_DIR)/lib/binder
  2  $$(BINDER_SDK_DIR)/lib
with BINDER_SDK_DIR=[$(BINDER_SDK_DIR)], which left these candidates to probe:
[$(strip $(BINDER_LIB_CANDIDATES))]
endef

ifneq ($(strip $(BINDER_HEADER_GOALS)),)
ifeq ($(strip $(BINDER_INCLUDE_DIR)),)
$(error $(BINDER_INCLUDE_NOT_FOUND))
endif
ifeq ($(strip $(BINDER_LIB_DIR)),)
$(error $(BINDER_LIB_NOT_FOUND))
endif
endif

AIDL_INCLUDE = -I$(HALIF_PREFIX)/hdmicec/0.1.0.0/include	\
	-I$(HALIF_PREFIX)/common/0.2.0.0/include	\
	-I$(HALIF_PREFIX)/common/current	\
	-I$(BINDER_INCLUDE_DIR)	\

# The four direct link edges, in dependency order, and the reason they are wrapped.
#
# All four are required to be direct DT_NEEDED entries of the shipping
# libRCEC.so: AAP 0.4.2 names them as four unconditional edges and AAP 0.9.4
# verifies the produced ELF against the expected NEEDED set.  Naming them on the
# link line does not achieve that on its own.  --as-needed is the effective
# default in the GCC configurations this and the RDK toolchains use - measured
# here, where a plain "g++ -shared ... -lm" leaves no DT_NEEDED for libm at all -
# and it drops the edge for any library that no symbol in the output references.
# libcommon-v0.2.0.0-cpp is exactly that library: PropertyValue is reached only
# through the hdmicec stub library's own interface, so with --as-needed in force
# the fourth edge disappeared from the ELF and deleting its token from this line
# would have changed nothing observable.  That silence is what the wrapping below
# closes, and it is why CI asserts four SONAMEs rather than three.
#
# --no-as-needed is scoped to these four and --as-needed is restored immediately
# after them, so the libraries the compiler driver appends of its own accord
# (libstdc++, libm, libgcc_s, libc) keep exactly the disposition they have today
# and nothing else about this link changes.
#
# libbinder's own transitive closure - liblog, libbase, libcutils,
# libcutils_sockets - is staged and made loadable rather than named here, because
# nothing in the middleware references it.
AIDL_LIBS = -Wl,--no-as-needed -lhdmicec-v0.1.0.0-cpp -lcommon-v0.2.0.0-cpp -lbinder -lutils -Wl,--as-needed
# $(addprefix) rather than a bare -L on the probed directory: an empty value
# would otherwise emit a lone -L, which the driver reads as taking the NEXT
# token - the first entry of AIDL_LIBS - as its directory, turning a missing
# prefix into a link failure that names the wrong thing.  The goal-guarded
# error above is what an empty value normally reaches first; this keeps the
# flag well-formed on any path that does not.
AIDL_LDFLAGS = -L$(HALIF_LIB_DIR) $(addprefix -L,$(strip $(BINDER_LIB_DIR)))

INCLUDE = -I.\
	-I../include	\
	-I../../osal/include	\
	-I../drivers/include	\
	$(AIDL_INCLUDE)


CFLAGS+= $(INCLUDE)

# ---------------------------------------------------------------------------
# Binder wire ABI, and the Binder kernel UAPI, as build prerequisites.
#
# This file has no configure step, so the two decisions configure makes for the
# Autotools build are made here in make, from the same evidence and honouring the
# same override variable, so that both build systems settle on the same answer:
#
#   BINDER_IPC_32BIT   yes | no.  Optional override.  When it is not set, the
#                      value is DERIVED by measuring the staged libbinder (see
#                      below).  When it is set and disagrees with what was
#                      measured, the build stops: silently trusting either side
#                      would produce a library whose marshalling does not match
#                      the library it links against.
#
# Why the macro matters at all: it selects BINDER_CURRENT_PROTOCOL_VERSION (7
# with it, 8 without) and the width of binder_size_t / binder_uintptr_t (__u32
# with it, __u64 without) in the very UAPI structures DriverAidlImpl marshals
# into the driver.  A middleware compiled without it against an SDK built with
# it writes 64-bit fields into a 32-bit layout: nothing fails to build, and
# nothing fails at link, so the mismatch is invisible until the driver rejects
# or misreads the transaction at run time.
#
# How it is derived: android::Parcel::ipcSetDataReference takes a
# `const binder_size_t *`, so the staged libbinder's own exported symbol carries
# the decision in its Itanium mangling.  This reads the answer out of the
# artefact that will actually be linked, rather than out of a build variable that
# may not describe it.  Its declaration is
#
#   void ipcSetDataReference(const uint8_t *data, size_t dataSize,
#                            const binder_size_t *objects, size_t objectsCount,
#                            release_func relFunc);
#
# and TWO DIFFERENT WIDTHS appear in that one mangling.  Confusing them is the
# trap this detector exists to avoid, because only one of them is the wire ABI:
#
#   the DATA MODEL of the target.  `size_t` appears twice - `dataSize` and
#   `objectsCount` - and mangles `m` (unsigned long) on an LP64 target such as
#   x86_64 or aarch64 but `j` (unsigned int) on an ILP32 target such as armhf or
#   i386, which is a large part of the SOC population this build path serves.  It
#   describes the target's data model and says nothing whatever about the wire,
#   so BOTH forms are accepted here.  Because the two occurrences are the same
#   C++ type in one signature they must agree with each other: a candidate whose
#   two `size_t` characters differ is some other function, and keying the wire
#   decision off it would be keying it off a misparse, so it is rejected.
#
#   the WIRE ABI, which is what BINDER_IPC_32BIT actually selects.  Only the
#   middle `const binder_size_t *` carries it - `PKj` (const unsigned int *) for
#   the 32-bit wire ABI, `PKy` (const unsigned long long *) for the 64-bit one -
#   and it is therefore the sole basis for the decision below.  All four
#   combinations occur in the field: an ILP32 middleware speaks either wire ABI
#   and so does an LP64 one, which is exactly why the two widths cannot be read
#   as one.
#
# The symbol looked for is therefore `EPKh<s>PK<w><s>`, with the same <s> in both
# places, and what is carried forward from it is <w> alone.
#
# Anything requested other than a bare `clean` requires these prerequisites: a
# clean must stay possible on a host that has no staged SDK at all.
BINDER_ABI_LIB := $(BINDER_LIB_DIR)/libbinder.so

ifeq ($(strip $(MAKECMDGOALS)),clean)
BINDER_PREREQ_REQUIRED := no
else
BINDER_PREREQ_REQUIRED := yes
endif

# The symbol table is read the same way by the derivation and by the diagnostic
# in its error path, so that a failure reports exactly what the derivation looked
# at.  Recursively assigned, so it is text substituted into each of them rather
# than a command run here.
BINDER_ABI_SYMBOLS = { nm -D --defined-only "$(BINDER_ABI_LIB)" 2>/dev/null || \
		readelf -sW "$(BINDER_ABI_LIB)" 2>/dev/null; }

# Holds <w> - `PKj` or `PKy` - taken from the one candidate whose two <s>
# characters agree.  The agreement is enforced by the backreference in the grep
# pattern, which is why the pattern is a POSIX BRE; the sed then discards the
# data model and keeps the wire parameter, which is the whole of the decision.
#
# `sort -u | head -n 2` bounds the result exactly as before: a library exporting
# two symbols that DISAGREE about the wire reduces to two words, matches neither
# value below, and lands in `unknown` - the hard error - instead of silently
# taking the first.  Two that agree about the wire and differ only in data model
# reduce to one word, which is the right answer, because they make the same
# decision.
BINDER_ABI_WIRE_MANGLING := $(shell if [ -r "$(BINDER_ABI_LIB)" ]; then \
		$(BINDER_ABI_SYMBOLS) | \
		grep -o '19ipcSetDataReferenceEPKh\([jm]\)PK[jy]\1' | \
		sed -n 's/^19ipcSetDataReferenceEPKh[jm]PK\([jy]\)[jm]$$/PK\1/p' | \
		sort -u | head -n 2 | tr '\n' ' '; \
	fi)

# Every ipcSetDataReference mangling the staged library really exports, whatever
# its shape.  Recursively assigned, so the symbol table is read a second time
# only if the error below actually fires, and then only to report what was found
# in place of a recognised candidate - a mismatched-`size_t` symbol, a differently
# shaped signature, or nothing at all.
BINDER_ABI_CANDIDATES = $(shell if [ ! -r "$(BINDER_ABI_LIB)" ]; then \
		echo "none, $(BINDER_ABI_LIB) is not readable"; \
	else \
		ccec_found=$$($(BINDER_ABI_SYMBOLS) | \
			grep -o '19ipcSetDataReference[A-Za-z0-9_]*' | sort -u | \
			tr '\n' ' ' | sed 's/ *$$//'); \
		if [ -z "$$ccec_found" ]; then echo none; else echo "$$ccec_found"; fi; \
	fi)

ifeq ($(strip $(BINDER_ABI_WIRE_MANGLING)),PKj)
BINDER_ABI_DETECTED := yes
else
ifeq ($(strip $(BINDER_ABI_WIRE_MANGLING)),PKy)
BINDER_ABI_DETECTED := no
else
BINDER_ABI_DETECTED := unknown
endif
endif

ifeq ($(strip $(BINDER_IPC_32BIT)),)
ifeq ($(BINDER_ABI_DETECTED),unknown)
ifeq ($(BINDER_PREREQ_REQUIRED),yes)
$(error Cannot determine the Binder wire ABI of $(BINDER_ABI_LIB). The staged libbinder must export exactly one android::Parcel::ipcSetDataReference whose mangling is 19ipcSetDataReferenceEPKh<s>PK<w><s>, where <s> is the target's size_t - `j` on an ILP32 target such as armhf or i386, `m` on an LP64 one such as aarch64 or x86_64 - the SAME character in both places, and <w> alone is the wire ABI: `j` for the 32-bit one, so ...EPKhjPKjj on an ILP32 target and ...EPKhmPKjm on an LP64 one, or `y` for the 64-bit one, so ...EPKhjPKyj and ...EPKhmPKym respectively. Exported instead: $(BINDER_ABI_CANDIDATES). Check that BINDER_SDK_DIR (or BINDER_LIB_DIR) points at the staged linux_binder_idl tree built for this target and that nm or readelf is on PATH, or state the ABI explicitly with `make BINDER_IPC_32BIT=yes` / `make BINDER_IPC_32BIT=no` to match the SDK you staged)
endif
BINDER_IPC_32BIT := no
BINDER_ABI_SOURCE := an undetermined ABI on a clean-only invocation
else
BINDER_IPC_32BIT := $(BINDER_ABI_DETECTED)
BINDER_ABI_SOURCE := the staged libbinder at $(BINDER_ABI_LIB)
endif
else
ifeq ($(filter yes no,$(strip $(BINDER_IPC_32BIT))),)
$(error BINDER_IPC_32BIT was set to "$(BINDER_IPC_32BIT)", which is not a recognised value. Use BINDER_IPC_32BIT=yes for the 32-bit Binder wire ABI or BINDER_IPC_32BIT=no for the 64-bit one, or leave it unset to derive it from the staged libbinder)
endif
ifneq ($(BINDER_ABI_DETECTED),unknown)
ifneq ($(strip $(BINDER_IPC_32BIT)),$(BINDER_ABI_DETECTED))
$(error BINDER_IPC_32BIT=$(strip $(BINDER_IPC_32BIT)) disagrees with the staged libbinder at $(BINDER_ABI_LIB), which was measured as BINDER_IPC_32BIT=$(BINDER_ABI_DETECTED). One of the two is wrong: stage an SDK built for the ABI you asked for, or drop the override and let it be derived. Building either way would marshal Binder transactions in a layout libbinder does not use)
endif
endif
BINDER_ABI_SOURCE := the explicit BINDER_IPC_32BIT=$(strip $(BINDER_IPC_32BIT)) override
endif

ifeq ($(strip $(BINDER_IPC_32BIT)),yes)
AIDL_ABI_CFLAGS := -DBINDER_IPC_32BIT
BINDER_ABI_PROTOCOL := 7
else
AIDL_ABI_CFLAGS :=
BINDER_ABI_PROTOCOL := 8
endif

# Into CFLAGS, which is what every compile command below expands, so that every
# translation unit that can see a Binder header sees the same wire layout.
CFLAGS+= $(AIDL_ABI_CFLAGS)

$(info libRCEC Binder wire ABI: BINDER_IPC_32BIT=$(strip $(BINDER_IPC_32BIT)), protocol $(BINDER_ABI_PROTOCOL), from $(BINDER_ABI_SOURCE))

# The Binder kernel UAPI is a HARD PREREQUISITE of libRCEC, not an optional
# extra.  DriverAidlImpl's transport preflight marshals struct binder_write_read
# and struct binder_version and compares BINDER_CURRENT_PROTOCOL_VERSION, so a
# build without the header cannot produce a working AIDL back-end - and a build
# that quietly degraded instead of stopping would ship a library that links
# Binder, reports success, and can never select the AIDL back-end on any
# platform.  The probe exercises the three constructs the preflight uses rather
# than testing for the header's presence, so a stub or truncated header fails
# here rather than mid-compile.
ifeq ($(BINDER_PREREQ_REQUIRED),yes)
# The header is pulled in with -include rather than with a directive in the probe
# text, so that no `#` has to survive make's own comment stripping on its way to
# the shell.
BINDER_UAPI_PROBE := $(shell printf '%s\n' \
		'int ccec_binder_uapi_probe(void);' \
		'int ccec_binder_uapi_probe(void) {' \
		'    struct binder_write_read bwr;' \
		'    struct binder_version ver;' \
		'    binder_uintptr_t addr = (binder_uintptr_t)0;' \
		'    bwr.write_size = 0;' \
		'    bwr.write_buffer = addr;' \
		'    ver.protocol_version = BINDER_CURRENT_PROTOCOL_VERSION;' \
		'    return (int)(bwr.write_size + (unsigned long)ver.protocol_version);' \
		'}' | $(CXX) -x c++ -include linux/android/binder.h -c - $(CFLAGS) -o /dev/null > /dev/null 2>&1 && echo yes || echo no)
ifneq ($(strip $(BINDER_UAPI_PROBE)),yes)
$(error The Binder kernel UAPI is unusable: linux/android/binder.h could not be compiled with struct binder_write_read, struct binder_version, binder_uintptr_t and BINDER_CURRENT_PROTOCOL_VERSION. libRCEC carries the AIDL back-end unconditionally and cannot be built without it. Install the kernel UAPI headers for the target (Debian/Ubuntu linux-libc-dev; Yocto/OE the kernel-headers or linux-libc-headers recipe already present in most sysroots), or add the sysroot that provides linux/android/binder.h to CFLAGS. Reproduce with: $(CXX) -x c++ -include linux/android/binder.h -fsyntax-only /dev/null $(CFLAGS))
endif
endif
# ---------------------------------------------------------------------------

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
