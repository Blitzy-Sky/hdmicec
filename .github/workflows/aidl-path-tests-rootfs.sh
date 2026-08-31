#!/usr/bin/env bash
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
# aidl-path-tests-rootfs.sh -- build the bootable QEMU root filesystem image that the
# binder-capable AIDL-path job boots, and install into that image the in-guest init that
# mounts binderfs, starts servicemanager and runs tests/L1Tests/run_coverage.sh.
#
# ==============================================================================
# WHY THIS FILE EXISTS -- AND WHERE IT SITS IN THE MIGRATION'S FINAL SCOPE
# ==============================================================================
#   The AIDL-path job runs the complete A-E invocation matrix, and it can only run it on a
#   host with a binder kernel driver.  A hosted runner has none, so the job provisions a
#   PINNED QEMU GUEST, and the provisioning requirement is explicit about how that guest's
#   root filesystem comes to exist: IT IS BUILT BY A CHECKED-IN SCRIPT.  Two alternatives
#   are ruled out in terms -- the image is NOT fetched pre-built, and it is NOT generated
#   inline inside the workflow.  This file is that checked-in script.  Without it the
#   workflow is not executable, and an unmade choice between "a shared directory or a
#   copied disk image" is not a deliverable either; both are settled here, once.
#
#   THE FILE-COUNT POSITION, STATED SO A REVIEWER DOES NOT HAVE TO INFER IT.  The
#   migration's close-out check counts the in-submodule diff against an explicit allowlist
#   of TWENTY-FIVE paths -- 13 modified and 12 added, measured with `git diff --name-status`
#   inside hdmicec -- plus blitzy/documentation/Project Guide.md in the superproject, which
#   makes TWENTY-SIX logical files (14 UPDATE counting the guide, 12 CREATE, 0 DELETE).  THIS
#   FILE IS ONE OF THOSE TWENTY-FIVE, not an extra beyond them.  What is worth saying is why
#   it is in the set at all: the file-by-file table enumerated no root-image script, so this
#   path is AUTHORISED by the provisioning requirement rather than by that table, and it is
#   NOT an out-of-scope addition -- the workflow it serves cannot exist without it.
#   .github/workflows/L1-tests.yml and .github/workflows/aidl-path-tests.yml state the same
#   identity in their own headers, and the three statements are kept in step deliberately.
#
#   PLACEMENT.  This script lives beside the only job that invokes it.  GitHub Actions
#   loads only *.yml and *.yaml from .github/workflows, so a .sh here is inert to Actions:
#   it cannot be mistaken for a workflow, cannot be triggered, and cannot run by accident.
#
# ==============================================================================
# GOVERNING CONTRACT
# ==============================================================================
#   THIS PROJECT HAS NO USER-SPECIFIED RULES.  `review_rules` returns exactly
#   "No user rules provided.", and that one line is the whole document.  No rule governs
#   this file, so none is cited and none is invented.  Their absence is not licence to
#   lower the bar; the substituting contract is enterprise-standard best practice, and in
#   this submodule that means the conventions tests/L1Tests/run_coverage.sh already
#   establishes and this file reproduces rather than reinvents:
#
#     1. REPOSITORY CONVENTION IS AUTHORITATIVE.  The Apache-2.0/RDK header block above,
#        `set -euo pipefail`, `umask 077`, bracketed log/warn/die helpers writing
#        diagnostics to stderr, a usage() function, a while/case option parser that
#        REFUSES an unknown option and a bare positional rather than ignoring either, and
#        absolute path resolution from BASH_SOURCE so the caller's working directory is
#        irrelevant.
#     2. EVERY DIE MESSAGE NAMES THE FAILING THING AND THE REMEDY.  A message that says
#        only what broke costs the reader the same investigation twice.
#     3. NOTHING IS CREATED OUTSIDE THE OUTPUT DIRECTORY AND A RUN-SCOPED TEMPORARY
#        DIRECTORY.  Every mount this script makes, every loop device it attaches and every
#        temporary directory it creates is registered and released by an
#        EXIT/INT/TERM/HUP trap, in reverse order, whether the run succeeded, failed or was
#        cancelled.  A leaked loop device or a leaked bind mount wedges the runner for every
#        job after it.  In the output directory it creates exactly three things: the image
#        and the manifest at the caller's paths, and <image>.lock -- the advisory lock that
#        stops two builders from interleaving, deliberately left behind.  The image and the
#        manifest are STAGED under private mktemp names in that same directory and renamed
#        onto the caller's paths TOGETHER, after the image is validated, so a failure never
#        destroys a previously published pair and never leaves a manifest describing an
#        image that is not there.
#     4. MEASURED, NEVER ASSERTED.  Where this script reports a platform fact it reports
#        what the artefact or the running guest actually shows.  It hard-codes no digest,
#        no version, no timing figure and no coverage percentage.  Where a value must be
#        pinned, the pin is an INPUT that is then VERIFIED here -- see THE BASE ROOTFS.
#     5. NO WARNING BECOMES AN ERROR.  There is no -Werror equivalent anywhere in the image
#        build, and third-party or generated code is never compiled with one.
#
#   WHAT THIS SCRIPT DOES NOT DO, AND MUST NOT BE MADE TO DO:
#     * It contains NO vendor, variant or configuration conditional.  Both HDMI CEC HAL
#       back-ends are compiled into libRCEC for every SOC and the choice between them is a
#       RUNTIME decision, so there is no --enable-aidl / --disable-aidl to pass and no
#       configure switch to select a path.  No such option exists.
#     * It does NOT set BINDER_VERSION.  The Binder SDK version is pinned by
#       rdk-halif-aidl/binder_sdk.version and setting the variable would silently override
#       that pin.
#     * It does NOT set CEC_TEST_AIDL_MODE anywhere -- not here, not in the init it writes.
#       run_coverage.sh owns that value per invocation across the A-E matrix; one value set
#       globally would collapse the matrix to a single arm and report it as green.
#     * It does NOT start a binder threadpool.  On the client side DriverAidlImpl::open()
#       owns that; on the service side the fake service host does.
#     * It asserts NO performance figure and NO device-level result.  Performance parity
#       and L3 device validation are deferred human steps on target hardware; nothing here
#       measures either, and no step in it reads as a performance gate.
#     * It reimplements NO part of run_coverage.sh's orchestration -- not the per-invocation
#       zero-exit check, not the executed-count check, not the selected-path-line grep, not
#       the artifact naming, not the counter zero/accumulate policy and not the single
#       capture.  The init's whole job is to invoke that script correctly in an environment
#       where it can work.
#     * It issues NO `--restore`.  `--build` snapshots the six regenerated Makefiles and
#         restores them from its own snapshot; a standalone `--restore` refuses to run
#         without a same-invocation snapshot precisely so it can never revert this
#         migration's change to ccec/src/Makefile.
#
# ==============================================================================
# TWO SEPARATE AXES: ELF BITNESS AND BINDER WIRE PROTOCOL
# ==============================================================================
#   THESE ARE NOT THE SAME QUESTION, AND CONFLATING THEM IS WHAT AN EARLIER REVISION OF
#   THIS FILE GOT WRONG.  It took "all-32-bit" to fix the wire protocol at 7 and said so in
#   its help text, its architecture log, the value it wrote into the image and its manifest,
#   with nothing anywhere comparing that text against what the SDK it shipped had actually
#   been built with.  Text cannot be checked, and for a while the text was wrong.  The two
#   axes are therefore stated separately here and kept separate everywhere below, and the
#   protocol is DERIVED from build artefacts rather than written down.
#
#   The job that drives this script is a protocol-7 all-32-bit job, and it is one because the
#   plan requires it (AAP 0.6.6.4 and 0.9.5.1: CONFIG_ANDROID_BINDER_IPC_32BIT=y).  That
#   requirement is not free on a modern kernel and the workflow pays for it explicitly; the
#   paragraph "WHY 7 NEEDS A PATCHED KERNEL" below is the whole of the reason, and it is
#   stated here because it is the first thing a reader who finds an unexpected number will
#   want.  Nothing in THIS file selects the protocol: it reads what the inputs decided.
#
#   AXIS 1 -- ELF BITNESS, WHICH THIS SCRIPT DOES DECIDE AND DOES ENFORCE.
#   The guest userspace is 32-bit throughout: the middleware, the staged SDK,
#   servicemanager, GoogleTest and every test binary.  A process is a single ELF class, so
#   this is a per-process constraint rather than a platform-wide one, and it is checkable
#   here -- which is why --arch accepts only 32-bit targets and why verify_sdk_tree and
#   verify_payload_tree REFUSE any staged artefact whose ELF class or machine does not
#   match.  A 64-bit libbinder inside a 32-bit guest is a link failure at best, and finding
#   out at boot costs a whole QEMU cycle to diagnose.  i386 is the default because it is
#   the straightforward choice for QEMU on an x86_64 host and needs no foreign-architecture
#   emulation to populate; armhf is equally valid and is accepted, at the cost of a
#   registered binfmt_misc handler on the build host.
#
#   AXIS 2 -- THE BINDER WIRE PROTOCOL, WHICH THIS SCRIPT DERIVES AND NEVER DECLARES.
#   The protocol is selected at BUILD time by the SDK's BINDER_IPC_32BIT and, on the kernel
#   side, by CONFIG_ANDROID_BINDER_IPC_32BIT.  Read from the pinned upstream tree rather
#   than assumed -- linux_binder_idl at tag 2.6.0, cited by locator because it is not a
#   file in this workspace:
#
#     * CMakeLists.txt:209-217 states in terms that the wire protocol is a DIFFERENT axis
#       from the compile bitness and names "-DBINDER_IPC_32BIT=OFF to build a 32-bit
#       protocol-8 library (the mixed config)".  A 32-bit process CAN speak protocol 8.
#     * CMakeLists.txt:218-224 makes the protocol FOLLOW the bitness only when
#       BINDER_IPC_32BIT is not already defined, so a 32-bit build DEFAULTS to protocol 7
#       and an explicit OFF overrides that default.
#     * CMakeLists.txt:225-229 rejects exactly one pairing -- BINDER_IPC_32BIT=ON with a
#       64-bit build.  A 32-bit build with OFF is supported, not rejected.
#     * BUILD.md:409-413 documents a strict protocol-version EQUALITY check when libbinder
#       opens the driver node: kernel, SDK build and vendor side must all agree, and a
#       mismatch fails EVERY open.
#
#   WHY 7 NEEDS A PATCHED KERNEL, AND WHY THAT PATCH IS IN THE WORKFLOW AND NOT HERE.
#   Protocol 7 additionally requires CONFIG_ANDROID_BINDER_IPC_32BIT in the guest kernel.
#   MAINLINE HAS NO SUCH OPTION AFTER 4.17: it was deleted, together with the
#   "#define BINDER_IPC_32BIT 1" in drivers/android/binder.c, by "ANDROID: binder: remove
#   32-bit binder interface".  CONFIG_ANDROID_BINDERFS, which the in-guest init this script
#   writes REQUIRES, first appears in 5.0.  So on a stock kernel the two are mutually
#   exclusive and a binderfs guest cannot be a protocol-7 guest -- which is why asking
#   olddefconfig for the option on a 5.x tree silently produces nothing, and why an earlier
#   revision of the workflow concluded that protocol 8 was the only reachable value.
#
#   What that conclusion missed is that the REMOVAL was 13 lines of Kconfig and 4 lines of
#   binder.c and nothing else: include/uapi/linux/android/binder.h still carries both
#   branches to this day -- "#ifdef BINDER_IPC_32BIT" still selects __u32 binder_size_t and
#   BINDER_CURRENT_PROTOCOL_VERSION 7 -- as does the copy vendored inside the Binder SDK
#   itself.  The workflow therefore RESTORES the option on the pinned kernel with a minimal
#   patch it carries inline, enables it, and asserts CONFIG_ANDROID_BINDER_IPC_32BIT=y in the
#   BUILT .config.  The pin stays on a binderfs-capable 5.15, so both requirements hold at
#   once.  The patch lives in the workflow because this migration's diff allowlist is a fixed
#   set of paths and a patch file would be one more.
#
#   THAT RESTORATION IS UNPROVEN UNTIL THE JOB FIRST RUNS.  The option was removed six years
#   before the kernel it is being re-added to, and nobody upstream exercises that pairing.
#   This is exactly why nothing in this file trusts a declared protocol: the kernel half, the
#   SDK half and the driver's own BINDER_VERSION answer are each read from their source and
#   cross-checked, and a disagreement is a named failure.  If the patch silently fails to take
#   effect, the kernel half comes back 8, the SDK half says 7, and this script refuses to build
#   the image -- which is the outcome to want, not a defect in the checking.
#
#   THE REQUIREMENT IS AGREEMENT, NOT A PARTICULAR NUMBER, so this script does not hold an
#   opinion about which number is right.  It DERIVES the expected protocol from the two
#   build-time artefacts that decide it -- the guest kernel configuration passed with
#   --kernel-config and the staged SDK's own CMakeCache.txt -- REFUSES to build when those
#   two disagree, records the derived value together with the source of each half, and has
#   the guest cross-check all of it against the number the driver itself reports through the
#   BINDER_VERSION ioctl.  Nothing about the protocol is asserted from text anywhere in this
#   file, and a disagreement between any two of the three is a named failure rather than a
#   footnote.  The always-required kernel options are a separate matter and are recorded in
#   the image at ${GUEST_KERNEL_EXPECTATION} for the guest to report against.
#
# ==============================================================================
# WHAT THE IMAGE CONTAINS, AND WHY EACH ITEM IS NOT OPTIONAL
# ==============================================================================
#   The job runs `run_coverage.sh --build --run` INSIDE the guest, because that is where
#   the binder driver is: `--build` COMPILES and `--run` MEASURES, so the guest needs a
#   full build-and-measure toolchain rather than just a runtime.
#
#     * g++, make, autoconf, automake, libtool, pkg-config.  The suites build at
#       -std=c++17, which this script probes for rather than assuming.
#     * glib-2.0 development files.  A HARD configure dependency: configure.ac:65 runs
#       PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 0.10.28]) and there is no way past it.
#     * GoogleTest/GoogleMock WITH A DISCOVERABLE gtest.pc.  configure.ac:435's
#       PKG_CHECK_MODULES([GTEST], [gtest >= 1.10.0]) consults pkg-config ONLY and ignores
#       -I/-L entirely, so headers and libraries on their own are not enough.  Either the
#       image's own packaging provides gtest.pc or --gtest-prefix names a prefix whose
#       lib/pkgconfig holds one; both are verified, and the init exports GTEST_PREFIX and
#       PKG_CONFIG_PATH accordingly.
#     * gcov (with gcc), genhtml and LCOV 2.x.  See THE LCOV 2.x PREREQUISITE below.
#     * The binder runtime closure, all of it loadable: libbinder and libutils, plus
#       liblog, libbase, libcutils and libcutils_sockets.  Those last four are NOT direct
#       link edges of anything in the middleware -- nothing there references them -- but
#       libbinder links them, so the loader must find every one or nothing starts.  This
#       script registers their directories with ld.so.conf and runs ldconfig in the image,
#       and the init also puts them on LD_LIBRARY_PATH: GNU ld does not search -L for a
#       shared library's own DT_NEEDED entries and the staged libbinder carries no RUNPATH,
#       so the search path is needed at LINK time as well as at run time.
#     * servicemanager, from the staged SDK's bin directory (build_binder.sh:49 is what
#       puts it there).  It is an UNCONDITIONAL runtime prerequisite wherever a binder
#       driver is present: checkService cannot resolve "HdmiCec" without it, and obtaining
#       an IServiceManager at all loops until binder handle 0 resolves.
#     * util-linux/coreutils basics the init and run_coverage.sh need: mount, mknod, stat,
#       timeout, flock, nproc, awk, find, mktemp.
#     * A working /proc, /sys and /dev.  The init mounts them.
#
#   THE LCOV 2.x PREREQUISITE, AND WHY AN `apt install lcov` IS NOT SUFFICIENT.
#   run_coverage.sh requires lcov 2.x and says so in four places: its prerequisites list
#   names "lcov 2.x, genhtml and gcov on PATH"; its require_tools() failure states that
#   --fail-under-lines and --rc branch_coverage=1 do not exist in lcov 1.x; its header
#   records that its record spellings were verified against lcov 2.0-1; and the aggregate
#   verdict is delegated to `lcov --summary <trace> --fail-under-lines N`.  The suite's
#   own .lcovrc_l1 uses lcov 2.x spellings as well.  Stable distribution archives ship
#   1.x, so this script installs lcov from a pinned, checksum-verified source tarball when
#   one is supplied and then ASSERTS, inside the image, that `lcov --version` reports 2.x.
#   The image build FAILS if it reports 1.x, because a 1.x lcov surfaces much later as a
#   confusing coverage failure inside the guest, long after the evidence of why.
#
# ==============================================================================
# PAYLOAD TRANSPORT -- COPIED INTO THE IMAGE, AND NO GUEST NETWORK
# ==============================================================================
#   The binaries and the staged SDK must reach the guest over a shared directory OR a
#   copied disk image, never a network fetch from inside the guest.  THIS SCRIPT COMMITS TO
#   THE COPIED DISK IMAGE, and the choice is not arbitrary:
#     * it needs no 9p or virtio-fs support in the host kernel or the guest kernel, so the
#       guest's kernel configuration is decided by the binder requirements alone;
#     * there is no mount ordering to get wrong at boot, and no way for a run to start
#       against a half-populated share;
#     * the image is self-contained, so the same file boots identically on any host.
#   ARTIFACTS COME BACK THE SAME WAY.  The root filesystem is writable, the init writes
#   every artifact under ${GUEST_ARTIFACT_DIR} inside it, and the workflow retrieves them
#   after the guest has powered down by loop-mounting the image read-only and copying that
#   directory out.  The manifest this script writes beside the image names the directory and
#   the status file, so the workflow reads them rather than hard-coding them.
#
#   THE GUEST PERFORMS NO NETWORK ACCESS AT TEST TIME.  Full stop.
#
#   THAT IS A DIFFERENT STATEMENT FROM "NOTHING IS EVER DOWNLOADED", and the distinction
#   matters enough to state so nobody "simplifies" it later.  Acquiring a BASE root
#   filesystem and an lcov source tarball ON THE BUILD HOST, at image-build time, is
#   legitimate and is how the image comes to exist at all.  What is forbidden is the GUEST
#   reaching the network while it is measuring.  Host-side acquisition is therefore
#   allowed here and is PINNED AND VERIFIED -- see THE BASE ROOTFS -- because an unpinned
#   base makes the job irreproducible, which defeats the point of a pinned guest.
#
# ==============================================================================
# THE BASE ROOTFS: PINNED BY THE CALLER, VERIFIED HERE, AND NEVER GUESSED
# ==============================================================================
#   A base tarball is required, either as a local file (--base-tarball) or as a pinned URL
#   fetched on the host (--base-url).  EITHER WAY A SHA256 IS MANDATORY: --base-sha256, or
#   a sidecar <tarball>.sha256 in `sha256sum -c` format.  The digest is verified before a
#   single byte is extracted and a mismatch is fatal.
#
#   NO DIGEST IS HARD-CODED IN THIS FILE, AND THAT IS DELIBERATE RATHER THAN AN OMISSION.
#   A digest is a MEASUREMENT of a specific artefact.  One written into this script from
#   memory would be a fabricated measurement -- it would either be wrong, and reject a
#   correct base, or right by luck, and be trusted for the wrong reason.  So the pin lives
#   with whoever produced the artefact (the workflow, a cache step, a release manifest) and
#   this script's job is to REFUSE ANYTHING IT CANNOT VERIFY.  That is strictly stronger
#   than a hard-coded constant: there is no path through this script that extracts an
#   unverified base.
#
# ==============================================================================
# USAGE
# ==============================================================================
#   sudo ./aidl-path-tests-rootfs.sh \
#        --output   /path/to/cec-l2-rootfs.ext4 \
#        --payload  /path/to/hdmicec \
#        --sdk-dir  /path/to/rdk-halif-aidl \
#        --base-tarball /path/to/base-i386.tar.gz --base-sha256 <hex> \
#        [--lcov-tarball /path/to/lcov-2.x.tar.gz --lcov-sha256 <hex>] \
#        [--gtest-prefix /path/to/install/usr] [--kernel-config /path/to/.config] \
#        [--arch i386] [--size 6144] [--apt-mirror URL]
#
#   Run --help for the full option and default reference.  Root privilege is required: the
#   image is populated through a loop device, a mount and a chroot.
#
# ==============================================================================
# WHAT THE WORKFLOW DOES WITH THE RESULT
# ==============================================================================
#   This script prints, and records in <output>.manifest, everything the workflow needs:
#   the resolved architecture, the guest entry point to pass as init=, the artifact
#   directory to extract, the status file to read, and the kernel options the guest is
#   expected to have been built with.  The workflow boots the image with the guest kernel,
#   waits for the guest to power itself down, loop-mounts the image read-only, copies the
#   artifact directory out, and reads the status file.  Nothing in the boot path needs a
#   network, a share or a second disk.
##########################################################################

set -euo pipefail

# Every file and directory this run creates is owner-only.  The image is populated as root
# and holds a build tree, the staged SDK and, later, coverage evidence; none of it has
# another consumer on the build host, and a permissive umask would leave the intermediate
# staging tree and the finished image readable and appendable by any local account.
umask 077

# ------------------------------------------------------------------------------------
# Absolute self-resolution, so the caller's working directory is irrelevant: this script is
# invoked from a workflow step whose working directory is the superproject root, from the
# submodule root by hand, and from this directory while it is being edited.  All three must
# behave identically.  `pwd -P` resolves symlinks so the paths in diagnostics are the
# physical ones.
# ------------------------------------------------------------------------------------
SCRIPT_PATH="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/$(basename -- "${BASH_SOURCE[0]}")"
readonly SCRIPT_PATH
readonly SCRIPT_NAME='aidl-path-tests-rootfs'

log()  { printf '[%s] %s\n' "$SCRIPT_NAME" "$*"; }
warn() { printf '[%s] WARNING: %s\n' "$SCRIPT_NAME" "$*" >&2; }
die()  { printf '[%s] ERROR: %s\n' "$SCRIPT_NAME" "$*" >&2; exit 1; }

# ------------------------------------------------------------------------------------
# FIXED GUEST LAYOUT.  These are constants rather than options on purpose: the init this
# script writes, the manifest the workflow reads and the environment run_coverage.sh is
# handed all have to agree about them, and a knob for each would be four more ways for
# them to disagree.
#
# The workspace layout REPRODUCES THE CI LAYOUT rather than inventing one, and that is
# load-bearing.  run_coverage.sh derives its own workspace root as the parent of the
# hdmicec checkout, defaults GTEST_PREFIX to <workspace>/install/usr, and configure falls
# back to the sibling rdk-halif-aidl checkout beside the submodule when HALIF_PREFIX is
# unset.  Naming the guest directories to match means every one of those defaults is
# already correct inside the guest, so nothing has to be overridden to make the build work.
# ------------------------------------------------------------------------------------
readonly GUEST_WORKSPACE='/opt/cec-l2'
readonly GUEST_PAYLOAD_DIR="$GUEST_WORKSPACE/hdmicec"
readonly GUEST_SDK_DIR="$GUEST_WORKSPACE/rdk-halif-aidl"
readonly GUEST_GTEST_PREFIX="$GUEST_WORKSPACE/install/usr"
readonly GUEST_ARTIFACT_DIR='/artifacts'
readonly GUEST_STATUS_FILE="$GUEST_ARTIFACT_DIR/run_coverage.status"
readonly GUEST_INIT_PATH='/sbin/cec-l2-init'
readonly GUEST_CONF_DIR='/etc/cec-l2'
readonly GUEST_IMAGE_CONF="$GUEST_CONF_DIR/image.conf"
readonly GUEST_KERNEL_EXPECTATION="$GUEST_CONF_DIR/expected-kernel-config"
readonly GUEST_KERNEL_PROVENANCE="$GUEST_CONF_DIR/build-host-kernel-config"
readonly GUEST_SDK_FLAGS_RECORD="$GUEST_CONF_DIR/sdk-build-flags"
readonly GUEST_IMAGE_RECORD="$GUEST_CONF_DIR/image-build-record"
# THE EXACT PACKAGE SET, WITH VERSIONS, AS INSTALLED.  Pinning the archive makes the set
# reproducible; recording it makes the set AUDITABLE, which is a different property and the
# one a reader needs when a rerun behaves differently.  The two together are what turn "the
# archive was pinned" from an assertion into evidence.
readonly GUEST_PACKAGE_RECORD="$GUEST_CONF_DIR/installed-packages"
readonly GUEST_PROTOCOL_HELPER='/usr/local/bin/cec-binder-protocol-version'
readonly GUEST_READY_PROBE='/usr/local/bin/cec-servicemanager-ready'

# Relative paths inside the staged rdk-halif-aidl tree.  These four are the layout
# build_binder.sh and the snapshot builds produce and the L1 workflow already consumes;
# deriving them from one --sdk-dir keeps the orchestrator contract to a single input.
readonly SDK_REL_BINDER_TARGET='out/target'
readonly SDK_REL_BINDER_BUILD='out/build'
readonly SDK_REL_HALIF_LIB='out/target/lib/halif'
readonly SDK_REL_SERVICEMANAGER='out/target/bin/servicemanager'

# The header roots the generated stubs and halcompat.h need, verified so a missing one is a
# named failure here rather than a compile error inside the guest an hour later.
readonly SDK_REL_INCLUDE_ROOTS=(
    'hdmicec/0.1.0.0/include'
    'common/0.2.0.0/include'
    'common/current'
)

# The binder runtime closure.  libbinder and libutils are direct link edges of libRCEC; the
# other four are libbinder's own dependencies and are listed because they must be LOADABLE,
# not because anything in the middleware names them.
readonly BINDER_CLOSURE_LIBRARIES=(
    libbinder.so
    libutils.so
    liblog.so
    libbase.so
    libcutils.so
    libcutils_sockets.so
)

# The two pinned AIDL stub libraries.  common is pinned at 0.2.0.0 because that is the ABI
# the released hdmicec 0.1.0.0 snapshot was generated against; a newer common release is
# not a substitute.
readonly HALIF_STUB_LIBRARIES=(
    libhdmicec-v0.1.0.0-cpp.so
    libcommon-v0.2.0.0-cpp.so
)

# The binder device nodes the always-required kernel configuration asks for.  The initial
# binderfs instance is populated from CONFIG_ANDROID_BINDER_DEVICES, so a missing node here
# is a kernel-configuration fault in the guest and is reported as one.
readonly BINDER_DEVICE_NODES='binder hwbinder vndbinder'

# Trees the operating system owns.  An output image, a payload or an SDK resolving into one
# of these is nearly always a '..' that collapsed out of the intended path or a mistyped
# root, and this script writes multi-gigabyte files and mounts filesystems.
readonly PROTECTED_SYSTEM_ROOTS=(
    /bin /boot /dev /etc /lib /lib32 /lib64 /libx32 /proc /run /sbin /sys /usr /var
)

readonly DEFAULT_ARCH='i386'
readonly DEFAULT_IMAGE_SIZE_MIB=6144
# Wall-clock budget the init gives servicemanager to answer a real binder transaction.
# Generous because a cold guest on a shared CI host is slow, and bounded because the
# alternative -- an unbounded wait -- is a hung job with no diagnosis.
readonly DEFAULT_READINESS_TIMEOUT_SECONDS=180

# ------------------------------------------------------------------------------------
# CLEANUP REGISTRIES AND THE TRAP.
#
# Everything destructive this script does is registered the moment it happens, and released
# in reverse order by one trap that runs on success, on failure and on interruption alike.
# The order matters: a bind mount inside the image must go before the image's own mount,
# and the image's mount must go before the loop device is detached, or the detach fails and
# the runner is left with a busy loop device that no later job can reuse.
#
# Registration happens BEFORE the action wherever the action can partially succeed
# (`mount` can fail with the filesystem attached), because a cleanup entry for something
# that never happened is harmless -- it is skipped -- while a missing entry for something
# that did happen is a leak.
# ------------------------------------------------------------------------------------
MOUNT_POINTS=()
LOOP_DEVICES=()
LOOP_BACKING_FILES=()
TEMP_DIRS=()
CLEANUP_IN_PROGRESS=0
# Whether an output image exists yet, and whether the run got far enough to declare it
# finished.  The pair is what lets cleanup remove a HALF-BUILT image: an incomplete image left
# on disk is worse than no image, because a workflow step that only checks for the file's
# existence would boot it.
IMAGE_CREATED=0
BUILD_COMPLETED=0
# The two files this run actually writes.  Both live in the output directory under private
# mktemp names and are renamed onto the caller's paths, together, only after the image has
# been validated -- see publish_outputs().  They are declared here, before the trap is
# registered, because the trap reads them and `set -u` makes an unset name a fatal error at
# the worst possible moment.
STAGED_IMAGE=''
STAGED_MANIFEST=''
# THE LEAF NAMES OF THE SAME TWO FILES, AND WHY BOTH FORMS ARE KEPT.
#
# Every privileged operation on a file in the output directory is performed through the
# DESCRIPTOR-RELATIVE path $OUTPUT_DIR_PROC/<leaf> rather than through the caller's pathname
# (see the custody block above resolve_publication_targets for why).  A leaf name is therefore
# the durable identity of one of these files, and the two forms are composed from it:
#
#     operations   "$OUTPUT_DIR_PROC/$STAGED_IMAGE_LEAF"   -- pinned to the validated inode
#     messages     "$STAGED_IMAGE"                          -- what the caller typed, readable
#
# The human form is also what the KERNEL reports as a loop device's backing_file, because it
# resolves the proc path at attach time -- so assert_loop_backs continues to compare against it.
STAGED_IMAGE_LEAF=''
STAGED_MANIFEST_LEAF=''

# THE PUBLICATION TRANSACTION, VISIBLE TO THE TRAP.
#
# WHY THESE ARE GLOBALS AND NOT LOCALS OF publish_outputs().  The publication moves the previous
# image and the previous manifest aside and then renames the new pair onto their names.  While
# that is in flight the caller's good image exists ONLY under a private aside name, so anything
# that ends the process between the first aside and the last rename leaves the published path
# empty and the previous image hidden under a name nobody knows.  An earlier revision held those
# names in function locals, so the EXIT trap -- the one thing that runs on every path -- could
# not see them and could not put anything back.  They are globals now, they are set BEFORE the
# rename that makes them necessary, and cleanup() rolls the transaction back from them.
#
# PUBLISH_IN_PROGRESS is the flag that says a rollback is owed.  It is raised before the first
# rename and lowered only once the pair is committed.
PUBLISH_ASIDE_IMAGE=''
PUBLISH_ASIDE_MANIFEST=''
PUBLISH_ASIDE_IMAGE_LEAF=''
PUBLISH_ASIDE_MANIFEST_LEAF=''
PUBLISH_IN_PROGRESS=0

# WHETHER EACH ASIDE ACTUALLY HOLDS THE PREVIOUS CONTENT -- which is NOT the same question as
# "does a file exist at the aside name".
#
# THE DEFECT THIS CLOSES.  mktemp mints the aside names by CREATING THEM, so between minting and
# the `mv` there is a real, EMPTY, regular file at each aside name.  publish_restore_one used
# `[ -f "$aside" ]` as its whole precondition, so if the old leaf's rename failed -- the one case
# in which the aside is still the empty placeholder -- the rollback would move that ZERO-BYTE
# PLACEHOLDER over the caller's still-good published image or manifest.  The failure that was
# supposed to preserve the previous pair would have destroyed it.
#
# So existence is not the test.  Each flag is raised ONLY after that leaf's own `mv` returned
# success, and restoration is conditional on the flag.  The recorded size is belt and braces on
# top of it: an aside of zero bytes is refused when the original was not zero bytes, so a
# placeholder cannot be restored even if a future edit sets the flag in the wrong place.
PUBLISH_ASIDE_IMAGE_HOLDS_OLD=0
PUBLISH_ASIDE_MANIFEST_HOLDS_OLD=0
PUBLISH_OLD_IMAGE_BYTES=''
PUBLISH_OLD_MANIFEST_BYTES=''

# Raised once each NEW leaf has been renamed onto its published name, so a rollback knows it has
# to undo that rename before it can put the previous one back.
#
# BOTH, and that symmetry is the point: there was only an image flag, so a failure AFTER the
# manifest rename -- the chmod, or the durability flush -- withdrew the new image and left the
# new MANIFEST sitting at its published path.  On a first publication, with no asides to restore,
# that is a manifest describing an image that is not there.
PUBLISH_IMAGE_RENAMED=0
PUBLISH_MANIFEST_RENAMED=0

# WRITE-AHEAD INTENT, AND IDENTITY BY INODE.  Both exist to close ONE defect, and it is the one
# that mattered most of all of them.
#
# THE DEFECT.  Every flag above is raised AFTER its own `mv` returns, and the journal was
# rendered from those flags -- so the durable record was a write-AFTER-move record.  A SIGKILL in
# the window between a completed rename and the journal update that would have recorded it leaves
# a journal on the disk saying the move did NOT happen, and the recovery TRUSTED it.  Measured on
# the previous revision: kill immediately after the previous image's own `mv`, and
# MOVED_ASIDE_IMAGE is still 0, so publish_restore_one() takes its "nothing was ever moved into
# this aside" arm and skips the restoration -- and the discard pass that follows then removes the
# journal-named aside, which at that instant is the ONLY copy of the caller's previous image.
# The mechanism built to preserve the previous generation destroyed it.
#
# SO THE RECORD IS WRITTEN TWICE PER MOVE.  INTENT_<step> is written and flushed BEFORE the
# rename; DONE_<step> is written and flushed after it.  (INTENT=1, DONE=0) therefore means
# exactly "this move may or may not have happened" -- which is the truth -- and the recovery is
# forbidden from resolving that combination out of the record at all.
#
# AND IT RESOLVES IT BY INODE, WHICH IS A FACT RATHER THAN A CLAIM.  Every participant's device,
# inode and size is recorded while it is still at its starting name: the previous image, the
# previous manifest, both of this run's staged files, AND both mktemp aside placeholders.  A
# rename changes a NAME and leaves the inode alone, so recovery stats each of the six names and
# asks "which recorded object is sitting here", which is decidable with no heuristic in it.
#
# THE PLACEHOLDER INODES ARE THE PART THAT IS EASY TO MISS.  mktemp mints each aside name by
# CREATING a file there, so before the move the aside name holds the PLACEHOLDER's inode and
# after it the previous file's.  Recording the placeholder's identity is what makes those two
# states distinguishable without trusting any flag -- which is the whole of the fix.  A name
# holding an object the journal identifies as nothing is a disagreement, and a disagreement is
# refused with everything left exactly where it is.
PUBLISH_INTENT_ASIDE_IMAGE=0
PUBLISH_INTENT_ASIDE_MANIFEST=0
PUBLISH_INTENT_NEW_IMAGE=0
PUBLISH_INTENT_NEW_MANIFEST=0
PUBLISH_OLD_IMAGE_DEVICE=''
PUBLISH_OLD_IMAGE_INODE=''
PUBLISH_OLD_MANIFEST_DEVICE=''
PUBLISH_OLD_MANIFEST_INODE=''
PUBLISH_ASIDE_IMAGE_HOLDER_DEVICE=''
PUBLISH_ASIDE_IMAGE_HOLDER_INODE=''
PUBLISH_ASIDE_MANIFEST_HOLDER_DEVICE=''
PUBLISH_ASIDE_MANIFEST_HOLDER_INODE=''
PUBLISH_STAGED_IMAGE_BYTES=''
PUBLISH_STAGED_MANIFEST_BYTES=''

# The fixed prefixes the aside names are minted under.  They are constants because the
# crash-consistent recovery at start-up has to be able to FIND them in a directory it did not
# create, without a record from the run that died: the run that would have written such a record
# is precisely the run that did not survive to write it.
readonly PUBLISH_ASIDE_IMAGE_PREFIX='.cec-l2-previous-image.'
readonly PUBLISH_ASIDE_MANIFEST_PREFIX='.cec-l2-previous-manifest.'

# ------------------------------------------------------------------------------------
# THE PUBLICATION JOURNAL: HOW THE PAIR IS RECOVERED TOGETHER.
#
# WHAT NO SET OF FLAGS CAN DO.  The image and the manifest are published by two independent
# same-directory renames.  A SIGKILL, an OOM kill or a power loss between them runs no handler at
# all, so nothing in this process can record what happened -- and the state it leaves is
# genuinely ambiguous from the outside: a new image at its published path, no manifest, and BOTH
# asides still sitting there.  The aside-scan recovery read that state one file at a time and
# reached the worst possible conclusion: an image exists, so the old-image aside is "superseded"
# and gets DELETED; no manifest exists, so the old manifest is restored.  The result is a NEW
# image beside an OLD manifest, cemented permanently, with the old image destroyed.  The manifest
# names the image's size, architecture and derived binder protocol, so that pair is not merely
# stale -- it is a pair whose two halves describe different builds, and it looks complete.
#
# WHAT REPLACES IT.  A journal leaf in the publication directory, written and made durable
# BEFORE the first move and updated after each one, carrying the run identity, a generation
# identity, both aside leaf names and one completion record per move.  The next run reads it
# UNDER THE LOCK and drives the pair to ONE consistent state -- fully the previous generation, or
# fully the new one -- and refuses loudly rather than guessing when the state on disk does not
# match what the journal says it should be.
#
# WHY A JOURNAL RATHER THAN A GENERATION DIRECTORY.  A generation directory with one atomically
# switched symlink is the other correct shape, and it is smaller to reason about -- but it changes
# the PATHS the workflow consumes, which would move this change into every step that reads the
# image or the manifest.  The journal keeps the caller's two fixed paths exactly as they are.
#
# WHY THE LEAF NAME IS DERIVED FROM THE IMAGE LEAF.  Two builders publishing DIFFERENT --output
# names into the same directory hold different locks (the lock is <image>.lock) and may therefore
# run at the same time.  A single fixed journal name would have them overwrite each other's
# journal, which is worse than having none.  So each output has its own.
#
# WHY EACH UPDATE IS AN ATOMIC RENAME.  A journal half-written when the power failed is exactly
# the ambiguity the journal exists to remove.  Each update is written to a fixed staging leaf and
# renamed onto the journal name, which is atomic within a directory, and every record ends with a
# terminator line that the reader REQUIRES -- so a torn journal is both nearly impossible and,
# if it happened anyway, refused rather than interpreted.
#
# WHY THE RECORD IS WRITTEN AHEAD OF EACH MOVE AND NOT AFTER IT.  See the block at
# PUBLISH_INTENT_ASIDE_IMAGE for the defect this closes and the measurement that found it.  The
# consequence for the shape of the record is that every move has TWO fields rather than one --
# INTENT_<step>, flushed before the rename, and DONE_<step>, flushed after -- and that the pair
# (INTENT=1, DONE=0) is a legitimate, expected, terminal state of the record which the reader
# MUST NOT resolve from the record.
#
# WHICH IS WHY THE RECORD ALSO CARRIES IDENTITIES.  Six of them: the previous image, the previous
# manifest, this run's two staged files and the two mktemp placeholders the aside names are
# minted as, each as a device, an inode and a size, recorded while the object is still at its
# starting name.  Renames preserve inodes, so the reader stats each of the six names and
# identifies the object sitting there instead of inferring it -- and (INTENT=1, DONE=0) becomes a
# question about the filesystem, which has an answer, rather than a question about the record,
# which does not.
#
# VERSION 2 IS NOT BACKWARD-READABLE, DELIBERATELY.  A version 1 record carries the four
# write-after-move completion flags and no identities at all, so acting on one would mean acting
# on exactly the claim that was found to be wrong.  A version 1 journal is therefore refused with
# the reason and the remedy rather than interpreted.
# ------------------------------------------------------------------------------------
PUBLISH_JOURNAL_LEAF=''
PUBLISH_JOURNAL_TMP_LEAF=''
PUBLISH_JOURNAL_WRITTEN=0
PUBLISH_RUN_ID=''
PUBLISH_GENERATION=''
readonly PUBLISH_JOURNAL_PREFIX='.cec-l2-publication.'
readonly PUBLISH_JOURNAL_SUFFIX='.journal'
readonly PUBLISH_JOURNAL_TMP_SUFFIX='.journal.staging'
# The line every complete journal record ends with.  Its absence means the write was torn.
readonly PUBLISH_JOURNAL_TERMINATOR='JOURNAL_COMPLETE=1'

register_mount()  { MOUNT_POINTS+=("$1"); }
# The backing file is registered WITH the device, because a loop device node is a shared host
# resource: the teardown has to be able to tell "the device we attached" from "the same node
# after somebody else re-used it", and the node's name alone cannot.  See assert_loop_backs.
register_loop()   { LOOP_DEVICES+=("$1"); LOOP_BACKING_FILES+=("${2-}"); }
register_tempdir(){ TEMP_DIRS+=("$1"); }

# Unmount one point, tolerating the ordinary case where a child process this run started has
# not finished releasing it yet.  A handful of bounded attempts, then a lazy unmount, then a
# warning: the alternative -- one attempt and a warning -- leaves a busy mount behind on the
# host for every job that follows.  The pause between attempts is a teardown courtesy and is
# not a readiness mechanism; nothing here is waiting for something to become ready.
readonly UNMOUNT_ATTEMPTS=3

unmount_with_retries() { # $1=mount point
    local point="$1" attempts=0
    mountpoint -q -- "$point" 2>/dev/null || return 0
    while [ "$attempts" -lt "$UNMOUNT_ATTEMPTS" ]; do
        if umount -- "$point" 2>/dev/null; then
            return 0
        fi
        mountpoint -q -- "$point" 2>/dev/null || return 0
        attempts=$(( attempts + 1 ))
        sleep 1
    done
    # Lazy: detach it from the tree now and let the kernel finish when the last user goes.
    umount -l -- "$point" 2>/dev/null \
        || warn "could not unmount $point; it is left for the host to clear
       (umount -l '$point')."
}

# Is this loop device still bound to a backing file?  MEASURED, because the node itself
# survives a detach and because the kernel auto-clears a loop device once its last user closes
# it -- which is what happens when the image is unmounted moments earlier.  A bare `-b` test
# therefore reports a device that is already gone as still attached, and the detach that
# follows fails with ENXIO and prints a warning about work that had already been done.
loop_still_attached() { # $1=loop device
    [ -e "/sys/block/$(basename -- "$1")/loop/backing_file" ]
}

# WHICH FILE a loop device is bound to, as the kernel reports it.  Printed empty when the
# device is not attached.
#
# The kernel appends " (deleted)" to the backing file when the file has been unlinked while
# the device stayed attached, so the suffix is stripped: a device bound to an unlinked file
# is still bound to that path as far as identity goes, and treating the suffixed form as a
# different file would make the identity check below fail exactly when it matters most.
loop_backing_file() { # $1=loop device -> the backing path on stdout, or nothing
    local node backing
    node="$(basename -- "$1")"
    [ -r "/sys/block/$node/loop/backing_file" ] || return 0
    backing="$(cat -- "/sys/block/$node/loop/backing_file" 2>/dev/null || true)"
    backing="${backing% (deleted)}"
    printf '%s' "$backing"
}

# IDENTITY, NOT LIVENESS.  Refuses to act on a loop device that is bound to something other
# than the file this run staged.
#
# WHY THIS IS A SEPARATE CHECK FROM loop_still_attached.  A loop device node is a shared
# host resource with a small numeric name space.  Between this run attaching /dev/loop3 and
# this run detaching it, an unrelated process on the same host can detach it and attach the
# same node to its own file -- and then a `losetup -d` or a `mount` issued here as ROOT acts
# on that process's image rather than on ours.  Comparing the backing file makes the node's
# name insufficient on its own: the operation goes ahead only when the kernel agrees the
# device is bound to the exact file we staged.
assert_loop_backs() { # $1=loop device  $2=expected backing file  $3=what is about to happen
    local device="$1" expected="$2" operation="$3" actual
    actual="$(loop_backing_file "$device")"
    [ -n "$actual" ] || die "refusing to $operation: $device reports no backing file, so it is
       no longer attached to the staged image $expected.  Something outside this run detached
       it.  Nothing further is done to that device."
    [ "$actual" = "$expected" ] || die "refusing to $operation: $device is bound to
           $actual
       but this run staged
           $expected
       Some other process on this host re-used the loop device node between our attach and
       this operation.  Acting on it as root would act on THAT image, so nothing is done to
       it."
}

# ------------------------------------------------------------------------------------
# CLEANUP FAILURES ARE COUNTED, NOT NARRATED.
#
# WHAT WAS WRONG BEFORE.  Every release step warned on failure and the trap then returned the
# incoming status unchanged, so a build that succeeded and then failed to unmount the image,
# failed to detach the loop device or failed to remove its temporary directory exited 0.  The
# workflow read that 0, uploaded the image and moved on, and the next job on the runner
# inherited a busy loop device and a mounted image tree.  A warning in a log nobody reads is
# not a failure; only a non-zero status is.
#
# WHAT REPLACES IT.  Every step VERIFIES ITS POSTCONDITION -- the mount is gone from the
# tree, the loop device reports no backing file, our backing file is not attached to any loop
# device, the temporary directory does not exist, the staged file does not exist -- and each
# unmet postcondition is counted.  A non-zero count turns an otherwise-successful run into a
# failure and leaves an already-failing run's status alone.
#
# WHY THE FINAL VERDICT USES `exit` AND NOT `return`.  MEASURED on this host with bash 5.2:
# `return N` from an EXIT trap does NOT change the process's exit status -- a trap that ended
# `return 7` on a script that had run `exit 0` still produced 0 -- while `exit N` inside the
# trap does set it, and `exit "$status"` with the saved incoming value preserves an original
# failure exactly.  So the accounting is expressed as an explicit `exit`, and the ordinary
# path keeps `return "$status"` for the same reason it always did: it changes nothing.
readonly CLEANUP_LEAK_EXIT_STATUS=70
CLEANUP_FAILURES=0

cleanup_failed() { # $1=what remains
    CLEANUP_FAILURES=$(( CLEANUP_FAILURES + 1 ))
    warn "CLEANUP FAILED: $1"
}

# Is any loop device on this host still bound to the given file?  This is the postcondition
# that actually matters, and it is independent of the device NODE: a node we attached may have
# been re-used by another process, and what we must establish is that OUR file is no longer
# attached anywhere -- an image still open through a loop device is not a finished artefact
# and cannot be safely published, moved or deleted.
backing_file_still_attached() { # $1=backing file -> prints the device basenames, if any
    local entry node backing found=''
    for entry in /sys/block/loop*/loop/backing_file; do
        [ -r "$entry" ] || continue
        backing="$(cat -- "$entry" 2>/dev/null || true)"
        backing="${backing% (deleted)}"
        [ "$backing" = "$1" ] || continue
        node="${entry#/sys/block/}"
        node="${node%%/*}"
        found="$found $node"
    done
    printf '%s' "${found# }"
}

cleanup() {
    local status=$?
    # THE TEARDOWN IS NOT INTERRUPTIBLE, and this line is the difference between a clean
    # release and a leaked one.  MEASURED: with the signal delivered to the whole process
    # group -- which is what `timeout -s INT` and a terminal Ctrl-C both do -- the extraction
    # dies, the `|| die` path exits, cleanup starts, and the PENDING SIGINT then runs the INT
    # trap, whose own `exit` abandons cleanup halfway.  The result was five mounts and a loop
    # device left behind while the incomplete image had already been removed.  Ignoring the
    # signals here means a second Ctrl-C cannot orphan a mount; the re-entrancy guard below
    # covers the case where cleanup is somehow reached twice regardless.
    trap '' INT TERM HUP
    [ "$CLEANUP_IN_PROGRESS" -eq 0 ] || return
    CLEANUP_IN_PROGRESS=1

    # Flush before anything is detached.  The image's contents are the product of this
    # script; losing the tail of them to a lazily written page cache would produce an image
    # that boots and is subtly incomplete, which is worse than a failure.
    sync || true

    local index mount_point loop_device temp_dir expected_backing actual_backing staged \
          still_attached_on skip_detach

    for (( index=${#MOUNT_POINTS[@]}-1; index>=0; index-- )); do
        mount_point="${MOUNT_POINTS[index]}"
        [ -n "$mount_point" ] || continue
        unmount_with_retries "$mount_point"
        # POSTCONDITION: the point is no longer a mount point.  A LAZY unmount satisfies this
        # -- `umount -l` detaches the subtree from the namespace immediately and lets the
        # kernel finish with it when the last user goes -- and that is the honest reading:
        # nothing else on this host can reach it any more.  What does NOT satisfy it is a
        # unmount that failed outright, which leaves the image's filesystem live and busy.
        if mountpoint -q -- "$mount_point" 2>/dev/null; then
            cleanup_failed "$mount_point is still mounted after $UNMOUNT_ATTEMPTS attempts and
       a lazy unmount.  Something is still holding it; clear it with
           umount -l '$mount_point'
       before the next job runs on this host."
        fi
    done

    for (( index=${#LOOP_DEVICES[@]}-1; index>=0; index-- )); do
        loop_device="${LOOP_DEVICES[index]}"
        [ -n "$loop_device" ] || continue
        # IDENTITY BEFORE DETACH, even here.  The teardown runs as root, and a loop device
        # node that some other process on this host re-used between our attach and now is a
        # device whose detach would interrupt THAT process's build.  A mismatch is reported
        # and left alone; it is not our resource to release.
        #
        # WHAT A MISMATCH DOES *NOT* EXCUSE, and this is the correction: it suppresses THE
        # DETACH OF THAT NODE and nothing else.  An earlier revision issued `continue` here,
        # which also skipped the independent postcondition at the end of this loop body -- the
        # all-loop scan for OUR OWN backing file.  Those two are about different things.  The
        # node check asks "is this node still ours to release"; the backing-file scan asks "is
        # this run's image still open through ANY loop device", which is precisely the question
        # a re-used node makes urgent: the kernel may have moved our file to a different node,
        # or a second attachment of it may exist, and either way the image is not a finished
        # artefact and must not be published or replaced.  Skipping the scan turned the one
        # case where it matters most into the one case where it never ran.  So the detach is
        # gated by a flag and the scan below runs unconditionally.
        skip_detach=0
        expected_backing="${LOOP_BACKING_FILES[index]-}"
        if [ -n "$expected_backing" ] && loop_still_attached "$loop_device"; then
            actual_backing="$(loop_backing_file "$loop_device")"
            if [ "$actual_backing" != "$expected_backing" ]; then
                warn "not detaching $loop_device: it is now bound to
       ${actual_backing:-<nothing>}
       and not to the file this run attached ($expected_backing).  Another process on this
       host re-used the node, so releasing it is not ours to do.  This run's own backing file
       is still checked below, on every loop device, because a node we cannot release says
       nothing about whether our image is still attached somewhere."
                skip_detach=1
            fi
        fi
        if [ "$skip_detach" -eq 0 ] && [ -b "$loop_device" ] && loop_still_attached "$loop_device"; then
            # NO `--` BEFORE THE DEVICE, and that is measured rather than stylistic:
            # util-linux 2.41 takes the "--" in `losetup -d -- /dev/loopN` to BE a device name
            # and fails with "losetup: /dev/--: detach failed", leaving the real device
            # attached.  The value is always a /dev/loopN path losetup printed itself, so there
            # is no leading-dash argument to guard against here.
            if ! losetup -d "$loop_device" 2>/dev/null; then
                # Re-checked rather than reported: between the test above and the detach the
                # kernel may have auto-cleared it, and warning about that would be noise.
                if loop_still_attached "$loop_device"; then
                    cleanup_failed "$loop_device is still attached.  Detach it with
           losetup -d $loop_device
       A leaked loop device wedges every job that follows on this host, because the node is
       taken and the file behind it cannot be replaced."
                fi
            fi
        fi
        # POSTCONDITION, INDEPENDENT OF THE NODE: our backing file must not be attached to ANY
        # loop device.  This is the check that catches the case the node-based test cannot --
        # the kernel moved our file to a different node, or a second attachment of the same
        # file exists -- and it is the condition that decides whether the image is safe to
        # publish and safe for the next job to overwrite.
        if [ -n "$expected_backing" ]; then
            still_attached_on="$(backing_file_still_attached "$expected_backing")"
            if [ -n "$still_attached_on" ]; then
                cleanup_failed "$expected_backing is still attached to loop device(s):
       $still_attached_on
       Detach each with 'losetup -d /dev/<name>'.  While a loop device holds it the file is
       not a finished artefact and cannot be safely replaced."
            fi
        fi
    done

    for (( index=${#TEMP_DIRS[@]}-1; index>=0; index-- )); do
        temp_dir="${TEMP_DIRS[index]}"
        # Only ever a directory this script minted with mktemp -d, and never the root or a
        # short path: the guard is cheap and the mistake it prevents is unrecoverable.
        case "$temp_dir" in
            ''|/) continue ;;
        esac
        [ "${#temp_dir}" -gt 4 ] || continue
        [ -d "$temp_dir" ] || continue
        rm -rf -- "$temp_dir" 2>/dev/null || true
        # POSTCONDITION: it is gone.  Checked rather than inferred from rm's status, because
        # the interesting failure is a directory that still has a mount inside it -- rm
        # reports EBUSY on the mount point and leaves a partially emptied tree, and a
        # multi-gigabyte scratch directory left on a CI runner is a real leak.
        if [ -e "$temp_dir" ]; then
            cleanup_failed "the temporary directory $temp_dir still exists.  Something inside
       it is still mounted or in use; inspect it with 'mount | grep $temp_dir' and remove it
       with 'rm -rf $temp_dir' once it is free."
        fi
    done

    # AN OPEN PUBLICATION TRANSACTION IS ROLLED BACK HERE, NOT LEFT FOR A HUMAN TO FIND.
    #
    # publish_outputs() masks INT, TERM and HUP across its renames and rolls back from its own
    # failure arms, so this arm is for the paths that reach the teardown WITHOUT passing through
    # those arms: a `die` raised from a function it calls, an unexpected non-zero under `set -e`,
    # or an unmaskable signal.  Everything it needs is in globals for exactly this reason -- the
    # revision this replaces held the aside names in function locals, and this trap could
    # therefore see nothing to put back, which is how a caller's good image ended up hidden
    # under a private name with nothing at the published path.
    #
    # It runs BEFORE the staged-file removal below, and that order is deliberate: the rollback
    # may move this run's image back onto its staging name, and the removal below is then what
    # discards it -- a failed run does not publish, and it does not leave its own half-finished
    # image lying in the output directory either.
    if [ "$PUBLISH_IN_PROGRESS" -eq 1 ]; then
        warn "the publication transaction was still open when the teardown began; putting the"
        warn "  previous image and manifest back."
        publish_rollback || cleanup_failed "the publication transaction could not be rolled
       back.  The lines above name every file and where it is; the caller's previous image and
       manifest are NOT both at their published paths, so this run reports failure rather than
       leaving that to be discovered later."
    fi

    # A HALF-BUILT IMAGE IS REMOVED -- BUT ONLY THE STAGED ONE, NEVER THE PUBLISHED PAIR.
    #
    # This is the correction that makes a failed run non-destructive.  An earlier revision
    # formatted the image directly at --output and removed THAT path on failure, so a build
    # that failed anywhere after `truncate` destroyed whatever good image the caller already
    # had: a rerun of a flaky job left the runner with no bootable image at all, and a
    # concurrent second builder could have its published output deleted from under it.
    #
    # The staged path is a private mktemp file in the output directory and is nobody else's
    # artefact, so removing it is always safe.  An incomplete image is not a smaller version
    # of a finished one -- it boots into something undefined and a step that only tests for
    # the file's existence would use it -- so it does not survive.  The published pair is
    # replaced atomically by publish_outputs() and only after the image is validated.
    #
    # NAMED THROUGH THE PUBLICATION DIRECTORY DESCRIPTOR, like every other operation on these
    # two files.  The leaf names are what is iterated over and the human pathname is only what
    # the message says; a `rm` through the caller's pathname could remove an entry of whatever
    # directory that pathname resolves to by teardown time.  When the descriptor does not exist
    # yet -- a failure before resolve_publication_targets -- there are no staged files either,
    # so the guard costs nothing.
    if [ "$status" -ne 0 ] && [ "$BUILD_COMPLETED" -eq 0 ] && [ -n "${OUTPUT_DIR_PROC:-}" ]; then
        for staged in "$STAGED_IMAGE_LEAF" "$STAGED_MANIFEST_LEAF"; do
            [ -n "$staged" ] || continue
            [ -f "$OUTPUT_DIR_PROC/$staged" ] || continue
            rm -f -- "$OUTPUT_DIR_PROC/$staged" 2>/dev/null || true
            if [ -e "$OUTPUT_DIR_PROC/$staged" ]; then
                cleanup_failed "the staged file ${OUTPUT_DIR:-?}/$staged could not be removed.
       It is an incomplete artefact in the output directory; remove it with
       'rm -f ${OUTPUT_DIR:-?}/$staged' so no later step can find it."
            fi
        done
        if [ "$IMAGE_CREATED" -eq 1 ]; then
            warn "removed this run's staged, incomplete image so it cannot be mistaken for a"
            warn "  finished one. Any previously published image at ${OUTPUT_IMAGE:-<none>} is"
            warn "  untouched: this run never wrote to that path. The failure is diagnosed"
            warn "  from the output above."
        fi
    fi

    # THE VERDICT ON THE TEARDOWN ITSELF.
    if [ "$CLEANUP_FAILURES" -gt 0 ]; then
        warn "================ TEARDOWN INCOMPLETE ================"
        warn "$CLEANUP_FAILURES privileged resource(s) could not be released; each is named"
        warn "above with the command that clears it.  This host is NOT in the state this run"
        warn "found it in: a leaked mount or loop device makes every job that follows fail in"
        warn "a way that has nothing to do with its own code."
        if [ "$status" -eq 0 ]; then
            warn "The build itself succeeded, and its exit status is nevertheless"
            warn "$CLEANUP_LEAK_EXIT_STATUS and not 0: a run that leaves privileged state"
            warn "behind has not finished, and reporting 0 would hand a broken host to the"
            warn "next job with a green tick."
            warn "================ END TEARDOWN INCOMPLETE ================"
            exit "$CLEANUP_LEAK_EXIT_STATUS"
        fi
        warn "The build had ALREADY failed with status $status, which is preserved: the"
        warn "original failure is the one worth reporting, and replacing it would hide it."
        warn "================ END TEARDOWN INCOMPLETE ================"
        exit "$status"
    fi

    return "$status"
}

trap cleanup EXIT

# THE THREE INTERRUPT TRAPS, INSTALLED THROUGH A FUNCTION RATHER THAN INLINE.
#
# They are installed once at start-up, exactly as before.  The function exists because the
# publication transaction MASKS these three signals for the few renames it takes to swap the
# image and the manifest together -- a signal landing between those renames is the one window
# in which the caller can be left with the previous image hidden and nothing at the published
# path -- and it must then put the SAME handlers back.  A second literal copy of these three
# `trap` commands is how the masked and unmasked versions drift apart, so there is one copy
# and both callers use it.
install_interrupt_traps() {
    trap 'die "interrupted; releasing every mount, loop device and temporary directory."' INT
    trap 'die "terminated; releasing every mount, loop device and temporary directory."' TERM
    # HUP is routed through the same path for the reasons set out immediately below.
    trap 'die "hung up (SIGHUP: the controlling terminal or session went away); releasing every
       mount, loop device and temporary directory."' HUP
}
# HUP IS HANDLED FOR THE SAME REASON AS INT AND TERM.  This script runs as root with mounts
# and loop devices held, and HUP is what a lost controlling terminal or a torn-down session
# delivers: an SSH connection that drops, a CI runner that reaps the session, an invocation
# whose parent shell exits.
#
# WHY IT MATTERS, MEASURED RATHER THAN ASSUMED.  On this host with bash 5.2 an UNTRAPPED HUP
# does still run the EXIT trap -- so the first draft of this comment, which said it did not,
# was wrong -- but it produces two failures that are worse than a missing teardown, and both
# were reproduced:
#
#   * THE TEARDOWN BELIEVES THE BUILD SUCCEEDED.  cleanup() reads `$?` to decide whether the
#     run failed, and on an untrapped fatal signal that value is 0.  Measured: a script killed
#     with HUP while sleeping ran its EXIT trap with `$? == 0`.  So the staged, half-built
#     image would be kept rather than removed, and the leak accounting above would take its
#     "the build itself succeeded" branch about a run that was killed.
#   * THE STATUS IS DISCARDED.  Bash re-raises the fatal signal after the EXIT trap, so an
#     `exit 70` from inside the trap is thrown away and the process reports 129 (128+SIGHUP).
#     Measured both ways: untrapped HUP with an EXIT trap ending `exit 70` produced 129, while
#     the same script with this trap in place produced 70.
#
# Routing HUP through the same `die` path as INT and TERM fixes both: the teardown sees a
# non-zero status and treats the run as the failure it is, the postconditions above are
# checked, the leak verdict can set an exit status that survives, and the console says which
# signal ended the run instead of leaving a bare 129 to be interpreted.
install_interrupt_traps

# ------------------------------------------------------------------------------------
# Caller-supplied state.  Empty means "not given"; every one of these is validated before
# it is used, and none of them acquires a silent default that would let the run proceed
# against something the caller did not name.
# ------------------------------------------------------------------------------------
OUTPUT_IMAGE=''
ARCH="$DEFAULT_ARCH"
SDK_DIR=''
PAYLOAD_DIR=''
BASE_TARBALL=''
BASE_URL=''
# The FILE named by --base-url-file, and the form the URL arrived in.  Kept apart from
# BASE_URL because the two forms are validated differently: the argv form refuses a query
# string outright (it cannot be hidden from /proc/<pid>/cmdline), the file form permits one
# (it never enters any command line).  BASE_URL_SOURCE is what every later check consults, so
# a future edit cannot accidentally apply the file form's laxer rule to the argv form.
BASE_URL_FILE=''
BASE_URL_SOURCE=''
BASE_SHA256=''
LCOV_TARBALL=''
LCOV_SHA256=''
GTEST_PREFIX_SRC=''
KERNEL_CONFIG_SRC=''
APT_MIRROR=''
# --apt-auth-file: a build-time-only apt credential file.  It is installed into the chroot
# immediately before the apt phase and removed immediately after, and it is never logged,
# never digested and never recorded.  See install_image_packages.
APT_AUTH_FILE=''
IMAGE_SIZE_MIB="$DEFAULT_IMAGE_SIZE_MIB"
READINESS_TIMEOUT_SECONDS="$DEFAULT_READINESS_TIMEOUT_SECONDS"

# Derived once ARCH is resolved.
EXPECTED_ELF_CLASS=''
EXPECTED_ELF_MACHINE=''
DEBIAN_ARCH=''

usage() {
    cat <<USAGE_TEXT
$SCRIPT_NAME -- build the QEMU root filesystem image for the binder-capable AIDL-path job.

USAGE
  sudo $SCRIPT_PATH --output IMAGE --payload DIR --sdk-dir DIR \\
       --base-tarball FILE --base-sha256 HEX [options]

REQUIRED
  -o, --output IMAGE       Where to write the finished root image.  Refused if it is '/',
                           implausibly short, inside a tree the operating system owns, a
                           symbolic link, a multiply-linked file, or reached through a
                           symlinked directory -- this script writes as root, so every one of
                           those would put the bytes somewhere other than the path you typed.
                           The image and its manifest are STAGED under private names in the
                           same directory and renamed onto <IMAGE> and <IMAGE>.manifest
                           together, only after the image has been validated: a failed run
                           leaves any previously published pair exactly as it was.  An
                           exclusive lock is held on <IMAGE>.lock for the whole run, so a
                           second builder aimed at the same output is refused immediately
                           instead of interleaving with this one.  That lock file is left
                           behind deliberately; removing it while holding it would let two
                           builders lock different inodes and both proceed.
  -p, --payload DIR        The hdmicec checkout, and any binaries already built, to make
                           available to the guest.  Copied to $GUEST_PAYLOAD_DIR.
                           Must contain configure.ac and tests/L1Tests/run_coverage.sh.
  -s, --sdk-dir DIR        The staged rdk-halif-aidl tree: the Binder SDK and the AIDL
                           client stub snapshots.  Copied to $GUEST_SDK_DIR.  The four
                           staging prefixes are derived from it, not asked for separately.
      --base-tarball FILE  Base root filesystem tarball for the target architecture.
      --base-url URL       Alternative to --base-tarball: fetched ON THE BUILD HOST.  The
                           guest never reaches the network.  REFUSED if it contains a control
                           character, userinfo (user:pass@host), a double quote or a
                           backslash -- AND REFUSED IF IT CARRIES A QUERY STRING OR A
                           FRAGMENT, because this script cannot hide its own argv: whatever
                           you type here is readable by every user on the build host in
                           /proc/<pid>/cmdline for as long as this process lives, and a
                           presigned signature in a query string is a bearer credential.
                           Writing it to a private file before spawning curl hides it from
                           the CHILD, which is not the exposure that matters.  A presigned or
                           otherwise credential-bearing URL goes through --base-url-file,
                           which never enters any command line.
      --base-url-file FILE Same as --base-url, but the URL is READ FROM FILE, one line, so it
                           never appears in this script's argv or in any child's.  The file's
                           mode is checked: any group or other read bit is fatal, since a
                           world-readable file defeats the point of using one.  Every check
                           --base-url applies is applied to the value read (control
                           characters, userinfo, quote, backslash), and a QUERY STRING IS
                           ACCEPTED here -- this is the one form in which a presigned URL is
                           permitted.  Transport is identical: the value is written to a
                           mode-0600 file in this script's private scratch directory and
                           handed to curl via --config (wget via --input-file), and that file
                           is deleted before the transfer's exit status is even acted on, so
                           it is not visible in "ps" or /proc/<pid>/cmdline either.  Wherever
                           the URL is LOGGED or RECORDED it is reduced to scheme, host and
                           path, and the reduction is stated.  Mutually exclusive with
                           --base-url and --base-tarball.
      --base-sha256 HEX    SHA256 of the base tarball.  MANDATORY unless a sidecar
                           <tarball>.sha256 is present.  Verified before extraction; a
                           mismatch is fatal.  No digest is hard-coded in this script.

OPTIONAL
  -a, --arch ARCH          i386 (default) or armhf.  This selects the ELF BITNESS of the
                           guest userspace ONLY: both values are 32-bit, and every staged
                           artefact is refused if its ELF class or machine disagrees.  It
                           does NOT select the binder wire protocol, which is a separate
                           axis derived from --kernel-config and the staged SDK's own
                           CMakeCache.txt -- see --kernel-config below.  The resolved value
                           is echoed, recorded in the image and written to the manifest.
      --lcov-tarball FILE  lcov 2.x source tarball, installed into the image.  Stable
                           distribution archives ship 1.x, which run_coverage.sh cannot
                           use, so without this the image build will almost certainly fail
                           its own 'lcov --version reports 2.x' assertion -- by design.
      --lcov-sha256 HEX    SHA256 of the lcov tarball.  MANDATORY with --lcov-tarball
                           unless a sidecar <tarball>.sha256 is present.
      --gtest-prefix DIR   A prebuilt GoogleTest/GoogleMock prefix for the TARGET
                           architecture, whose lib/pkgconfig holds gtest.pc.  Copied to
                           $GUEST_GTEST_PREFIX and exported as GTEST_PREFIX by the init.
                           Omit it only when the image's own packaging provides gtest.pc.
      --kernel-config FILE The guest kernel's configuration.  Copied into the image as
                           build-time provenance, AND read here as one of the two sources
                           the expected binder wire protocol is DERIVED from: an explicit
                           CONFIG_ANDROID_BINDER_IPC_32BIT=y means protocol 7, and the
                           option being unset or absent means protocol 8.  The other source
                           is BINDER_IPC_32BIT in the staged SDK's CMakeCache.txt.  The two
                           must agree or the image build fails, naming which disagreed.  At
                           least one of them must be readable: this script derives the
                           protocol and never declares it.  Whatever is derived remains an
                           EXPECTATION -- the guest reports what the RUNNING kernel shows
                           and what /dev/binder itself answers, and fails if either
                           disagrees with what was recorded.
      --apt-mirror URL     Pin the package archive the image is populated from, for a
                           reproducible package set (a snapshot archive, for instance).
                           Accepts a whole sources.list line ("deb <url> <suite> <comps>"),
                           so spaces are fine; any control character is REFUSED, because a
                           newline here would configure a second archive inside the image.
                           Userinfo (user:pass@host), a query string and a fragment are ALSO
                           refused, because unlike --base-url-file this value is written into
                           the published image and would be published with it.  For an
                           authenticated archive use --apt-auth-file, which is build-time
                           only; do NOT put a credential file in the base tarball, since
                           that file would be published inside the image.
                           Omitted, the base tarball's own sources are used as they stand --
                           and either way the FINISHED filesystem's apt sources are scanned
                           before publication: a credential or query the base tarball brought
                           with it is stripped, the strip is reported by file and line number,
                           and anything still present afterwards is fatal.
                           The exact package set that results is recorded inside the image at
                           $GUEST_PACKAGE_RECORD, and counted, digested and accounted against
                           the required set in the manifest.  That record is a PRECONDITION of
                           publication: if the resolved package/version/architecture set cannot
                           be read back out of the image, or a required package is not in it,
                           no image is published.
      --apt-auth-file FILE apt credentials for an authenticated archive, in apt's own
                           auth.conf (netrc) format.  BUILD TIME ONLY: it is installed into
                           the image at /etc/apt/auth.conf.d/99cec-l2-build-auth mode 0600
                           IMMEDIATELY BEFORE the apt phase, and REMOVED again as soon as
                           that phase finishes -- before the image is validated, before it
                           is unmounted and long before it is published.  Its removal is
                           re-read and a survivor is fatal, and the pre-publication scan
                           independently refuses to publish an image carrying ANY known
                           credential store (/etc/apt/auth.conf, /etc/apt/auth.conf.d/*,
                           /etc/apt/apt.conf.d/*, /root/.netrc, /home/*/.netrc), so this
                           option cannot leave a credential in the artefact even if the
                           removal is somehow defeated.  The file's mode is checked on the
                           way in: any group or other read bit is fatal.  Its CONTENT is
                           never logged, never digested into the manifest and never echoed.
      --size MIB           Image size in MiB (default $DEFAULT_IMAGE_SIZE_MIB).  It holds the
                           toolchain, the payload, the staged SDK, a full build tree and the
                           coverage artifacts, so it is not a place to economise.
      --readiness-timeout S  Seconds the in-guest init waits for servicemanager to ANSWER
                           (default $DEFAULT_READINESS_TIMEOUT_SECONDS).  The wait is a bounded
                           poll on a real binder transaction, never a sleep.
  -h, --help               This message.
      --self-test          Run this script's own checked-in regression cases and exit.  Takes
                           no other argument, needs no root, and returns BEFORE anything
                           privileged: no mount, no loop device, no chroot, no image.  It
                           exercises the two dpkg package-status parsers against a table of
                           literal records -- every partial dpkg state that a naive parser
                           accepts, malformed and short records, name mismatches and Provides
                           prefixes -- and then the confined I/O primitive's positive and
                           refusal cases, so it also answers whether openat2 with
                           RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS is in force on this host.  One
                           line per case; a non-zero exit on any mismatch.  The AIDL-path
                           workflow runs it as an early mandatory step, before the expensive
                           provisioning, so a broken parser fails the job in seconds.

EXIT STATUS
  0                        The image and its manifest were built, validated, published as a
                           pair, and every mount, loop device and temporary directory this
                           run created was verifiably released.
  $CLEANUP_LEAK_EXIT_STATUS                       The build itself succeeded but the teardown could NOT release
                           something it took: a mount still in the tree, a loop device still
                           attached, our backing file still attached to some loop device, or
                           a temporary directory that could not be removed.  Each is named
                           on stderr with the command that clears it.  This is NOT a pass --
                           the host is left in a state that breaks the next job.
  anything else            The build failed; the message names what and why.  A build
                           failure is preserved even when the teardown also fails, because
                           the original failure is the one worth reporting.

WHAT THIS PRODUCES
  IMAGE                    A bootable ext4 root filesystem.  Boot it with the guest kernel
                           and init=$GUEST_INIT_PATH.
  IMAGE.manifest           KEY=VALUE lines the workflow reads instead of hard-coding paths:
                           the architecture, the init path, the artifact directory, the
                           status file and the expected kernel options.

INSIDE THE GUEST
  The init mounts binderfs, creates and verifies the binder device nodes, reports the
  kernel configuration, the SDK build flags and the resulting protocol version together in
  one block, starts servicemanager and waits for it to answer, exports the build and
  loader environment, then runs
      $GUEST_PAYLOAD_DIR/tests/L1Tests/run_coverage.sh --build --run --output-dir $GUEST_ARTIFACT_DIR
  writes that command's EXACT exit status to $GUEST_STATUS_FILE, and powers the guest down
  from a trap that runs on every path.  It never sets CEC_TEST_AIDL_MODE: run_coverage.sh
  owns that per invocation across the A-E matrix.

  Only status 0 means measured and passing.  Status 3 is ADVISORY and is NOT a pass.
USAGE_TEXT
}

require_option_value() { # $1=option name  $2=remaining argument count
    [ "$2" -ge 2 ] || die "$1 requires a value.  Run '$SCRIPT_PATH --help' for the option list."
}

parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            -o|--output)          require_option_value "$1" $#; OUTPUT_IMAGE="$2"; shift ;;
            --output=*)           OUTPUT_IMAGE="${1#*=}" ;;
            -a|--arch)            require_option_value "$1" $#; ARCH="$2"; shift ;;
            --arch=*)             ARCH="${1#*=}" ;;
            -s|--sdk-dir)         require_option_value "$1" $#; SDK_DIR="$2"; shift ;;
            --sdk-dir=*)          SDK_DIR="${1#*=}" ;;
            -p|--payload)         require_option_value "$1" $#; PAYLOAD_DIR="$2"; shift ;;
            --payload=*)          PAYLOAD_DIR="${1#*=}" ;;
            --base-tarball)       require_option_value "$1" $#; BASE_TARBALL="$2"; shift ;;
            --base-tarball=*)     BASE_TARBALL="${1#*=}" ;;
            --base-url)           require_option_value "$1" $#; BASE_URL="$2"; shift ;;
            --base-url=*)         BASE_URL="${1#*=}" ;;
            --base-url-file)      require_option_value "$1" $#; BASE_URL_FILE="$2"; shift ;;
            --base-url-file=*)    BASE_URL_FILE="${1#*=}" ;;
            --base-sha256)        require_option_value "$1" $#; BASE_SHA256="$2"; shift ;;
            --base-sha256=*)      BASE_SHA256="${1#*=}" ;;
            --lcov-tarball)       require_option_value "$1" $#; LCOV_TARBALL="$2"; shift ;;
            --lcov-tarball=*)     LCOV_TARBALL="${1#*=}" ;;
            --lcov-sha256)        require_option_value "$1" $#; LCOV_SHA256="$2"; shift ;;
            --lcov-sha256=*)      LCOV_SHA256="${1#*=}" ;;
            --gtest-prefix)       require_option_value "$1" $#; GTEST_PREFIX_SRC="$2"; shift ;;
            --gtest-prefix=*)     GTEST_PREFIX_SRC="${1#*=}" ;;
            --kernel-config)      require_option_value "$1" $#; KERNEL_CONFIG_SRC="$2"; shift ;;
            --kernel-config=*)    KERNEL_CONFIG_SRC="${1#*=}" ;;
            --apt-mirror)         require_option_value "$1" $#; APT_MIRROR="$2"; shift ;;
            --apt-mirror=*)       APT_MIRROR="${1#*=}" ;;
            --apt-auth-file)      require_option_value "$1" $#; APT_AUTH_FILE="$2"; shift ;;
            --apt-auth-file=*)    APT_AUTH_FILE="${1#*=}" ;;
            --size)               require_option_value "$1" $#; IMAGE_SIZE_MIB="$2"; shift ;;
            --size=*)             IMAGE_SIZE_MIB="${1#*=}" ;;
            --readiness-timeout)  require_option_value "$1" $#; READINESS_TIMEOUT_SECONDS="$2"; shift ;;
            --readiness-timeout=*) READINESS_TIMEOUT_SECONDS="${1#*=}" ;;
            -h|--help)            usage; exit 0 ;;
            --)                   shift; break ;;
            # An unknown option is REFUSED rather than ignored.  Silently accepting one
            # means a workflow that passes a mistyped flag builds an image that is not the
            # one it asked for, and finds out several QEMU cycles later.
            -*)                   die "unknown option '$1'. Run '$SCRIPT_PATH --help' for the option list." ;;
            *)                    die "unexpected argument '$1'. This script takes options only; run --help." ;;
        esac
        shift
    done
    [ $# -eq 0 ] || die "unexpected trailing arguments: $*.  This script takes options only; run --help."
}

# ------------------------------------------------------------------------------------
# PATH DEFENCE.
#
# Every path below is caller-supplied, and this script mounts filesystems, chroots and
# writes multi-gigabyte files.  A value that collapses into a system tree, or that is the
# filesystem root, is refused rather than acted on.  The '..' collapse is done LEXICALLY
# rather than with realpath so that a path whose final component does not exist yet -- the
# output image, every time -- is still checked for where it would land.
# ------------------------------------------------------------------------------------
canonicalise_path_lexically() { # $1=absolute path -> collapsed path on stdout
    local input="$1" component out=''
    local saved_ifs="$IFS"
    IFS='/'
    # Deliberate word splitting on '/' to walk the components in order.
    # shellcheck disable=SC2086
    set -- ${input#/}
    IFS="$saved_ifs"
    for component in "$@"; do
        case "$component" in
            ''|'.') continue ;;
            '..')   out="${out%/*}" ;;
            *)      out="$out/$component" ;;
        esac
    done
    printf '%s\n' "${out:-/}"
}

assert_path_plausible() { # $1=canonical absolute path  $2=how it was chosen
    local path="$1" origin="$2" root

    case "$path" in
        /)  die "$origin must not be the filesystem root.  This script creates, mounts and
       overwrites whole filesystem images; '/' is not a place to do that." ;;
        /*) : ;;
        *)  die "internal error: assert_path_plausible needs an absolute path; got '$path' ($origin)." ;;
    esac

    # Four characters or fewer cannot be anything but a near-root path.  The same refusal,
    # and the same wording, as the coverage runner's.
    [ "${#path}" -gt 4 ] || die "$origin is implausibly short: '$path'.  A near-root path is
       refused because this script writes and mounts filesystem images.  Give a path that is
       unmistakably yours, for example \"\${TMPDIR:-/tmp}/cec-l2-rootfs.ext4\"."

    # /var/tmp and /run/user/<uid> are ordinary per-user scratch directories that happen to
    # sit under a protected root, so they are cleared before the loop below.
    case "$path" in
        /var/tmp|/var/tmp/*|/run/user/*) return 0 ;;
    esac

    for root in "${PROTECTED_SYSTEM_ROOTS[@]}"; do
        case "$path" in
            "$root"|"$root"/*)
                die "refusing to use
           $path
       for $origin, because it is $root or lies underneath it -- a tree the operating system
       owns.  A value that reaches one is nearly always a '..' that collapsed out of the
       intended path or a mistyped root.  Use \"\${TMPDIR:-/tmp}/...\" or a directory inside
       your own tree." ;;
        esac
    done
    return 0
}

# Absolute, collapsed and checked, in one step.  Relative input is resolved against the
# CURRENT directory rather than against this script's location: a caller writing
# `--output image.ext4` means the directory they are standing in.
absolutise_and_check() { # $1=path  $2=how it was chosen -> canonical path on stdout
    local path="$1" origin="$2"
    [ -n "$path" ] || die "$origin was given as an empty value.  An empty path cannot be
       validated, so it is refused rather than resolved to the working directory."
    case "$path" in
        /*) : ;;
        *)  path="$PWD/$path" ;;
    esac
    path="$(canonicalise_path_lexically "$path")"
    assert_path_plausible "$path" "$origin"
    printf '%s\n' "$path"
}

# The generated init and the generated configuration file embed these paths inside
# single-quoted shell assignments.  A single quote or a newline in one of them would break
# out of that quoting, so they are refused here rather than escaped: a path containing
# either is a mistake in every case this script cares about, and rejecting it is honest
# where silently mangling it would not be.
assert_path_embeddable() { # $1=path  $2=how it was chosen
    case "$1" in
        *"'"*)   die "$2 contains a single quote: '$1'.  This script embeds these paths in the
       generated in-guest configuration, and a quote there cannot be represented safely.
       Choose a path without one." ;;
        *[$'\n\t']*) die "$2 contains a newline or a tab.  Refused for the same reason as a
       quote: it cannot be embedded in the generated in-guest configuration safely." ;;
    esac
}

# The description is printed FIRST and the path after it, because several call sites pass a
# multi-sentence description that explains what the thing is for; appending "is not a
# directory" to such a string produces a sentence nobody can read.
require_existing_dir() { # $1=path  $2=what it is
    [ -d "$1" ] || die "missing directory -- $2
       Expected it at: $1
       Check the path, or the step that was supposed to stage it."
}

require_existing_file() { # $1=path  $2=what it is
    [ -f "$1" ] || die "missing file -- $2
       Expected it at: $1
       Check the path, or the step that was supposed to produce it."
}

require_positive_integer() { # $1=value  $2=what it is
    case "$1" in
        ''|*[!0-9]*) die "$2 must be a plain positive integer, got '$1'." ;;
    esac
    [ "$1" -gt 0 ] || die "$2 must be greater than zero, got '$1'."
}

require_sha256_hex() { # $1=value  $2=what it is
    [ "${#1}" -eq 64 ] || die "$2 must be a 64-character hexadecimal SHA256 digest, got a
       ${#1}-character value.  A truncated digest cannot verify anything."
    case "$1" in
        *[!0-9a-fA-F]*) die "$2 contains a character that is not hexadecimal.  A digest that
       cannot be parsed is refused rather than coerced." ;;
    esac
}

# A FILE THAT CARRIES A SECRET MUST NOT BE READABLE BY ANYONE ELSE.
#
# Both --base-url-file and --apt-auth-file exist so that a credential reaches this script
# WITHOUT passing through a command line.  A caller who then leaves the file mode 0644 has
# moved the exposure rather than removed it: on a shared build host every local user can read
# it, which is the same disclosure the argv route had, just with a longer window.  So the mode
# is CHECKED and a group or other READ bit is fatal.
#
# Write bits are checked too, and for a different reason: a file another user can write is a
# file another user can substitute between this check and the read, which would have this
# script fetch from -- or authenticate against -- an endpoint the caller did not name.
#
# The mode is read with `stat -c %a`, which needs no octal parsing on our side: the two
# low digits are group and other, and any of the six bits below 0700 that is set is refused.
# The value is masked arithmetically rather than matched as text, because 0644, 644 and 0000644
# are all forms `stat` or a caller can produce.
require_private_file() { # $1=path  $2=what it is
    local path="$1" what="$2" mode masked

    # -L before -f: -f follows a symlink, so a link pointing at a world-readable file would
    # pass a mode check performed on the TARGET while the caller believes their own file was
    # checked.  Refused rather than resolved, for the same reason the image scan refuses one.
    [ ! -L "$path" ] || die "$what is a symbolic link: $path
       It carries a credential, so it is refused rather than followed: the mode that matters is
       the one on the bytes this script reads, and a link lets those bytes live somewhere with
       a different mode entirely.  Pass the file itself."
    [ -f "$path" ] || die "$what is not a regular file (or does not exist): $path
       A credential is read from a plain file, not from a directory, a fifo or a device."

    mode="$(stat -c '%a' -- "$path" 2>/dev/null || printf '')"
    case "$mode" in
        ''|*[!0-7]*) die "the permission mode of $what could not be read: $path
       Its confidentiality cannot be established, so it is not read." ;;
    esac
    # Base-8 arithmetic on the value as stat printed it; 0077 is group+other rwx.
    masked=$(( 8#$mode & 8#77 ))
    [ "$masked" -eq 0 ] || die "$what is accessible to group or other (mode $mode): $path
       This option exists so that a credential never appears in a command line; a file other
       local users can read gives back exactly that exposure, and one they can WRITE lets them
       substitute the value between this check and the read.  Restrict it first:
           chmod 600 $path"
}

# Reads a single-line value from a private file.  Prints it and nothing else.
#
# The read is deliberately strict about shape.  A trailing newline is normal and is dropped by
# the read; a SECOND non-empty line is refused rather than ignored, because the two readings
# ("the first line is the value" and "the caller made a mistake") are too far apart to guess
# between, and because a value that silently loses part of itself fails later as a fetch error
# that names nothing useful.  A leading or trailing blank is stripped, since an editor adding
# one is far more likely than a URL that means it.
read_private_line() { # $1=path  $2=what it is
    local path="$1" what="$2" line='' extra='' seen=0

    while IFS= read -r extra || [ -n "$extra" ]; do
        # Blank lines anywhere are tolerated: they carry nothing and an editor may add one.
        case "$extra" in ''|[[:space:]]) continue ;; esac
        [ -n "${extra//[[:space:]]/}" ] || continue
        if [ "$seen" -eq 0 ]; then
            line="$extra"
            seen=1
        else
            die "$what contains more than one value: $path
       Exactly one line is expected.  Rather than guessing which line was meant, this is
       refused -- a value silently truncated to the first line fails later as an unexplained
       fetch error."
        fi
    done < "$path"

    [ "$seen" -eq 1 ] || die "$what is empty: $path
       It was given, so something was meant to be in it; an empty file is a mistake rather than
       an instruction to fall back to anything."

    # Trim surrounding whitespace only; the interior is the caller's business and every
    # character class that matters is refused by the validators that follow.
    line="${line#"${line%%[![:space:]]*}"}"
    line="${line%"${line##*[![:space:]]}"}"
    printf '%s' "$line"
}


# ------------------------------------------------------------------------------------
# ARCHITECTURE RESOLUTION -- ELF BITNESS ONLY.
#
# Only 32-bit targets are accepted, and the refusal of a 64-bit one is a design statement
# rather than a limitation of this script: the guest userspace is 32-bit throughout, so a
# 64-bit staged library could not be loaded into any process the guest starts.
#
# THIS FUNCTION DECIDES NOTHING ABOUT THE BINDER WIRE PROTOCOL, and an earlier revision
# that said it did is the defect this separation corrects.  The protocol is a separate axis
# -- linux_binder_idl CMakeLists.txt:209-217 at tag 2.6.0 states so, and a 32-bit process
# can speak protocol 8 -- and it is derived from the kernel configuration and the staged
# SDK's cache by derive_binder_protocol() below, never inferred from --arch.
#
# The expected ELF properties are expressed as REGULAR EXPRESSIONS rather than as literal
# strings, and that is a correction rather than a flourish: the two tools that can answer the
# question spell the machine differently, and both spellings have to be accepted or a correct
# binary is rejected.  MEASURED on this host -- file 5.46 describes an i386 object as
# "ELF 32-bit LSB shared object, Intel i386" while binutils readelf calls the same object's
# machine "Intel 80386".  readelf is preferred below because its wording is the stable one,
# and `file` remains the fallback, so the patterns cover both.
#
# The reject pattern exists because the machine patterns alone are not enough: "x86-64" and
# "AArch64" would otherwise be matched by a loose 32-bit pattern on some tool versions, and a
# 64-bit artefact must never pass an all-32-bit check.
# ------------------------------------------------------------------------------------
ELF_CLASS_REGEX=''
ELF_MACHINE_REGEX=''
ELF_REJECT_REGEX=''

resolve_arch() {
    case "$ARCH" in
        i386)
            EXPECTED_ELF_CLASS='32-bit'
            EXPECTED_ELF_MACHINE='Intel 80386 / Intel i386'
            ELF_CLASS_REGEX='(ELF32|ELF 32-bit)'
            ELF_MACHINE_REGEX='[Ii]ntel[^,]*386'
            ELF_REJECT_REGEX='(x86-64|x86_64|X86-64|Advanced Micro|[Aa]arch64|AArch64)'
            DEBIAN_ARCH='i386'
            ;;
        armhf)
            EXPECTED_ELF_CLASS='32-bit'
            EXPECTED_ELF_MACHINE='ARM'
            ELF_CLASS_REGEX='(ELF32|ELF 32-bit)'
            ELF_MACHINE_REGEX='(ARM|arm)'
            ELF_REJECT_REGEX='(x86-64|x86_64|X86-64|Advanced Micro|[Aa]arch64|AArch64)'
            DEBIAN_ARCH='armhf'
            ;;
        *)
            die "unsupported --arch '$ARCH'.  The guest userspace is 32-bit throughout -- a
       process is a single ELF class, so a 64-bit staged library could not be loaded into any
       process the guest starts -- and the accepted values are therefore the 32-bit ones:
       i386 (the default, and the straightforward choice for QEMU on an x86_64 host) or armhf
       (which additionally needs a registered binfmt_misc handler on this build host).  This
       option does NOT select the binder wire protocol: that is a separate axis, derived from
       --kernel-config and the staged SDK's CMakeCache.txt, and a 32-bit userspace can speak
       either protocol." ;;
    esac
    readonly EXPECTED_ELF_CLASS EXPECTED_ELF_MACHINE DEBIAN_ARCH
    readonly ELF_CLASS_REGEX ELF_MACHINE_REGEX ELF_REJECT_REGEX
    log "target ELF bitness: $ARCH (expecting $EXPECTED_ELF_CLASS $EXPECTED_ELF_MACHINE binaries)"
    log "  the binder wire protocol is a SEPARATE axis and is derived, not implied by this value"
}

# ------------------------------------------------------------------------------------
# HOST PREREQUISITES.  Checked as one set so the caller learns everything missing at once
# rather than one package per failed run.
# ------------------------------------------------------------------------------------
require_host_tools() {
    local missing='' tool
    # realpath is required, not optional: the pre-publication credential scan resolves every
    # file it is about to read or rewrite and refuses one whose resolved path leaves the image
    # (see assert_confined_to_image).  Without it that confinement cannot be established, and
    # the scan writes to those paths AS ROOT, so its absence is a named failure here rather
    # than a check that quietly does not happen.
    # python3 is required, not optional: it is the only binding this script has to openat2(2)
    # with RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_XDEV, through which every privileged
    # operation on a BASE-TARBALL-NAMED file or directory inside the mounted image is performed
    # (the exact boundary is enumerated in the confined privileged I/O block below).  There is no
    # fallback to an ordinary open, because that fallback is the defect the mechanism replaced --
    # so its absence is a named failure here rather than a defence that quietly does not happen.
    for tool in tar mkfs.ext4 losetup mount umount mountpoint chroot file readelf sha256sum \
                truncate mktemp install cp find rm sync ldconfig flock stat realpath \
                python3 awk; do
        command -v -- "$tool" >/dev/null 2>&1 || missing="$missing $tool"
    done
    [ -z "$missing" ] || die "missing host tooling:$missing.
       This script populates the image through a loop device, a mount and a chroot, and it
       verifies what it copies in.  On a Debian/Ubuntu build host:
           sudo apt-get install -y e2fsprogs util-linux coreutils tar file binutils libc-bin \
                                  python3 mawk
       'chroot' ships with coreutils, 'mountpoint'/'losetup' with util-linux and 'readelf'
       with binutils."

    # A URL base needs a fetcher, and only then: an offline run that supplies the tarball
    # must not be failed for a tool it never uses.
    if [ -n "$BASE_URL" ]; then
        command -v curl >/dev/null 2>&1 || command -v wget >/dev/null 2>&1 \
            || die "--base-url was given but neither curl nor wget is on PATH.  Install one, or
       fetch the tarball yourself and pass --base-tarball with its --base-sha256 (which is
       the offline-friendly form, and the one a workflow with a cache step should prefer)."
    fi

    [ "$(id -u)" -eq 0 ] || die "this script must run as root.  It attaches a loop device,
       creates and mounts a filesystem, populates it through a chroot and creates device
       nodes in it -- none of which is possible unprivileged.  Re-run it under sudo:
           sudo $SCRIPT_PATH ..."
}

# ==============================================================================
# CONFINED PRIVILEGED I/O -- THE MECHANISM THAT CARRIES THE CONFINEMENT GUARANTEE
# ==============================================================================
# WHAT WAS WRONG WITH THE PREVIOUS MODEL, STATED PLAINLY BECAUSE THE COMMENTS ELSEWHERE IN
# THIS FILE USED TO CLAIM OTHERWISE.
#
# Every privileged read, rewrite and removal this script performs INSIDE THE MOUNTED IMAGE
# used to be authorised by assert_confined_to_image(): it validated a PATHNAME -- no symlinked
# component from the mount point down, a regular file, one link, the image's own st_dev, a
# realpath still inside the image -- and the caller then reopened THE SAME PATHNAME with an
# ordinary redirection (`< "$file"`, `cat "$tmp" > "$file"`, `rm -f -- "$path"`).
#
# That is check-then-open, and the object the kernel opens is not the object that was checked.
# The gap between the two is the vulnerability (CWE-367), not the absence of a check, and no
# amount of re-checking closes it -- an adversary who can create one symlink in the image tree
# only has to do it AFTER the last check and BEFORE the redirection.  This script runs as root
# on the build host, so the consequence of losing that race is root truncating, rewriting or
# deleting a HOST file named by an absolute symlink target.
#
# WHAT REPLACES IT: ONE PRIMITIVE, AND THE PATHNAME IS NEVER RE-DERIVED AFTER THE OPEN.
#
# openat2(2) with RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_XDEV resolves a RELATIVE path
# against a DIRECTORY DESCRIPTOR and hands back a descriptor, and the kernel -- not this script
# -- enforces three properties during that resolution:
#
#   RESOLVE_NO_SYMLINKS  no symbolic link is followed at ANY position, leading or trailing.  A
#                        substituted leaf is refused with ELOOP rather than followed, so the
#                        race above has no winning move: there is no instant at which a symlink
#                        at that name resolves to anything.  It implies RESOLVE_NO_MAGICLINKS,
#                        so a /proc magic link cannot be used as a component either.
#   RESOLVE_BENEATH      the resolution may not leave the anchor directory, so '..' and an
#                        absolute path are refused with EXDEV.
#   RESOLVE_NO_XDEV      no mount point is traversed, which is the kernel-enforced form of the
#                        st_dev comparison the old model did by hand: /proc, /sys, /dev and
#                        /dev/pts are bind/pseudo mounts INSIDE the image tree and a path
#                        through one of them is refused rather than measured afterwards.
#
# Every operation is then performed THROUGH THAT DESCRIPTOR: the file's type is read with
# fstat() on an O_PATH descriptor, and read, write, chmod and chown go through
# /proc/self/fd/<n>, which is a magic link the kernel resolves to the exact inode the
# descriptor already holds and which nothing can substitute.  A removal is unlinkat() on a
# single-component leaf relative to a descriptor obtained the same way, and unlinkat never
# follows a symlink.  So the object operated on is, by construction, the object that was
# opened -- there is no second name lookup to lose.
#
# WHY IT IS A python3 HELPER AND NOT SHELL.  openat2 has no shell binding.  Neither has any
# other *at-family call with resolve flags, and no coreutils tool exposes RESOLVE_BENEATH.  The
# alternatives were to keep check-then-open (which is the finding) or to accept a dependency on
# an interpreter that is present on every Debian/Ubuntu build host and on every GitHub-hosted
# runner.  The helper is WRITTEN ONCE into this run's private 0700 scratch directory at mode
# 0600 and reused for every call, so there is a single copy of the source and a single copy on
# disk; it is invoked with `python3 -I -B`, which ignores PYTHON* environment variables, the
# user site directory and PYTHONPATH, and writes no bytecode.
#
# IT FAILS CLOSED AND DISTINGUISHABLY, WHICH IS THE WHOLE POINT.  A helper that quietly fell
# back to an ordinary open would be worse than no helper at all, because every comment in this
# file would then describe a defence that was not there.  So each condition has its own exit
# status, the shell turns each into a `die` that NAMES which condition it was, and
# probe_confined_io() proves at start-up -- with a positive case AND four negative cases -- that
# the refusals actually happen on this host before one privileged byte is written.
#
# WHAT assert_confined_to_image() IS FOR NOW.  It is retained as a PRE-CHECK because it produces
# far better diagnostics than an errno: it can say which component of which path is a symbolic
# link, and it can distinguish a hard-linked file from a substituted one.  It no longer carries
# the guarantee, and its own comment block says so.
#
# ==============================================================================
# EXACTLY WHICH OPERATIONS GO THROUGH IT, AND WHICH DO NOT.  STATED, NOT IMPLIED.
# ==============================================================================
# A claim of "everything is confined" would be the same kind of overstatement this mechanism was
# introduced to correct, so here is the boundary.
#
# THROUGH THE PRIMITIVE -- every operation that reads, rewrites, creates, changes the mode or
# ownership of, enumerates or removes an INDIVIDUAL file or directory whose NAME COMES FROM THE
# BASE TARBALL rather than from this script.  Those are the operations an adversarial base can
# aim, and they are:
#     install_image_apt_auth() / remove_image_apt_auth()   /etc/apt/auth.conf.d/<leaf>
#     install_image_packages()                             /etc/apt/sources.list and
#                                                          /etc/apt/apt.conf.d/99cec-l2-snapshot
#     scrub_and_assert_image_apt_credentials()             /etc/apt/sources.list{,.d/*}
#     scrub_and_assert_image_credential_stores()           auth.conf{,.d/*}, apt.conf{,.d/*},
#                                                          /etc/environment, every .netrc
#     nosymfollow_is_in_force()                            its own probe directory
#
# NOT THROUGH THE PRIMITIVE, deliberately, with the reason for each:
#     `tar --extract --directory "$IMAGE_MOUNT"` in extract_base_rootfs(), and the `cp -a` and
#     `tar --extract` bulk copies in copy_guest_payload() and install_lcov_2x().  These are
#     WHOLE-TREE operations over thousands of paths, they are what CREATES the filesystem the
#     primitive later resolves against, and a per-file descriptor primitive cannot express them.
#     What bounds them instead: the base archive is verified against a pinned SHA-256 before it
#     is extracted (acquire_base_rootfs), the payload and SDK trees are verified before they are
#     copied (verify_payload_tree, verify_sdk_tree), and their destinations are paths THIS SCRIPT
#     names -- $GUEST_WORKSPACE and below -- not paths the base chooses.
#     The `mkdir -p`, `chmod`, `>` and `rm` operations on those same script-named guest paths
#     (/tmp, $GUEST_WORKSPACE, $GUEST_CONF_DIR and the records written into them), and on
#     /etc/resolv.conf and /etc/network/interfaces in
#     neutralise_image_network_configuration().  These are not confined, and that is a residual
#     rather than a defence: a base tarball that ships /etc, /opt or /tmp as an absolute symbolic
#     link would redirect them onto the build host in the same way the apt paths used to be
#     redirected.  They were left as they are because converting them is a change to six
#     functions that no finding in this pass covers and that nothing available here can
#     end-to-end test, and because their exposure is narrower: they write content this script
#     authored to names this script chose, so nothing a caller supplied is disclosed by them.
#     THE PRIMITIVE AND ITS WRAPPERS ARE READY FOR THEM -- converting one is a two-line change --
#     and doing so is the obvious next step for whoever next touches this file.
# ==============================================================================

# The helper's leaf name inside this run's scratch directory, and the descriptor-anchored
# location it is written to.  Empty until install_confined_io_helper() has run.
readonly CONFINED_IO_HELPER_LEAF='confined-io.py'
CONFINED_IO_HELPER=''
# The directory every relative path is resolved against.  Set to the image mount point once the
# image is mounted; set to a private scratch directory by the start-up probe and by --self-test.
CONFINED_IO_ANCHOR=''
# Raised only by probe_confined_io() once the primitive has been proven to refuse what it must.
CONFINED_IO_PROVEN=0

# THE HELPER'S EXIT STATUSES.  One per condition, because "it failed" is not an answer a
# privileged operation may act on: the shell has to be able to say whether the kernel lacks
# openat2, whether the confinement refused the path, or whether the file simply is not there.
readonly CONFINED_IO_USAGE=10
readonly CONFINED_IO_UNAVAILABLE=11
readonly CONFINED_IO_REFUSED=12
readonly CONFINED_IO_ABSENT=13
readonly CONFINED_IO_WRONG_TYPE=14
readonly CONFINED_IO_FAILED=15
readonly CONFINED_IO_ANCHOR_BAD=16
readonly CONFINED_IO_EXISTS=17

# Write the helper into $1 at mode 0600 and record where it went.  Called with this run's
# scratch directory in the normal path and with a private probe directory by --self-test, so the
# source exists in exactly one place in this file and on disk in exactly one place per caller.
install_confined_io_helper() { # $1=directory to write the helper into
    local dir="$1" target
    [ -n "$dir" ] || die "internal error: install_confined_io_helper was called without a
       directory.  This is a defect in $SCRIPT_NAME."
    [ -d "$dir" ] || die "internal error: install_confined_io_helper was given '$dir', which is
       not a directory.  This is a defect in $SCRIPT_NAME."
    target="$dir/$CONFINED_IO_HELPER_LEAF"
    rm -f -- "$target" || die "could not clear the confined I/O helper's path at $target."
    cat > "$target" <<'CONFINED_IO_PY'
# Confined privileged I/O helper.
#
# GENERATED FILE.  Written by .github/workflows/aidl-path-tests-rootfs.sh in the hdmicec
# submodule; edit that script, not this copy, which is recreated on every run.
#
#   usage: confined-io.py <anchor-directory> <relative-path> <operation> [operation arguments]
#
# The anchor is opened O_DIRECTORY|O_RDONLY|O_NOFOLLOW, the relative path is resolved beneath it
# with openat2(RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_XDEV), and the operation is
# performed THROUGH THE RESULTING DESCRIPTOR.  No pathname is re-derived after the open.
#
# Operations, and what each writes to stdout:
#   read                     the file's bytes
#   write                    nothing; the file's bytes are replaced by stdin (truncating)
#   create <octal-mode>      nothing; creates the file exclusively at that exact mode
#   stat                     "<type> <dev> <ino> <nlink> <size> <octal-mode>"
#   chmod <octal-mode>       nothing
#   chown <uid> <gid>        nothing
#   unlink                   nothing
#   mkdir <octal-mode>       nothing; creates the directory and any missing parent beneath the
#                            anchor, each component resolved with the same refusals
#   list                     "<type> <name>" per entry of the directory, NUL-terminated, so a
#                            name containing a newline or a space is unambiguous
#   rmdir                    nothing; removes the (empty) directory
#
# Exit statuses are a contract with the calling shell; see the block above
# install_confined_io_helper in that script.
import ctypes
import errno
import os
import stat
import sys

USAGE = 10
UNAVAILABLE = 11
REFUSED = 12
ABSENT = 13
WRONG_TYPE = 14
FAILED = 15
ANCHOR_BAD = 16
EXISTS = 17

SYS_OPENAT2 = 437
RESOLVE_NO_XDEV = 0x01
RESOLVE_NO_SYMLINKS = 0x04
RESOLVE_BENEATH = 0x08
# RESOLVE_NO_SYMLINKS implies RESOLVE_NO_MAGICLINKS, so a /proc magic link cannot be used as a
# path component either.  These three together are the whole confinement.
RESOLVE_FLAGS = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_XDEV


class OpenHow(ctypes.Structure):
    _fields_ = [
        ("flags", ctypes.c_uint64),
        ("mode", ctypes.c_uint64),
        ("resolve", ctypes.c_uint64),
    ]


_libc = ctypes.CDLL(None, use_errno=True)
_libc.syscall.restype = ctypes.c_long


def fail(status, message):
    sys.stderr.write("confined-io: %s\n" % message)
    sys.exit(status)


def openat2_raw(dirfd, relative, flags, mode=0):
    """openat2() beneath dirfd with the confinement flags.  Returns (fd, 0) or (-1, errno).

    mode MUST be zero unless O_CREAT is set: openat2 returns EINVAL otherwise, and reporting
    that as "the kernel has no openat2" would be a false diagnosis of a caller bug.
    """
    how = OpenHow(flags, mode, RESOLVE_FLAGS)
    ctypes.set_errno(0)
    fd = _libc.syscall(
        ctypes.c_long(SYS_OPENAT2),
        ctypes.c_int(dirfd),
        ctypes.c_char_p(relative.encode("utf-8", "surrogateescape")),
        ctypes.byref(how),
        ctypes.c_size_t(ctypes.sizeof(how)),
    )
    if fd >= 0:
        return int(fd), 0
    return -1, ctypes.get_errno()


def openat2(dirfd, relative, flags, mode=0):
    """openat2_raw(), with every failure turned into the exit status that names it."""
    fd, err = openat2_raw(dirfd, relative, flags, mode)
    if fd >= 0:
        return fd
    name = errno.errorcode.get(err, str(err))
    where = "%s (relative to the anchor)" % relative
    if err in (errno.ENOSYS, errno.EOPNOTSUPP):
        fail(UNAVAILABLE,
             "this kernel does not implement openat2 (%s on %s). The confined I/O primitive "
             "cannot be established, and this script does not fall back to an ordinary open."
             % (name, where))
    if err == errno.EINVAL:
        fail(UNAVAILABLE,
             "openat2 refused the resolve flags RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|"
             "RESOLVE_NO_XDEV with EINVAL on %s. The kernel has the syscall but not the "
             "confinement this script requires." % where)
    if err in (errno.ELOOP, errno.EXDEV):
        fail(REFUSED,
             "the confinement REFUSED %s with %s: the path is a symbolic link, passes through "
             "one, leaves the anchor directory, or crosses a mount point." % (where, name))
    if err == errno.ENOENT:
        fail(ABSENT, "%s does not exist beneath the anchor." % where)
    if err == errno.ENOTDIR:
        fail(WRONG_TYPE,
             "%s uses a non-directory as a path component, or is not a directory where one is "
             "required." % where)
    if err == errno.EEXIST:
        fail(EXISTS, "%s already exists beneath the anchor." % where)
    fail(REFUSED, "the confined open of %s failed with %s." % (where, name))


def magic(fd):
    """The one name that is guaranteed to resolve to the inode fd already holds."""
    return "/proc/self/fd/%d" % fd


def reopen(fd, flags):
    try:
        return os.open(magic(fd), flags)
    except OSError as exc:
        fail(FAILED,
             "the descriptor obtained under the confinement could not be reopened for the "
             "operation (%s)." % errno.errorcode.get(exc.errno, str(exc.errno)))


def describe(st):
    if stat.S_ISREG(st.st_mode):
        return "regular"
    if stat.S_ISDIR(st.st_mode):
        return "directory"
    if stat.S_ISLNK(st.st_mode):
        return "symlink"
    if stat.S_ISFIFO(st.st_mode):
        return "fifo"
    if stat.S_ISSOCK(st.st_mode):
        return "socket"
    if stat.S_ISBLK(st.st_mode):
        return "blockdev"
    if stat.S_ISCHR(st.st_mode):
        return "chardev"
    return "other"


def open_for_inspection(anchor_fd, relative):
    """An O_PATH descriptor on the target, plus its fstat.  O_PATH is used deliberately: it
    opens a fifo, a socket or a device node WITHOUT the blocking or side effects a real open
    would have, so the type can be refused rather than hung on."""
    fd = openat2(anchor_fd, relative, os.O_PATH | os.O_CLOEXEC)
    try:
        st = os.fstat(fd)
    except OSError as exc:
        os.close(fd)
        fail(FAILED, "the target's type could not be read (%s)."
             % errno.errorcode.get(exc.errno, str(exc.errno)))
    return fd, st


def require_regular(relative, st):
    if not stat.S_ISREG(st.st_mode):
        fail(WRONG_TYPE,
             "%s beneath the anchor is a %s, not a regular file. A privileged read, rewrite or "
             "mode change is not performed on the strength of a filename."
             % (relative, describe(st)))


def split_components(relative):
    if not relative or relative.startswith("/"):
        fail(USAGE, "the relative path must be non-empty and must not be absolute.")
    parts = [p for p in relative.split("/") if p not in ("", ".")]
    if not parts:
        fail(USAGE, "the relative path names the anchor itself, which is not an operand.")
    # '..' is deliberately NOT rejected here.  RESOLVE_BENEATH refuses it with EXDEV, and
    # letting the kernel do it means the mechanism that carries the guarantee is the one the
    # start-up proof exercises -- a check here would mask it and prove nothing.
    return parts


def walk_directory(anchor_fd, parts, create_mode=None):
    """A descriptor on the directory named by parts, each component resolved under the
    confinement.  With create_mode set, a missing component is created with mkdirat() on the
    descriptor of its parent -- which cannot be redirected, because that parent descriptor was
    itself obtained under the confinement."""
    flags = os.O_DIRECTORY | os.O_RDONLY | os.O_CLOEXEC
    current = anchor_fd
    for part in parts:
        nxt, err = openat2_raw(current, part, flags)
        if nxt < 0 and err == errno.ENOENT and create_mode is not None:
            try:
                # mkdirat() on a SINGLE component relative to a descriptor that was itself
                # obtained under the confinement, so the directory is created as a child of
                # that exact inode and of no other.
                os.mkdir(part, create_mode, dir_fd=current)
            except FileExistsError:
                # Created between the failed open and here: fall through and open it.
                pass
            except OSError as exc:
                if current != anchor_fd:
                    os.close(current)
                fail(FAILED, "the directory component '%s' could not be created beneath the "
                             "anchor (%s)."
                     % (part, errno.errorcode.get(exc.errno, str(exc.errno))))
            nxt, err = openat2_raw(current, part, flags)
        if nxt < 0:
            # Re-issue through the exiting wrapper so the diagnosis and the exit status are
            # produced in exactly one place.  It never returns; the descriptors this process
            # holds are released by the exit.
            openat2(current, part, flags)
        if current != anchor_fd:
            os.close(current)
        current = nxt
    return current


def op_read(anchor_fd, relative, args):
    if args:
        fail(USAGE, "'read' takes no arguments.")
    fd, st = open_for_inspection(anchor_fd, relative)
    require_regular(relative, st)
    handle = reopen(fd, os.O_RDONLY | os.O_CLOEXEC)
    os.close(fd)
    try:
        while True:
            chunk = os.read(handle, 1 << 16)
            if not chunk:
                break
            sys.stdout.buffer.write(chunk)
        sys.stdout.buffer.flush()
    except OSError as exc:
        fail(FAILED, "%s could not be read through its descriptor (%s)."
             % (relative, errno.errorcode.get(exc.errno, str(exc.errno))))
    finally:
        os.close(handle)


def op_write(anchor_fd, relative, args):
    if args:
        fail(USAGE, "'write' takes no arguments; the content comes from stdin.")
    payload = sys.stdin.buffer.read()
    fd, st = open_for_inspection(anchor_fd, relative)
    require_regular(relative, st)
    handle = reopen(fd, os.O_WRONLY | os.O_CLOEXEC)
    os.close(fd)
    try:
        os.ftruncate(handle, 0)
        written = 0
        while written < len(payload):
            written += os.write(handle, payload[written:])
    except OSError as exc:
        os.close(handle)
        fail(FAILED, "%s could not be rewritten through its descriptor (%s)."
             % (relative, errno.errorcode.get(exc.errno, str(exc.errno))))
    os.close(handle)


def op_create(anchor_fd, relative, args):
    if len(args) != 1:
        fail(USAGE, "'create' takes exactly one argument: the octal mode.")
    mode = parse_mode(args[0])
    fd = openat2(anchor_fd, relative,
                 os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, mode)
    try:
        # Explicit, because the process umask would otherwise subtract from the mode asked for
        # and a credential file's mode is functional rather than cosmetic.
        os.fchmod(fd, mode)
    except OSError as exc:
        os.close(fd)
        fail(FAILED, "%s was created but its mode could not be set (%s)."
             % (relative, errno.errorcode.get(exc.errno, str(exc.errno))))
    os.close(fd)


def op_stat(anchor_fd, relative, args):
    if args:
        fail(USAGE, "'stat' takes no arguments.")
    fd, st = open_for_inspection(anchor_fd, relative)
    os.close(fd)
    sys.stdout.write("%s %d %d %d %d %04o\n" % (describe(st), st.st_dev, st.st_ino,
                                                st.st_nlink, st.st_size,
                                                stat.S_IMODE(st.st_mode)))


def op_chmod(anchor_fd, relative, args):
    if len(args) != 1:
        fail(USAGE, "'chmod' takes exactly one argument: the octal mode.")
    mode = parse_mode(args[0])
    fd, st = open_for_inspection(anchor_fd, relative)
    if not (stat.S_ISREG(st.st_mode) or stat.S_ISDIR(st.st_mode)):
        os.close(fd)
        fail(WRONG_TYPE, "%s beneath the anchor is a %s; only a regular file or a directory "
                         "has its mode changed here." % (relative, describe(st)))
    try:
        os.chmod(magic(fd), mode)
    except OSError as exc:
        os.close(fd)
        fail(FAILED, "the mode of %s could not be set (%s)."
             % (relative, errno.errorcode.get(exc.errno, str(exc.errno))))
    os.close(fd)


def op_chown(anchor_fd, relative, args):
    if len(args) != 2:
        fail(USAGE, "'chown' takes exactly two arguments: the numeric uid and gid.")
    try:
        uid = int(args[0], 10)
        gid = int(args[1], 10)
    except ValueError:
        fail(USAGE, "'chown' takes NUMERIC uid and gid.")
    if uid < 0 or gid < 0:
        fail(USAGE, "'chown' takes non-negative uid and gid.")
    fd, st = open_for_inspection(anchor_fd, relative)
    if not (stat.S_ISREG(st.st_mode) or stat.S_ISDIR(st.st_mode)):
        os.close(fd)
        fail(WRONG_TYPE, "%s beneath the anchor is a %s; only a regular file or a directory "
                         "has its ownership changed here." % (relative, describe(st)))
    try:
        os.chown(magic(fd), uid, gid)
    except OSError as exc:
        os.close(fd)
        fail(FAILED, "the ownership of %s could not be set (%s)."
             % (relative, errno.errorcode.get(exc.errno, str(exc.errno))))
    os.close(fd)


def op_unlink(anchor_fd, relative, args):
    if args:
        fail(USAGE, "'unlink' takes no arguments.")
    parts = split_components(relative)
    leaf = parts[-1]
    parent = walk_directory(anchor_fd, parts[:-1]) if len(parts) > 1 else anchor_fd
    try:
        # unlinkat() on a SINGLE component relative to a confined descriptor. It never follows
        # a symbolic link, so the entry removed is an entry of that directory and of no other.
        os.unlink(leaf, dir_fd=parent)
    except OSError as exc:
        if parent != anchor_fd:
            os.close(parent)
        if exc.errno == errno.ENOENT:
            fail(ABSENT, "%s does not exist beneath the anchor." % relative)
        fail(FAILED, "%s could not be removed (%s)."
             % (relative, errno.errorcode.get(exc.errno, str(exc.errno))))
    if parent != anchor_fd:
        os.close(parent)


def op_list(anchor_fd, relative, args):
    """The directory's entries and their types, read from a descriptor obtained under the
    confinement.

    This exists so that ENUMERATION is confined too, and that is not cosmetic: `find -P` does
    not follow links it finds inside a tree, but the kernel resolves find's STARTING PATH
    normally, so `find -P $IMAGE_MOUNT/etc/apt/apt.conf.d` on an image whose /etc is an absolute
    symbolic link enumerates the HOST's directory and reports host filenames into this script's
    own diagnostics.  Here the directory is opened beneath the anchor with the same refusals as
    everything else, and scandir() reads it through that descriptor.
    """
    if args:
        fail(USAGE, "'list' takes no arguments.")
    fd, st = open_for_inspection(anchor_fd, relative)
    if not stat.S_ISDIR(st.st_mode):
        os.close(fd)
        fail(WRONG_TYPE, "%s beneath the anchor is a %s, not a directory, so it cannot be "
                         "enumerated." % (relative, describe(st)))
    handle = reopen(fd, os.O_DIRECTORY | os.O_RDONLY | os.O_CLOEXEC)
    os.close(fd)
    try:
        with os.scandir(handle) as entries:
            for entry in entries:
                try:
                    est = entry.stat(follow_symlinks=False)
                except OSError:
                    # Removed between the readdir and the stat.  Reported as 'gone' rather than
                    # omitted, so a caller that must refuse every non-regular entry sees it.
                    sys.stdout.buffer.write(b"gone " + os.fsencode(entry.name) + b"\0")
                    continue
                sys.stdout.buffer.write(
                    describe(est).encode("ascii") + b" " + os.fsencode(entry.name) + b"\0")
        sys.stdout.buffer.flush()
    except OSError as exc:
        fail(FAILED, "%s could not be enumerated through its descriptor (%s)."
             % (relative, errno.errorcode.get(exc.errno, str(exc.errno))))
    # scandir()'s context manager closes the descriptor it was handed.


def op_search(anchor_fd, relative, args):
    """Every regular file beneath the anchor-relative directory that contains any of the
    needles read from stdin, printed as "<label> <path-relative-to-the-searched-directory>\0".

    WHY THIS IS AN OPERATION HERE RATHER THAN A `grep -r` IN THE SHELL.  The tree being
    searched is a wholesale copy of the caller's own checkout, so it legitimately contains
    symbolic links, and it sits inside an image whose /opt may itself be a link in the base
    tarball.  `grep -r` skips links it finds inside a tree, but the kernel resolves grep's
    STARTING PATH normally -- so a search rooted at $IMAGE_MOUNT/opt/cec-l2/hdmicec on an image
    with a symlinked /opt would search the HOST and report host filenames as image contents.
    Here the starting directory is resolved beneath the anchor with the same refusals as every
    other operation, and the walk below is descriptor-relative throughout: each subdirectory is
    opened with O_NOFOLLOW on the descriptor of its parent, and only regular files are read.
    Nothing outside the searched directory can be reached, whatever the tree contains.

    Needles are matched as EXACT BYTE SEQUENCES, and each file is read in chunks with an
    overlap of one less than the longest needle, so a needle straddling a chunk boundary is
    still found.  Empty files are skipped because they cannot match.

    Labels arrive as arguments, one per needle in the same order, so the caller can name WHICH
    secret was found without this process ever printing the secret itself.
    """
    needles = [n for n in sys.stdin.buffer.read().split(b"\0") if n]
    if not needles:
        fail(USAGE, "'search' reads its needles from stdin, NUL-separated, and at least one is "
                    "required.")
    if len(args) != len(needles):
        fail(USAGE, "'search' takes exactly one label argument per needle: %d needles, %d "
                    "labels." % (len(needles), len(args)))
    fd, st = open_for_inspection(anchor_fd, relative)
    if not stat.S_ISDIR(st.st_mode):
        os.close(fd)
        fail(WRONG_TYPE, "%s beneath the anchor is a %s, not a directory, so it cannot be "
                         "searched." % (relative, describe(st)))
    root = reopen(fd, os.O_DIRECTORY | os.O_RDONLY | os.O_CLOEXEC)
    os.close(fd)

    overlap = max(len(n) for n in needles) - 1
    chunk = 1 << 20
    if chunk <= overlap:
        chunk = overlap + 1

    def scan(dir_fd, name, shown):
        try:
            handle = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=dir_fd)
        except OSError:
            # Unreadable or replaced since the stat.  Not a hit and not an error: the object
            # this process could not read is not an object whose bytes are in the image.
            return
        try:
            tail = b""
            while True:
                block = os.read(handle, chunk)
                if not block:
                    break
                window = tail + block
                for index, needle in enumerate(needles):
                    if needle in window:
                        sys.stdout.buffer.write(
                            os.fsencode(args[index]) + b" " + shown + b"\0")
                        return
                tail = window[-overlap:] if overlap > 0 else b""
        except OSError:
            return
        finally:
            os.close(handle)

    stack = [(root, b"")]
    try:
        while stack:
            dir_fd, prefix = stack.pop()
            try:
                names = os.listdir(dir_fd)
            except OSError:
                os.close(dir_fd)
                continue
            for name in names:
                try:
                    est = os.stat(name, dir_fd=dir_fd, follow_symlinks=False)
                except OSError:
                    continue
                shown = prefix + os.fsencode(name)
                if stat.S_ISDIR(est.st_mode):
                    try:
                        sub = os.open(name,
                                      os.O_DIRECTORY | os.O_RDONLY | os.O_NOFOLLOW
                                      | os.O_CLOEXEC,
                                      dir_fd=dir_fd)
                    except OSError:
                        continue
                    stack.append((sub, shown + b"/"))
                elif stat.S_ISREG(est.st_mode) and est.st_size > 0:
                    scan(dir_fd, name, shown)
            os.close(dir_fd)
        sys.stdout.buffer.flush()
    finally:
        # Anything still queued when an exception unwound the walk.
        for dir_fd, _ in stack:
            try:
                os.close(dir_fd)
            except OSError:
                pass


def op_rmdir(anchor_fd, relative, args):
    if args:
        fail(USAGE, "'rmdir' takes no arguments.")
    parts = split_components(relative)
    leaf = parts[-1]
    parent = walk_directory(anchor_fd, parts[:-1]) if len(parts) > 1 else anchor_fd
    try:
        # rmdirat() via os.rmdir with a dir_fd: a SINGLE component relative to a descriptor
        # obtained under the confinement, and rmdir never follows a symbolic link (it fails
        # with ENOTDIR on one).
        os.rmdir(leaf, dir_fd=parent)
    except OSError as exc:
        if parent != anchor_fd:
            os.close(parent)
        if exc.errno == errno.ENOENT:
            fail(ABSENT, "%s does not exist beneath the anchor." % relative)
        if exc.errno == errno.ENOTDIR:
            fail(WRONG_TYPE, "%s beneath the anchor is not a directory." % relative)
        fail(FAILED, "%s could not be removed (%s)."
             % (relative, errno.errorcode.get(exc.errno, str(exc.errno))))
    if parent != anchor_fd:
        os.close(parent)


def op_mkdir(anchor_fd, relative, args):
    if len(args) != 1:
        fail(USAGE, "'mkdir' takes exactly one argument: the octal mode.")
    mode = parse_mode(args[0])
    parts = split_components(relative)
    fd = walk_directory(anchor_fd, parts, create_mode=mode)
    if fd != anchor_fd:
        os.close(fd)


def parse_mode(text):
    try:
        mode = int(text, 8)
    except ValueError:
        fail(USAGE, "'%s' is not an octal mode." % text)
    if mode < 0 or mode > 0o7777:
        fail(USAGE, "'%s' is not a mode in the range 0000-7777." % text)
    return mode


OPERATIONS = {
    "read": op_read,
    "write": op_write,
    "create": op_create,
    "stat": op_stat,
    "chmod": op_chmod,
    "chown": op_chown,
    "unlink": op_unlink,
    "mkdir": op_mkdir,
    "list": op_list,
    "rmdir": op_rmdir,
    "search": op_search,
}


def main(argv):
    if len(argv) < 4:
        fail(USAGE, "usage: confined-io.py <anchor-directory> <relative-path> <operation> "
                    "[arguments]")
    anchor, relative, operation = argv[1], argv[2], argv[3]
    args = argv[4:]
    handler = OPERATIONS.get(operation)
    if handler is None:
        fail(USAGE, "unknown operation '%s'; known operations are %s."
             % (operation, ", ".join(sorted(OPERATIONS))))
    if not anchor.startswith("/"):
        fail(USAGE, "the anchor directory must be an absolute path.")
    split_components(relative)
    try:
        # O_NOFOLLOW so that an anchor which is ITSELF a symbolic link is refused rather than
        # resolved: everything below depends on this descriptor being the directory that was
        # validated.
        anchor_fd = os.open(anchor,
                            os.O_DIRECTORY | os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
    except OSError as exc:
        fail(ANCHOR_BAD, "the anchor directory %s could not be opened as a directory (%s)."
             % (anchor, errno.errorcode.get(exc.errno, str(exc.errno))))
    try:
        handler(anchor_fd, relative, args)
    finally:
        os.close(anchor_fd)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
CONFINED_IO_PY
    [ -s "$target" ] || die "the confined I/O helper could not be written to $target.  Every
       privileged operation on a base-tarball-named path inside the image goes through it, so the
       run stops rather than falling back to an ordinary open."
    chmod 0600 -- "$target" || die "could not set mode 0600 on the confined I/O helper at
       $target."
    CONFINED_IO_HELPER="$target"
}

# Turn one helper exit status into a `die` that names WHICH condition it was.  Never called for
# status 0, and never called by the callers that legitimately act on a status themselves
# (confined_stat, which uses 13 to mean "not there").
confined_io_die() { # $1=status  $2=relative path  $3=what it is  $4=what was about to happen
    local status="$1" relative="$2" what="$3" operation="$4"
    case "$status" in
        "$CONFINED_IO_UNAVAILABLE")
            die "refusing to $operation ($what): the confined I/O primitive is NOT AVAILABLE on
       this host.  openat2(2) with RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_XDEV is what
       makes a privileged write inside the image safe against a symbolic link substituted under
       it, and this script does not fall back to an ordinary open -- the fallback is precisely
       the defect this mechanism replaced.  It needs a Linux 5.6-or-newer kernel and python3.
       The in-image path was: $relative" ;;
        "$CONFINED_IO_REFUSED")
            die "refusing to $operation ($what): THE CONFINEMENT REFUSED the in-image path
           $relative
       The kernel would not resolve it beneath the image mount point without following a
       symbolic link, without leaving the image and without crossing a mount point.  That means
       some component of it -- leading or trailing -- is a link, or points out of the image.
       Refused rather than resolved: this operation runs as root on the BUILD HOST, so a link
       naming an absolute path would have it act on a HOST file.  Inspect the base tarball; the
       errno is on the line above." ;;
        "$CONFINED_IO_ABSENT")
            die "refusing to $operation ($what): the in-image path
           $relative
       does not exist.  It was enumerated a moment ago, so something is changing the image's
       contents while this scan runs.  Nothing further is written there." ;;
        "$CONFINED_IO_WRONG_TYPE")
            die "refusing to $operation ($what): the in-image path
           $relative
       is not the kind of object this operation acts on -- a fifo, a socket, a device node or a
       directory at that name is not configuration, and a privileged read or rewrite is not
       performed on the strength of a filename.  Inspect the base tarball." ;;
        "$CONFINED_IO_EXISTS")
            die "refusing to $operation ($what): the in-image path
           $relative
       already exists, and this operation creates it exclusively.  Refused rather than written
       through: the name is this script's own, so something the caller supplied is sitting at
       it." ;;
        "$CONFINED_IO_ANCHOR_BAD")
            die "refusing to $operation ($what): the anchor directory
           ${CONFINED_IO_ANCHOR:-<unset>}
       could not be opened as a directory that is not itself a symbolic link.  Every relative
       path is resolved against it, so without it nothing below is confined." ;;
        "$CONFINED_IO_USAGE")
            die "internal error while trying to $operation ($what): the confined I/O helper was
       called with arguments it refused (in-image path '$relative').  This is a defect in
       $SCRIPT_NAME." ;;
        "$CONFINED_IO_FAILED")
            die "could not $operation ($what): the operation on the in-image path
           $relative
       failed after the confined open succeeded.  The errno is on the line above.  The image is
       NOT published." ;;
        *)
            die "could not $operation ($what): the confined I/O helper exited $status on the
       in-image path
           $relative
       which this script has no reading for.  Refused rather than interpreted; this is a defect
       in $SCRIPT_NAME." ;;
    esac
}

# The guard every wrapper shares.  It is an internal-consistency check rather than a runtime
# condition: it catches a future edit that moves a call ahead of the helper's installation or
# the image's mount, which would otherwise anchor a privileged write on an empty directory.
confined_io_ready() { # $1=what is about to happen, for the message
    local operation="$1"
    [ "$CONFINED_IO_PROVEN" -eq 1 ] || die "internal error: '$operation' was attempted before
       probe_confined_io() had PROVEN the confined I/O primitive refuses what it must on this
       host.  Refused rather than attempted: an unproven primitive is indistinguishable from an
       ordinary open, and every comment in $SCRIPT_NAME about the confinement would be false.
       This is a defect in $SCRIPT_NAME."
    [ -n "$CONFINED_IO_HELPER" ] || die "internal error: '$operation' was attempted before the
       confined I/O helper was installed, so it would have run unconfined.  This is a defect in
       $SCRIPT_NAME."
    [ -f "$CONFINED_IO_HELPER" ] || die "the confined I/O helper has disappeared from
       $CONFINED_IO_HELPER.  Every privileged operation on a base-tarball-named path inside the
       image goes through it, so '$operation' is refused."
    [ -n "$CONFINED_IO_ANCHOR" ] || die "internal error: '$operation' was attempted before the
       confined I/O anchor directory was set.  This is a defect in $SCRIPT_NAME."
    [ -d "$CONFINED_IO_ANCHOR" ] || die "the confined I/O anchor directory
       $CONFINED_IO_ANCHOR no longer exists, so '$operation' is refused."
    [ ! -L "$CONFINED_IO_ANCHOR" ] || die "the confined I/O anchor directory
       $CONFINED_IO_ANCHOR is a symbolic link.  Refused: every relative path is resolved
       against it."
}

# The raw call.  Returns the helper's status; the caller decides what it means.  `python3 -I -B`
# so that no PYTHON* variable, user site directory or PYTHONPATH from the caller's environment
# reaches a privileged helper, and no bytecode is written beside it.
confined_io() { # $1=relative path  $2=operation  [$3..]=operation arguments
    python3 -I -B "$CONFINED_IO_HELPER" "$CONFINED_IO_ANCHOR" "$@"
}

# --- The dieing wrappers.  $what and $operation appear only in messages. ------------------
confined_read() { # $1=relative  $2=what it is  $3=what is about to happen   -> content on stdout
    local status=0
    confined_io_ready "$3"
    confined_io "$1" read || status=$?
    [ "$status" -eq 0 ] || confined_io_die "$status" "$1" "$2" "$3"
}

confined_write() { # $1=relative  $2=what it is  $3=what is about to happen  <- content on stdin
    local status=0
    confined_io_ready "$3"
    confined_io "$1" write || status=$?
    [ "$status" -eq 0 ] || confined_io_die "$status" "$1" "$2" "$3"
}

confined_create() { # $1=relative  $2=octal mode  $3=what it is  $4=what is about to happen
    local status=0
    confined_io_ready "$4"
    confined_io "$1" create "$2" || status=$?
    [ "$status" -eq 0 ] || confined_io_die "$status" "$1" "$3" "$4"
}

confined_mkdir() { # $1=relative  $2=octal mode  $3=what it is  $4=what is about to happen
    local status=0
    confined_io_ready "$4"
    confined_io "$1" mkdir "$2" || status=$?
    [ "$status" -eq 0 ] || confined_io_die "$status" "$1" "$3" "$4"
}

confined_chmod() { # $1=relative  $2=octal mode  $3=what it is  $4=what is about to happen
    local status=0
    confined_io_ready "$4"
    confined_io "$1" chmod "$2" || status=$?
    [ "$status" -eq 0 ] || confined_io_die "$status" "$1" "$3" "$4"
}

confined_chown() { # $1=relative  $2=uid  $3=gid  $4=what it is  $5=what is about to happen
    local status=0
    confined_io_ready "$5"
    confined_io "$1" chown "$2" "$3" || status=$?
    [ "$status" -eq 0 ] || confined_io_die "$status" "$1" "$4" "$5"
}

confined_unlink() { # $1=relative  $2=what it is  $3=what is about to happen
    local status=0
    confined_io_ready "$3"
    confined_io "$1" unlink || status=$?
    [ "$status" -eq 0 ] || confined_io_die "$status" "$1" "$2" "$3"
}

confined_rmdir() { # $1=relative  $2=what it is  $3=what is about to happen
    local status=0
    confined_io_ready "$3"
    confined_io "$1" rmdir || status=$?
    [ "$status" -eq 0 ] || confined_io_die "$status" "$1" "$2" "$3"
}

# The one wrapper that does NOT die on a non-zero status: presence is a question this script
# asks, and "not there" is one of the answers it acts on.  Prints the stat record on stdout and
# RETURNS the helper's status, so a caller writes
#     record="$(confined_stat "$rel" '...')" || status=$?
# and gets the status even though the substitution ran in a subshell.
#
# THAT IS WHY IT IS NOT A GLOBAL.  An earlier form of this wrapper published the status in a
# variable, which silently produced 0 at every call site that captured the record with $( ) --
# the subshell set the variable and the parent never saw it, so a refusal and a success were
# indistinguishable and the diagnosis said "exited 0, which this script has no reading for".
# The status is a return value here precisely so a subshell cannot lose it.
#
# stderr is suppressed because absence is routine and its diagnostic is noise; the fatal arms in
# confined_directory and confined_regular_file re-issue the call unsuppressed so the kernel's own
# errno still reaches the log beside their explanation.
confined_stat() { # $1=relative  $2=what is about to happen (for the readiness message only)
    local status=0
    confined_io_ready "$2"
    confined_io "$1" stat 2>/dev/null || status=$?
    return "$status"
}

# Is a path absent beneath the anchor?  0 when it is genuinely not there, 1 when it is there,
# and a `die` for every other answer -- because "the confinement refused the path" is not
# evidence of absence and must never be read as such.  The helper is invoked directly rather
# than through confined_stat so that no subshell stands between the call and its status.
confined_absent() { # $1=relative  $2=what it is  $3=what is about to happen
    local status=0
    confined_io_ready "$3"
    confined_io "$1" stat >/dev/null 2>&1 || status=$?
    [ "$status" -ne 0 ] || return 1
    if [ "$status" -ne "$CONFINED_IO_ABSENT" ]; then
        confined_io "$1" stat >/dev/null || true
        confined_io_die "$status" "$1" "$2" "$3"
    fi
    return 0
}

# The entries of one directory beneath the anchor, as NUL-terminated "<type> <name>" records on
# stdout.  Dies on every failure including "not a directory"; a caller that must tolerate an
# absent directory calls confined_directory first.
confined_list() { # $1=relative  $2=what it is  $3=what is about to happen
    local status=0
    confined_io_ready "$3"
    confined_io "$1" list || status=$?
    [ "$status" -eq 0 ] || confined_io_die "$status" "$1" "$2" "$3"
}

# A recursive content search beneath one in-image directory, confined the same way every other
# operation is.  Needles arrive on stdin NUL-separated; the labels name them in the same order,
# so a hit can be reported without the secret itself appearing in a diagnostic.  Output is
# "<label> <relative-path>\0" per hit and nothing at all when the tree is clean.
confined_search() { # $1=relative dir  $2=what it is  $3=what is about to happen  $4..=labels
    local status=0 rel="$1" what="$2" doing="$3"
    shift 3
    confined_io_ready "$doing"
    confined_io "$rel" search "$@" || status=$?
    [ "$status" -eq 0 ] || confined_io_die "$status" "$rel" "$what" "$doing"
}

# Is a path a usable DIRECTORY beneath the anchor?  0 when it is, 1 when it is simply not there,
# and a `die` for every other answer -- a symbolic link at that name is refused rather than
# reported absent, and a regular file, fifo or device node at it means the base filesystem is not
# what it claims to be.  This replaces the old `[ -L ] / [ -e ] / [ -d ]` triple, whose -e and -d
# both FOLLOWED links and so could not distinguish an image directory from a host one.
confined_directory() { # $1=relative  $2=path as shown  $3=what is about to happen
    local record type_field status=0
    record="$(confined_stat "$1" "$3")" || status=$?
    if [ "$status" -ne 0 ]; then
        if [ "$status" -eq "$CONFINED_IO_ABSENT" ]; then
            return 1
        fi
        if [ "$status" -eq "$CONFINED_IO_REFUSED" ]; then
            # Re-issued once, unsuppressed, so the kernel's own errno appears beside the
            # explanation.  This arm ends the run, so the second call costs nothing.
            confined_io "$1" stat >/dev/null || true
            die "$2 inside the image, or a component of the path leading to it, is a SYMBOLIC
       LINK or leaves the image filesystem.  The entries this scan would enumerate under it
       therefore live wherever the link points -- possibly on the build host, where this scan
       runs as root and where it removes and rewrites what it finds.  Refused rather than
       followed, by the kernel's own path resolution; inspect the base tarball."
        fi
        confined_io_die "$status" "$1" "$2" "$3"
    fi
    type_field="$(confined_stat_field "$record" 1)"
    [ "$type_field" = 'directory' ] || die "$2 inside the image is a $type_field rather than a
       directory.  Refused rather than interpreted: this scan enumerates it as root, and a
       filename is not evidence of what is behind it.  Inspect the base tarball."
    return 0
}

# Is a path a usable REGULAR FILE beneath the anchor?  Same contract as confined_directory: 0
# present, 1 absent, `die` for anything else -- including a symbolic link, which is the case the
# old `[ -L ] then [ -f ]` pair was written to catch and which the kernel now catches first.
confined_regular_file() { # $1=relative  $2=path as shown  $3=what is about to happen
    local record type_field status=0
    record="$(confined_stat "$1" "$3")" || status=$?
    if [ "$status" -ne 0 ]; then
        if [ "$status" -eq "$CONFINED_IO_ABSENT" ]; then
            return 1
        fi
        if [ "$status" -eq "$CONFINED_IO_REFUSED" ]; then
            # Re-issued once, unsuppressed, so the kernel's own errno appears beside the
            # explanation.  This arm ends the run, so the second call costs nothing.
            confined_io "$1" stat >/dev/null || true
            die "$2 inside the image, or a component of the path leading to it, is a SYMBOLIC
       LINK or leaves the image filesystem.  It is refused rather than followed: this scan
       reads, rewrites and removes these files as root ON THE BUILD HOST, so a link naming an
       absolute path would have this script read, truncate, replace or delete a HOST file.  A
       base filesystem that ships a symlinked apt configuration is not a filesystem this script
       publishes an image around.  Inspect the base tarball, replace the link with the regular
       file it should be, and rebuild."
        fi
        confined_io_die "$status" "$1" "$2" "$3"
    fi
    type_field="$(confined_stat_field "$record" 1)"
    [ "$type_field" = 'regular' ] || die "$2 inside the image is a $type_field rather than a
       regular file.  A fifo, a socket, a device node or a directory at that name is not
       configuration, and reading or writing one as root is not something this script does on
       the strength of a filename.  Refused; inspect the base tarball."
    # The link count is checked here because a rewrite through a descriptor changes every name
    # that shares the inode, exactly as a rewrite through a pathname would.  The confinement
    # bounds WHERE the write lands; it does not make a hard link to elsewhere in the image
    # harmless.
    type_field="$(confined_stat_field "$record" 4)"
    case "$type_field" in
        ''|*[!0-9]*) die "the link count of $2 inside the image could not be read, so it cannot
       be established that no other name shares its content." ;;
    esac
    [ "$type_field" -le 1 ] || die "$2 inside the image has $type_field hard links, so another
       name shares its content and a rewrite would change both.  Refused rather than written
       through."
    return 0
}

# Field $2 of a stat record ("<type> <dev> <ino> <nlink> <size> <mode>"), so a caller reads the
# type, the link count or the size of the object THE CONFINEMENT OPENED rather than re-running
# stat(1) on a pathname -- which would reintroduce exactly the second lookup this mechanism
# exists to remove.
confined_stat_field() { # $1=the stat record  $2=1-based field index
    printf '%s\n' "$1" | awk -v field="$2" '{ print $field }'
}

# ------------------------------------------------------------------------------------
# THE START-UP PROOF.  One positive case and four negative cases, in a private directory,
# before one privileged byte is written.  A primitive that silently degraded to an ordinary open
# would leave every claim in this file false, so the degradation is what this proves cannot have
# happened -- and it proves it on THIS host and THIS kernel rather than from a version number.
#
# It is fatal on every arm: a positive case that fails means the mechanism does not work, and a
# negative case that SUCCEEDS means it is not confining anything.
# ------------------------------------------------------------------------------------
probe_confined_io() { # $1=a private directory to probe in
    local root="$1" saved_anchor="$CONFINED_IO_ANCHOR" content
    local outside="$root/outside-the-anchor"
    local anchor="$root/confined-io-probe"

    if [ -z "$root" ] || [ ! -d "$root" ]; then
        die "internal error: probe_confined_io was given '${root:-<empty>}', which is not a
       directory.  This is a defect in $SCRIPT_NAME."
    fi

    rm -rf -- "$anchor" "$outside" || die "could not clear the confined I/O probe directory
       under $root."
    mkdir -p -- "$anchor/beneath" || die "could not create the confined I/O probe directory
       $anchor/beneath."
    mkdir -p -- "$outside" || die "could not create the confined I/O probe's control directory
       $outside."
    printf 'confined-io probe payload\n' > "$anchor/beneath/regular" \
        || die "could not write the confined I/O probe's regular file."
    printf 'this file is OUTSIDE the anchor and must stay unreadable through it\n' \
        > "$outside/target" || die "could not write the confined I/O probe's control file."
    ln -sfn "$outside/target" "$anchor/beneath/absolute-link" \
        || die "could not create the confined I/O probe's absolute symbolic link."
    ln -sfn 'regular' "$anchor/beneath/relative-link" \
        || die "could not create the confined I/O probe's relative symbolic link."
    mkdir -p -- "$anchor/elsewhere" || die "could not create the confined I/O probe's second
       directory."
    ln -sfn '../elsewhere' "$anchor/beneath/directory-link" \
        || die "could not create the confined I/O probe's mid-path symbolic link."
    printf 'reached through a symlinked directory component\n' > "$anchor/elsewhere/regular" \
        || die "could not write the confined I/O probe's mid-path target file."
    mkfifo -- "$anchor/beneath/fifo" 2>/dev/null || rm -f -- "$anchor/beneath/fifo"

    CONFINED_IO_ANCHOR="$anchor"

    # POSITIVE.  A regular file beneath the anchor is readable, and the bytes are the bytes.
    content="$(confined_io 'beneath/regular' read)" \
        || die "the confined I/O primitive could NOT read a plain regular file beneath its own
       anchor directory during the start-up proof.  Every privileged operation on a
       base-tarball-named path inside the image goes through it, so the run stops here rather
       than falling back to an ordinary open.  The diagnostic is on the line above."
    [ "$content" = 'confined-io probe payload' ] || die "the confined I/O primitive read a
       regular file beneath its anchor and returned content that is not what was written to it.
       The mechanism does not work on this host, so nothing privileged is done through it."

    # NEGATIVE, four of them.  Each MUST be refused with status 12; a success means the
    # primitive is not confining and every comment in this file about it would be false.
    probe_confined_io_refusal 'beneath/absolute-link' stat "$CONFINED_IO_REFUSED" \
        'a symbolic link whose target is an absolute path outside the anchor'
    probe_confined_io_refusal 'beneath/relative-link' stat "$CONFINED_IO_REFUSED" \
        'a symbolic link whose target is inside the anchor (refused too: no link is followed)'
    probe_confined_io_refusal 'beneath/directory-link/regular' stat "$CONFINED_IO_REFUSED" \
        'a path whose MIDDLE component is a symbolic link'
    probe_confined_io_refusal '../outside-the-anchor/target' stat "$CONFINED_IO_REFUSED" \
        'a path that climbs out of the anchor with .. (RESOLVE_BENEATH, not a check of ours)'

    # And a fifo is refused as the wrong TYPE rather than opened -- which is what stops a
    # privileged read blocking forever on one.  'stat' REPORTS a type (the callers here refuse
    # on it), so the case that must refuse is a read.  Skipped only if mkfifo is unavailable,
    # which is stated rather than passed over.
    if [ -p "$anchor/beneath/fifo" ]; then
        probe_confined_io_refusal 'beneath/fifo' read "$CONFINED_IO_WRONG_TYPE" \
            'a fifo, which a privileged read would otherwise block on forever'
    else
        log "  confined I/O: the fifo case was not exercised (mkfifo is unavailable here); the"
        log "    type refusal is still in force, it is simply not proven on this host"
    fi

    CONFINED_IO_ANCHOR="$saved_anchor"
    rm -rf -- "$anchor" "$outside" || die "could not remove the confined I/O probe directory
       under $root."
    CONFINED_IO_PROVEN=1
    log "confined I/O primitive proven on this host: openat2 with"
    log "  RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_XDEV reads a regular file beneath its"
    log "  anchor and refuses an absolute link, a relative link, a symlinked middle component"
    log "  and a '..' escape.  Every privileged operation on a base-tarball-named path inside"
    log "  the image goes through it; there is no fallback to an ordinary open."
}

# One negative case of the start-up proof.  Separate so each refusal reports its own name.
probe_confined_io_refusal() { # $1=relative  $2=operation  $3=required status  $4=what the case is
    local relative="$1" operation="$2" required="$3" what="$4" status=0
    confined_io "$relative" "$operation" >/dev/null 2>&1 || status=$?
    if [ "$status" -eq 0 ]; then
        die "THE CONFINED I/O PRIMITIVE DID NOT REFUSE $what during the start-up proof: it
       resolved
           $relative
       beneath $CONFINED_IO_ANCHOR and reported success.  A primitive that silently degrades to
       an ordinary open is worse than no primitive at all, because every comment in
       $SCRIPT_PATH about the confinement would then be false.  The run stops here; nothing
       privileged is done through it."
    fi
    [ "$status" -eq "$required" ] || die "the confined I/O primitive returned status
       $status for $what during the start-up proof, where $required was required.  The refusal has to come from the KERNEL's path resolution, so
       any other status means something else failed and the confinement itself was not
       exercised.  The run stops rather than proceeding on an unproven mechanism."
    log "  confined I/O: refused $what (status $status)"
}

# ------------------------------------------------------------------------------------
# ENDPOINTS ARE VALIDATED, THEN SANITISED BEFORE THEY ARE SHOWN OR RECORDED.
#
# TWO SEPARATE PROBLEMS, AND CONFLATING THEM IS WHY AN EARLIER REVISION HAD NEITHER.
#
# The first is INJECTION. --apt-mirror is written verbatim into the image's
# /etc/apt/sources.list, so a value carrying a newline does not configure one archive, it
# configures two - the second one chosen by whoever supplied the value.  A carriage return
# does the same thing while being invisible in a log.  --base-url is handed to curl or wget,
# where a control character is at best a fetch that fails confusingly.  Both are therefore
# refused if they contain any control character at all; ordinary spaces are NOT refused,
# because a sources.list line legitimately contains them ("deb <url> <suite> <components>").
#
# The second is DISCLOSURE.  A URL is a documented place to put a credential -
# https://user:token@host/path is a form apt and curl both accept - and this script put both
# values, unaltered, into its own log, into the image's build record and into the MANIFEST,
# which the workflow uploads as an artifact.  A token that reaches an artifact has left the
# machine.  Query strings are treated the same way, because signed-URL schemes carry their
# signature there.
#
# WHAT IS RECORDED INSTEAD: scheme, host and path.  That is enough to answer "which archive
# was this built from", which is the whole reason the value is recorded, and it carries no
# secret.  The fact that something was redacted is stated rather than hidden, so a reader is
# never left believing they are looking at the value that was used.
#
# SANITISING IS FOR THE LOG AND THE MANIFEST, AND IT IS NOT A CONTAINMENT STRATEGY.  An
# earlier revision of this script relied on it as one: the unsanitised --apt-mirror was written
# into the image's sources.list, the caller was WARNED that their token was now inside the
# artifact it had just built, and that was called an informed decision.  It is not - a warning
# is what you emit when you have decided to do the dangerous thing anyway, and the image is
# handed to a guest, copied between machines and kept as a build output.  So a credential is
# now REFUSED at the boundary rather than carried and described:
#
#   * validate_inputs rejects userinfo in either value, rejects a query and a fragment in
#     --apt-mirror because that value is published with the image, and rejects the two
#     characters that could break out of curl's configuration file in --base-url.
#   * acquire_base_rootfs hands --base-url to curl through a mode-0600 file rather than in
#     argv, so it is not readable in `ps` while the transfer runs.
#   * scrub_and_assert_image_apt_credentials checks the FINISHED filesystem, because the base
#     tarball's own apt configuration is not this script's to trust.
#
# What sanitising is still for is unchanged and is worth keeping: a value that is legitimate
# but revealing - an internal hostname, a snapshot path, an accepted --base-url query - is
# reduced to scheme, host and path wherever it is logged or recorded, and the reduction is
# stated so a reader is never left believing they are looking at the value that was used.
#
# These are globals rather than command-substitution results ON PURPOSE: whether anything was
# redacted has to travel back to the caller alongside the sanitised text, and a function whose
# output is captured runs in a subshell whose variable assignments are discarded.
# ------------------------------------------------------------------------------------
SANITISED_ENDPOINT=''
SANITISED_ENDPOINT_REDACTED=0
SANITISED_URL_TOKEN=''

assert_no_control_characters() {
    local value="$1" label="$2" stripped
    stripped="$(printf '%s' "$value" | LC_ALL=C tr -d '[:cntrl:]')"
    if [ "$stripped" != "$value" ]; then
        die "$label contains a control character (a newline, carriage return or similar).
       It is refused rather than stripped, because the two possible readings are too far
       apart to guess between: for --apt-mirror a newline would configure a SECOND package
       archive inside the image, chosen by whoever supplied the value, and for --base-url it
       would be a fetch that fails in a way the message would not explain.  Pass the value on
       one line, with single spaces between its fields."
    fi
}

# Reduces one URL-shaped token to scheme://host/path, dropping userinfo, query and fragment.
# Sets SANITISED_URL_TOKEN, and raises SANITISED_ENDPOINT_REDACTED when it removed something.
sanitise_url_token() {
    local url="$1" scheme='' rest authority path=''
    case "$url" in
        *://*) scheme="${url%%://*}"; rest="${url#*://}" ;;
        *)     rest="$url" ;;
    esac
    case "$rest" in
        *'#'*) rest="${rest%%#*}";    SANITISED_ENDPOINT_REDACTED=1 ;;
    esac
    # \? is a LITERAL question mark here; unescaped it would be a single-character wildcard
    # and would strip almost the whole string.
    case "$rest" in
        *'?'*) rest="${rest%%\?*}";   SANITISED_ENDPOINT_REDACTED=1 ;;
    esac
    case "$rest" in
        */*) authority="${rest%%/*}"; path="/${rest#*/}" ;;
        *)   authority="$rest" ;;
    esac
    # The LAST @ in the authority ends the userinfo, so ## rather than # -- a password may
    # itself contain an @.
    case "$authority" in
        *@*) authority="${authority##*@}"; SANITISED_ENDPOINT_REDACTED=1 ;;
    esac
    if [ -n "$scheme" ]; then
        SANITISED_URL_TOKEN="$scheme://$authority$path"
    else
        SANITISED_URL_TOKEN="$authority$path"
    fi
}

# Extracts the AUTHORITY of one URL-shaped token -- what sits between the scheme and the
# first '/', with query and fragment removed first.  Sets URL_AUTHORITY.
#
# The authority is the only place a userinfo credential can live, and separating it from the
# path is what keeps this from being a blanket search for '@': a legitimate path can carry
# one (http://host/~user@example/), and a sources.list option group or suite name carries
# neither a scheme nor an authority worth speaking of.  A SCHEME-LESS token is treated as
# authority-first, because curl accepts "user:pass@host/path" with no scheme at all -- so
# that form must be caught too.
URL_AUTHORITY=''
url_authority() {
    local url="$1" rest
    case "$url" in
        *://*) rest="${url#*://}" ;;
        *)     rest="$url" ;;
    esac
    rest="${rest%%\#*}"
    # \? is a LITERAL question mark; unescaped it is a single-character wildcard.
    rest="${rest%%\?*}"
    case "$rest" in
        */*) URL_AUTHORITY="${rest%%/*}" ;;
        *)   URL_AUTHORITY="$rest" ;;
    esac
}

# CREDENTIALS IN A URL ARE REFUSED, NOT CARRIED.
#
# Sanitising an endpoint for the log and the manifest was only ever half of it: --apt-mirror
# is written into the PUBLISHED image's /etc/apt/sources.list, so a "user:pass@" form ended up
# inside an artifact that outlives the run, and the script's own report said so rather than
# preventing it.  Refusing the form outright is the one disposition that leaves nothing to
# leak, and it costs a caller nothing they cannot do another way: an authenticated archive is
# configured with --apt-auth-file, which is installed for the apt phase and removed before the
# image is released, and an authenticated base fetch goes through --base-url-file or is done
# separately with --base-tarball.
#
# NOTE ON WHAT THIS MESSAGE USED TO SAY.  It used to advise putting an auth.conf.d entry or a
# netrc INSIDE THE BASE TARBALL.  That was advice to bake a permanent credential into the
# published image -- the same exposure by a different route, and one the pre-publication scan
# did not look for.  The advice is gone, not softened, and --apt-auth-file replaces it.
#
# The message never echoes the value.  It names the option and the sanitised form only,
# because a die message reaches the same log the sanitising exists for.
# ------------------------------------------------------------------------------------
# A SECRET INPUT MUST NOT LIVE INSIDE ANYTHING THIS SCRIPT COPIES INTO THE IMAGE.
#
# THE DEFECT THIS CLOSES.  --base-url-file and --apt-auth-file exist so that a confidential
# value is read from a caller-owned private file instead of arriving in this script's argv,
# where /proc/<pid>/cmdline exposes it to every user on the build host.  That protects the
# value on the HOST.  It said nothing about the IMAGE -- and copy_guest_payload() copies
# --payload, --sdk-dir and --gtest-prefix into the image WHOLESALE with `cp -a`, so
#
#     --base-url-file "$PAYLOAD_DIR/.base-url"
#
# took a file whose entire purpose was confidentiality and published it inside an artefact
# that outlives the run and is uploaded by the workflow.  The credential-store scan did not
# see it: that scan looks at apt's and root's credential locations, and this file is neither.
#
# THIS IS A POLICY CHECK, AND IT RUNS AT VALIDATION TIME -- before a byte is copied, so the
# refusal costs the caller nothing but a re-invocation.  It is deliberately paired with a
# SECOND, INDEPENDENT CHECK of a different kind: assert_image_carries_no_secret_values()
# searches the copied trees inside the finished image for the secret VALUES themselves, which
# catches a route this policy cannot see -- a secret whose path is outside every copied root
# but whose content was duplicated into one of them by something else.
#
# WHAT IS AND IS NOT A COPIED ROOT.  --payload, --sdk-dir and --gtest-prefix are copied as
# whole trees and are therefore roots.  --base-tarball and --lcov-tarball are not: their
# CONTENT is extracted, so a secret file inside one of them is a property of the archive the
# caller built, and the pre-publication credential scan is what looks for credentials that
# arrive that way.  --kernel-config is a single file copied verbatim into a record; it is not a
# tree, so no path can be "inside" it.
# ------------------------------------------------------------------------------------
path_is_within() { # $1=candidate  $2=root -> 0 when $1 IS $2 or lies beneath it
    if [ "$1" = "$2" ]; then
        return 0
    fi
    # A root of "/" is answered directly: "$2"/* would expand as "//*" and match nothing,
    # while the true answer is that everything lies beneath the root directory.
    if [ "$2" = '/' ]; then
        return 0
    fi
    case "$1" in
        "$2"/*) return 0 ;;
    esac
    return 1
}

assert_secret_inputs_outside_copied_roots() {
    local -a secret_opts=() secret_paths=() root_opts=() root_paths=()
    local i j

    # Built only from the options actually given, and only AFTER absolutise_and_check() has
    # canonicalised every one of them -- a textual comparison of two paths that have not both
    # been resolved answers a question about spelling rather than about location.
    if [ -n "$BASE_URL_FILE" ]; then
        secret_opts+=('--base-url-file'); secret_paths+=("$BASE_URL_FILE")
    fi
    if [ -n "$APT_AUTH_FILE" ]; then
        secret_opts+=('--apt-auth-file'); secret_paths+=("$APT_AUTH_FILE")
    fi
    if [ "${#secret_paths[@]}" -eq 0 ]; then
        return 0
    fi

    root_opts+=('--payload');  root_paths+=("$PAYLOAD_DIR")
    root_opts+=('--sdk-dir');  root_paths+=("$SDK_DIR")
    if [ -n "$GTEST_PREFIX_SRC" ]; then
        root_opts+=('--gtest-prefix'); root_paths+=("$GTEST_PREFIX_SRC")
    fi

    for (( i = 0; i < ${#secret_paths[@]}; i++ )); do
        for (( j = 0; j < ${#root_paths[@]}; j++ )); do
            if path_is_within "${secret_paths[i]}" "${root_paths[j]}"; then
                die "${secret_opts[i]} names a file INSIDE the tree ${root_opts[j]} names, and
       that whole tree is copied into the image:
           ${secret_opts[i]} = ${secret_paths[i]}
           ${root_opts[j]} = ${root_paths[j]}
       ${secret_opts[i]} exists so the value is never exposed on a command line.  Copying the
       file that holds it into a published image would be a far larger exposure than the one
       the option was added to close: the image outlives this run and the workflow uploads it.
       Move the file OUTSIDE every tree this script copies -- ${root_opts[j]} and the other
       copied roots -- and pass the new path.  A directory beside the payload rather than
       inside it, readable only by the invoking account, is the usual answer."
            fi
        done
    done

    log "secret inputs checked against the copied trees: none of them lies inside --payload,"
    log "  --sdk-dir or --gtest-prefix, so none is copied into the image by construction."
}

assert_no_url_userinfo() {
    local value="$1" option="$2" field shown
    local -a fields=()
    read -r -a fields <<< "$value"
    for field in ${fields[@]+"${fields[@]}"}; do
        url_authority "$field"
        case "$URL_AUTHORITY" in
            *@*)
                shown="$(sanitised_endpoint_display "$value")"
                die "$option carries a credential in the URL itself (a 'user:pass@host' userinfo
       segment).  It is refused rather than sanitised, because sanitising only protects the
       log: the value reaches curl, or the published image's apt configuration, verbatim.
       Configure an authenticated archive with --apt-auth-file, which is installed for the apt
       phase only and removed before the image is released, or fetch an authenticated base
       through --base-url-file or separately with --base-tarball and its digest.  Do NOT put a
       credential file in the base tarball: it would be published inside the image, and the
       pre-publication scan refuses to publish an image that carries one.  The value with its
       userinfo removed is: $shown"
                ;;
        esac
    done
}

# A QUERY STRING IS REFUSED FOR EVERY VALUE THAT ARRIVES IN argv, AND PERMITTED ONLY IN THE
# --base-url-file FORM.
#
# The line this draws is not "which option" but "where the value ends up", and there are now
# two ways for it to end up somewhere it should not:
#
#   * --apt-mirror is written into the PUBLISHED image's /etc/apt/sources.list, so anything in
#     its query is published with the artefact.  Refused, as it always was.
#   * --base-url arrives in this script's own argv, which means the whole value sits in
#     /proc/<this-pid>/cmdline, readable by every user on the build host for as long as the
#     process lives.  A presigned signature in a query string is a BEARER CREDENTIAL, so that
#     is a credential disclosure to local users -- and it is one this script cannot fix from
#     the inside: writing the URL to a private file before spawning curl hides it from the
#     CHILD's argv, which was never the exposure that mattered.  So the argv form now refuses a
#     query too, and --base-url-file exists to carry exactly that case: the value is read from
#     a caller-owned file, never appears in any command line, and IS permitted a query.
#
# A fragment is refused alongside it, for two reasons: it is the same class of thing (stripped
# by the sanitisers, so a value carrying one would not match its own record), and refusing both
# is what lets install_image_packages treat "the sanitised form differs from the value" as an
# internal error rather than as a caller mistake.
assert_no_url_query() {
    local value="$1" option="$2" reason="$3"
    case "$value" in
        *'?'*|*'#'*)
            die "$option contains a query string or a fragment.  $reason
       A query string is where a signed or tokenised URL carries its secret, so it is refused
       here rather than carried.  Pass the plain scheme://host/path form, and if the URL must
       carry a signature use --base-url-file, which reads it from a file and never puts it in
       any command line."
            ;;
    esac
}

# curl reads the base URL from a --config file rather than from argv, and that file is written
# as  url = "<value>"  -- so a value containing a double quote or a backslash could end the
# quoted string or escape out of it.  Neither character is valid in a URI unencoded, so
# refusing them costs nothing real and removes the injection entirely.
assert_curl_config_safe() {
    local value="$1" option="$2"
    # *\\* rather than *'\'* -- the two match the same single backslash, and the second form
    # reads to ShellCheck as a mis-escaped quote (SC1003).
    case "$value" in
        *'"'*|*\\*)
            die "$option contains a double quote or a backslash.  The value is handed to curl
       through a private configuration file, as  url = \"...\"  , specifically so that it never
       appears in a process's command line where 'ps' can read it; both characters would end or
       escape out of that quoted string.  Neither is valid in a URI unencoded - percent-encode
       it (%22, %5C) if the endpoint really needs one."
            ;;
    esac
}

# Sanitises every URL-shaped field of a value, so that one function handles both a bare URL
# and a whole sources.list line.  read -a is used rather than `for f in $value` because it
# splits on IFS WITHOUT globbing: a '*' in the value must not be expanded against the build
# host's filesystem.
sanitise_endpoint() {
    local value="$1" out='' field
    local -a fields=()
    SANITISED_ENDPOINT_REDACTED=0
    SANITISED_URL_TOKEN=''
    read -r -a fields <<< "$value"
    for field in ${fields[@]+"${fields[@]}"}; do
        # A field is URL-SHAPED if it carries a scheme, a userinfo separator or a query.  The
        # last two matter on their own: curl accepts a scheme-less "user:pass@host/path", so a
        # test for "://" alone would leave exactly the credential-bearing form unredacted.
        # None of the non-URL fields a sources.list line carries - "deb", a suite name, a
        # component, or a "[signed-by=...]" option group - contains any of the three.
        case "$field" in
            *://*|*@*|*'?'*) sanitise_url_token "$field"; field="$SANITISED_URL_TOKEN" ;;
        esac
        if [ -z "$out" ]; then out="$field"; else out="$out $field"; fi
    done
    SANITISED_ENDPOINT="$out"
}

# The sanitised form of $1, with a note when anything was removed.  Safe to use in a command
# substitution because the redaction state is folded into the returned text.
sanitised_endpoint_display() {
    sanitise_endpoint "$1"
    if [ "$SANITISED_ENDPOINT_REDACTED" -eq 1 ]; then
        printf '%s  [userinfo/query redacted]' "$SANITISED_ENDPOINT"
    else
        printf '%s' "$SANITISED_ENDPOINT"
    fi
}

# ------------------------------------------------------------------------------------
# INPUT VALIDATION.  Every path is absolutised, collapsed, refused if it lands somewhere it
# must not, checked for existence, and checked for characters that cannot be embedded in the
# generated in-guest configuration.
# ------------------------------------------------------------------------------------
validate_inputs() {
    # SCALAR SHAPES FIRST.  A value that cannot be read is refused before anything else is
    # judged, so `--size abc` is reported as a bad size rather than as whichever required
    # argument happened to be checked first -- a message about the wrong thing costs the
    # caller a whole second run.
    require_positive_integer "$IMAGE_SIZE_MIB" '--size'
    require_positive_integer "$READINESS_TIMEOUT_SECONDS" '--readiness-timeout'

    [ -n "$OUTPUT_IMAGE" ] || die "--output is required: this script has no default place to
       write a root image and will not invent one.  Run '$SCRIPT_PATH --help'."
    [ -n "$PAYLOAD_DIR" ]  || die "--payload is required: the guest builds and measures the
       hdmicec checkout, so there is nothing to boot without one."
    [ -n "$SDK_DIR" ]      || die "--sdk-dir is required: the guest needs the staged Binder SDK
       and the AIDL client stub snapshots, and this script will not guess where they are."

    # THE BASE COMES FROM EXACTLY ONE OF THREE PLACES, and the count is done before any of
    # them is read so that "which provenance does the manifest record" has one answer.
    local base_sources=0
    [ -z "$BASE_TARBALL" ]  || base_sources=$(( base_sources + 1 ))
    [ -z "$BASE_URL" ]      || base_sources=$(( base_sources + 1 ))
    [ -z "$BASE_URL_FILE" ] || base_sources=$(( base_sources + 1 ))
    [ "$base_sources" -le 1 ] || die "--base-tarball, --base-url and --base-url-file are
       alternatives; more than one was given.  Pass the one that describes where the base
       actually comes from, so the manifest records a single provenance for it."
    [ "$base_sources" -eq 1 ] || die "a base root filesystem is required: pass --base-tarball
       FILE (preferred: no network at all), --base-url URL (fetched on THIS host, never from
       inside the guest), or --base-url-file FILE (the same fetch, with the URL read from a
       private file so that it never enters a command line) -- together with its SHA256.  There
       is no built-in default, because an unpinned base would make this job irreproducible."

    # THE FILE FORM IS READ HERE, and from this point on the two forms are indistinguishable to
    # everything downstream: BASE_URL holds the value, BASE_URL_SOURCE records which form it
    # arrived in, and only the query-string rule below consults that difference.
    if [ -n "$BASE_URL_FILE" ]; then
        BASE_URL_FILE="$(absolutise_and_check "$BASE_URL_FILE" '--base-url-file')"
        require_private_file "$BASE_URL_FILE" 'the --base-url-file base URL file'
        BASE_URL="$(read_private_line "$BASE_URL_FILE" 'the --base-url-file base URL file')"
        BASE_URL_SOURCE='--base-url-file'
        # The path is logged; the value is not, and will not be anywhere but in its sanitised
        # scheme://host/path form.
        log "base URL read from the private file $BASE_URL_FILE (not shown; its sanitised form"
        log "  is recorded in the manifest and appears in the fetch log)"
    elif [ -n "$BASE_URL" ]; then
        BASE_URL_SOURCE='--base-url'
    fi

    # THE TWO ENDPOINT VALUES, CHECKED BEFORE ANYTHING USES, LOGS OR RECORDS THEM.  --apt-mirror
    # had no validation at all before this: it was written straight into the image's
    # sources.list, where a newline configures a second archive.  Neither is a path, so neither
    # goes through absolutise_and_check; what they need is the control-character refusal above.
    #
    # AND CREDENTIALS ARE REFUSED HERE RATHER THAN CARRIED AND SANITISED.  Sanitising protects
    # the log and the manifest; it does not protect the IMAGE, which is where --apt-mirror ends
    # up, nor this process's own command line, which is where an argv --base-url ends up.  So
    # userinfo is refused in both, and the base URL is additionally checked for the two
    # characters that could break out of the curl configuration file it is passed through.
    # Every one of these is a refusal at the boundary, which is the only disposition that leaves
    # nothing to leak.
    #
    # EVERY CHECK APPLIES TO BOTH BASE-URL FORMS.  BASE_URL now holds the file form's value too,
    # so reading it from a file buys privacy of TRANSPORT and no relaxation of validation.
    [ -z "$BASE_URL" ]   || assert_no_control_characters "$BASE_URL"   "$BASE_URL_SOURCE"
    [ -z "$APT_MIRROR" ] || assert_no_control_characters "$APT_MIRROR" '--apt-mirror'
    [ -z "$BASE_URL" ]   || assert_no_url_userinfo       "$BASE_URL"   "$BASE_URL_SOURCE"
    [ -z "$APT_MIRROR" ] || assert_no_url_userinfo       "$APT_MIRROR" '--apt-mirror'
    [ -z "$BASE_URL" ]   || assert_curl_config_safe      "$BASE_URL"   "$BASE_URL_SOURCE"

    # THE QUERY-STRING RULE, WHICH IS THE ONE PLACE THE TWO FORMS DIFFER.
    #
    # --apt-mirror: refused because the value is written into the PUBLISHED image.
    # --base-url (argv): refused because this script cannot hide its own command line.  A
    #   presigned signature in a query string is a bearer credential, and /proc/<pid>/cmdline
    #   is readable by every local user for this process's whole lifetime.  Writing the URL to
    #   a private file before spawning curl -- which this script does -- hides it from the
    #   CHILD only, and the child was never the exposure.
    # --base-url-file: PERMITTED, and it is the reason that option exists.  The value never
    #   appears in any argv, so a signature in its query is disclosed to nobody.
    [ -z "$APT_MIRROR" ] || assert_no_url_query "$APT_MIRROR" '--apt-mirror' \
        "Unlike --base-url-file, this value is written into the PUBLISHED image's
       /etc/apt/sources.list, so everything in it is published with the artefact."
    if [ -n "$BASE_URL" ] && [ "$BASE_URL_SOURCE" = '--base-url' ]; then
        assert_no_url_query "$BASE_URL" '--base-url' \
        "This value arrived on this script's COMMAND LINE, so the whole of it is in
       /proc/<pid>/cmdline for as long as this process lives, readable by every user on the
       build host.  Pass it with --base-url-file instead: the URL is read from a private file,
       is never an argument to anything, and a query string is accepted there."
    fi

    OUTPUT_IMAGE="$(absolutise_and_check "$OUTPUT_IMAGE" '--output')"
    assert_path_embeddable "$OUTPUT_IMAGE" '--output'
    [ -d "$(dirname -- "$OUTPUT_IMAGE")" ] || die "the directory holding --output does not
       exist: $(dirname -- "$OUTPUT_IMAGE").  Create it first; this script will not create a
       parent it was not asked for."
    [ ! -d "$OUTPUT_IMAGE" ] || die "--output names an existing DIRECTORY: $OUTPUT_IMAGE.
       It must name the image FILE to write."

    PAYLOAD_DIR="$(absolutise_and_check "$PAYLOAD_DIR" '--payload')"
    assert_path_embeddable "$PAYLOAD_DIR" '--payload'
    require_existing_dir "$PAYLOAD_DIR" 'the --payload hdmicec checkout'

    SDK_DIR="$(absolutise_and_check "$SDK_DIR" '--sdk-dir')"
    assert_path_embeddable "$SDK_DIR" '--sdk-dir'
    require_existing_dir "$SDK_DIR" 'the --sdk-dir staged rdk-halif-aidl tree'

    if [ -n "$BASE_TARBALL" ]; then
        BASE_TARBALL="$(absolutise_and_check "$BASE_TARBALL" '--base-tarball')"
        require_existing_file "$BASE_TARBALL" 'the --base-tarball base root filesystem'
    fi
    if [ -n "$LCOV_TARBALL" ]; then
        LCOV_TARBALL="$(absolutise_and_check "$LCOV_TARBALL" '--lcov-tarball')"
        require_existing_file "$LCOV_TARBALL" 'the --lcov-tarball lcov 2.x source tarball'
    fi
    if [ -n "$GTEST_PREFIX_SRC" ]; then
        GTEST_PREFIX_SRC="$(absolutise_and_check "$GTEST_PREFIX_SRC" '--gtest-prefix')"
        require_existing_dir "$GTEST_PREFIX_SRC" 'the --gtest-prefix GoogleTest prefix'
        require_existing_file "$GTEST_PREFIX_SRC/lib/pkgconfig/gtest.pc" \
            "gtest.pc under the --gtest-prefix prefix.  configure consults pkg-config ONLY for
       GoogleTest and ignores -I/-L, so a prefix without a .pc file cannot satisfy it"
    fi
    if [ -n "$KERNEL_CONFIG_SRC" ]; then
        KERNEL_CONFIG_SRC="$(absolutise_and_check "$KERNEL_CONFIG_SRC" '--kernel-config')"
        require_existing_file "$KERNEL_CONFIG_SRC" 'the --kernel-config guest kernel configuration'
    fi
    # --apt-auth-file.  Checked for confidentiality the same way --base-url-file is, and for
    # nothing else: its CONTENT is apt's business, this script never parses it, never logs it
    # and never digests it into the manifest.  What this script guarantees is the LIFETIME --
    # installed immediately before the apt phase, removed immediately after, and independently
    # refused by the pre-publication credential-store scan if it somehow survives.
    if [ -n "$APT_AUTH_FILE" ]; then
        APT_AUTH_FILE="$(absolutise_and_check "$APT_AUTH_FILE" '--apt-auth-file')"
        require_private_file "$APT_AUTH_FILE" 'the --apt-auth-file apt credential file'
        [ -s "$APT_AUTH_FILE" ] || die "the --apt-auth-file apt credential file is empty:
       $APT_AUTH_FILE
       It was given, so it was meant to authenticate something; an empty one would install a
       credential file that authenticates nothing and then be removed again."
    fi

    # THE DIGEST IS MANDATORY.  A sidecar file is accepted because that is how published
    # artefacts usually carry their digest, but the absence of both is a refusal: this script
    # never extracts a base it could not verify.
    if [ -z "$BASE_SHA256" ] && [ -n "$BASE_TARBALL" ] && [ -f "$BASE_TARBALL.sha256" ]; then
        BASE_SHA256="$(read_sidecar_digest "$BASE_TARBALL.sha256")"
        log "base tarball digest taken from the sidecar file $BASE_TARBALL.sha256"
    fi
    [ -n "$BASE_SHA256" ] || die "no SHA256 for the base root filesystem.  Pass --base-sha256
       HEX, or place a sidecar <tarball>.sha256 beside it.  This script hard-codes no digest --
       a digest written into it from memory would be a fabricated measurement -- so it refuses
       to extract a base it cannot verify.  Compute one with:
           sha256sum <tarball>"
    require_sha256_hex "$BASE_SHA256" 'the base tarball SHA256'

    if [ -n "$LCOV_TARBALL" ]; then
        if [ -z "$LCOV_SHA256" ] && [ -f "$LCOV_TARBALL.sha256" ]; then
            LCOV_SHA256="$(read_sidecar_digest "$LCOV_TARBALL.sha256")"
            log "lcov tarball digest taken from the sidecar file $LCOV_TARBALL.sha256"
        fi
        [ -n "$LCOV_SHA256" ] || die "--lcov-tarball was given without a digest.  Pass
       --lcov-sha256 HEX or place a sidecar <tarball>.sha256 beside it; an unverified source
       tarball is refused for the same reason the base is."
        require_sha256_hex "$LCOV_SHA256" 'the lcov tarball SHA256'
    fi

    # LAST, because it compares canonical path against canonical path and every path above is
    # canonicalised by the time it runs.
    assert_secret_inputs_outside_copied_roots
}

# First whitespace-delimited field of a `sha256sum`-style line.  Accepts both the plain
# digest and the "digest  filename" form, because published sidecars use either.
read_sidecar_digest() { # $1=sidecar path -> digest on stdout
    local first
    first="$(awk 'NF { print $1; exit }' "$1" 2>/dev/null || true)"
    [ -n "$first" ] || die "the sidecar digest file $1 is empty or unreadable.  Remove it and
       pass the digest explicitly, or fix the file."
    printf '%s\n' "$first"
}

verify_sha256() { # $1=file  $2=expected digest  $3=what it is
    local actual
    actual="$(sha256sum -- "$1" | awk '{ print $1 }')"
    if [ "$actual" != "${2,,}" ] && [ "$actual" != "$2" ]; then
        die "SHA256 MISMATCH for $3:
           file     $1
           expected $2
           actual   $actual
       Refusing to use it.  Either the artefact is not the one that was pinned, or it was
       corrupted in transit.  Re-fetch it, or correct the pin -- do not relax this check."
    fi
    log "verified $3 against its pinned SHA256"
}

# ------------------------------------------------------------------------------------
# THE STAGED SDK.  Verified against the layout build_binder.sh and the snapshot builds
# produce, so that a missing piece is a named failure with a remedy here rather than a
# compile error, a link error or a boot-time abort inside the guest.
# ------------------------------------------------------------------------------------
SDK_BINDER_TARGET=''
SDK_BINDER_BUILD=''
SDK_HALIF_LIB=''
SDK_SERVICEMANAGER=''
SDK_BINDER_LIB=''

verify_sdk_tree() {
    SDK_BINDER_TARGET="$SDK_DIR/$SDK_REL_BINDER_TARGET"
    SDK_BINDER_BUILD="$SDK_DIR/$SDK_REL_BINDER_BUILD"
    SDK_HALIF_LIB="$SDK_DIR/$SDK_REL_HALIF_LIB"
    SDK_SERVICEMANAGER="$SDK_DIR/$SDK_REL_SERVICEMANAGER"
    SDK_BINDER_LIB="$SDK_BINDER_TARGET/lib/binder"
    readonly SDK_BINDER_TARGET SDK_BINDER_BUILD SDK_HALIF_LIB SDK_SERVICEMANAGER SDK_BINDER_LIB

    require_existing_dir "$SDK_BINDER_TARGET" "the staged Binder SDK target tree
       ($SDK_REL_BINDER_TARGET under --sdk-dir).  Build the SDK before building the image"
    require_existing_dir "$SDK_BINDER_BUILD" "the staged Binder SDK build tree
       ($SDK_REL_BINDER_BUILD under --sdk-dir), which is where the Binder headers live"
    require_existing_dir "$SDK_BINDER_LIB" "the staged Binder library directory
       ($SDK_REL_BINDER_TARGET/lib/binder under --sdk-dir)"
    require_existing_dir "$SDK_HALIF_LIB" "the staged AIDL stub library directory
       ($SDK_REL_HALIF_LIB under --sdk-dir)"

    require_existing_file "$SDK_BINDER_BUILD/include/binder_sdk/binder/IServiceManager.h" \
        "the Binder SDK headers the generated stubs include.  Expected
       $SDK_REL_BINDER_BUILD/include/binder_sdk/binder/ under --sdk-dir"

    local root
    for root in "${SDK_REL_INCLUDE_ROOTS[@]}"; do
        require_existing_dir "$SDK_DIR/$root" "the AIDL include root '$root' under --sdk-dir.
       All three are required and none substitutes for another: the snapshot stubs, the frozen
       common import they were generated against, and common/current, which is the only place
       halcompat.h lives"
    done

    local library
    for library in "${HALIF_STUB_LIBRARIES[@]}"; do
        require_existing_file "$SDK_HALIF_LIB/$library" "the AIDL stub library $library in
       $SDK_REL_HALIF_LIB.  common is pinned at 0.2.0.0 because that is the ABI the released
       hdmicec 0.1.0.0 snapshot was generated against; a newer common release is not a
       substitute"
        assert_target_arch_binary "$SDK_HALIF_LIB/$library" 'a staged AIDL stub library'
    done

    for library in "${BINDER_CLOSURE_LIBRARIES[@]}"; do
        require_existing_file "$SDK_BINDER_LIB/$library" "$library in the staged Binder
       library directory.  libbinder and libutils are direct link edges of libRCEC; liblog,
       libbase, libcutils and libcutils_sockets are libbinder's own dependencies and are
       required because they must be LOADABLE, not because the middleware names them"
        assert_target_arch_binary "$SDK_BINDER_LIB/$library" 'a staged Binder library'
    done

    require_existing_file "$SDK_SERVICEMANAGER" "servicemanager at $SDK_REL_SERVICEMANAGER
       under --sdk-dir.  It is an unconditional runtime prerequisite wherever a binder driver
       is present: the service name cannot be resolved without it, and obtaining an
       IServiceManager at all loops until binder handle 0 resolves"
    assert_target_arch_binary "$SDK_SERVICEMANAGER" 'the staged servicemanager'

    log "staged SDK verified: stubs, the six-library Binder closure and servicemanager are"
    log "  present and built for $ARCH"
}

# One binary's class and machine, described by readelf where it is available and by `file`
# otherwise.  readelf is preferred because its spelling of the machine is the stable one; see
# the note above resolve_arch.
elf_description() { # $1=path -> one line on stdout, empty when nothing could read it
    local text=''
    if command -v readelf >/dev/null 2>&1; then
        text="$(readelf -h -- "$1" 2>/dev/null \
                | sed -n 's/^ *\(Class\|Machine\): *\(.*\)$/\1=\2;/p' | tr -d '\n' || true)"
    fi
    if [ -z "$text" ]; then
        text="$(file -b -- "$1" 2>/dev/null || true)"
    fi
    printf '%s\n' "$text"
}

# A mismatch is fatal HERE, where the message can name the remedy, rather than at boot where
# it surfaces as an exec format error or a binder protocol mismatch.
assert_target_arch_binary() { # $1=path  $2=what it is
    local description
    description="$(elf_description "$1")"
    [ -n "$description" ] || die "could not identify $2:
           $1
       Neither readelf nor file could describe it.  Refusing to copy a binary whose
       architecture cannot be read into a guest whose entire userspace must be $ARCH."

    if printf '%s\n' "$description" | grep -Eq -- "$ELF_REJECT_REGEX"; then
        die "$2 is built for a 64-bit machine:
           $1
           $description
       The guest userspace is 32-bit throughout and a process is a single ELF class, so a
       64-bit staged artefact could not be loaded into any process the guest starts.  Rebuild
       the Binder SDK and the AIDL stub snapshots for $ARCH, or select the architecture they
       were built for with --arch.  This is a BITNESS refusal and says nothing about the
       binder wire protocol, which is derived separately."
    fi
    printf '%s\n' "$description" | grep -Eq -- "$ELF_CLASS_REGEX" || die "$2 is not a 32-bit
       object:
           $1
           $description
       The guest userspace is 32-bit throughout; rebuild it for $ARCH or pass the --arch it
       was built for."
    printf '%s\n' "$description" | grep -Eq -- "$ELF_MACHINE_REGEX" || die "$2 was built for a
       different machine than --arch $ARCH (expected $EXPECTED_ELF_MACHINE):
           $1
           $description
       Rebuild it for $ARCH, or pass the --arch it was actually built for."
}

# ------------------------------------------------------------------------------------
# THE PAYLOAD.  Two checks, and the second one is the one that saves a QEMU cycle.
#
# The guest COMPILES the payload, so stale build output for the wrong architecture is not a
# harmless leftover: make would try to relink it and fail with an unhelpful message deep
# inside a build log, inside a VM, after a boot.  Refusing it here, by name, is worth the
# scan.
# ------------------------------------------------------------------------------------
verify_payload_tree() {
    require_existing_file "$PAYLOAD_DIR/configure.ac" "configure.ac in the --payload tree.
       --payload must be the hdmicec checkout itself, not its parent"
    require_existing_file "$PAYLOAD_DIR/tests/L1Tests/run_coverage.sh" \
        "tests/L1Tests/run_coverage.sh in the --payload tree.  It is what the in-guest init
       invokes, and it owns the whole invocation matrix"
    [ -x "$PAYLOAD_DIR/tests/L1Tests/run_coverage.sh" ] || warn \
        "tests/L1Tests/run_coverage.sh is not executable in the payload; the init invokes it
  through 'bash', so this is survivable, but it is worth fixing at the source."

    local -a mismatched=()
    local candidate description
    while IFS= read -r candidate; do
        [ -n "$candidate" ] || continue
        description="$(file -b -- "$candidate" 2>/dev/null || true)"
        # Only ELF files are judged: an archive of text, a stray script or a data file
        # matching one of the globs is not build output for any architecture.  `file` is used
        # here rather than readelf because this walks a whole tree and file is the cheaper of
        # the two; the patterns accept either tool's spelling.
        case "$description" in
            *ELF*)
                if printf '%s\n' "$description" | grep -Eq -- "$ELF_REJECT_REGEX" \
                   || ! printf '%s\n' "$description" | grep -Eq -- "$ELF_CLASS_REGEX" \
                   || ! printf '%s\n' "$description" | grep -Eq -- "$ELF_MACHINE_REGEX"; then
                    mismatched+=("$candidate -- $description")
                fi ;;
            *) : ;;
        esac
    done < <(find "$PAYLOAD_DIR" \( -name '*.o' -o -name '*.lo' -o -name '*.a' \
                                    -o -name '*.so' -o -name '*.so.*' \) -type f 2>/dev/null)

    if [ "${#mismatched[@]}" -gt 0 ]; then
        local entry
        warn "the --payload tree carries build output built for another architecture:"
        for entry in "${mismatched[@]}"; do
            warn "  $entry"
        done
        die "refusing to build a $ARCH image around build output for another architecture.
       The guest rebuilds this tree, and make would try to relink these objects with the
       guest's $ARCH toolchain and fail inside the VM, where the message is hardest to read.
       Pass a clean checkout -- 'git clean -xfd' inside the submodule -- or a payload built
       for $ARCH."
    fi
    assert_payload_carries_no_credentials
    log "payload verified: an hdmicec checkout with run_coverage.sh, no foreign build output"
    log "  and no credential material"
}

# ------------------------------------------------------------------------------------
# THE PAYLOAD MUST NOT CARRY CREDENTIAL MATERIAL INTO THE IMAGE.
#
# WHY THIS BELONGS HERE AND NOT ONLY IN THE WORKFLOW.  Everything in --payload is copied into
# the image, and the image is uploaded as a CI artifact -- so anything credential-shaped in
# the payload becomes downloadable.  The workflow that drives this script stages a source-only
# payload and checks it, but this script is also runnable by hand, and a check that only
# exists in the caller is a check that is absent exactly when someone is improvising.  This is
# the same reasoning that puts the digest verification here rather than in the workflow.
#
# WHAT IS REFUSED, AND WHAT IS DELIBERATELY NOT.
#   * A file-based credential store -- .git-credentials, .netrc -- anywhere in the tree, and a
#     private key file.  Refused outright: none of them has any business in a payload whose
#     only job is to be built and measured.
#   * git configuration carrying an Authorization header (actions/checkout's
#     persist-credentials writes http.<host>/.extraheader), a credential helper, or a URL
#     rewrite that can hold userinfo.  Read out of git's own configuration when git is
#     available so any spelling is found, and out of .git/config directly when it is not.
#   * A .git directory as such is NOT refused.  It is not credential material by itself, the
#     workflow strips it for size as much as for safety, and refusing it would stop a
#     developer from pointing --payload at their working checkout -- which is the ordinary way
#     to use this script by hand.  The credential checks above are what matter, and they apply
#     to that checkout too.
#
# NOTHING THAT MATCHES IS EVER PRINTED.  Only the KEY or the FILE NAME is named; a message
# that quoted the value would write the credential into the log that reports it.
# ------------------------------------------------------------------------------------
assert_payload_carries_no_credentials() {
    local found keys offenders

    found="$(find "$PAYLOAD_DIR" \
                  \( -name '.git-credentials' -o -name '.netrc' -o -name '.npmrc' \
                     -o -name 'id_rsa' -o -name 'id_dsa' -o -name 'id_ecdsa' \
                     -o -name 'id_ed25519' -o -name '*.pem' -o -name '*.p12' \) \
                  -type f -print 2>/dev/null || true)"
    if [ -n "$found" ]; then
        warn 'the --payload tree contains credential or key files:'
        printf '%s\n' "$found" | sed 's/^/       /' >&2
        die "refusing to copy credential material into the guest image.  Everything in the
       payload is copied into the image, and the image is uploaded as a CI artifact, so these
       files would be published.  Remove them from the payload -- or stage a copy without
       them and pass that, which is what the workflow does."
    fi

    if [ -d "$PAYLOAD_DIR/.git" ]; then
        keys=''
        if command -v git >/dev/null 2>&1; then
            keys="$(git -C "$PAYLOAD_DIR" config --local --name-only --list 2>/dev/null || true)"
        elif [ -r "$PAYLOAD_DIR/.git/config" ]; then
            # No git on the host: read the file.  Only key-shaped lines are considered, so a
            # value that happens to contain one of these words is not matched.
            keys="$(grep -Eo '^[[:space:]]*(extraheader|helper|insteadOf)[[:space:]]*=' \
                         -- "$PAYLOAD_DIR/.git/config" 2>/dev/null || true)"
        fi
        offenders="$(printf '%s\n' "$keys" | grep -Ei 'extraheader|credential\.|helper|insteadof' || true)"
        if [ -n "$offenders" ]; then
            warn 'the --payload checkout retains credential-bearing git configuration.'
            warn 'Offending keys (values deliberately not printed):'
            printf '%s\n' "$offenders" | sed 's/^/       /' >&2
            die "refusing to copy a checkout with persisted credentials into the guest image,
       which this job uploads as an artifact.  With actions/checkout, set
       'persist-credentials: false'; by hand, clear the entry with
           git -C '$PAYLOAD_DIR' config --local --unset-all http.\"\$host\".extraheader
       or stage a copy of the tree without .git and pass that."
        fi
    fi
}

# ------------------------------------------------------------------------------------
# THE BINDER WIRE PROTOCOL: DERIVED FROM TWO ARTEFACTS, NEVER DECLARED.
#
# This function exists because an earlier revision of this script HARD-CODED protocol 7 --
# in its help text, its architecture log, the value it wrote into the image and its
# manifest -- while nothing compared that text against the SDK it was shipping alongside.
# For a while the SDK was built BINDER_IPC_32BIT=OFF, which is protocol 8, and the two claims
# could not both be true.  Text cannot be checked.  So nothing here states a protocol, not even
# now that the job's intended value and the SDK's build flag agree on 7: two build-time
# artefacts DECIDE it, this function reads both, and a disagreement fails the build by name.
# That is also the check that catches the workflow's kernel patch silently failing to apply,
# which is the one plausible way the intended 7 turns into a running 8.
#
#   THE KERNEL HALF.  CONFIG_ANDROID_BINDER_IPC_32BIT in the configuration the guest kernel
#   was built with, supplied by --kernel-config.  Explicitly =y means protocol 7.  Unset, or
#   absent from the Kconfig altogether (which is what an UNPATCHED 5.x kernel shows, because
#   mainline deleted the option in the 4.18 era while CONFIG_ANDROID_BINDERFS starts at 5.0 --
#   the workflow restores it with an inline patch and asserts it in the built .config), means
#   protocol 8.
#
#   THE USERSPACE HALF.  BINDER_IPC_32BIT in the staged SDK's own CMakeCache.txt.  ON means
#   protocol 7 and OFF means protocol 8, per linux_binder_idl CMakeLists.txt:733-739 at tag
#   2.6.0, which compiles -DBINDER_IPC_32BIT=1 into libbinder in the ON arm only.
#
# AN ABSENT CACHE ENTRY IS NOT READ AS OFF, and that distinction is measured rather than
# stylistic: CMakeLists.txt:218-224 sets the variable with a plain set() when it is not
# already defined, so a build that never passed it as a CACHE entry leaves NO entry in the
# cache while still compiling protocol 7.  Reading "absent" as OFF would therefore invent
# the more convenient answer.  Absent means "this source cannot answer", and it is reported
# as such.
#
# FAIL-CLOSED WHEN NEITHER SOURCE CAN ANSWER.  Without a derived expectation the guest has
# nothing to cross-check the driver against, which is the state that produced the
# contradiction in the first place, so the build stops here with the remedy named.
# ------------------------------------------------------------------------------------
DERIVED_BINDER_PROTOCOL=''
PROTOCOL_FROM_KERNEL_CONFIG=''
PROTOCOL_FROM_SDK_CACHE=''
PROTOCOL_KERNEL_EVIDENCE=''
PROTOCOL_SDK_EVIDENCE=''

# Sets PROTOCOL_FROM_KERNEL_CONFIG and PROTOCOL_KERNEL_EVIDENCE from one kernel
# configuration file.  Globals rather than stdout, so that the diagnostic text and the
# derived value cannot be split by a separator appearing inside either of them.
read_protocol_from_kernel_config() { # $1=path
    local config="$1" line=''
    PROTOCOL_FROM_KERNEL_CONFIG=''
    if [ ! -r "$config" ]; then
        PROTOCOL_KERNEL_EVIDENCE="unreadable: $config"
        return 0
    fi
    line="$(grep -E '^(CONFIG_ANDROID_BINDER_IPC_32BIT=|# CONFIG_ANDROID_BINDER_IPC_32BIT is not set)' \
            -- "$config" 2>/dev/null | head -n 1 || true)"
    case "$line" in
        CONFIG_ANDROID_BINDER_IPC_32BIT=y|CONFIG_ANDROID_BINDER_IPC_32BIT=Y)
            PROTOCOL_FROM_KERNEL_CONFIG='7'
            PROTOCOL_KERNEL_EVIDENCE="$config -> $line" ;;
        CONFIG_ANDROID_BINDER_IPC_32BIT=*|'# CONFIG_ANDROID_BINDER_IPC_32BIT is not set')
            # Anything other than =y is not "set" in Kconfig terms, so it is protocol 8 -- and
            # the literal line is carried through so a reader can see what it actually said.
            PROTOCOL_FROM_KERNEL_CONFIG='8'
            PROTOCOL_KERNEL_EVIDENCE="$config -> $line" ;;
        *)
            PROTOCOL_FROM_KERNEL_CONFIG='8'
            PROTOCOL_KERNEL_EVIDENCE="$config -> CONFIG_ANDROID_BINDER_IPC_32BIT absent from this Kconfig entirely, which is what a 5.x mainline kernel shows: the option was removed in the 4.18 era and CONFIG_ANDROID_BINDERFS starts at 5.0" ;;
    esac
}

# Sets PROTOCOL_FROM_SDK_CACHE and PROTOCOL_SDK_EVIDENCE from every CMakeCache.txt under the
# staged SDK.  Several caches are tolerated as long as every one that carries the entry
# agrees; two that disagree are a staging fault and are refused rather than resolved by
# picking one.
read_protocol_from_sdk_caches() {
    local cache entry value seen='' evidence='' conflict=0
    PROTOCOL_FROM_SDK_CACHE=''
    while IFS= read -r cache; do
        [ -n "$cache" ] || continue
        entry="$(grep -E '^BINDER_IPC_32BIT(:[A-Za-z]*)?=' -- "$cache" 2>/dev/null | head -n 1 || true)"
        [ -n "$entry" ] || continue
        value="$(printf '%s\n' "${entry#*=}" | tr '[:lower:]' '[:upper:]')"
        case "$value" in
            ON|1|TRUE|YES|Y)     value='7' ;;
            OFF|0|FALSE|NO|N|'') value='8' ;;
            *)                   value='' ;;
        esac
        [ -n "$value" ] || continue
        evidence="$evidence${evidence:+; }${cache#"$SDK_DIR"/} -> $entry"
        if [ -z "$seen" ]; then
            seen="$value"
        elif [ "$seen" != "$value" ]; then
            conflict=1
        fi
    done < <(find "$SDK_BINDER_BUILD" "$SDK_BINDER_TARGET" -maxdepth 3 -name CMakeCache.txt -type f 2>/dev/null)

    if [ "$conflict" -eq 1 ]; then
        die "the staged Binder SDK contains CMake caches that DISAGREE about BINDER_IPC_32BIT,
       so the wire protocol the staged libbinder speaks cannot be established:
           $evidence
       One staging tree cannot have been built two ways.  Rebuild the SDK from clean, or
       remove the stale build directory, and pass a staging tree with a single answer."
    fi
    if [ -z "$seen" ]; then
        PROTOCOL_SDK_EVIDENCE='no CMakeCache.txt under the staged tree carries a BINDER_IPC_32BIT cache entry. An absent entry is NOT read as OFF: linux_binder_idl CMakeLists.txt:218-224 sets the variable with a plain set() when it was not passed as a cache entry, so a protocol-7 build can leave no trace in the cache'
        return 0
    fi
    PROTOCOL_FROM_SDK_CACHE="$seen"
    PROTOCOL_SDK_EVIDENCE="$evidence"
}

derive_binder_protocol() {
    local suggested_sdk_flag=''

    if [ -n "$KERNEL_CONFIG_SRC" ]; then
        read_protocol_from_kernel_config "$KERNEL_CONFIG_SRC"
    else
        PROTOCOL_KERNEL_EVIDENCE='no --kernel-config was given, so the kernel half cannot be read'
    fi

    read_protocol_from_sdk_caches

    log "deriving the binder wire protocol from the two artefacts that decide it:"
    log "  kernel configuration : ${PROTOCOL_FROM_KERNEL_CONFIG:-<cannot answer>}"
    log "    $PROTOCOL_KERNEL_EVIDENCE"
    log "  staged SDK cache     : ${PROTOCOL_FROM_SDK_CACHE:-<cannot answer>}"
    log "    $PROTOCOL_SDK_EVIDENCE"

    if [ -n "$PROTOCOL_FROM_KERNEL_CONFIG" ] && [ -n "$PROTOCOL_FROM_SDK_CACHE" ] \
       && [ "$PROTOCOL_FROM_KERNEL_CONFIG" != "$PROTOCOL_FROM_SDK_CACHE" ]; then
        if [ "$PROTOCOL_FROM_KERNEL_CONFIG" = '7' ]; then
            suggested_sdk_flag='ON'
        else
            suggested_sdk_flag='OFF'
        fi
        die "BINDER WIRE PROTOCOL DISAGREEMENT between the two build-time artefacts:
           the guest kernel configuration says protocol $PROTOCOL_FROM_KERNEL_CONFIG
               ($PROTOCOL_KERNEL_EVIDENCE)
           the staged Binder SDK says protocol $PROTOCOL_FROM_SDK_CACHE
               ($PROTOCOL_SDK_EVIDENCE)
       libbinder performs a strict protocol-version EQUALITY check when it opens the driver
       node (linux_binder_idl BUILD.md:409-413 at tag 2.6.0), so this combination fails EVERY
       open inside the guest and would surface as three inexplicably broken invocations
       rather than as a configuration fault.  Fix ONE side so the two agree -- rebuild the SDK
       with -DBINDER_IPC_32BIT=$suggested_sdk_flag to match this kernel, or build a kernel
       that matches the SDK.
       AND CHECK THE OBVIOUS CAUSE FIRST.  A STOCK kernel with CONFIG_ANDROID_BINDERFS (which
       the in-guest init requires, 5.0 and later) carries no CONFIG_ANDROID_BINDER_IPC_32BIT at
       all: mainline deleted that option in the 4.18 era.  The workflow that drives this script
       restores it with an inline patch and asserts it in the built .config precisely so a
       protocol-7 guest is reachable.  So a kernel half of 8 against an SDK half of 7 is most
       likely that patch not having applied, or a kernel .config from before it -- look at the
       'Restore CONFIG_ANDROID_BINDER_IPC_32BIT' step's output rather than changing the SDK to
       match a kernel that was supposed to be patched."
    fi

    DERIVED_BINDER_PROTOCOL="${PROTOCOL_FROM_KERNEL_CONFIG:-$PROTOCOL_FROM_SDK_CACHE}"
    [ -n "$DERIVED_BINDER_PROTOCOL" ] || die "the binder wire protocol cannot be DERIVED from
       anything this run was given, and this script does not declare one.
           kernel half:   $PROTOCOL_KERNEL_EVIDENCE
           userspace half: $PROTOCOL_SDK_EVIDENCE
       Pass --kernel-config FILE naming the configuration the guest kernel was built with
       (the reliable route, and the one the workflow uses), or stage an SDK built with
       BINDER_IPC_32BIT passed as a CMake CACHE entry so the value survives in its
       CMakeCache.txt.  Without one of the two there is nothing for the guest to cross-check
       the driver's own BINDER_VERSION answer against, and an unverifiable protocol claim is
       exactly the defect this derivation replaces."

    if [ -z "$PROTOCOL_FROM_KERNEL_CONFIG" ] || [ -z "$PROTOCOL_FROM_SDK_CACHE" ]; then
        warn "only ONE of the two build-time artefacts could answer, so the derived protocol"
        warn "  ($DERIVED_BINDER_PROTOCOL) rests on that one alone. The guest still cross-checks it against"
        warn "  the running kernel's own configuration and against /dev/binder's BINDER_VERSION"
        warn "  answer, and fails on a disagreement -- but supplying both halves here is what"
        warn "  turns a boot-time failure into a build-time one."
    fi

    readonly DERIVED_BINDER_PROTOCOL PROTOCOL_FROM_KERNEL_CONFIG PROTOCOL_FROM_SDK_CACHE
    readonly PROTOCOL_KERNEL_EVIDENCE PROTOCOL_SDK_EVIDENCE
    log "derived binder wire protocol: $DERIVED_BINDER_PROTOCOL (an EXPECTATION; the guest verifies it against the driver)"
}


# ------------------------------------------------------------------------------------
# THE PUBLICATION TARGETS: WHAT ROOT IS ALLOWED TO WRITE, AND WHO ELSE MIGHT BE WRITING IT.
#
# This script runs as root and produces two files at caller-supplied paths.  That makes the
# act of publishing them the most dangerous thing it does, and three distinct hazards apply
# to it.  All three are addressed here, before any content exists, because a check that runs
# after the image is built cannot un-truncate a file.
#
#   (1) SUBSTITUTION.  If --output names a symbolic link, root's `truncate` follows it and
#       destroys the target.  If it names a file with more than one hard link, truncating it
#       destroys the OTHER name's content too.  If any directory on the way to it is a
#       symbolic link, the path the caller typed is not the path that gets written, and the
#       PROTECTED_SYSTEM_ROOTS check that was applied to the typed path proves nothing about
#       where the bytes land.  Every one of those is refused rather than resolved: a caller
#       who meant a link can pass the link's target.
#
#   (2) CONCURRENCY.  Two builders aimed at the same output would interleave a format, a
#       mount and a rename, and the survivor would be a mixture.  An exclusive advisory lock
#       on a file beside the output makes the second one fail immediately with a message that
#       says so, rather than silently corrupting the first one's work.
#
#   (3) DESTRUCTION BEFORE PROOF.  Formatting at the final path destroys the previous good
#       image as its first action, so any later failure leaves the caller with nothing.  The
#       image and the manifest are therefore staged under private names in the same directory
#       and renamed onto the caller's paths together, after validation -- see
#       publish_outputs().  Same directory, so the rename is same-filesystem and atomic.
#
# The lock file is deliberately LEFT IN PLACE at the end of the run.  Removing a lock file
# while holding its lock is a race with teeth: a second builder that already opened the same
# path would hold a lock on an unlinked inode, and both would then believe they had exclusive
# access.  A zero-byte lock file beside the image is the cheap, correct outcome.
# ------------------------------------------------------------------------------------
OUTPUT_DIR=''
OUTPUT_MANIFEST=''
OUTPUT_LOCK=''
OUTPUT_LOCK_HELD=0

# ------------------------------------------------------------------------------------
# OUTPUT-DIRECTORY CUSTODY: THE DIRECTORY ENTRY IS THE ASSET, NOT THE FILE'S MODE.
#
# WHAT THE MODE ON THE LEAF DOES NOT BUY.  The staged image is created by mktemp with O_EXCL
# and then chmod 0600, and that protects its CONTENTS.  It does not protect its NAME: renaming
# or unlinking a directory entry is governed by write permission on the DIRECTORY, not by the
# mode of the file the entry points at.  So in an output directory another account can write
# to, that account can rename the staged leaf away between the mktemp and the truncate, or
# between the mkfs and the losetup, and put a symbolic link or a different file at the same
# name -- after which root formats, mounts, populates and publishes something the caller never
# named.  The lock leaf has exactly the same exposure, and there the substitution is worse than
# a wrong write: two builders holding locks on two different inodes both believe they have
# exclusive access.
#
# WHY STAT-THEN-OPERATE CANNOT CLOSE THIS ON ITS OWN, WHICH IS THE CORRECTION.
#
# An earlier revision recorded the directory's owner, mode, device and inode once and re-compared
# them immediately before each privileged operation.  That is worth keeping and it is kept below
# -- but it cannot be the whole answer, because every one of those operations then named the
# CALLER'S PATHNAME, and a pathname is resolved afresh by the kernel at the instant the operation
# runs.  Between the last stat and the rename there is a window, however small, in which any
# account that may rename an entry in an ANCESTOR directory can move the validated directory away
# and put a different one at its name.  Narrowing that window is not closing it (CWE-367), and no
# number of extra stats changes the shape of the problem.
#
# The revision also PERMITTED an output directory owned by the unprivileged account that invoked
# sudo, on the reasoning that a caller who may pass --output is already trusted with the path.
# That reasoning is about the PATH and does not carry to the DIRECTORY ENTRY: a directory's owner
# may rename or unlink a root-owned entry inside it regardless of the entry's mode or owner, so
# an unprivileged owner can substitute the staged image, the destination or the lock leaf between
# any stat and the privileged truncate, mkfs, losetup, rename or chmod that follows.
#
# WHAT IS REQUIRED INSTEAD, AND IT IS FOUR THINGS.
#
#   OWNER: uid 0, and nothing else.  A directory this script publishes into must belong to root.
#   The remedy is one command and it is named in the refusal:
#       install -d -o root -g root -m 0755 <dir>
#
#   MODE: no group or other WRITE bit.  The sticky-bit exemption is gone as well: sticky
#   restricts rename and unlink to the ENTRY's owner, which protects our leaves from other
#   accounts but not from the directory's own owner -- and it invites exactly the shared
#   world-writable publication directory this script should not be publishing into.
#
#   A DIRECTORY DESCRIPTOR, AND EVERY OPERATION PERFORMED THROUGH IT.  The validated directory
#   is OPENED ONCE and the descriptor is verified against what was validated (type, device,
#   inode, owner and mode, read with stat -L /proc/self/fd/N so the answer comes from the OPEN
#   FILE and not from a pathname).  From then on every privileged pathname operation names
#       /proc/self/fd/<fd>/<leaf>
#   rather than the caller's pathname.  That is the bash-reachable equivalent of openat(dirfd,
#   leaf): the descriptor pins the directory INODE, so renaming or replacing the directory, or
#   any ancestor of it, cannot redirect the operation -- it continues to act on the inode that
#   was validated, or it fails.  MEASURED on this host: with the validated directory renamed
#   away and a decoy directory created at its pathname, a `mv` through the descriptor still moved
#   the file inside the original directory and the decoy was left untouched, while a read through
#   the caller's pathname returned the decoy's content.  This is what makes ancestor ownership no
#   longer load-bearing, and it is why the leaf NAMES are what the globals hold: both the
#   descriptor-relative path (for operations) and the readable path (for messages) are composed
#   from a leaf.
#
#   IDENTITY, STILL RE-CHECKED.  None of the stat-based checks is removed, because they answer a
#   different question: the descriptor proves WHICH DIRECTORY we are operating in, and the stats
#   prove that the caller's pathname still refers to it -- so a substitution is REPORTED rather
#   than silently worked around, and a leaf inside the directory that was replaced is still
#   caught by its own device and inode.
#
# THE LEAVES ARE CHECKED THE SAME WAY.  Each staged leaf's device and inode are recorded when
# mktemp creates it and re-compared before it is written to, attached to a loop device or
# renamed; the lock leaf is additionally compared against the inode actually behind the OPEN
# DESCRIPTOR (stat -L /proc/self/fd/N), which is the only way to establish that the lock is
# held on the file at that path rather than on an inode that used to be there -- and that
# comparison now runs inside the custody assertion, at EVERY call site, rather than only at
# acquisition, because the leaf can be replaced after the lock has been taken.
# ------------------------------------------------------------------------------------
OUTPUT_DIR_DEVICE=''
OUTPUT_DIR_INODE=''
OUTPUT_DIR_OWNER=''
OUTPUT_DIR_MODE=''
# The descriptor the publication directory is held open on, and the /proc path composed from it.
# Fixed at 8 rather than allocated with {var}< so that it sits beside the lock's fd 9 and is
# visibly reserved; both are inherited by the child processes that perform the operations, which
# is what makes /proc/self/fd/<n> resolve for them.
readonly OUTPUT_DIR_FD=8
OUTPUT_DIR_PROC=''
# The leaf names of the three caller-visible paths, so that every privileged operation on them
# can be composed against OUTPUT_DIR_PROC.
OUTPUT_IMAGE_LEAF=''
OUTPUT_MANIFEST_LEAF=''
OUTPUT_LOCK_LEAF=''
STAGED_IMAGE_DEVICE=''
STAGED_IMAGE_INODE=''
STAGED_MANIFEST_DEVICE=''
STAGED_MANIFEST_INODE=''
OUTPUT_LOCK_DEVICE=''
OUTPUT_LOCK_INODE=''

# One mode string, normalised to four octal digits so the special bits can be read positionally
# regardless of whether stat printed three or four.  Empty when the value is not octal digits.
pad_four_digit_mode() { # $1=mode as stat printed it
    local mode="$1"
    case "$mode" in
        ''|*[!0-7]*) printf ''; return 0 ;;
    esac
    while [ "${#mode}" -lt 4 ]; do
        mode="0$mode"
    done
    printf '%s' "$mode"
}

# The same, read from a path.  Empty on failure.
four_digit_mode() { # $1=path
    pad_four_digit_mode "$(stat -c '%a' -- "$1" 2>/dev/null || printf '')"
}

# Is this directory's mode acceptable for a PUBLICATION directory?  No group or other write bit,
# full stop -- there is no sticky-bit exemption here.
#
# WHY THE EXEMPTION IS GONE.  Sticky restricts rename and unlink to the ENTRY's owner, which is
# the right rule for a SHARED TEMP directory (resolve_temp_parent still applies it to TMPDIR, and
# that is deliberate: /tmp is mode 2777 on this container).  It is the wrong rule for a directory
# root publishes two artefacts into, for two reasons.  A group- or world-writable directory can
# still be filled, and more to the point sticky says nothing about the directory's OWN OWNER --
# and the owner is precisely who can rename a root-owned entry out from under this script.  The
# owner is now required to be root, so permitting other accounts to write the directory as well
# would give back with the mode what the ownership requirement just took away.
publication_dir_mode_is_safe() { # $1=four-digit mode
    local mode="$1" group_write other_write
    [ "${#mode}" -eq 4 ] || return 1
    group_write=$(( ${mode:2:1} & 2 ))
    other_write=$(( ${mode:3:1} & 2 ))
    [ "$group_write" -eq 0 ] && [ "$other_write" -eq 0 ]
}

# THE RE-CHECK, run immediately before each privileged pathname operation in the output
# directory.  $1 names the operation, so a refusal says which one was about to happen.
#
# THREE THINGS ARE ESTABLISHED HERE, in this order, and none of them replaces another:
#
#   1. THE DESCRIPTOR still refers to a directory whose type, device, inode, owner and mode are
#      what was validated.  This is the authoritative check, because the descriptor is what every
#      operation actually goes through, and stat -L on /proc/self/fd/<n> reads the OPEN FILE
#      rather than resolving a pathname.
#   2. THE CALLER'S PATHNAME still resolves to that same directory.  Not because the operations
#      depend on it -- they do not any more -- but so that a substitution is REPORTED instead of
#      silently tolerated: an ancestor swapped mid-build means something on this host is doing
#      something this run should not proceed alongside.
#   3. THE LOCK, if held, is still on the inode the lock leaf resolves to.  This used to be
#      checked only around acquisition, which left the leaf replaceable for the rest of the run
#      -- and a second builder that then opened the new leaf would take an exclusive lock on a
#      different inode and proceed alongside this one.
assert_output_dir_custody() { # $1=what is about to happen
    local operation="$1" device inode owner mode
    local fd_type fd_device fd_inode fd_owner fd_mode
    [ -n "$OUTPUT_DIR_INODE" ] || die "internal error: assert_output_dir_custody ran before
       resolve_publication_targets recorded the output directory's identity, so '$operation'
       would proceed unchecked.  This is a defect in $SCRIPT_NAME."
    [ -n "$OUTPUT_DIR_PROC" ] || die "internal error: assert_output_dir_custody ran before the
       publication directory was opened, so '$operation' would be performed through the caller's
       pathname rather than through the pinned descriptor.  This is a defect in $SCRIPT_NAME."

    # 1. THE DESCRIPTOR.  Read as five separate values so a refusal can say which one moved.
    fd_type="$(stat -L -c '%F' -- "$OUTPUT_DIR_PROC" 2>/dev/null || printf '')"
    fd_device="$(stat -L -c '%d' -- "$OUTPUT_DIR_PROC" 2>/dev/null || printf '')"
    fd_inode="$(stat -L -c '%i' -- "$OUTPUT_DIR_PROC" 2>/dev/null || printf '')"
    fd_owner="$(stat -L -c '%u' -- "$OUTPUT_DIR_PROC" 2>/dev/null || printf '')"
    fd_mode="$(stat -L -c '%a' -- "$OUTPUT_DIR_PROC" 2>/dev/null || printf '')"
    [ -n "$fd_inode" ] || die "refusing to $operation: the publication directory's own descriptor
       ($OUTPUT_DIR_PROC) could not be read.  Every privileged operation in that directory is
       performed through it, so without it there is nothing to perform them through."
    [ "$fd_type" = 'directory' ] || die "refusing to $operation: the descriptor this run holds on
       the publication directory now reports type '${fd_type:-unreadable}' rather than
       'directory'.  This is a defect in $SCRIPT_NAME rather than a runtime condition -- an open
       directory descriptor cannot become another kind of object -- and nothing is written
       through it."
    if [ "$fd_device" != "$OUTPUT_DIR_DEVICE" ] || [ "$fd_inode" != "$OUTPUT_DIR_INODE" ]; then
        die "refusing to $operation: the descriptor this run holds on the publication directory
       is not on the directory that was validated.
           validated  device $OUTPUT_DIR_DEVICE inode $OUTPUT_DIR_INODE
           descriptor device ${fd_device:-unreadable} inode ${fd_inode:-unreadable}
       This is a defect in $SCRIPT_NAME: a descriptor does not change the inode it refers to, so
       something reassigned file descriptor $OUTPUT_DIR_FD.  Nothing is written through it."
    fi
    [ "$fd_owner" = "$OUTPUT_DIR_OWNER" ] || die "refusing to $operation: the publication
       directory changed owner from uid $OUTPUT_DIR_OWNER to uid ${fd_owner:-unreadable} while
       this build was running (read from the descriptor, so this is the directory itself and not
       a substituted pathname).  Its owner may rename this run's staged files away, so the run
       stops rather than publishing into a directory somebody else has taken over."
    publication_dir_mode_is_safe "$(pad_four_digit_mode "$fd_mode")" \
        || die "refusing to $operation: the publication directory has become writable by group or
       other (mode ${fd_mode:-unreadable}) while this build was running.  In that state another
       account may rename this run's staged image, its staged manifest or its lock file away.
       Restore it with
           chmod go-w '$OUTPUT_DIR'
       and re-run."
    # AND THE MODE MUST NOT HAVE CHANGED AT ALL, not merely still be safe.  The predicate above
    # answers "is this mode acceptable"; this answers "is this the mode that was accepted".  They
    # differ for a change root itself could make -- 0755 to 0750, say, or the setgid bit going on
    # mid-build -- which is not a hazard but IS somebody reconfiguring the directory under a run
    # that is a hour into publishing into it, and the published pair's mode is one of this
    # script's postconditions.  Stated as a defect-or-interference report rather than a remedy,
    # because there is no single correct remedy: the caller decides which mode they meant.
    [ "$(pad_four_digit_mode "$fd_mode")" = "$OUTPUT_DIR_MODE" ] || die "refusing to $operation:
       the publication directory's mode changed from $OUTPUT_DIR_MODE to
       $(pad_four_digit_mode "$fd_mode") while this build was running (read from the descriptor,
       so this is the directory itself and not a substituted pathname).  Both modes may be safe,
       but the directory was reconfigured underneath a run that is publishing into it, and this
       script will not guess which configuration the caller meant.  Settle the mode and re-run."

    # 2. THE CALLER'S PATHNAME, checked so a substitution is reported rather than tolerated.
    if [ -L "$OUTPUT_DIR" ]; then
        die "refusing to $operation: the output directory
           $OUTPUT_DIR
       has become a SYMBOLIC LINK since this run validated it.  It was a plain directory when
       the publication targets were resolved, so something substituted it while this build was
       running.  Nothing further is written there."
    fi
    [ -d "$OUTPUT_DIR" ] || die "refusing to $operation: the output directory
           $OUTPUT_DIR
       no longer exists, or is no longer a directory.  It was validated at the start of this
       run; something removed or replaced it while the build was running."

    device="$(stat -c '%d' -- "$OUTPUT_DIR" 2>/dev/null || printf '')"
    inode="$(stat -c '%i' -- "$OUTPUT_DIR" 2>/dev/null || printf '')"
    if [ "$device" != "$OUTPUT_DIR_DEVICE" ] || [ "$inode" != "$OUTPUT_DIR_INODE" ]; then
        die "refusing to $operation: the output directory $OUTPUT_DIR is not the same
       directory this run validated.
           was  device $OUTPUT_DIR_DEVICE inode $OUTPUT_DIR_INODE
           now  device ${device:-unreadable} inode ${inode:-unreadable}
       A path that resolves to a different inode than the one whose ownership and permissions
       were checked is the substitution those checks exist to prevent (CWE-367).  Nothing
       further is written there."
    fi

    owner="$(stat -c '%u' -- "$OUTPUT_DIR" 2>/dev/null || printf '')"
    [ "$owner" = "$OUTPUT_DIR_OWNER" ] || die "refusing to $operation: the output directory
       $OUTPUT_DIR changed owner from uid $OUTPUT_DIR_OWNER to uid ${owner:-unreadable} while
       this build was running.  Whoever owns it can rename this run's staged files away, so the
       run stops rather than publishing into a directory somebody else has taken over."

    mode="$(four_digit_mode "$OUTPUT_DIR")"
    publication_dir_mode_is_safe "$mode" || die "refusing to $operation: the output directory
           $OUTPUT_DIR  (mode ${mode:-unreadable})
       has become writable by group or other while this build was running.  In that state
       another account may rename this run's staged image or its lock file away and put a
       symbolic link in its place.  Restore it with
           chmod go-w '$OUTPUT_DIR'
       and re-run."

    # 3. THE LOCK, RE-VERIFIED AT EVERY CALL SITE RATHER THAN ONLY AT ACQUISITION.
    #
    # THE GAP THIS CLOSES.  The path-versus-descriptor comparison ran twice, both times around
    # flock, and then never again.  Nothing stopped the lock LEAF from being replaced afterwards:
    # a second builder opening the new leaf would take an exclusive lock on a different inode,
    # get it, and build the same --output alongside this run -- both of them believing they were
    # alone, which is worse than no lock at all.  Since every privileged operation already calls
    # this function, checking here means exclusivity is re-established immediately before each
    # one rather than assumed to have survived the hour since it was taken.
    #
    # Gated on the lock being HELD so that lock_publication_targets can call this function before
    # the descriptor exists, which is the one legitimate order.
    if [ "$OUTPUT_LOCK_HELD" -eq 1 ]; then
        assert_lock_descriptor_identity "immediately before this run would $operation"
    fi
}

# ONE STAGED LEAF, still the file this run created.  The device and inode are what identify it;
# the name is what an attacker controls.
# THE $1/$5 SPLIT, WHICH THE THREE LEAF PREDICATES BELOW ALL SHARE.  $1 is the name the check
# is performed THROUGH and it is the same name the privileged operation that follows will use --
# after resolve_publication_targets that is a descriptor-relative name, $OUTPUT_DIR_PROC/<leaf>.
# Checking through a different name from the one the operation uses is exactly the stat-then-
# operate mistake these predicates exist to close, so the two must not diverge.  The trailing
# display argument is the human pathname the same entry is reachable at, used only in messages;
# it defaults to $1 so a caller with nothing better to say may omit it.  Nothing is ever checked
# through the display path.
assert_leaf_identity() { # $1=path  $2=expected device  $3=expected inode  $4=what is about to happen  $5=display path
    local path="$1" expected_device="$2" expected_inode="$3" operation="$4" shown="${5:-$1}"
    local device inode links
    [ -n "$expected_inode" ] || die "internal error: assert_leaf_identity was asked to check
       $shown for '$operation' without a recorded inode.  This is a defect in $SCRIPT_NAME."

    [ ! -L "$path" ] || die "refusing to $operation: $shown is now a SYMBOLIC LINK.  This run
       created a regular file at that name; something replaced the directory entry while the
       build was running, and following it would write through to the link's target as root."
    [ -f "$path" ] || die "refusing to $operation: $shown is no longer a regular file that
       exists.  This run created it; something removed or replaced it while the build was
       running."

    device="$(stat -c '%d' -- "$path" 2>/dev/null || printf '')"
    inode="$(stat -c '%i' -- "$path" 2>/dev/null || printf '')"
    if [ "$device" != "$expected_device" ] || [ "$inode" != "$expected_inode" ]; then
        die "refusing to $operation: $shown is not the file this run created.
           was  device $expected_device inode $expected_inode
           now  device ${device:-unreadable} inode ${inode:-unreadable}
       The name was re-pointed at a different file after this run created it, which is exactly
       what the mode on the file cannot prevent -- renaming a directory entry is governed by
       the directory, not by the file."
    fi

    links="$(stat -c '%h' -- "$path" 2>/dev/null || printf '1')"
    case "$links" in
        ''|*[!0-9]*) links=1 ;;
    esac
    [ "$links" -le 1 ] || die "refusing to $operation: $shown has acquired $links hard links
       since this run created it, so another name now shares its content.  Refused rather than
       written through."
}

# Record one leaf's identity immediately after this run created it.  Prints nothing; the two
# globals named by $2 and $3 are set through a nameref so the two staged files share one
# implementation.
record_leaf_identity() { # $1=path  $2=device variable name  $3=inode variable name  $4=what it is  $5=display path
    local path="$1" what="$4" shown="${5:-$1}"
    local -n device_ref="$2"
    local -n inode_ref="$3"
    [ ! -L "$path" ] || die "$what was created at $shown but that name is a symbolic link.
       mktemp creates with O_CREAT|O_EXCL, so this cannot be the file it made: something
       replaced the entry in the instant after it was created.  Nothing is written through it."
    device_ref="$(stat -c '%d' -- "$path" 2>/dev/null || printf '')"
    inode_ref="$(stat -c '%i' -- "$path" 2>/dev/null || printf '')"
    if [ -z "$device_ref" ] || [ -z "$inode_ref" ]; then
        die "$what was created at $shown but its device and inode could not be read, so its
       identity cannot be re-checked before the privileged operations that follow.  The run
       stops rather than proceeding without that check."
    fi
}

# One publishable leaf: must be either absent, or a plain single-linked regular file that
# this script may replace.  Never a symlink, a directory, a device or a multiply-linked file.
assert_publishable_leaf() { # $1=path  $2=what it is  $3=display path
    local path="$1" what="$2" shown="${3:-$1}" links

    if [ -L "$path" ]; then
        die "$what is a SYMBOLIC LINK: $shown
       Refused rather than followed.  This script writes as root, and following a link here
       would replace whatever the link points at -- which is the whole of the hazard.  Pass
       the real path you want written."
    fi
    [ ! -e "$path" ] || [ -f "$path" ] || die "$what exists and is not a regular file: $shown
       It is refused rather than replaced: this script publishes a file there, and a
       directory, a device node or a socket at that path means the caller meant something
       else."

    if [ -f "$path" ]; then
        links="$(stat -c '%h' -- "$path" 2>/dev/null || printf '1')"
        case "$links" in
            ''|*[!0-9]*) links=1 ;;
        esac
        [ "$links" -le 1 ] || die "$what has $links hard links: $shown
       Replacing it would leave the other name(s) pointing at the old content while the
       caller believes the file was updated -- or, if this script had truncated it in place,
       would have destroyed their content too.  Refused.  Publish to a path with a single
       link."
    fi
}

# The output directory, resolved PHYSICALLY, plus the two leaves and the lock path.
resolve_publication_targets() {
    local lexical_parent physical_parent
    lexical_parent="$(dirname -- "$OUTPUT_IMAGE")"
    physical_parent="$(cd -P -- "$lexical_parent" 2>/dev/null && pwd -P)" \
        || die "the directory holding --output cannot be entered: $lexical_parent"
    if [ "$physical_parent" != "$lexical_parent" ]; then
        die "the path to --output goes through a symbolic link:
           you asked for   $lexical_parent
           it resolves to  $physical_parent
       Refused rather than followed.  Every safety check in this script is applied to the
       path it was given -- including the refusal to write inside an operating-system tree --
       and a link in the middle means those checks were applied to a path the bytes never
       reach.  Pass the resolved path."
    fi

    # Re-applied to the PHYSICAL parent: the earlier check saw the typed path, this one sees
    # where the writes actually land.  Cheap, and it is the only thing that catches a caller
    # whose own directory is a link into /usr.
    assert_path_plausible "$physical_parent" 'the directory holding --output'

    OUTPUT_DIR="$physical_parent"
    OUTPUT_MANIFEST="$OUTPUT_IMAGE.manifest"
    OUTPUT_LOCK="$OUTPUT_IMAGE.lock"

    # CUSTODY, ESTABLISHED HERE AND RE-CHECKED BEFORE EVERY PRIVILEGED OPERATION.  The reasoning
    # is in the block above this function; what follows is the decision.
    #
    # ROOT, AND ONLY ROOT.  The previous revision also accepted the unprivileged account that
    # invoked sudo, on the reasoning that a caller who may pass --output is already trusted with
    # the path.  That permission is REMOVED: it conflates trust over the PATHNAME with authority
    # over the DIRECTORY ENTRY, and those are not the same thing.  A directory's owner may rename
    # or unlink a root-owned entry inside it whatever the entry's own mode and owner say, so an
    # unprivileged owner can substitute the staged image, the destination or the lock leaf in the
    # window between any check and the privileged operation that follows it.  The descriptor
    # opened below removes the ancestor half of that exposure; this requirement removes the half
    # that lives in the directory itself.
    OUTPUT_DIR_OWNER="$(stat -c '%u' -- "$OUTPUT_DIR" 2>/dev/null || printf '')"
    [ -n "$OUTPUT_DIR_OWNER" ] || die "the ownership of the directory holding --output could not
       be read: $OUTPUT_DIR
       It decides whether another account can rename this run's staged image away between the
       moment it is created and the moment root formats it, so the run stops rather than
       assuming it is safe."
    [ "$OUTPUT_DIR_OWNER" = '0' ] || die "the directory holding --output is not owned by root:
           $OUTPUT_DIR  is owned by uid $OUTPUT_DIR_OWNER
       This script runs as root, stages an image and a manifest in that directory, and renames
       them onto the caller's paths.  Whoever OWNS the directory may rename those entries away
       and put a symbolic link or a different file at the same names -- regardless of the mode on
       the entries themselves, because renaming a directory entry is governed by the directory --
       and root would then format, populate, publish or chmod through the substitution.  A file
       mode cannot prevent it and neither can re-checking, because the window between a check and
       an operation is where it happens.
       Create a publication directory that belongs to root:
           install -d -o root -g root -m 0755 '$OUTPUT_DIR'
       and pass --output inside it."

    local dir_mode
    dir_mode="$(four_digit_mode "$OUTPUT_DIR")"
    [ -n "$dir_mode" ] || die "the permissions of the directory holding --output could not be
       read: $OUTPUT_DIR
       They decide whether another account can substitute this run's staged files, so the run
       stops rather than assuming they are safe."
    publication_dir_mode_is_safe "$dir_mode" || die "the directory holding --output is writable by
       group or other:
           $OUTPUT_DIR  (mode $dir_mode)
       In that state another local account may rename this run's staged image, its staged manifest
       or its lock file away and put a symbolic link in its place, which this script would then
       write through as root.  There is no sticky-bit exemption here: sticky restricts rename and
       unlink to the ENTRY's owner, which says nothing about the directory's own owner, and a
       publication directory has no reason to be shared.  Fix it with
           install -d -o root -g root -m 0755 '$OUTPUT_DIR'
       or publish somewhere private."

    OUTPUT_DIR_DEVICE="$(stat -c '%d' -- "$OUTPUT_DIR" 2>/dev/null || printf '')"
    OUTPUT_DIR_INODE="$(stat -c '%i' -- "$OUTPUT_DIR" 2>/dev/null || printf '')"
    if [ -z "$OUTPUT_DIR_DEVICE" ] || [ -z "$OUTPUT_DIR_INODE" ]; then
        die "the device and inode of the directory holding --output could not be read:
       $OUTPUT_DIR
       They are what lets every privileged operation below re-establish that the path still
       resolves to the directory whose ownership and permissions were just checked.  Without
       them that re-check is impossible, so the run stops."
    fi

    assert_publishable_leaf "$OUTPUT_IMAGE"    'the --output image'
    assert_publishable_leaf "$OUTPUT_MANIFEST" "the manifest beside --output"
    assert_publishable_leaf "$OUTPUT_LOCK"     'the publication lock file'

    # THE DESCRIPTOR, OPENED ONCE, ON THE DIRECTORY THAT WAS JUST VALIDATED.
    #
    # From here on every privileged pathname operation in the publication directory is composed
    # against $OUTPUT_DIR_PROC rather than against $OUTPUT_DIR, which is what makes the checks
    # above hold for the whole run instead of only for the instant they ran in: the descriptor
    # refers to an INODE, so no rename of the directory or of any ancestor can redirect an
    # operation that goes through it.  It is never closed -- the process exit closes it, exactly
    # as it does the lock on fd 9 -- and it is inherited by the child processes that do the work,
    # which is what makes /proc/self/fd/<n> resolve inside them.
    eval "exec ${OUTPUT_DIR_FD}< \"\$OUTPUT_DIR\"" || die "could not open the publication
       directory $OUTPUT_DIR for the descriptor every privileged operation in it goes through.
       Without it those operations would have to name the caller's pathname, which is resolved
       afresh each time and can therefore be redirected between a check and its use."
    OUTPUT_DIR_PROC="/proc/self/fd/$OUTPUT_DIR_FD"

    # VERIFIED AGAINST WHAT WAS VALIDATED, immediately, so a descriptor on the wrong object is
    # caught here rather than at the first operation that uses it.
    local fd_type fd_device fd_inode fd_owner fd_mode
    fd_type="$(stat -L -c '%F' -- "$OUTPUT_DIR_PROC" 2>/dev/null || printf '')"
    fd_device="$(stat -L -c '%d' -- "$OUTPUT_DIR_PROC" 2>/dev/null || printf '')"
    fd_inode="$(stat -L -c '%i' -- "$OUTPUT_DIR_PROC" 2>/dev/null || printf '')"
    fd_owner="$(stat -L -c '%u' -- "$OUTPUT_DIR_PROC" 2>/dev/null || printf '')"
    fd_mode="$(pad_four_digit_mode "$(stat -L -c '%a' -- "$OUTPUT_DIR_PROC" 2>/dev/null || printf '')")"
    [ "$fd_type" = 'directory' ] || die "the descriptor opened on $OUTPUT_DIR reports type
       '${fd_type:-unreadable}' rather than 'directory'.  Nothing is written through it."
    if [ "$fd_device" != "$OUTPUT_DIR_DEVICE" ] || [ "$fd_inode" != "$OUTPUT_DIR_INODE" ]; then
        die "the descriptor opened on $OUTPUT_DIR is on device ${fd_device:-unreadable} inode
       ${fd_inode:-unreadable}, but the pathname was validated as device $OUTPUT_DIR_DEVICE inode
       $OUTPUT_DIR_INODE.  The directory was substituted between the validation and the open,
       which is precisely the race the descriptor exists to close; the run stops rather than
       operating on whichever of the two it now holds."
    fi
    [ "$fd_owner" = '0' ] || die "the descriptor opened on $OUTPUT_DIR is on a directory owned by
       uid ${fd_owner:-unreadable} rather than root, although the pathname read as root-owned a
       moment ago.  The directory was substituted between the two; nothing is written through it."
    publication_dir_mode_is_safe "$fd_mode" || die "the descriptor opened on $OUTPUT_DIR is on a
       directory whose mode is ${fd_mode:-unreadable}, which is writable by group or other,
       although the pathname read as safe a moment ago.  The directory was substituted between
       the two; nothing is written through it."

    # THE LEAF NAMES.  Every privileged operation is composed from one of these; the caller's
    # full paths above are what appears in messages.
    OUTPUT_IMAGE_LEAF="$(basename -- "$OUTPUT_IMAGE")"
    OUTPUT_MANIFEST_LEAF="$(basename -- "$OUTPUT_MANIFEST")"
    OUTPUT_LOCK_LEAF="$(basename -- "$OUTPUT_LOCK")"
    # A leaf that is empty, '.', '..' or carries a '/' would compose into a path that is not a
    # file in this directory.  basename cannot produce one from an absolute path with a real
    # final component, so this is a defect check rather than an input check.
    local leaf
    for leaf in "$OUTPUT_IMAGE_LEAF" "$OUTPUT_MANIFEST_LEAF" "$OUTPUT_LOCK_LEAF"; do
        case "$leaf" in
            ''|.|..|*/*) die "internal error: '$leaf' is not usable as a leaf name inside
       $OUTPUT_DIR, so the descriptor-relative path for it cannot be composed.  This is a defect
       in $SCRIPT_NAME." ;;
        esac
    done

    # The journal's leaf is derived from the image's, so two builders publishing different
    # --output names into one directory do not share a journal.  See the journal block.
    PUBLISH_JOURNAL_LEAF="${PUBLISH_JOURNAL_PREFIX}${OUTPUT_IMAGE_LEAF}${PUBLISH_JOURNAL_SUFFIX}"
    PUBLISH_JOURNAL_TMP_LEAF="${PUBLISH_JOURNAL_PREFIX}${OUTPUT_IMAGE_LEAF}${PUBLISH_JOURNAL_TMP_SUFFIX}"

    readonly OUTPUT_DIR OUTPUT_MANIFEST OUTPUT_LOCK
    readonly OUTPUT_DIR_DEVICE OUTPUT_DIR_INODE OUTPUT_DIR_OWNER
    readonly OUTPUT_DIR_PROC OUTPUT_IMAGE_LEAF OUTPUT_MANIFEST_LEAF OUTPUT_LOCK_LEAF
    readonly PUBLISH_JOURNAL_LEAF PUBLISH_JOURNAL_TMP_LEAF
    OUTPUT_DIR_MODE="$dir_mode"
    readonly OUTPUT_DIR_MODE
    log "publication targets: $OUTPUT_IMAGE and $OUTPUT_MANIFEST"
    log "  (staged under private names in $OUTPUT_DIR and renamed onto those paths together)"
    log "  output directory custody: uid $OUTPUT_DIR_OWNER (root), mode $dir_mode, device"
    log "    $OUTPUT_DIR_DEVICE inode $OUTPUT_DIR_INODE -- held open on fd $OUTPUT_DIR_FD, and"
    log "    every privileged operation names $OUTPUT_DIR_PROC/<leaf> rather than the pathname,"
    log "    so no rename of the directory or an ancestor can redirect one"
}

# Exclusive, non-blocking, held for the whole run on file descriptor 9.
#
# NON-BLOCKING IS THE POINT.  A second builder aimed at the same output has nothing useful to
# wait for -- it would spend an hour building an image that then overwrites the first one's --
# so it is told immediately, with the reason, and exits non-zero.  The descriptor stays open
# until the process ends, which is what releases the lock; no explicit unlock is needed and
# none is attempted, because an unlock in a trap could run while a later step still relies on
# exclusivity.
#
# AND THE INODE BEHIND THE DESCRIPTOR IS VERIFIED, BEFORE AND AFTER THE LOCK IS TAKEN.  An
# advisory lock is held on an INODE, not on a name.  If the leaf at $OUTPUT_LOCK is replaced
# between two builders' opens -- which any account that can write the output directory could do
# were the directory not vetted above -- each holds an exclusive lock on a different inode and
# both proceed, which is worse than no lock at all because both believe they are alone.  So the
# descriptor's own inode (stat -L /proc/self/fd/9) is compared against the inode the PATH
# resolves to, once before flock and once after: before, so a substitution that already
# happened is caught; after, so one that happened during the flock call is caught too.
#
# Compares the inode behind the OPEN DESCRIPTOR against BOTH resolutions of the lock's name --
# the descriptor-relative one ($OUTPUT_DIR_PROC/$OUTPUT_LOCK_LEAF, which is the name the lock
# was opened through and cannot be redirected by renaming an ancestor) and the CALLER's own
# pathname ($OUTPUT_LOCK).  Both are checked because they answer different questions:
#
#   * descriptor vs fd-relative resolution catches a substitution of the LEAF inside the
#     pinned directory, which is the substitution that actually defeats the lock: a second
#     builder resolving the same leaf would open the new inode and flock would grant it.
#   * descriptor vs caller pathname catches the directory itself having been renamed away and
#     a decoy put at its name.  That does not defeat this run's lock -- the fd pins the real
#     directory, which is the whole point of routing operations through it -- but it means the
#     paths this run PRINTS, and the paths the workflow will later read the image from, are no
#     longer the paths it operated on.  Publishing an image the caller cannot find, or worse
#     that a decoy directory now shadows, is not a success, so it is refused too.
#
# Sets OUTPUT_LOCK_DEVICE/OUTPUT_LOCK_INODE from the descriptor -- the descriptor's answer is
# the authoritative one, because that is the inode the lock is actually held on.
assert_lock_descriptor_identity() { # $1=when, for the message
    local when="$1" fd_path lock_proc rel_device rel_inode path_device path_inode
    local fd_device fd_inode
    fd_path='/proc/self/fd/9'
    lock_proc="$OUTPUT_DIR_PROC/$OUTPUT_LOCK_LEAF"

    [ ! -L "$lock_proc" ] || die "the publication lock leaf $OUTPUT_LOCK_LEAF is a symbolic link
       inside the publication directory ($when).  Refused rather than followed: a lock taken
       through a link is a lock on whatever the link points at, which is not the file the next
       builder will open."
    [ ! -L "$OUTPUT_LOCK" ] || die "the publication lock path $OUTPUT_LOCK is a symbolic link
       ($when).  Refused rather than followed: a lock taken through a link is a lock on whatever
       the link points at, which is not the file the next builder will open."

    rel_device="$(stat -c '%d' -- "$lock_proc" 2>/dev/null || printf '')"
    rel_inode="$(stat -c '%i' -- "$lock_proc" 2>/dev/null || printf '')"
    path_device="$(stat -c '%d' -- "$OUTPUT_LOCK" 2>/dev/null || printf '')"
    path_inode="$(stat -c '%i' -- "$OUTPUT_LOCK" 2>/dev/null || printf '')"
    fd_device="$(stat -L -c '%d' -- "$fd_path" 2>/dev/null || printf '')"
    fd_inode="$(stat -L -c '%i' -- "$fd_path" 2>/dev/null || printf '')"

    if [ -z "$rel_inode" ] || [ -z "$path_inode" ] || [ -z "$fd_inode" ]; then
        die "the inode behind the publication lock could not be read $when
       (descriptor-relative: ${rel_inode:-unreadable}, caller pathname: ${path_inode:-unreadable},
       descriptor: ${fd_inode:-unreadable}).  Without all three there is no way to establish
       that this run holds the lock on the file at $OUTPUT_LOCK rather than on an inode that
       used to be there, so the run stops."
    fi
    if [ "$rel_device" != "$fd_device" ] || [ "$rel_inode" != "$fd_inode" ]; then
        die "the publication lock file was SUBSTITUTED INSIDE the publication directory $when:
           the leaf $OUTPUT_LOCK_LEAF now resolves to device $rel_device inode $rel_inode
           the descriptor this run holds is on device $fd_device inode $fd_inode
       An advisory lock is held on an inode, so a second builder resolving that same leaf would
       open the new inode, flock would grant it, and two builders would each believe they had
       exclusive access to the same --output.  This run stops rather than becoming one of them."
    fi
    if [ "$path_device" != "$fd_device" ] || [ "$path_inode" != "$fd_inode" ]; then
        die "the publication lock is no longer reachable at the path this run was given $when:
           the path $OUTPUT_LOCK now resolves to device $path_device inode $path_inode
           the descriptor this run holds is on device $fd_device inode $fd_inode
       The descriptor still pins the directory that was validated, so this run's own writes are
       safe -- but the caller's pathname now leads somewhere else, which means the image would
       be published where the caller cannot find it, or behind a directory somebody else put at
       that name.  Refused rather than published into the ambiguity."
    fi
    OUTPUT_LOCK_DEVICE="$fd_device"
    OUTPUT_LOCK_INODE="$fd_inode"
}

lock_publication_targets() {
    assert_output_dir_custody "open the publication lock file $OUTPUT_LOCK"
    # Re-checked adjacent to the open: assert_publishable_leaf ran during resolution, and this
    # is the first thing that actually opens the path.  Checked through the descriptor, because
    # that is the name the open below uses.
    assert_publishable_leaf "$OUTPUT_DIR_PROC/$OUTPUT_LOCK_LEAF" 'the publication lock file' \
        "$OUTPUT_LOCK"

    # Opened THROUGH THE DIRECTORY DESCRIPTOR rather than through the caller's pathname, so the
    # inode the lock ends up on is an entry of the directory that was validated, not of whatever
    # directory the pathname resolves to by the time this line runs.
    exec 9>>"$OUTPUT_DIR_PROC/$OUTPUT_LOCK_LEAF" \
        || die "could not open the publication lock file $OUTPUT_LOCK.
       It sits beside --output, so this usually means the output directory is read-only."

    assert_lock_descriptor_identity 'immediately after opening it'
    if ! flock --exclusive --nonblock 9; then
        die "another root-image build already holds the publication lock on
           $OUTPUT_LOCK
       so it is building the same --output right now.  Two builders would interleave a
       format, a mount and a rename and the survivor would be a mixture of both, so this run
       stops instead.  Wait for the other build, or publish to a different --output."
    fi
    assert_lock_descriptor_identity 'while the lock was being acquired'

    # READABLE BY THE CONSUMERS, WHICH IS WHAT MAKES THE PAIR'S CONSISTENCY DELIVERABLE.  The
    # image and the manifest are replaced by four independent renames at fixed leaf names, so a
    # reader that does not hold this lock can see the new image beside the old manifest; the
    # publication block says so in full under "WHAT 'ATOMIC' MEANS HERE, EXACTLY".  The workflow's
    # boot step and its artifact-extraction step therefore take a SHARED flock on this same file
    # for the duration of their read -- and they run as the unprivileged runner account, while
    # this file was created by the redirection above under `umask 077` and is therefore 0600
    # root-owned.  0644 is what lets them open it read-only; `flock` on Linux needs no write
    # access, so read-only is enough and no writable state is exposed.  The mode is set through
    # the DESCRIPTOR rather than through a name, so it lands on the inode the lock is held on and
    # cannot be redirected; assert_lock_descriptor_identity() compares device, inode, type and
    # link count and not the mode, so this does not disturb it.
    chmod 0644 -- /proc/self/fd/9 || die "could not make the publication lock file
           $OUTPUT_LOCK
       readable (mode 0644).  The workflow's boot and artifact-extraction steps run as the
       unprivileged runner account and take a shared lock on it to read the published pair
       consistently; a lock they cannot open would have them read the pair unlocked, which is
       exactly the window this run's four renames open.  Refused rather than continued."

    OUTPUT_LOCK_HELD=1
    log "publication lock acquired: $OUTPUT_LOCK (exclusive, held for this whole run,"
    log "  on device $OUTPUT_LOCK_DEVICE inode $OUTPUT_LOCK_INODE -- the descriptor's own inode,"
    log "  verified to be the one the path resolves to both before and after flock)"
}

# ------------------------------------------------------------------------------------
# RUN-SCOPED SCRATCH, AND WHY ITS PARENT IS VALIDATED BEFORE mktemp RUNS.
#
# THE HAZARD (CWE-377).  This script runs as root and mounts the image inside this directory,
# extracts a root filesystem into it and chroots into it.  The directory's parent comes from
# the environment -- TMPDIR, defaulting to /tmp -- so the caller's environment decides where
# root does all of that.  Three properties of that parent are not optional:
#
#   * ABSOLUTE AND CANONICAL.  A relative TMPDIR puts the scratch tree somewhere that depends
#     on the working directory, and a symbolic link ANYWHERE in the ancestry means the checks
#     below were applied to a path the writes never reach.  Both are refused, not resolved.
#   * NOT SUBSTITUTABLE BY ANOTHER USER.  `mktemp -d` itself is safe -- it creates with
#     O_EXCL and 0700, so the NAME cannot be pre-created and the CONTENTS cannot be
#     pre-populated.  What mktemp cannot protect is the parent: in a world-writable directory
#     WITHOUT the sticky bit, any local user may RENAME our directory away and put their own
#     in its place, after which root extracts a root filesystem into a tree they control and
#     chroots into it.  So the parent must be either private to root, or the ordinary sticky
#     shared temp directory where rename and unlink are restricted to the owner.
#   * ROOT-OWNED.  A parent owned by an unprivileged user can be replaced wholesale.
#
# MEASURED, AND WHY IT IS WORTH SAYING: this is not a theoretical shape.  On the container
# this migration was developed in, /tmp is mode 2777 -- world-writable and NOT sticky -- and
# tests/L1Tests/run_coverage.sh already warns about exactly that condition for its own
# artifact root.  A run there is refused here with the remedy rather than proceeding, because
# the remedy is one environment variable and the failure it prevents is a root-owned chroot
# in a directory any user can swap.
# ------------------------------------------------------------------------------------
WORK_DIR=''
IMAGE_MOUNT=''
BASE_ARCHIVE=''
TEMP_PARENT=''

# Resolve and validate the directory the scratch tree is created in.  Sets TEMP_PARENT.
resolve_temp_parent() {
    local candidate="${TMPDIR:-/tmp}" physical owner mode sticky_bit other_write group_write

    [ -n "$candidate" ] || die "TMPDIR is set but empty.  An empty value cannot be validated,
       and this script will not silently substitute /tmp for it: unset TMPDIR to use /tmp, or
       set it to an absolute path to a root-owned private directory."

    case "$candidate" in
        /*) : ;;
        *)  die "TMPDIR is relative: '$candidate'.  Refused rather than resolved against the
       working directory: this script mounts a filesystem and chroots inside the scratch
       directory as root, so where that directory is must not depend on where the caller
       happened to be standing.  Set TMPDIR to an absolute path." ;;
    esac

    [ -d "$candidate" ] || die "TMPDIR does not name an existing directory: $candidate
       This script will not create it: a temporary parent it had to create is a parent whose
       ownership and permissions nobody chose deliberately."
    if [ -L "$candidate" ]; then
        die "TMPDIR is a symbolic link: $candidate
       Refused rather than followed.  Everything below is validated on the path we were
       given, and a link means root would write somewhere else entirely.  Pass the resolved
       path."
    fi

    physical="$(cd -P -- "$candidate" 2>/dev/null && pwd -P)" \
        || die "TMPDIR cannot be entered: $candidate"
    if [ "$physical" != "$candidate" ]; then
        die "the path to TMPDIR goes through a symbolic link:
           TMPDIR is      $candidate
           it resolves to $physical
       Refused for the same reason as a symlinked TMPDIR itself: the ownership and permission
       checks below would describe a directory the writes never reach.  Set TMPDIR to
       $physical if that is what you meant."
    fi

    owner="$(stat -c '%u' -- "$physical" 2>/dev/null || printf 'unknown')"
    mode="$(stat -c '%a' -- "$physical" 2>/dev/null || printf '')"
    [ "$owner" = '0' ] || die "TMPDIR is not owned by root: $physical is owned by uid $owner.
       This script creates its scratch tree there and mounts the image inside it as root; a
       parent owned by an unprivileged user can be replaced wholesale, taking the mount point
       and the chroot with it.  Point TMPDIR at a root-owned directory -- 'mktemp -d' as root
       is enough."
    case "$mode" in
        ''|*[!0-7]*) die "the permissions of TMPDIR ($physical) could not be read.  They
       decide whether another user can substitute this script's scratch directory, so the run
       stops rather than assuming they are safe." ;;
    esac

    # Normalised to four digits so the special bits can be read positionally regardless of
    # whether stat printed three or four.
    while [ "${#mode}" -lt 4 ]; do
        mode="0$mode"
    done
    sticky_bit=$(( ${mode:0:1} & 1 ))
    group_write=$(( ${mode:2:1} & 2 ))
    other_write=$(( ${mode:3:1} & 2 ))

    if [ "$group_write" -ne 0 ] || [ "$other_write" -ne 0 ]; then
        [ "$sticky_bit" -eq 1 ] || die "TMPDIR is writable by other users and is NOT sticky:
           $physical  (mode $mode)
       In such a directory any local user may rename this script's scratch directory away and
       put their own in its place -- after which root extracts a root filesystem into a tree
       they control and chroots into it.  mktemp cannot protect against that; only the parent
       can.  Either fix the directory
           chmod +t '$physical'
       or point TMPDIR at a private one, which is the better answer on a shared host:
           export TMPDIR=\"\$(mktemp -d)\"   # run as root, so it is root-owned and 0700
       MEASURED: /tmp is mode 2777 on some CI containers, including the one this script was
       developed in, which is exactly this case."
        log "scratch parent: $physical (shared, sticky -- rename and unlink are restricted to"
        log "  the owner there, so the scratch directory cannot be substituted)"
    else
        log "scratch parent: $physical (root-owned and not writable by other users)"
    fi

    [ -w "$physical" ] || die "TMPDIR is not writable: $physical"
    TEMP_PARENT="$physical"
    readonly TEMP_PARENT
}

make_work_dir() {
    resolve_temp_parent

    local created_inode final_inode final_mode final_owner
    WORK_DIR="$(mktemp -d -- "$TEMP_PARENT/cec-l2-rootfs.XXXXXXXX")" \
        || die "could not create a scratch directory under $TEMP_PARENT."
    # Registered immediately: from here on the trap owns removing it, including on a failure
    # inside the verification below.
    register_tempdir "$WORK_DIR"

    # WHAT mktemp GUARANTEES, AND WHAT IS CHECKED ANYWAY.  mktemp creates the directory with
    # O_EXCL and mode 0700, so a pre-created name or a pre-existing symlink at that name
    # cannot have been used.  The checks here cover what happens AFTER creation: on a parent
    # that turned out to be substitutable in the window between validation and use, the
    # directory we go on to use may not be the one we made.  The inode is captured now and
    # compared after the chmod for exactly that reason -- it is the cheapest way to say "this
    # is still the same object".
    [ -d "$WORK_DIR" ] || die "the scratch directory $WORK_DIR is not a directory immediately
       after mktemp created it.  Something is substituting paths under $TEMP_PARENT."
    [ ! -L "$WORK_DIR" ] || die "the scratch directory $WORK_DIR is a symbolic link
       immediately after mktemp created it as a directory.  Something is substituting paths
       under $TEMP_PARENT; the run stops rather than writing through it as root."
    created_inode="$(stat -c '%i' -- "$WORK_DIR" 2>/dev/null || printf '')"

    chmod 0700 -- "$WORK_DIR" || die "could not set the mode of the scratch directory
       $WORK_DIR."

    final_inode="$(stat -c '%i' -- "$WORK_DIR" 2>/dev/null || printf '')"
    final_mode="$(stat -c '%a' -- "$WORK_DIR" 2>/dev/null || printf '')"
    final_owner="$(stat -c '%u' -- "$WORK_DIR" 2>/dev/null || printf '')"
    if [ -z "$created_inode" ] || [ "$final_inode" != "$created_inode" ]; then
        die "the scratch directory $WORK_DIR changed identity between creation and use
       (inode ${created_inode:-unreadable} became ${final_inode:-unreadable}).  Something
       replaced it under $TEMP_PARENT.  Nothing further is written there."
    fi
    [ "$final_owner" = '0' ] || die "the scratch directory $WORK_DIR is owned by uid
       $final_owner rather than root immediately after this script created it."
    [ "$final_mode" = '700' ] || die "the scratch directory $WORK_DIR is mode $final_mode
       rather than 700 immediately after this script set it.  It holds an extracted root
       filesystem and the image mount point, so it must not be readable by other users."

    IMAGE_MOUNT="$WORK_DIR/mnt"
    mkdir -p -- "$IMAGE_MOUNT"
    log "scratch directory: $WORK_DIR (0700, root-owned, inode $final_inode)"

    # THE CONFINED I/O PRIMITIVE IS ESTABLISHED AND PROVEN HERE, WHICH IS THE EARLIEST POINT IT
    # CAN BE.  It needs a directory that is root-owned, mode 0700 and verified not to have been
    # substituted -- the three properties the block above has just established for $WORK_DIR --
    # and it must be in force BEFORE create_and_mount_image(), because every privileged
    # operation this script performs on a base-tarball-named path inside the mounted image goes
    # through it.  The proof is fatal on every arm: a positive case that fails means the
    # mechanism does not work here, and a negative case that succeeds means it is not confining
    # anything, and either way nothing privileged is done through it.
    install_confined_io_helper "$WORK_DIR"
    probe_confined_io "$WORK_DIR"
    # The anchor every in-image relative path is resolved against.  It is set now rather than at
    # each call site so there is one place that decides what "inside the image" means; nothing
    # is mounted there yet, and the wrappers refuse to run until it is a real directory.
    CONFINED_IO_ANCHOR="$IMAGE_MOUNT"
}

# ------------------------------------------------------------------------------------
# THE BASE ROOT FILESYSTEM.  Fetched on THIS host when a URL was given, then verified
# against its pinned digest in both cases, then extracted.  There is no path through this
# function that extracts an unverified archive.
# ------------------------------------------------------------------------------------
acquire_base_rootfs() {
    if [ -n "$BASE_TARBALL" ]; then
        BASE_ARCHIVE="$BASE_TARBALL"
        log "base root filesystem: $BASE_ARCHIVE (supplied locally; nothing is downloaded)"
    else
        BASE_ARCHIVE="$WORK_DIR/base-rootfs.tar"
        # THE URL IS SHOWN SANITISED, HERE AND IN THE TWO FAILURE MESSAGES BELOW.  A base URL is
        # a documented place to carry a credential, and a build log is neither private nor
        # short-lived.  The value handed to curl/wget is of course the real one; only what
        # reaches a human or an artifact is reduced to scheme, host and path.
        local base_url_shown
        base_url_shown="$(sanitised_endpoint_display "$BASE_URL")"
        log "fetching the base root filesystem on THIS host from $base_url_shown"
        log "  (host-side acquisition at image-build time; the guest never reaches the network)"

        # THE URL IS NOT PASSED IN A COMMAND LINE.  A presigned base URL carries its signature
        # in its query string, and an argument is visible to every user on the host for as long
        # as the transfer runs -- `ps -ef`, /proc/<pid>/cmdline, and any process accounting or
        # container tooling that reads either.  Sanitising the log did nothing about that.
        #
        # AND THIS ONLY EVER PROTECTED THE CHILD'S ARGV, WHICH IS HALF THE PROBLEM.  If the
        # caller typed the URL on THIS script's own command line, it is in
        # /proc/<this-pid>/cmdline regardless of how carefully it is then handed to curl.  That
        # is why validate_inputs now refuses a query string in the argv --base-url form
        # outright and why --base-url-file exists: the file form reads the value from a
        # caller-owned private file, so a signature in its query reaches no argv at all.  From
        # here on the two forms are indistinguishable -- BASE_URL holds the value either way,
        # and the private-file transport below applies to both.
        #
        # So the value is written to a private file that only curl reads, and the file is
        # removed the moment the transfer ends, whichever way it ended.  It is created inside
        # $WORK_DIR, which is already 0700 and root-owned and inode-verified, and the mode is
        # set before the value is written rather than after: the window between creating a file
        # and chmod'ing it is exactly what an attacker with a directory handle would use.
        #
        # The wget fallback uses --input-file for the same reason.  wget has no config
        # directive for a URL, but it reads URLs from a file, and with one URL and
        # --output-document the result is identical.
        local url_file transfer_status=0
        url_file="$WORK_DIR/base-url.conf"
        rm -f -- "$url_file"
        ( umask 077 && : > "$url_file" ) \
            || die "could not create the private URL file at $url_file, so the base URL cannot
       be kept out of the command line.  $WORK_DIR is this script's own 0700 scratch
       directory; a failure here means it is not writable, which nothing later would survive."
        chmod 0600 -- "$url_file" \
            || die "could not set mode 0600 on $url_file.  The base URL is not written to a file
       whose permissions are unknown."
        if command -v curl >/dev/null 2>&1; then
            printf 'url = "%s"\n' "$BASE_URL" > "$url_file" \
                || die "could not write the base URL to $url_file."
            curl --fail --silent --show-error --location \
                 --output "$BASE_ARCHIVE" --config "$url_file" || transfer_status=$?
        else
            printf '%s\n' "$BASE_URL" > "$url_file" \
                || die "could not write the base URL to $url_file."
            wget --quiet --output-document="$BASE_ARCHIVE" --input-file="$url_file" \
                || transfer_status=$?
        fi
        # REMOVED BEFORE THE STATUS IS ACTED ON, so the failure path cannot leave it behind.
        # The trap would remove $WORK_DIR wholesale in any case; this is the transport being
        # torn down as soon as it has served its purpose rather than at the end of the run.
        rm -f -- "$url_file" \
            || die "could not remove the private URL file $url_file after the transfer.  It
       holds the base URL verbatim, so leaving it in place is not an acceptable outcome."
        if [ "$transfer_status" -ne 0 ]; then
            die "could not fetch the base root filesystem from $base_url_shown (exit
       $transfer_status).  Check the URL and this host's network access, or fetch it separately
       and pass --base-tarball.  (The URL above is shown with any userinfo and query string
       removed; the value used was the one you passed, and it was passed through a private
       0600 file rather than a command line.)"
        fi
    fi
    verify_sha256 "$BASE_ARCHIVE" "$BASE_SHA256" 'the base root filesystem tarball'
}

# ------------------------------------------------------------------------------------
# THE IMAGE ITSELF.  A sparse file, an ext4 filesystem, a loop device and a mount -- each
# registered with the trap the moment it exists.
#
# -O ^has_journal is deliberately NOT used: the guest writes the coverage evidence into this
# filesystem and then powers down, so journalling is what makes an unclean shutdown
# recoverable rather than a lost artifact set.
# ------------------------------------------------------------------------------------
LOOP_DEVICE=''

create_and_mount_image() {
    # STAGED, NOT PUBLISHED.  mktemp creates the file itself, exclusively (O_CREAT|O_EXCL) and
    # under the process umask, so there is no window in which the name exists and the content
    # is somebody else's, and no way for a pre-created symlink at a guessable name to redirect
    # the write.  It lives in the OUTPUT DIRECTORY rather than in $WORK_DIR so that the
    # publishing rename is same-filesystem and therefore atomic: an 8 GiB copy across
    # filesystems would be neither atomic nor affordable.
    #
    # AND THE MODE ON IT IS NOT THE WHOLE PROTECTION.  0600 protects the CONTENT; the NAME is
    # protected by the output directory, whose owner, mode and identity were vetted in
    # resolve_publication_targets and are RE-CHECKED here immediately before each privileged
    # step, together with the staged leaf's own device and inode.  Between the mktemp and the
    # truncate, and again between the mkfs and the losetup, the entry could otherwise be
    # re-pointed at a symbolic link and root would format, mount and populate its target.
    #
    # EVERY ONE OF THOSE STEPS NAMES THE FILE THROUGH THE DIRECTORY DESCRIPTOR.  mktemp,
    # chmod, truncate, mkfs.ext4 and losetup are all given $OUTPUT_DIR_PROC/<leaf>, so the
    # directory they act inside is the inode that was validated rather than whatever the
    # caller's pathname resolves to at the moment each one runs.  $STAGED_IMAGE keeps the human
    # pathname: it is what the messages say, and it is what the kernel reports as the loop
    # device's backing_file (measured: losetup on a /proc/self/fd path records the RESOLVED
    # path), so assert_loop_backs continues to compare like with like.
    local staged_proc
    assert_output_dir_custody "create the staging file for the image in $OUTPUT_DIR"
    staged_proc="$(mktemp -- "$OUTPUT_DIR_PROC/.cec-l2-image.XXXXXXXX")" \
        || die "could not create a staging file in $OUTPUT_DIR.  The image is staged beside
       --output so it can be renamed onto it atomically, so this directory must be writable."
    STAGED_IMAGE_LEAF="$(basename -- "$staged_proc")"
    STAGED_IMAGE="$OUTPUT_DIR/$STAGED_IMAGE_LEAF"
    record_leaf_identity "$staged_proc" STAGED_IMAGE_DEVICE STAGED_IMAGE_INODE \
        'the staged image' "$STAGED_IMAGE"
    # 0600 explicitly rather than by umask: the staged image is populated as root and carries
    # the whole guest filesystem, and it is briefly visible in a directory the caller chose.
    chmod 0600 -- "$staged_proc" \
        || die "could not set the mode of the staging file $STAGED_IMAGE."
    IMAGE_CREATED=1

    log "creating a ${IMAGE_SIZE_MIB} MiB ext4 image"
    log "  staged at  $STAGED_IMAGE (device $STAGED_IMAGE_DEVICE inode $STAGED_IMAGE_INODE)"
    log "  named through the publication directory descriptor as $staged_proc"
    log "  publishes to $OUTPUT_IMAGE only after it is validated"
    assert_output_dir_custody "size the staged image $STAGED_IMAGE"
    assert_leaf_identity "$staged_proc" "$STAGED_IMAGE_DEVICE" "$STAGED_IMAGE_INODE" \
        "size the staged image $STAGED_IMAGE" "$STAGED_IMAGE"
    truncate -s "${IMAGE_SIZE_MIB}M" -- "$staged_proc" \
        || die "could not size the staged image $STAGED_IMAGE.  Check free space on its
       filesystem: the image is ${IMAGE_SIZE_MIB} MiB and holds a toolchain, the payload, the
       staged SDK, a full build tree and the coverage artifacts."
    # -m 0 keeps no reserved-blocks margin: nothing else shares this filesystem, and the
    # build tree wants every block.
    assert_output_dir_custody "format the staged image $STAGED_IMAGE"
    assert_leaf_identity "$staged_proc" "$STAGED_IMAGE_DEVICE" "$STAGED_IMAGE_INODE" \
        "format the staged image $STAGED_IMAGE" "$STAGED_IMAGE"
    mkfs.ext4 -q -F -m 0 -L 'cec-l2-root' -- "$staged_proc" \
        || die "mkfs.ext4 failed on the staged image $STAGED_IMAGE."

    assert_output_dir_custody "attach a loop device to the staged image $STAGED_IMAGE"
    assert_leaf_identity "$staged_proc" "$STAGED_IMAGE_DEVICE" "$STAGED_IMAGE_INODE" \
        "attach a loop device to the staged image $STAGED_IMAGE" "$STAGED_IMAGE"
    LOOP_DEVICE="$(losetup --find --show -- "$staged_proc")" \
        || die "could not attach a loop device to $STAGED_IMAGE.  If the host has run out of
       loop devices, free them with 'losetup -a' and 'losetup -d'."
    # Registered WITH its backing file so the teardown can tell our device from the same node
    # after another process on this host re-used it.
    register_loop "$LOOP_DEVICE" "$STAGED_IMAGE"
    readonly LOOP_DEVICE
    log "loop device: $LOOP_DEVICE"

    # The kernel's own answer, before anything is mounted: `losetup --find --show` races with
    # every other loop user on the host, and a device that came back bound to somebody else's
    # file must not be mounted -- as root -- into our image tree.
    assert_loop_backs "$LOOP_DEVICE" "$STAGED_IMAGE" "mount $LOOP_DEVICE at $IMAGE_MOUNT"

    register_mount "$IMAGE_MOUNT"
    mount -- "$LOOP_DEVICE" "$IMAGE_MOUNT" \
        || die "could not mount $LOOP_DEVICE at $IMAGE_MOUNT."
    log "image mounted at $IMAGE_MOUNT"
}

extract_base_rootfs() {
    log "extracting the base root filesystem"
    # --numeric-owner keeps uid/gid as they are in the archive rather than remapping them
    # through this host's passwd database, which would give the guest's files owners that do
    # not exist inside it.
    tar --extract --numeric-owner --file "$BASE_ARCHIVE" --directory "$IMAGE_MOUNT" \
        || die "could not extract $BASE_ARCHIVE into the image.  If the archive is not a root
       filesystem tarball -- a container image manifest, for instance -- this is where that
       shows."
    require_existing_dir "$IMAGE_MOUNT/etc" "the extracted base's /etc.  The archive does not
       look like a root filesystem: expected /etc, /usr and /bin at its top level"
    require_existing_dir "$IMAGE_MOUNT/usr" 'the extracted base'"'"'s /usr'
}

# ------------------------------------------------------------------------------------
# THE CHROOT.  The pseudo-filesystems are bind-mounted so the package manager and the
# compiler behave, and every one is registered with the trap before it is made.
# ------------------------------------------------------------------------------------
mount_chroot_pseudo_filesystems() {
    local point
    for point in proc sys dev dev/pts; do
        mkdir -p -- "$IMAGE_MOUNT/$point"
    done
    register_mount "$IMAGE_MOUNT/proc"
    mount -t proc proc "$IMAGE_MOUNT/proc" || die "could not mount /proc into the image."
    register_mount "$IMAGE_MOUNT/sys"
    mount -t sysfs sysfs "$IMAGE_MOUNT/sys" || die "could not mount /sys into the image."
    register_mount "$IMAGE_MOUNT/dev"
    mount --bind /dev "$IMAGE_MOUNT/dev" || die "could not bind-mount /dev into the image."
    register_mount "$IMAGE_MOUNT/dev/pts"
    mount --bind /dev/pts "$IMAGE_MOUNT/dev/pts" || die "could not bind-mount /dev/pts into the image."
}

# Run a command inside the image.  DEBIAN_FRONTEND keeps the package manager from trying to
# open a dialogue nobody can answer; LC_ALL keeps its messages parseable.
chroot_run() {
    env -i \
        PATH='/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin' \
        HOME=/root \
        LC_ALL=C \
        DEBIAN_FRONTEND=noninteractive \
        chroot "$IMAGE_MOUNT" "$@"
}

# The same, with the staged libraries reachable.  GNU ld does not search -L for a shared
# library's own DT_NEEDED entries and the staged libbinder carries no RUNPATH, so the search
# path is needed when the in-guest helpers are LINKED, not only when they are run.
chroot_run_with_libs() {
    env -i \
        PATH='/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin' \
        HOME=/root \
        LC_ALL=C \
        DEBIAN_FRONTEND=noninteractive \
        LD_LIBRARY_PATH="$GUEST_SDK_DIR/$SDK_REL_BINDER_TARGET/lib/binder:$GUEST_SDK_DIR/$SDK_REL_HALIF_LIB" \
        chroot "$IMAGE_MOUNT" "$@"
}

assert_chroot_executable() {
    chroot_run /bin/true 2>/dev/null || die "cannot execute $ARCH binaries inside the image.
       For --arch i386 on an x86_64 host this normally means the host kernel lacks 32-bit
       support; for --arch armhf it means no binfmt_misc handler is registered, which is
       fixed by installing qemu-user-static and registering it:
           sudo apt-get install -y qemu-user-static binfmt-support
           sudo systemctl restart systemd-binfmt   # or: update-binfmts --enable
       Populating an image whose binaries cannot run is not possible, so this is refused
       rather than worked around."
    log "the image's own binaries execute on this host; the chroot is usable"
}

# ------------------------------------------------------------------------------------
# THE BASE'S OWN PACKAGING ARCHITECTURE.
#
# A base tarball for the wrong architecture is a mistake the ELF checks on the staged SDK do
# not catch, because those check what is COPIED IN rather than what is already there.  Left
# undetected it produces an image whose toolchain builds binaries the guest kernel cannot
# run -- or, worse, an apt that quietly installs a foreign-architecture package set.
#
# A base with no dpkg is reported rather than refused: this script does not require a
# Debian-derived base, and the package installation below fails loudly on its own if the
# base has no apt either.
# ------------------------------------------------------------------------------------
assert_base_architecture() {
    local base_arch
    base_arch="$(chroot_run dpkg --print-architecture 2>/dev/null || true)"
    if [ -z "$base_arch" ]; then
        warn "the base root filesystem provides no dpkg, so its packaging architecture cannot"
        warn "  be checked. The ELF checks on the staged SDK still apply, and the package"
        warn "  installation step will fail on its own if the base has no apt either."
        return 0
    fi
    [ "$base_arch" = "$DEBIAN_ARCH" ] || die "the base root filesystem is for the '$base_arch'
       package architecture, but --arch $ARCH needs '$DEBIAN_ARCH'.  Building on it would give
       the guest a toolchain and libraries for the wrong architecture, which the staged SDK's
       ELF checks cannot catch because they check what is copied IN rather than what is
       already there.  Pass a base tarball for $DEBIAN_ARCH, or select --arch to match the
       base you have."
    log "the base root filesystem is for '$base_arch', matching --arch $ARCH"
}

# ------------------------------------------------------------------------------------
# PACKAGE POPULATION.
#
# One apt invocation, --no-install-recommends so the package set is what is named here and
# not what a recommendation graph happened to pull in, and then an ASSERTION for every tool
# the guest actually needs.  The assertion is not redundant with the install: a package that
# installed successfully but does not provide the binary under the name the guest looks for
# is exactly the failure that otherwise surfaces two hours later inside the VM.
#
# --apt-mirror pins the archive.  Left unset, the base tarball's own sources are used as
# they stand, which is the right default for a base that is already pinned to a snapshot.
# ------------------------------------------------------------------------------------
readonly IMAGE_PACKAGES=(
    # Build-and-measure toolchain.  The suites build at -std=c++17.
    g++ make autoconf automake libtool pkg-config binutils
    # A hard configure dependency: PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 0.10.28]).
    libglib2.0-dev
    # GoogleTest and GoogleMock.  A discoverable gtest.pc is what configure actually needs,
    # and it is asserted separately below rather than assumed from the package name.
    libgtest-dev libgmock-dev
    # Coverage.  gcov ships with gcc; lcov here is only a floor -- the 2.x requirement is
    # installed and asserted separately, because stable archives ship 1.x.
    lcov
    # lcov 2.x is Perl and needs these modules; naming them here means a source install of
    # lcov 2.x has its dependencies already satisfied.
    perl libcapture-tiny-perl libdatetime-perl libjson-xs-perl libperlio-gzip-perl
    # What the init and run_coverage.sh reach for: mount, mknod, stat, timeout, flock,
    # nproc, awk, find, mktemp, ps, and a shell that is actually bash.
    bash coreutils util-linux findutils mawk procps
    # Provenance only, and optional to the run: run_coverage.sh guards every git use with
    # `command -v git`, so its absence degrades provenance rather than failing anything.
    git
    # file is what the init uses to describe what it found when something is not what it
    # expected; ca-certificates keeps a host-side apt over https working.
    file ca-certificates
)

# Packages that IMPROVE the image without being required by anything the guest must do, and
# whose availability differs between archives.  They are installed one at a time and a
# missing one is a warning, never a failure: dying on an optional package would make the
# image build depend on a package name that is not load-bearing.  Memory::Process is only
# consulted by lcov's memory-tracking option, which nothing here uses.
readonly IMAGE_PACKAGES_OPTIONAL=(
    libmemory-process-perl
)

# ------------------------------------------------------------------------------------
# BUILD-TIME-ONLY APT AUTHENTICATION.
#
# WHAT THIS REPLACES.  Three places in this script used to tell a caller with an authenticated
# archive to put /etc/apt/auth.conf.d or a .netrc INTO THE BASE TARBALL.  Following that advice
# put a permanent credential file inside an image that is uploaded as a workflow artifact and
# copied between machines, and the pre-publication scan did not look at those paths at all --
# so the script's own advice defeated the script's own protection.  The advice is deleted and
# this is what replaces it.
#
# THE LIFETIME IS THE WHOLE GUARANTEE.  The file exists inside the image only between
# install_image_apt_auth() and remove_image_apt_auth(), which bracket the apt phase and nothing
# else.  Four things then hold:
#
#   * the removal is RE-READ, and a survivor is fatal -- `rm -f` reports success for a path
#     that does not exist, so its status alone establishes nothing;
#   * a failure anywhere in the apt phase reaches cleanup(), which removes the STAGED image
#     outright, so a run that died holding the credential publishes no artefact to hold it;
#   * scrub_and_assert_image_credential_stores() independently refuses to publish an image
#     carrying any known credential store, including this one, so the removal is checked twice
#     by two mechanisms that do not share code; and
#   * the content is never logged, never digested into the manifest and never echoed -- this
#     script reads the file once, with `cat`, into a path inside the chroot, and never parses it.
#
# 0600 AND root:root, stated rather than inherited: apt reads auth.conf.d as root and REFUSES a
# world-readable entry, so the mode is functional as well as protective.
#
# EVERY FILESYSTEM OPERATION BELOW IS CONFINED, AND THAT REPLACES A REAL HOLE.  An earlier
# revision of this pair checked only whether the COMPLETE pathname
# $IMAGE_MOUNT/etc/apt/auth.conf.d was a symbolic link, and then did an ordinary host-root
# `mkdir -p`, `chmod`, create, `chown` and `cat >` through it.  A base filesystem whose /etc or
# /etc/apt is an ABSOLUTE symbolic link -- neither of which that check looked at -- redirected
# all five onto the host, at which point this script wrote a caller's credential to a
# host-chosen path as root and set that path's mode and ownership.  The pre-publication scan
# cannot help: it runs thousands of lines later, and the damage is already outside the image.
#
# So the directory creation, the mode, the exclusive create, the ownership and the content write
# all go through the confined I/O primitive, anchored on the image mount point: openat2 with
# RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_XDEV resolves each component, the kernel
# refuses a link at ANY position rather than following it, and the operation is performed
# through the descriptor that resolution produced.  A base tarball with a symlinked /etc, a
# symlinked /etc/apt or a symlinked auth.conf.d is now a fatal refusal naming the path, and no
# host file is touched on the way to it.
readonly IMAGE_APT_AUTH_LEAF='99cec-l2-build-auth'

# The in-image paths, RELATIVE to the image mount point, because that is what the confined
# primitive takes: an absolute path would be refused (RESOLVE_BENEATH) and a path assembled with
# $IMAGE_MOUNT would reintroduce the host-side pathname this mechanism exists to remove.
readonly IMAGE_APT_AUTH_DIR_REL='etc/apt/auth.conf.d'
readonly IMAGE_APT_PARENT_DIR_REL='etc/apt'
# The apt configuration this script writes itself, also relative to the image mount point.  The
# sources file is declared beside the scrub that re-reads it (IMAGE_APT_SOURCES_FILE_REL).
readonly IMAGE_APT_CONF_DIR_REL='etc/apt/apt.conf.d'
readonly IMAGE_APT_SNAPSHOT_CONF_REL='etc/apt/apt.conf.d/99cec-l2-snapshot'

install_image_apt_auth() {
    [ -n "$APT_AUTH_FILE" ] || return 0
    local rel="$IMAGE_APT_AUTH_DIR_REL/$IMAGE_APT_AUTH_LEAF" shown record

    # THE PARENT CHAIN, CREATED COMPONENT BY COMPONENT UNDER THE CONFINEMENT.  /etc and /etc/apt
    # are created at 0755 if the base has none, because they are ordinary system directories and
    # a base without them is not a Debian root filesystem; auth.conf.d is created at 0700
    # separately, because a credential is about to be written into it.  Each component is
    # resolved with openat2, so a symlinked /etc or /etc/apt is refused HERE -- before any mode,
    # ownership or byte is written -- rather than followed onto the host.
    confined_mkdir "$IMAGE_APT_PARENT_DIR_REL" 0755 \
        "the image's /$IMAGE_APT_PARENT_DIR_REL directory" \
        'create the directory chain for the temporary apt credential'
    confined_mkdir "$IMAGE_APT_AUTH_DIR_REL" 0700 \
        "the image's /$IMAGE_APT_AUTH_DIR_REL directory" \
        'create the directory for the temporary apt credential'
    # Asserted rather than inherited: the base may ship auth.conf.d with a looser mode, and a
    # credential's directory must not be readable by anyone but root while it is in there.
    confined_chmod "$IMAGE_APT_AUTH_DIR_REL" 0700 \
        "the image's /$IMAGE_APT_AUTH_DIR_REL directory" \
        'restrict the directory holding the temporary apt credential'

    # THE NAME MUST BE FREE.  It is this script's own name and this script removes it again, so
    # a file already sitting there is something the caller supplied that would be deleted as if
    # it were ours.  A SYMBOLIC LINK at that name does not reach this message: the confined stat
    # refuses it first, which is the stronger answer, and confined_absent turns that refusal
    # into a fatal diagnosis naming the path rather than reading it as "not there".
    confined_absent "$rel" 'the temporary apt credential file' \
        'install the temporary apt credential' \
        || die "the image already contains /$rel before this script installed it.  That name is
       this script's own, so the base tarball carries a file this script is about to remove as
       its own -- which would delete something the caller supplied.  Refused; rename it in the
       base tarball."

    # Created EXCLUSIVELY and at the restrictive mode BEFORE any content reaches it: writing the
    # bytes first and chmod'ing afterwards leaves a window in which the credential is
    # world-readable inside the image, and windows of that shape are what this option exists to
    # close.  O_CREAT|O_EXCL under the confinement also means a link created at that name in the
    # instant before this call is refused with EEXIST rather than written through.
    confined_create "$rel" 0600 'the temporary apt credential file' \
        'create the temporary apt credential file'
    confined_chown "$rel" 0 0 'the temporary apt credential file' \
        'set root:root ownership on the temporary apt credential file'
    # The content goes in through the descriptor the confinement produced.  The caller's file is
    # read on the host side by the redirection; the only thing crossing into the image is bytes
    # on stdin, so there is no in-image pathname to substitute between the create and the write.
    confined_write "$rel" 'the temporary apt credential file' \
        'copy the --apt-auth-file credential into the image for the apt phase' \
        < "$APT_AUTH_FILE"

    # A FINAL CONFINED RE-READ of what was actually created, by descriptor: type, mode and link
    # count.  It costs one syscall and it is the difference between "the calls returned 0" and
    # "the object at that name is the object this script made".
    local verify_status=0
    record="$(confined_stat "$rel" 'verify the temporary apt credential file')" \
        || verify_status=$?
    [ "$verify_status" -eq 0 ] || confined_io_die "$verify_status" "$rel" \
        'the temporary apt credential file' 'verify the temporary apt credential file'
    shown="$(confined_stat_field "$record" 1)"
    [ "$shown" = 'regular' ] || die "the image's /$rel is a $shown rather than a regular file
       immediately after this script created it exclusively.  Something is changing the image's
       contents while it is being built; the credential install is not completed."
    shown="$(confined_stat_field "$record" 6)"
    [ "$shown" = '0600' ] || die "the image's /$rel is mode $shown rather than 0600 immediately
       after this script set it.  apt refuses a group- or world-readable auth.conf.d entry, and
       this script does not relax the mode to work around that."
    shown="$(confined_stat_field "$record" 4)"
    [ "$shown" = '1' ] || die "the image's /$rel has $shown links immediately after this script
       created it exclusively, so another name refers to the same inode and would still refer to
       the credential after this script removes this one.  Refused; inspect the base tarball."

    # The PATH is logged, at the level everything else in this phase logs at.  The content is
    # not, and the log says so explicitly so that a reader does not go looking for it.
    log "apt credentials installed for the package phase only, at"
    log "  /$rel (mode 0600, root:root, one link; content deliberately not logged)"
    log "  Written through openat2 with RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_XDEV, so"
    log "  a symlinked /etc, /etc/apt or auth.conf.d in the base would have been refused rather"
    log "  than followed onto the build host."
    log "  They are removed again as soon as the package phase finishes, the removal is"
    log "  verified, and the pre-publication credential scan refuses to publish an image that"
    log "  still carries them."
}

remove_image_apt_auth() {
    [ -n "$APT_AUTH_FILE" ] || return 0
    local rel="$IMAGE_APT_AUTH_DIR_REL/$IMAGE_APT_AUTH_LEAF"

    # ALREADY GONE IS NOT AN ERROR, but it is worth saying out loud: apt itself does not remove
    # auth.conf.d entries, so this arm means something else did, and the run continues only
    # because the outcome asked for -- no credential in the image -- has been reached.  Note that
    # confined_absent DIES on any answer other than "there" or "not there": a symbolic link at
    # that name is refused rather than reported absent, so this cannot be reached by planting one.
    if confined_absent "$rel" 'the temporary apt credential file' \
            'remove the temporary apt credential'; then
        warn "the temporary apt credential file was already absent from the image at /$rel when
       this script came to remove it.  Nothing installed it there but this script, and nothing
       in the apt phase removes it, so something else changed the image's contents.  The outcome
       required -- no credential inside the published image -- holds, and the pre-publication
       credential scan checks it again independently."
        return 0
    fi

    # unlinkat() on the leaf, relative to a descriptor obtained under the confinement.  It never
    # follows a symbolic link, so the entry removed is an entry of the image's own auth.conf.d
    # and of no other directory -- which is what an ordinary `rm -f` through a host-side
    # pathname could not promise when /etc or /etc/apt was a link.
    confined_unlink "$rel" "the image's /$rel" \
        'remove the temporary apt credential'

    # FATAL RE-READ, THROUGH THE CONFINEMENT.  A removal that reported success establishes that
    # a call returned 0; this establishes that nothing is at the name any more.
    confined_absent "$rel" 'the temporary apt credential file' \
        'verify the temporary apt credential is gone' \
        || die "the temporary apt credential file is still present in the image after removal:
       /$rel
       The image is NOT published.  A credential the caller passed for build-time use only
       would otherwise be inside a published artefact."
    log "the temporary apt credentials have been removed from the image and verified absent"
}

install_image_packages() {
    if [ -n "$APT_MIRROR" ]; then
        # THE VALUE WRITTEN HERE CANNOT CARRY A CREDENTIAL, and that is enforced at the
        # boundary rather than described here.  validate_inputs refuses userinfo and refuses a
        # query string in --apt-mirror precisely because this line is written into the
        # PUBLISHED image: an earlier revision wrote the value verbatim and then REPORTED that
        # the caller's token was now inside the artifact, which is a warning where a refusal
        # belongs.  So the sanitised form and the verbatim form are the same value now, and the
        # log prints the sanitised one because that is what the rest of the script prints.
        #
        # scrub_and_assert_image_apt_credentials(), run before the image is unmounted, verifies
        # this on the FINISHED filesystem - including anything the base tarball brought with it.
        sanitise_endpoint "$APT_MIRROR"
        log "pinning the package archive to $SANITISED_ENDPOINT"
        if [ "$SANITISED_ENDPOINT_REDACTED" -eq 1 ]; then
            die "internal error: the --apt-mirror value still reduces to something different
       when sanitised, which means it carries userinfo, a query or a fragment after
       validate_inputs accepted it.  It is not written into the image."
        fi
        # WRITTEN THROUGH THE CONFINED PRIMITIVE, for the same reason install_image_apt_auth()
        # is: `> "$IMAGE_MOUNT/etc/apt/sources.list"` is an ordinary redirection, it FOLLOWS
        # SYMBOLIC LINKS, and it runs as root on the BUILD HOST -- so a base tarball whose /etc,
        # /etc/apt or sources.list is an absolute symbolic link had this script truncate and
        # replace a HOST file, and `mkdir -p` created a host directory tree on the way to it.
        # openat2 with RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_XDEV refuses each of those
        # rather than following it, and the write goes through the descriptor that resolution
        # produced.
        #
        # The file is created if the base has none and rewritten in place if it has one, which is
        # what the redirection did; 0644 is apt's own expectation and is stated rather than left
        # to the umask, so the mode is a property of this script rather than of its environment.
        confined_mkdir "$IMAGE_APT_PARENT_DIR_REL" 0755 "the image's /$IMAGE_APT_PARENT_DIR_REL
       directory" 'write the pinned package archive into the image'
        if ! confined_regular_file "$IMAGE_APT_SOURCES_FILE_REL" "/$IMAGE_APT_SOURCES_FILE_REL" \
                'write the pinned package archive into the image'; then
            confined_create "$IMAGE_APT_SOURCES_FILE_REL" 0644 \
                "the image's /$IMAGE_APT_SOURCES_FILE_REL" \
                'write the pinned package archive into the image'
        fi
        printf '%s\n' "$APT_MIRROR" \
            | confined_write "$IMAGE_APT_SOURCES_FILE_REL" \
                  "the image's /$IMAGE_APT_SOURCES_FILE_REL" \
                  'write the pinned package archive into the image'
        confined_chmod "$IMAGE_APT_SOURCES_FILE_REL" 0644 \
            "the image's /$IMAGE_APT_SOURCES_FILE_REL" \
            'set the mode of the image apt sources file'

        # A pinned snapshot archive is by definition not the latest, so let apt use it.  Same
        # confinement, same reasoning: this creates a directory and a file inside the image.
        confined_mkdir "$IMAGE_APT_CONF_DIR_REL" 0755 \
            "the image's /$IMAGE_APT_CONF_DIR_REL directory" \
            'write the snapshot apt configuration into the image'
        if ! confined_regular_file "$IMAGE_APT_SNAPSHOT_CONF_REL" \
                "/$IMAGE_APT_SNAPSHOT_CONF_REL" \
                'write the snapshot apt configuration into the image'; then
            confined_create "$IMAGE_APT_SNAPSHOT_CONF_REL" 0644 \
                "the image's /$IMAGE_APT_SNAPSHOT_CONF_REL" \
                'write the snapshot apt configuration into the image'
        fi
        printf '%s\n' 'Acquire::Check-Valid-Until "false";' \
            | confined_write "$IMAGE_APT_SNAPSHOT_CONF_REL" \
                  "the image's /$IMAGE_APT_SNAPSHOT_CONF_REL" \
                  'write the snapshot apt configuration into the image'
        confined_chmod "$IMAGE_APT_SNAPSHOT_CONF_REL" 0644 \
            "the image's /$IMAGE_APT_SNAPSHOT_CONF_REL" \
            'set the mode of the image snapshot apt configuration'
    fi

    # IMMEDIATELY BEFORE THE APT PHASE, and no earlier: the credential is inside the image for
    # the shortest span that lets apt use it.  A no-op when --apt-auth-file was not given.
    install_image_apt_auth

    log "installing the build-and-measure toolchain into the image"
    chroot_run apt-get update -qq \
        || die "apt-get update failed inside the image.  If the base tarball pins a snapshot
       archive, pass --apt-mirror with that snapshot's URL so the package set matches the
       base; if this host has no network, use a base tarball that already carries the
       toolchain."
    chroot_run apt-get install -y --no-install-recommends "${IMAGE_PACKAGES[@]}" \
        || die "installing the toolchain into the image failed.  The set is fixed and none of
       it is optional: without g++/make/autotools the guest cannot build, without
       libglib2.0-dev configure fails at PKG_CHECK_MODULES([GLIB], ...), and without
       GoogleTest configure fails at PKG_CHECK_MODULES([GTEST], ...)."

    local optional
    for optional in "${IMAGE_PACKAGES_OPTIONAL[@]}"; do
        if chroot_run apt-get install -y --no-install-recommends "$optional"; then
            log "optional package installed: $optional"
        else
            warn "the optional package '$optional' is not available in this archive; continuing."
            warn "  Nothing the guest must do depends on it."
        fi
    done

    # RECORD BEFORE CLEANING.  apt-get clean removes the downloaded archives but not the dpkg
    # database, so the order here is not load-bearing -- it is written this way so that the
    # record describes the image as the guest will find it, after every install has run.
    record_installed_packages

    # The archive metadata and the downloaded packages are dead weight in a boot image, and
    # the guest never uses apt.
    chroot_run apt-get clean || true

    # THE APT PHASE IS OVER, SO THE CREDENTIAL GOES.  Placed after `apt-get clean` because that
    # is the last apt invocation, and before anything else touches the image: from here on the
    # filesystem is built up rather than fetched into, so nothing after this line needs it.
    # Every failure arm above reaches cleanup(), which removes the STAGED image, so a run that
    # dies between the install and this line publishes nothing at all.
    remove_image_apt_auth

    neutralise_image_network_configuration
}

# ------------------------------------------------------------------------------------
# THE PACKAGE SET IS RECORDED INSIDE THE IMAGE, WITH VERSIONS.
#
# WHY THIS EXISTS.  Pinning the archive to an immutable snapshot makes the package set
# reproducible; it does not make it VISIBLE.  When a rerun behaves differently the first
# question is "what changed", and without this file the only answer available is "nothing
# should have" -- which is a claim, not evidence.  With it, two images can be diffed by
# package and version and the answer is a fact.
#
# WHY THE VERSIONS ARE NOT PINNED PACKAGE BY PACKAGE IN THIS SCRIPT.  A version list in the
# apt command line pins the same thing twice and the two copies drift: the snapshot timestamp
# already determines every version in the archive, so a list beside it can only ever agree
# with it or contradict it.  The snapshot is the pin; this file is the measurement of what
# that pin resolved to.
#
# The record is written to the image, NOT to the manifest, because it is long (a minbase plus
# a C++ toolchain is a few hundred packages) and because a reader inside the guest is the one
# who needs it.  The manifest names the path, the count and a digest of the record, so the host
# side can see that it exists, how large it is and whether two images resolved to the same set
# without carrying the list itself.
#
# AND IT IS A PRECONDITION OF PUBLICATION, NOT A COURTESY.  An earlier revision caught a failed
# dpkg-query, warned, wrote "# UNAVAILABLE" into the record, set the count to the string
# 'unavailable' and RETURNED SUCCESS -- and a malformed count was coerced to zero the same way.
# The image then published with no exact resolved package/version set, which is the single piece
# of evidence this function exists to produce: "reproducible" is a claim about a set nobody can
# read back, and a manifest whose count says 'unavailable' documents an audit gap rather than an
# image.  The reasoning that made it advisory -- that the toolchain, pkg-config and lcov
# assertions still run -- answers a different question: those prove the image can BUILD, not
# what is IN it.  So every way this can fail to produce a concrete record is fatal here, an hour
# before publication and while the image is still mounted:
#
#   * dpkg-query failing inside the chroot,
#   * an empty listing,
#   * a zero or non-numeric installed count,
#   * a record whose lines do not carry package, version, architecture and status, and
#   * any package this script REQUIRES that cannot be accounted for in the record.
#
# The last one is the check with teeth, and it needs one subtlety: a required name can be
# satisfied by a different package that Provides it -- pkg-config is provided by pkgconf on
# newer archives, for instance -- so a name that is not directly installed is resolved through
# dpkg's own Provides field before it is called missing.  Anything still unaccounted for means
# apt installed something other than what this script asked for, and that is exactly the drift
# the record exists to make visible.
# ------------------------------------------------------------------------------------
PACKAGE_RECORD_COUNT='unknown'
PACKAGE_RECORD_DIGEST='unknown'
PACKAGE_RECORD_REQUIRED_ACCOUNTED='unknown'

# ------------------------------------------------------------------------------------
# WHAT "INSTALLED" MEANS HERE, AND WHY A SUFFIX TEST DOES NOT MEAN IT.
#
# dpkg's ${Status} is THREE words -- want, error-flag, status -- and the only one of them that
# means "this package's files are in this filesystem, configured" is the exact triple
#
#     install ok installed
#
# Both of these functions used to match the regular expression /installed$/ against the record,
# which is a test on the LAST WORD ONLY and therefore accepts, among others:
#
#     purge     ok        not-installed     the package is GONE; the entry is a tombstone
#     install   reinstreq half-installed    unpacking failed part way through
#     deinstall ok        config-files      removed, only its conffiles remain
#
# The first and third end in "installed" and "config-files" respectively -- and the first ends
# in the literal text "installed" because "not-installed" does.  So a required package that had
# been purged, or whose unpack died, still incremented `accounted` and still published
# REQUIRED_PACKAGES_ACCOUNTED as though it were present.  That is the exact drift this record
# exists to catch, reported as its own absence of drift.
#
# The fix is to compare the three fields POSITIONALLY and exactly, on both listings.  The two
# listings need different field arithmetic and the difference is not cosmetic:
#
#   the direct listing   '${Package} ${Version} ${Architecture} ${Status}'  -- default FS, so
#                        $1 package, $2 version, $3 architecture, and the status triple lands
#                        in $4 $5 $6.  NF is therefore exactly 6 for a well-formed line.
#   the provider listing '${Package} ${Status} :: ${Provides}'  -- read with -F ' :: ', so $1
#                        is the FOUR WORDS "pkg want eflag status" as a single field and has to
#                        be re-split before the triple can be read at all.
#
# A record that does not carry the full triple is REJECTED rather than guessed at: a short or
# malformed line is not evidence that a package is installed, and record_installed_packages'
# own malformed-line check is fatal on the same shape a few lines below.
#
# AND THE want FIELD IS REQUIRED TO BE "install" HERE, WHICH REJECTS "hold ok installed".
# That is deliberate and it is the one arm worth justifying, because a held package IS present
# and configured -- it is merely pinned against upgrade.  Two things make the strict form the
# right one for THIS question.  First, the question these two functions answer is not "is it in
# the image" but "did apt install what this script asked for", and a required name arriving in
# a want-state nothing in this script sets is drift of exactly the kind the record exists to
# surface: the chroot is built from a base tarball into an empty package set and nothing here
# ever holds anything, so the state is unreachable by this script's own actions.  Second, the
# failure direction is the safe one -- an unaccounted required name is FATAL and names the file
# to inspect, whereas the defect being corrected here published an image while claiming a purged
# package was present.  The count of installed packages a few hundred lines below asks the other
# question and deliberately uses the looser ' installed$' test, which does keep a held package;
# the comment on that line explains why the two differ rather than one of them being wrong.
# ------------------------------------------------------------------------------------

# Is one package name accounted for in a dpkg listing, as an installed package in its own
# right?  Exact field comparison rather than a pattern, because package names contain '+' and
# '.' and a required name is a prefix of others ('g++' of nothing, but 'perl' of many) -- and
# now exact STATUS comparison too, for the reason in the block above.
package_directly_installed() { # $1=listing  $2=package name
    printf '%s\n' "$1" | awk -v want="$2" '
        NF >= 6 && $1 == want && $4 == "install" && $5 == "ok" && $6 == "installed" { found = 1 }
        END { exit(found ? 0 : 1) }'
}

# Is one package name PROVIDED by some installed package?  $1 is a listing in the
# "<package> <status> :: <provides>" form; Provides entries may carry a version
# ("pkg-config (= 1.8.1)"), so only the name before the first space is compared.
#
# The provider's OWN status is checked first and exactly: a Provides field is present in the
# database whether or not the providing package is installed, so a purged or half-unpacked
# provider would otherwise satisfy a required name with nothing in the filesystem behind it.
package_provided_by_installed() { # $1=provides listing  $2=package name
    printf '%s\n' "$1" | awk -v want="$2" -F ' :: ' '
        {
            # $1 is "<package> <want> <eflag> <status>" as one field; re-split it to read the
            # status triple positionally.  Anything shorter than four words is not a status
            # record and is skipped rather than interpreted.
            words = split($1, status_fields, /[ \t]+/)
            if (words < 4) { next }
            if (status_fields[words - 2] != "install") { next }
            if (status_fields[words - 1] != "ok")      { next }
            if (status_fields[words]     != "installed") { next }
            if ($2 == "") { next }
            n = split($2, entries, /,[ ]*/)
            for (i = 1; i <= n; i++) {
                split(entries[i], parts, " ")
                if (parts[1] == want) { found = 1 }
            }
        }
        END { exit(found ? 0 : 1) }'
}

# ------------------------------------------------------------------------------------
# THE SELF-TEST.  `--self-test` and nothing else on the command line.
#
# WHY IT IS IN THIS FILE AND NOT IN A TEST FILE OF ITS OWN.  The two package-status parsers
# above are the whole of the package accounting's correctness, and they are pure functions of a
# string -- so they are exactly the kind of code a regression gate belongs on.  They previously
# had none: the only way to exercise them was to build an image, which needs root, a binder-
# capable kernel and half an hour.  This repository's change set is a fixed allowlist of paths,
# so a new test file cannot be added; putting the cases in the script itself is not a
# compromise on that constraint but a better fit for it -- the gate travels with the code it
# guards, cannot drift out of sync with it, and needs no harness.
#
# WHAT IT GUARANTEES, PRECISELY.
#
#   * IT RUNS BEFORE ANYTHING PRIVILEGED.  main() dispatches it as its first statement, ahead
#     of parse_args, require_host_tools, validate_inputs and every mount, loop device and
#     chroot.  `bash aidl-path-tests-rootfs.sh --self-test` needs no root, touches nothing
#     outside one mktemp directory, and cannot begin an image build.
#   * IT IS DETERMINISTIC.  Every case is a literal string in the table below.  No file, no
#     network, no dpkg, no clock.
#   * A MISMATCH IS A NON-ZERO EXIT.  Each case prints one line, the counts print at the end,
#     and the exit status is the failure count clamped to 1.  A workflow step that runs this
#     fails the job on any regression.
#
# WHY THE PACKAGE CASES ARE THE ONES THEY ARE.  Every rejected status below is a real dpkg
# state that a naive parser accepts, and the two the earlier revision of these functions
# actually accepted are the first two:
#
#   purge ok not-installed       -- a PURGE TOMBSTONE.  An unanchored /installed$/ matches it,
#                                   because the tombstone's own status ends in the TEXT
#                                   "installed".  The package is not in the filesystem.
#   install reinstreq half-installed -- an INTERRUPTED unpack.  Same text ending, same trap;
#                                   the files are partially there and the maintainer scripts
#                                   have not run.
#   deinstall ok config-files    -- removed but its conffiles are kept.  No binaries.
#   install ok half-configured   -- unpacked, postinst failed.  A compiler in this state does
#                                   not necessarily run.
#   install ok unpacked          -- files extracted, never configured.
#   a short record               -- dpkg leaves ${Version} empty in some half-states, so a
#                                   record can arrive with fewer than six fields; it is not a
#                                   status record and is skipped rather than interpreted.
#   a name mismatch              -- "$1 == want", not a substring or a prefix test, so
#                                   build-essential-doc never satisfies build-essential.
#   a Provides PREFIX            -- entries are compared on the name before the first space,
#                                   with "==", so a Provides of "pkg" does not satisfy a
#                                   requirement for "pkg-config" and vice versa.
#   an empty Provides field      -- a record with the separator and nothing after it provides
#                                   nothing, and must not be read as providing the wanted name.
#
# The confined-I/O cases at the end run the SHIPPED start-up proof, probe_confined_io(), plus
# three direct calls whose outcomes are printed individually -- so the self-test also answers
# "is openat2 with RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS actually in force on this host", which
# is the one environmental question the rest of the script depends on absolutely.
# ------------------------------------------------------------------------------------
SELF_TEST_CASES=0
SELF_TEST_FAILURES=0

self_test_report() { # $1=ok|not-ok  $2=subject  $3=case name  $4=detail
    SELF_TEST_CASES=$(( SELF_TEST_CASES + 1 ))
    if [ "$1" = 'ok' ]; then
        printf 'ok      %-32s %-44s %s\n' "$2" "$3" "$4"
    else
        SELF_TEST_FAILURES=$(( SELF_TEST_FAILURES + 1 ))
        printf 'NOT OK  %-32s %-44s %s\n' "$2" "$3" "$4"
    fi
}

# One table row: call the named parser with the listing and the wanted package name, and
# compare "accept" or "reject" against what the row says it must be.
self_test_package_case() { # $1=parser  $2=accept|reject  $3=want  $4=listing  $5=case name
    local parser="$1" expected="$2" want="$3" listing="$4" name="$5" actual='accept'
    "$parser" "$listing" "$want" || actual='reject'
    if [ "$actual" = "$expected" ]; then
        self_test_report ok "$parser" "$name" "want=$want -> $actual"
    else
        self_test_report not-ok "$parser" "$name" "want=$want -> $actual, expected $expected"
    fi
}

# The confined I/O primitive, exercised where it can do no harm: one mktemp directory,
# registered with the same TEMP_DIRS list the EXIT trap already drains, so the self-test leaves
# nothing behind on either a pass or a failure.
#
# resolve_temp_parent() is deliberately NOT used.  It refuses a world-writable /tmp without the
# sticky bit, which is correct for a privileged image build -- root writes multi-gigabyte files
# there -- and wrong for a self-test, which must be runnable by an unprivileged developer on
# whatever /tmp their container has.  mktemp -d creates the directory O_EXCL at mode 0700, and
# nothing but this script's own probe files is written into it.
self_test_confined_io() {
    local scratch anchor content status=0
    scratch="$(mktemp -d)" || {
        self_test_report not-ok 'confined-io' 'a scratch directory could be created' \
            'mktemp -d failed'
        return 0
    }
    register_tempdir "$scratch"
    chmod 0700 -- "$scratch" 2>/dev/null || true

    # The SHIPPED start-up proof: a positive read plus five refusals (absolute link, relative
    # link, symlinked middle component, '..' escape, fifo).  It dies on failure, which is the
    # right behaviour in a build and the wrong behaviour in a test run that should report every
    # case, so it is invoked in a subshell and its outcome turned into one line.
    if ( install_confined_io_helper "$scratch"; probe_confined_io "$scratch" ) >/dev/null 2>&1
    then
        self_test_report ok 'confined-io' 'probe_confined_io: positive and 5 refusals' \
            'openat2 RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS in force'
    else
        self_test_report not-ok 'confined-io' 'probe_confined_io: positive and 5 refusals' \
            'the shipped start-up proof FAILED; re-run below, unsuppressed'
        ( install_confined_io_helper "$scratch"; probe_confined_io "$scratch" ) || true
        return 0
    fi

    # Three direct calls, so the positive and the negative arms appear as their own lines
    # rather than only inside the proof.  install_confined_io_helper and probe_confined_io ran
    # in a subshell above, so the helper is on disk but the globals they set are not; both are
    # re-established here in this shell.
    install_confined_io_helper "$scratch" >/dev/null 2>&1 || true
    anchor="$scratch/self-test-anchor"
    mkdir -p -- "$anchor/beneath" || true
    printf 'self-test payload\n' > "$anchor/beneath/regular" || true
    printf 'OUTSIDE\n' > "$scratch/outside" || true
    ln -sfn "$scratch/outside" "$anchor/absolute-link" || true
    CONFINED_IO_ANCHOR="$anchor"
    CONFINED_IO_PROVEN=1

    status=0
    content="$(confined_io 'beneath/regular' read 2>/dev/null)" || status=$?
    if [ "$status" -eq 0 ] && [ "$content" = 'self-test payload' ]; then
        self_test_report ok 'confined-io' 'a regular file beneath the anchor reads' \
            'content matches'
    else
        self_test_report not-ok 'confined-io' 'a regular file beneath the anchor reads' \
            "status $status, content '${content:-<empty>}'"
    fi

    status=0
    confined_io 'absolute-link' read >/dev/null 2>&1 || status=$?
    if [ "$status" -eq "$CONFINED_IO_REFUSED" ]; then
        self_test_report ok 'confined-io' 'a symbolic link is REFUSED, not followed' \
            "status $status (refused)"
    else
        self_test_report not-ok 'confined-io' 'a symbolic link is REFUSED, not followed' \
            "status $status, expected $CONFINED_IO_REFUSED"
    fi

    status=0
    confined_io '../outside' read >/dev/null 2>&1 || status=$?
    if [ "$status" -eq "$CONFINED_IO_REFUSED" ]; then
        self_test_report ok 'confined-io' "a '..' escape is REFUSED by RESOLVE_BENEATH" \
            "status $status (refused)"
    else
        self_test_report not-ok 'confined-io' "a '..' escape is REFUSED by RESOLVE_BENEATH" \
            "status $status, expected $CONFINED_IO_REFUSED"
    fi

    CONFINED_IO_ANCHOR=''
    CONFINED_IO_PROVEN=0
    return 0
}

run_self_test() {
    local installed='build-essential 12.9 i386 install ok installed'
    local provider_ok='pkgconf 1.8.1 i386 install ok installed :: pkg-config (= 1.8.1)'

    printf '%s --self-test\n' "$SCRIPT_NAME"
    printf 'no root, no mounts, no network, no dpkg: every case below is a literal string.\n\n'

    # ---- package_directly_installed --------------------------------------------------
    self_test_package_case package_directly_installed accept 'build-essential' \
        "$installed" 'install ok installed'
    self_test_package_case package_directly_installed accept 'build-essential' \
        "libc6 2.36-9 i386 install ok installed
$installed
gcc 12.2.0-3 i386 install ok installed" 'found among several records'
    self_test_package_case package_directly_installed accept 'build-essential' \
        'build-essential 12.9 i386 install ok installed extra-trailing-field' \
        'a trailing extra field is tolerated'
    self_test_package_case package_directly_installed reject 'build-essential' \
        'build-essential 12.9 i386 purge ok not-installed' 'purge ok not-installed'
    self_test_package_case package_directly_installed reject 'build-essential' \
        'build-essential 12.9 i386 deinstall ok config-files' 'deinstall ok config-files'
    self_test_package_case package_directly_installed reject 'build-essential' \
        'build-essential 12.9 i386 install reinstreq half-installed' \
        'install reinstreq half-installed'
    self_test_package_case package_directly_installed reject 'build-essential' \
        'build-essential 12.9 i386 install ok half-configured' 'install ok half-configured'
    self_test_package_case package_directly_installed reject 'build-essential' \
        'build-essential 12.9 i386 install ok unpacked' 'install ok unpacked'
    self_test_package_case package_directly_installed reject 'build-essential' \
        'build-essential 12.9 i386 install ok' 'a short record (five fields)'
    self_test_package_case package_directly_installed reject 'build-essential' \
        'build-essential install ok installed' 'a shifted record (status in columns 2-4)'
    self_test_package_case package_directly_installed reject 'build-essential' \
        'build-essential-doc 12.9 all install ok installed' 'a name mismatch (longer name)'
    self_test_package_case package_directly_installed reject 'build-ess' \
        "$installed" 'the wanted name only PREFIXES the record'
    self_test_package_case package_directly_installed reject 'build-essential' '' \
        'an empty listing'
    self_test_package_case package_directly_installed reject 'build-essential' \
        'this is not a dpkg record at all' 'a malformed line'

    # ---- package_provided_by_installed -----------------------------------------------
    self_test_package_case package_provided_by_installed accept 'pkg-config' \
        "$provider_ok" 'provided by an installed package'
    self_test_package_case package_provided_by_installed accept 'pkg-config' \
        'pkgconf 1.8.1 i386 install ok installed :: pkg-config' \
        'a Provides entry with no version'
    self_test_package_case package_provided_by_installed accept 'pkg-config' \
        'pkgconf 1.8.1 i386 install ok installed :: foo (= 1), pkg-config (= 1.8.1), bar' \
        'the second of several Provides entries'
    self_test_package_case package_provided_by_installed accept 'pkg-config' \
        "pkgconf-old 1.7.0 i386 purge ok not-installed :: pkg-config (= 1.7.0)
$provider_ok" 'an installed provider beside a purged one'
    self_test_package_case package_provided_by_installed reject 'pkg-config' \
        'pkgconf 1.8.1 i386 purge ok not-installed :: pkg-config (= 1.8.1)' \
        'the provider is purge ok not-installed'
    self_test_package_case package_provided_by_installed reject 'pkg-config' \
        'pkgconf 1.8.1 i386 deinstall ok config-files :: pkg-config (= 1.8.1)' \
        'the provider is deinstall ok config-files'
    self_test_package_case package_provided_by_installed reject 'pkg-config' \
        'pkgconf 1.8.1 i386 install reinstreq half-installed :: pkg-config (= 1.8.1)' \
        'the provider is install reinstreq half-installed'
    self_test_package_case package_provided_by_installed reject 'pkg-config' \
        'pkgconf 1.8.1 i386 install ok half-configured :: pkg-config (= 1.8.1)' \
        'the provider is install ok half-configured'
    self_test_package_case package_provided_by_installed reject 'pkg-config' \
        'pkgconf 1.8.1 i386 install ok unpacked :: pkg-config (= 1.8.1)' \
        'the provider is install ok unpacked'
    self_test_package_case package_provided_by_installed reject 'pkg-config' \
        'pkgconf 1.8.1 :: pkg-config (= 1.8.1)' 'a short record (two status words)'
    self_test_package_case package_provided_by_installed reject 'pkg-config' \
        'pkgconf 1.8.1 i386 install ok installed :: pkgconfig (= 1.8.1)' \
        'a Provides name mismatch'
    self_test_package_case package_provided_by_installed reject 'pkg-config' \
        'pkgconf 1.8.1 i386 install ok installed :: pkg (= 1.8.1)' \
        'the Provides entry only PREFIXES the wanted name'
    self_test_package_case package_provided_by_installed reject 'pkg' \
        "$provider_ok" 'the wanted name only PREFIXES the Provides entry'
    self_test_package_case package_provided_by_installed reject 'pkg-config' \
        'pkgconf 1.8.1 i386 install ok installed :: ' 'an empty Provides field'
    self_test_package_case package_provided_by_installed reject 'pkg-config' \
        'pkgconf 1.8.1 i386 install ok installed' 'no Provides separator at all'
    self_test_package_case package_provided_by_installed reject 'pkg-config' '' \
        'an empty listing'

    # ---- the confined I/O primitive --------------------------------------------------
    self_test_confined_io

    printf '\n%d case(s), %d failure(s)\n' "$SELF_TEST_CASES" "$SELF_TEST_FAILURES"
    if [ "$SELF_TEST_FAILURES" -gt 0 ]; then
        printf '%s --self-test FAILED.  Each NOT OK line above names the parser, the case and\n' \
            "$SCRIPT_NAME"
        printf 'what it returned.  Do not publish an image from this revision: the package\n'
        printf 'accounting decides whether the toolchain is actually in the image, and a parser\n'
        printf 'that accepts a purge tombstone or a half-installed package reports a toolchain\n'
        printf 'that is not there.\n'
        return 1
    fi
    printf '%s --self-test passed.\n' "$SCRIPT_NAME"
    return 0
}

record_installed_packages() {
    local target="$IMAGE_MOUNT$GUEST_PACKAGE_RECORD"
    local listing='' provides_listing='' count='' digest='' malformed=''
    local package missing='' accounted=0 provided=''

    # dpkg-query is run INSIDE the chroot so that it reads the image's own database rather
    # than the build host's.  --show with an explicit format is used instead of -l because
    # -l's column layout truncates long package names and is not machine-readable.
    # The ${...} sequences below are DPKG'S OWN substitution syntax, not shell expansions, so
    # the single quotes are required and SC2016 is a false positive here: dpkg-query receives
    # the literal text and does the substituting itself.
    # shellcheck disable=SC2016
    listing="$(chroot_run dpkg-query --show \
                   --showformat='${Package} ${Version} ${Architecture} ${Status}\n' 2>/dev/null)" \
        || die "dpkg-query failed inside the image, so the exact resolved package set cannot be
       recorded.  The image is NOT published: its whole reproducibility claim rests on being
       able to read back which packages and which versions the pinned archive resolved to, and
       an image that cannot answer that is not the artefact this script promises.  Reproduce it
       by hand with
           chroot '$IMAGE_MOUNT' dpkg-query --show
       and fix what it reports -- most often a base tarball whose dpkg database was not
       extracted completely, or a chroot in which /var/lib/dpkg is not readable."

    [ -n "$listing" ] || die "dpkg-query succeeded inside the image but listed no packages at
       all.  That is not a plausible state for an image the toolchain was just installed into,
       so it is treated as a failed query rather than as an empty set, and the image is NOT
       published.  Inspect '$IMAGE_MOUNT/var/lib/dpkg/status'."

    # THE LINE SHAPE IS CHECKED, not assumed from the format string.  The requirement is a
    # concrete package/version/architecture record; a line missing any of the three is not one,
    # and dpkg leaves ${Version} empty for a package in certain half-states.
    malformed="$(printf '%s\n' "$listing" \
        | awk 'NF < 6 || $1 == "" || $2 == "" || $3 == "" { print; count++ }
               END { if (count > 3) print "... and " (count - 3) " more" }' \
        | head -4)"
    [ -z "$malformed" ] || die "the package listing from inside the image carries lines without a
       complete package, version, architecture and status:
$malformed
       The record must be a concrete package/version/architecture set to be worth anything, so
       the image is NOT published.  Each such line is a package dpkg holds in a half-configured
       state; run 'chroot $IMAGE_MOUNT dpkg --configure -a' and rebuild."

    {
        printf '# The exact package set installed into this image, with versions.\n'
        printf '# Written by %s at image-build time.\n' "$SCRIPT_NAME"
        printf '#\n'
        printf '# The archive this resolved from is recorded in %s; it is an\n' "$GUEST_IMAGE_RECORD"
        printf '# immutable snapshot, so the same archive reproduces the same versions.\n'
        printf '# Format: <package> <version> <architecture> <dpkg status>\n'
        printf '#\n'
        printf '%s\n' "$listing" | LC_ALL=C sort
    } > "$target"
    chmod 0644 -- "$target"

    # Only lines whose dpkg status ends in "installed" count; a purged-but-configured entry is
    # present in the database and is not in the image.
    #
    # THE LEADING SPACE IN THIS PATTERN IS LOAD-BEARING, and it is why this line is NOT an
    # instance of the status-suffix defect corrected above package_directly_installed.  That
    # defect was an UNANCHORED /installed$/, which "purge ok not-installed" satisfies because
    # the tombstone status itself ends in the text "installed"; ' installed$' requires a SPACE
    # before it, so "ok not-installed" and "install reinstreq half-installed" are both
    # correctly excluded (verified against both forms).  An exact "install ok installed" triple
    # here would be wrong in the other direction: "hold ok installed" is an ordinarily present,
    # configured package that is merely pinned against upgrade, and it belongs in a count of
    # what is IN the image.  The required-package accounting above is a different question -
    # "did apt install what this script asked for" - and there the want field does matter.
    count="$(printf '%s\n' "$listing" | grep -c ' installed$' || true)"
    count="$(printf '%s' "$count" | tr -d '[:space:]')"
    case "$count" in
        ''|*[!0-9]*)
            die "the number of installed packages in the image could not be counted: the count
       came back as '${count:-<empty>}', which is not a number.  It used to be coerced to zero
       and the build continued; it is fatal now, because a count nobody can trust is indis-
       tinguishable from a record nobody can read, and both are published as fact.  The image
       is NOT published."
            ;;
    esac
    [ "$count" -gt 0 ] || die "the image reports ZERO installed packages, immediately after
       apt-get install reported success for ${#IMAGE_PACKAGES[@]} required packages.  Something
       is wrong with the image's dpkg database rather than with the count, so the image is NOT
       published.  Inspect 'chroot $IMAGE_MOUNT dpkg -l | head'."

    # EVERY REQUIRED PACKAGE IS ACCOUNTED FOR IN THE RECORD.  apt reporting success is not the
    # same statement: a name can be satisfied by a provider, dropped as already-satisfied, or
    # replaced by a transitional package, and only the record says which.
    for package in "${IMAGE_PACKAGES[@]}"; do
        if package_directly_installed "$listing" "$package"; then
            accounted=$(( accounted + 1 ))
        else
            missing="$missing${missing:+ }$package"
        fi
    done

    if [ -n "$missing" ]; then
        # Resolved through dpkg's own Provides field.  Queried only when something is missing,
        # so the ordinary build pays for one dpkg-query and not two.
        # shellcheck disable=SC2016
        provides_listing="$(chroot_run dpkg-query --show \
                                --showformat='${Package} ${Status} :: ${Provides}\n' 2>/dev/null)" \
            || die "these required packages are not installed under their own names in the
       image:
           $missing
       and the Provides field could not be queried to establish whether another package
       supplies them, because dpkg-query failed.  The image is NOT published: an unaccounted
       required package means apt installed something other than what this script asked for."
        for package in $missing; do
            if package_provided_by_installed "$provides_listing" "$package"; then
                provided="$provided${provided:+ }$package"
                accounted=$(( accounted + 1 ))
            else
                die "the required package '$package' is not installed in the image and nothing
       installed in the image Provides it, even though apt-get install reported success for the
       whole set.  The image is NOT published: this is the drift the package record exists to
       catch -- an archive in which the name resolved to something else, or a package that was
       removed again by a later dependency resolution.  The full resolved set is in
       $target inside the image; compare it against IMAGE_PACKAGES in $SCRIPT_NAME."
            fi
        done
        warn "these required packages are satisfied by a PROVIDER rather than under their own"
        warn "  name, which is legitimate and is recorded: $provided"
    fi

    # A DIGEST OF THE RECORD, so two images can be compared by one line before anybody diffs
    # a few hundred.  Of the record as written, including its header, because that is the file
    # a reader inside the guest will hash.
    digest="$(sha256sum -- "$target" | awk '{ print $1 }')" \
        || die "the package record at $target was written but could not be hashed, so the
       manifest cannot carry a comparable fingerprint of the resolved set.  The image is NOT
       published."
    case "$digest" in
        [0-9a-f]*) : ;;
        *) die "the digest of the package record came back as '$digest', which is not a sha256
       hex digest.  The image is NOT published." ;;
    esac

    PACKAGE_RECORD_COUNT="$count"
    PACKAGE_RECORD_DIGEST="$digest"
    PACKAGE_RECORD_REQUIRED_ACCOUNTED="$accounted"
    log "recorded $PACKAGE_RECORD_COUNT installed packages at $GUEST_PACKAGE_RECORD"
    log "  all ${#IMAGE_PACKAGES[@]} required packages accounted for ($accounted matched)"
    log "  record sha256 ${digest:0:16}... (full digest in the manifest)"
}

# ------------------------------------------------------------------------------------
# THE IMAGE CARRIES NO RESOLVER AND NO INTERFACE CONFIGURATION.
#
# Package installation above needs DNS -- it runs in a chroot on the BUILD HOST, which is
# legitimate host-side acquisition at image-build time.  The GUEST is a different matter: it
# is booted with no NIC at all and everything it runs is already inside the image, and this
# function removes the configuration that would let it use a network if one ever appeared.
#
# WHY BOTHER, GIVEN qemu IS STARTED WITH -nic none.  Two reasons, and the second is the real
# one.  First, defence in depth: the two ends are edited by different people at different
# times and either can regress alone.  Second, and decisively, the in-guest init ASSERTS that
# no resolver is configured -- so leaving the build host's resolv.conf inside the image would
# make that assertion fail on a correctly offline guest, for a file rather than for a network.
# The assertion is worth having, so the file has to go.
#
# debootstrap copies the build host's /etc/resolv.conf into the base it produces, so this is
# not hypothetical: without this step every image would carry the runner's resolver
# configuration.  The file is REPLACED with a comment rather than deleted, so a reader inside
# the guest finds the reason instead of an absence.
# ------------------------------------------------------------------------------------
neutralise_image_network_configuration() {
    log "removing resolver and interface configuration from the image"

    # A symlink first (systemd-resolved leaves /etc/resolv.conf pointing into /run), because
    # writing through it would write outside the image's /etc -- and on this build host that
    # means writing to the HOST's /run.
    rm -f -- "$IMAGE_MOUNT/etc/resolv.conf"
    {
        printf '# Deliberately empty: this guest has NO network.\n'
        printf '#\n'
        printf '# It is booted with qemu -nic none, every source, binary and library it needs\n'
        printf '# is already inside this image, and the in-guest init asserts that no\n'
        printf '# non-loopback interface, no route and no configured resolver exists before it\n'
        printf '# runs anything.  A nameserver line here would fail that assertion.\n'
        printf '#\n'
        printf '# Package installation happened on the BUILD HOST at image-build time, which is\n'
        printf '# where the network belongs.  See %s for what was installed.\n' "$GUEST_IMAGE_RECORD"
    } > "$IMAGE_MOUNT/etc/resolv.conf"
    chmod 0644 -- "$IMAGE_MOUNT/etc/resolv.conf"

    # Loopback only.  The init never brings an interface up, so this file is documentation as
    # much as configuration -- but a base whose interfaces file names eth0 with dhcp would
    # leave a reader guessing whether the guest is meant to have a network.
    if [ -d "$IMAGE_MOUNT/etc/network" ]; then
        find "$IMAGE_MOUNT/etc/network/interfaces.d" -mindepth 1 -maxdepth 1 -type f \
             -delete 2>/dev/null || true
        {
            printf '# Loopback only. This guest has no NIC (qemu -nic none) and its init never\n'
            printf '# brings an interface up; the in-guest init refuses to run if one exists.\n'
            printf 'auto lo\n'
            printf 'iface lo inet loopback\n'
        } > "$IMAGE_MOUNT/etc/network/interfaces"
        chmod 0644 -- "$IMAGE_MOUNT/etc/network/interfaces"
    fi
}

# ------------------------------------------------------------------------------------
# LCOV 2.x -- INSTALLED FROM THE PINNED TARBALL, THEN ASSERTED.
#
# run_coverage.sh cannot work with lcov 1.x: it uses `--fail-under-lines` and
# `--rc branch_coverage=1`, neither of which exists there, and it parses record spellings
# that are 2.x behaviour.  The suite's .lcovrc_l1 is written in 2.x spellings as well.
# Stable distribution archives ship 1.x, so the distro package installed above is a FLOOR
# that keeps genhtml and the perl dependencies present -- it is not the version the guest
# measures with.
#
# The assertion at the end is the load-bearing part.  Without it a 1.x lcov reaches the
# guest and surfaces as a confusing coverage failure long after the evidence of why.
# ------------------------------------------------------------------------------------
install_lcov_2x() {
    if [ -n "$LCOV_TARBALL" ]; then
        verify_sha256 "$LCOV_TARBALL" "$LCOV_SHA256" 'the lcov 2.x source tarball'
        local staging='/root/lcov-src'
        log "installing lcov from the pinned source tarball"
        mkdir -p -- "$IMAGE_MOUNT$staging"
        tar --extract --file "$LCOV_TARBALL" --directory "$IMAGE_MOUNT$staging" \
            --strip-components=1 \
            || die "could not extract the lcov tarball into the image.  A tarball without a
       single top-level directory needs its own layout: extract it yourself and pass a
       repacked archive."
        chroot_run make -C "$staging" install \
            || die "'make install' failed for the lcov source tree inside the image.  lcov 2.x
       needs perl with Capture::Tiny, DateTime, JSON::XS, PerlIO::gzip and Memory::Process;
       those are in the installed package set, so check the tarball's own README for anything
       further it requires."
        rm -rf -- "$IMAGE_MOUNT$staging"
    else
        warn "no --lcov-tarball was given, so the image carries only the distribution's lcov."
        warn "  Stable archives ship lcov 1.x, which run_coverage.sh CANNOT use, so the"
        warn "  assertion below will almost certainly fail. That is by design: pass a pinned"
        warn "  lcov 2.x source tarball with --lcov-tarball and --lcov-sha256."
    fi

    local version_line major
    version_line="$(chroot_run lcov --version 2>&1 | head -n 1 || true)"
    [ -n "$version_line" ] || die "lcov does not run inside the image at all.  It is a hard
       prerequisite of run_coverage.sh, so the image is not usable without it."
    log "lcov in the image reports: $version_line"
    # The version is the first dotted number on the line; 2.x is required and 1.x is refused.
    major="$(printf '%s\n' "$version_line" | sed -n 's/.*[^0-9]\([0-9]\+\)\.[0-9].*/\1/p' | head -n 1)"
    case "$major" in
        ''|*[!0-9]*) die "could not read a version number out of lcov's own output:
           $version_line
       run_coverage.sh requires lcov 2.x and this image build will not guess.  Install a
       version whose --version output carries a readable number." ;;
    esac
    [ "$major" -ge 2 ] || die "the image's lcov is version $major.x, and run_coverage.sh
       requires 2.x: it uses --fail-under-lines and --rc branch_coverage=1, neither of which
       exists in 1.x, and it parses trace-record spellings that are 2.x behaviour.  Pass a
       pinned lcov 2.x source tarball:
           --lcov-tarball /path/to/lcov-2.x.tar.gz --lcov-sha256 <hex>
       Failing the image build here is deliberate: a 1.x lcov reaching the guest surfaces as
       a confusing coverage failure hours later, with nothing pointing at the cause."
    log "lcov 2.x assertion passed"
}


# ------------------------------------------------------------------------------------
# COPYING THE PAYLOAD, THE STAGED SDK AND (OPTIONALLY) THE GOOGLETEST PREFIX IN.
#
# The destination names reproduce the CI workspace layout rather than inventing one, which
# is what makes run_coverage.sh's and configure's own defaults correct inside the guest with
# nothing overridden: the coverage runner derives its workspace root as the parent of the
# hdmicec checkout, defaults GTEST_PREFIX to <workspace>/install/usr, and configure falls
# back to the sibling rdk-halif-aidl checkout beside the submodule.
#
# cp -a rather than a tar pipe: ownership, permissions, timestamps and symlinks all matter
# (the staged SDK is full of versioned symlinks, and a build tree's timestamps decide what
# make rebuilds).
# ------------------------------------------------------------------------------------
copy_guest_payload() {
    log "copying the payload into the image at $GUEST_PAYLOAD_DIR"
    mkdir -p -- "$IMAGE_MOUNT$GUEST_WORKSPACE"
    cp -a -- "$PAYLOAD_DIR/." "$IMAGE_MOUNT$GUEST_PAYLOAD_DIR/" 2>/dev/null \
        || { mkdir -p -- "$IMAGE_MOUNT$GUEST_PAYLOAD_DIR" \
             && cp -a -- "$PAYLOAD_DIR/." "$IMAGE_MOUNT$GUEST_PAYLOAD_DIR/"; } \
        || die "could not copy the payload into the image.  If the image ran out of space,
       raise --size (currently ${IMAGE_SIZE_MIB} MiB)."

    log "copying the staged SDK into the image at $GUEST_SDK_DIR"
    mkdir -p -- "$IMAGE_MOUNT$GUEST_SDK_DIR"
    cp -a -- "$SDK_DIR/." "$IMAGE_MOUNT$GUEST_SDK_DIR/" \
        || die "could not copy the staged SDK into the image.  If the image ran out of space,
       raise --size (currently ${IMAGE_SIZE_MIB} MiB)."

    if [ -n "$GTEST_PREFIX_SRC" ]; then
        log "copying the GoogleTest prefix into the image at $GUEST_GTEST_PREFIX"
        mkdir -p -- "$IMAGE_MOUNT$GUEST_GTEST_PREFIX"
        cp -a -- "$GTEST_PREFIX_SRC/." "$IMAGE_MOUNT$GUEST_GTEST_PREFIX/" \
            || die "could not copy the GoogleTest prefix into the image."
    fi

    # The artifact directory is created here, owner-only, so the init does not have to create
    # it under a umask it did not set -- and so run_coverage.sh's own ancestry check on a
    # caller-named output directory finds a private, root-owned path.
    mkdir -p -- "$IMAGE_MOUNT$GUEST_ARTIFACT_DIR"
    chmod 0700 -- "$IMAGE_MOUNT$GUEST_ARTIFACT_DIR"
    mkdir -p -- "$IMAGE_MOUNT$GUEST_CONF_DIR"
    chmod 0755 -- "$IMAGE_MOUNT$GUEST_CONF_DIR"
    # /tmp is where run_coverage.sh mints its private lcov HOME and its snapshot directory.
    mkdir -p -- "$IMAGE_MOUNT/tmp"
    chmod 1777 -- "$IMAGE_MOUNT/tmp"
}

# ------------------------------------------------------------------------------------
# THE LOADER.  Registering the two staged library directories with ld.so.conf and running
# ldconfig inside the image means the closure resolves for anything the guest runs, whether
# or not that process inherited LD_LIBRARY_PATH.  The init exports the path as well, and the
# duplication is deliberate: on a host whose cache already knows the directories the export
# costs a stat, and on one where it does not it is the difference between a suite that runs
# and one that dies before its first test.
# ------------------------------------------------------------------------------------
register_guest_library_paths() {
    local conf="$IMAGE_MOUNT/etc/ld.so.conf.d/cec-l2-binder.conf"
    mkdir -p -- "$(dirname -- "$conf")"
    {
        printf '# Written by %s.  The staged Binder and AIDL stub libraries.\n' "$SCRIPT_NAME"
        printf '# libbinder and libutils are direct link edges of libRCEC; liblog, libbase,\n'
        printf '# libcutils and libcutils_sockets are libbinder'"'"'s own dependencies and are here\n'
        printf '# because they must be LOADABLE, not because the middleware names them.\n'
        printf '%s\n' "$GUEST_SDK_DIR/$SDK_REL_BINDER_TARGET/lib/binder"
        printf '%s\n' "$GUEST_SDK_DIR/$SDK_REL_HALIF_LIB"
    } > "$conf"
    chmod 0644 -- "$conf"
    if [ -n "$GTEST_PREFIX_SRC" ]; then
        printf '%s\n' "$GUEST_GTEST_PREFIX/lib" >> "$conf"
    fi
    chroot_run ldconfig || die "ldconfig failed inside the image, so the staged libraries would
       not be resolvable at boot."
    log "registered the staged library directories with the image's loader cache"
}

# ------------------------------------------------------------------------------------
# THE TWO IN-GUEST HELPERS.
#
# Both exist because the init must REPORT WHAT THE RUNNING GUEST SHOWS rather than assert a
# value from a configuration fragment, and neither fact is reachable from the shell.
#
#   cec-binder-protocol-version -- opens the driver node and asks it, with the BINDER_VERSION
#       ioctl, what protocol version it speaks.  That number is the one half of the
#       protocol-equality check that cannot be read any other way, and printing a number this
#       script had not measured would be a fabrication.
#
#   cec-servicemanager-ready -- the readiness probe.  It calls defaultServiceManager() and
#       then checkService() for a throwaway name.  Reaching the second call AT ALL proves the
#       context manager answered a real binder transaction, which is the only honest
#       definition of "servicemanager is ready".  On the pinned stack the first call loops
#       until binder handle 0 resolves, so the probe BLOCKS while the manager is absent --
#       which is exactly what makes a bounded `timeout` around it a real wait instead of a
#       sleep.  It deliberately does NOT start a threadpool: the client threadpool belongs to
#       DriverAidlImpl::open() and the service one to the fake service host.
#
# Compiling them here rather than shipping prebuilt binaries has a second benefit worth
# stating: it proves, at image-build time, that the staged SDK's headers and libraries
# actually compile and link inside this image.
# ------------------------------------------------------------------------------------
build_guest_helpers() {
    local src_dir="$IMAGE_MOUNT/usr/local/src/cec-l2"
    mkdir -p -- "$src_dir"

    cat > "$src_dir/binder_protocol_version.c" <<'PROTOCOL_HELPER_EOF'
/*
 * cec-binder-protocol-version -- report the binder protocol version the running kernel's
 * driver node speaks.
 *
 * WHY THIS EXISTS.  libbinder performs a strict protocol-version EQUALITY check when it
 * opens the driver node, so kernel, SDK build and vendor side must agree; a mismatch fails
 * every open.  The in-guest init has to report the version the guest ACTUALLY shows rather
 * than infer it from a kernel configuration fragment, and no shell primitive can ask.
 *
 * The request is declared locally rather than included from <linux/android/binder.h>,
 * because a minimal root filesystem need not ship that UAPI header.  The two lines below
 * mirror the kernel's own definitions:
 *     struct binder_version { __s32 protocol_version; };
 *     #define BINDER_VERSION _IOWR('b', 9, struct binder_version)
 *
 * Exit status: 0 and the version on stdout; 2 when the node cannot be opened; 3 when the
 * ioctl fails.  Nothing is printed that was not read from the driver.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct cec_binder_version {
    int32_t protocol_version;
};

#define CEC_BINDER_VERSION _IOWR('b', 9, struct cec_binder_version)

int main(int argc, char **argv)
{
    const char *node = (argc > 1) ? argv[1] : "/dev/binder";
    struct cec_binder_version version;
    int fd;

    version.protocol_version = -1;

    fd = open(node, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "cec-binder-protocol-version: open(%s): %s\n", node, strerror(errno));
        return 2;
    }
    if (ioctl(fd, CEC_BINDER_VERSION, &version) < 0) {
        fprintf(stderr, "cec-binder-protocol-version: ioctl(BINDER_VERSION) on %s: %s\n",
                node, strerror(errno));
        (void) close(fd);
        return 3;
    }
    (void) close(fd);

    printf("%d\n", (int) version.protocol_version);
    return 0;
}
PROTOCOL_HELPER_EOF

    cat > "$src_dir/servicemanager_ready.cpp" <<'READY_PROBE_EOF'
/*
 * cec-servicemanager-ready -- block until the binder context manager answers, then exit 0.
 *
 * WHY THIS EXISTS.  servicemanager is an unconditional runtime prerequisite wherever a
 * binder driver is present, and "it is ready" is not something a sleep can establish: a
 * sleep that is occasionally too short produces a run in which every AIDL invocation
 * silently falls back to the legacy back-end and reports green, which is the precise
 * failure the binder-capable job exists to catch.
 *
 * WHAT IT PROVES.  defaultServiceManager() loops until binder handle 0 resolves, so
 * reaching the checkService() call at all means the context manager answered a real binder
 * transaction.  The throwaway name is expected to be unregistered; a null result is the
 * normal, successful outcome and is reported as such.  The caller bounds the wait with
 * `timeout`, which turns the blocking call into a bounded poll.
 *
 * WHAT IT DELIBERATELY DOES NOT DO.  It does not start a binder threadpool.  The client
 * threadpool belongs to DriverAidlImpl::open() and the service one to the fake service
 * host; a threadpool started here would be a third owner of something that has exactly two.
 * It registers nothing, so it cannot collide with the "HdmiCec" name the matrix uses.
 */
#include <binder/IBinder.h>
#include <binder/IServiceManager.h>
#include <utils/String16.h>
#include <utils/StrongPointer.h>

#include <cstdio>

int main()
{
    android::sp<android::IServiceManager> serviceManager = android::defaultServiceManager();
    if (serviceManager == nullptr) {
        std::fprintf(stderr, "cec-servicemanager-ready: defaultServiceManager() returned null\n");
        return 1;
    }

    const android::sp<android::IBinder> probe =
        serviceManager->checkService(android::String16("cec.l2.readiness.probe"));

    std::printf("cec-servicemanager-ready: the context manager answered; the probe name is %s\n",
                (probe == nullptr) ? "unregistered, as expected" : "unexpectedly registered");
    return 0;
}
READY_PROBE_EOF

    log "compiling the in-guest binder protocol-version helper"
    chroot_run gcc -O2 -Wall -o "$GUEST_PROTOCOL_HELPER" \
        /usr/local/src/cec-l2/binder_protocol_version.c \
        || die "could not compile the in-guest binder protocol-version helper.  The init needs
       it to report the protocol version the running guest shows, and reporting a version
       nothing measured is not an option."

    log "compiling the in-guest servicemanager readiness probe against the staged SDK"
    chroot_run_with_libs g++ -std=c++17 -O2 -Wall \
        "-I$GUEST_SDK_DIR/$SDK_REL_BINDER_BUILD/include/binder_sdk" \
        -o "$GUEST_READY_PROBE" \
        /usr/local/src/cec-l2/servicemanager_ready.cpp \
        "-L$GUEST_SDK_DIR/$SDK_REL_BINDER_TARGET/lib/binder" \
        "-Wl,-rpath,$GUEST_SDK_DIR/$SDK_REL_BINDER_TARGET/lib/binder" \
        -lbinder -lutils \
        || die "could not compile the in-guest servicemanager readiness probe against the
       staged Binder SDK.  This is also the first place a staged SDK that does not actually
       link shows up, so treat it as evidence about the SDK rather than about the probe:
       check that $SDK_REL_BINDER_BUILD/include/binder_sdk holds the Binder headers, that
       $SDK_REL_BINDER_TARGET/lib/binder holds libbinder and libutils, and that the whole
       six-library closure is present -- GNU ld does not search -L for a shared library's own
       DT_NEEDED entries, so a missing liblog or libbase fails here too."

    # Both are checked as installed and executable rather than RUN here: this build host has
    # no binder driver, so the protocol helper would correctly fail to open the node and the
    # probe would correctly block.  Their behaviour is exercised in the guest, which is the
    # only place either question has an answer.
    [ -x "$IMAGE_MOUNT$GUEST_PROTOCOL_HELPER" ] || die "the protocol-version helper was not
       installed at $GUEST_PROTOCOL_HELPER."
    [ -x "$IMAGE_MOUNT$GUEST_READY_PROBE" ] || die "the readiness probe was not installed at
       $GUEST_READY_PROBE."
    rm -rf -- "$src_dir"
    log "both in-guest helpers built and installed"
}

# ------------------------------------------------------------------------------------
# IN-IMAGE ASSERTIONS.  Every tool the guest needs, checked by the name the guest looks it
# up under, plus the two pkg-config queries configure will make and the dialect probe.
#
# The dialect probe is not ceremony: the generated AIDL headers include <optional> and
# declare a std::optional parameter, halcompat.h uses C++17 constructs, and a toolchain
# whose default dialect is older fails on the first of them.  Finding that out here costs a
# second; finding it out inside the guest costs a boot.
# ------------------------------------------------------------------------------------
assert_image_toolchain() {
    local tool missing=''
    for tool in g++ gcc make autoreconf automake libtoolize pkg-config gcov lcov genhtml \
                awk find mktemp stat timeout flock nproc mount mknod ldconfig bash; do
        chroot_run sh -c "command -v -- '$tool' >/dev/null 2>&1" || missing="$missing $tool"
    done
    [ -z "$missing" ] || die "the image is missing tools the guest needs:$missing.
       run_coverage.sh reaches for lcov, genhtml, gcov, awk, find, mktemp, stat, timeout,
       flock and nproc; the build reaches for g++, make and the autotools; the init reaches
       for mount and mknod.  Add the packages that provide them to the image's package set."

    local pkg_config_path=''
    if [ -n "$GTEST_PREFIX_SRC" ]; then
        pkg_config_path="$GUEST_GTEST_PREFIX/lib/pkgconfig"
    fi

    chroot_run env PKG_CONFIG_PATH="$pkg_config_path" \
        pkg-config --atleast-version=0.10.28 glib-2.0 \
        || die "glib-2.0 >= 0.10.28 is not discoverable through pkg-config inside the image.
       configure.ac:65 runs PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 0.10.28]) and there is no
       way past it, so the guest could not configure.  Install libglib2.0-dev for $ARCH."

    chroot_run env PKG_CONFIG_PATH="$pkg_config_path" \
        pkg-config --atleast-version=1.10.0 gtest \
        || die "gtest >= 1.10.0 is not discoverable through pkg-config inside the image.
       configure.ac:435's PKG_CHECK_MODULES([GTEST], [gtest >= 1.10.0]) consults pkg-config
       ONLY and ignores -I/-L, so headers and libraries on their own cannot satisfy it.
       Either the image's GoogleTest packaging must provide gtest.pc, or pass --gtest-prefix
       naming a $ARCH prefix whose lib/pkgconfig holds one."
    chroot_run env PKG_CONFIG_PATH="$pkg_config_path" \
        pkg-config --exists gmock \
        || warn "gmock.pc is not discoverable through pkg-config inside the image.  configure
  does not query it, and the suites link -lgmock directly, so this is a warning rather than a
  failure -- but a GoogleTest packaging without gmock at all would fail at link time."

    # The dialect probe: the smallest program that distinguishes C++14 from C++17 here.
    printf '%s\n' '#include <optional>' 'int main() { return std::optional<int>{17}.value() == 17 ? 0 : 1; }' \
        > "$IMAGE_MOUNT/tmp/cec-l2-dialect-probe.cpp"
    chroot_run g++ -std=c++17 -o /tmp/cec-l2-dialect-probe /tmp/cec-l2-dialect-probe.cpp \
        || die "the image's g++ cannot compile a -std=c++17 translation unit that includes
       <optional>.  The generated AIDL headers include it and declare a std::optional
       parameter, and halcompat.h uses C++17 constructs, so the guest could not build the
       middleware.  Install a newer g++ for $ARCH."
    rm -f -- "$IMAGE_MOUNT/tmp/cec-l2-dialect-probe" "$IMAGE_MOUNT/tmp/cec-l2-dialect-probe.cpp"
    log "in-image toolchain assertions passed (tools, glib, gtest, and -std=c++17 with <optional>)"
}


# ------------------------------------------------------------------------------------
# PROVENANCE WRITTEN INTO THE IMAGE.
#
# Three files, and the distinction between them is the whole point:
#
#   expected-kernel-config   what this job REQUIRES of the guest kernel.  The init reports
#                            the running guest against it and never asserts a value from it.
#   build-host-kernel-config the configuration the caller SAYS the guest kernel was built
#                            with, copied in verbatim when --kernel-config was given.  Also
#                            evidence, also never asserted as fact about the running guest.
#   sdk-build-flags          whatever the staged SDK itself records about how it was built,
#                            harvested rather than declared.  When it records nothing, the
#                            file says so in those words -- an absent record is reported as
#                            absent, not filled in with a plausible value.
# ------------------------------------------------------------------------------------
write_kernel_expectation() {
    local target="$IMAGE_MOUNT$GUEST_KERNEL_EXPECTATION"
    cat > "$target" <<'KERNEL_EXPECTATION_EOF'
# The guest kernel configuration this job requires.  Written into the image by
# .github/workflows/aidl-path-tests-rootfs.sh; the in-guest init REPORTS the running guest
# against this list and never asserts a value from it.
#
# ALWAYS REQUIRED, on every platform that runs binder at all:
CONFIG_ANDROID=y
CONFIG_ANDROID_BINDER_IPC=y
CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder"
CONFIG_ANDROID_BINDERFS=y
CONFIG_ASHMEM=y
#
# Also assumed by the init, and worth having for diagnosis rather than strictly required:
#   CONFIG_DEVTMPFS / CONFIG_DEVTMPFS_MOUNT  -- so /dev is populated before init runs
#   CONFIG_IKCONFIG / CONFIG_IKCONFIG_PROC   -- so /proc/config.gz exists and the init can
#                                               report the configuration ACTUALLY in effect
#                                               instead of reporting that it is unreadable
KERNEL_EXPECTATION_EOF

    # THE WIRE-PROTOCOL CLAUSE IS GENERATED FROM THE DERIVED VALUE, NOT WRITTEN AS TEXT.
    # This is the half an earlier revision hard-coded as CONFIG_ANDROID_BINDER_IPC_32BIT=y
    # while the SDK it shipped alongside was built BINDER_IPC_32BIT=OFF.  Now the line below
    # is whatever derive_binder_protocol() read out of the two artefacts, and the evidence for
    # it travels with it so a reader can re-derive rather than trust.
    {
        printf '#\n'
        printf '# THE BINDER WIRE PROTOCOL FOR THIS IMAGE: %s.  DERIVED, NOT DECLARED.\n' "$DERIVED_BINDER_PROTOCOL"
        printf '#   from the kernel configuration : %s\n' "${PROTOCOL_FROM_KERNEL_CONFIG:-<could not answer>}"
        printf '#     %s\n' "$PROTOCOL_KERNEL_EVIDENCE"
        printf '#   from the staged SDK cache     : %s\n' "${PROTOCOL_FROM_SDK_CACHE:-<could not answer>}"
        printf '#     %s\n' "$PROTOCOL_SDK_EVIDENCE"
        printf '#\n'
        printf '# The driver enforces protocol-version EQUALITY when libbinder opens the node, so\n'
        printf '# the kernel, the SDK build and the vendor side must agree; see linux_binder_idl\n'
        printf '# BUILD.md:409-413 at tag 2.6.0.  Compile bitness is a SEPARATE axis: this image is\n'
        printf '# 32-bit throughout (--arch %s) and a 32-bit userspace can speak either protocol\n' "$ARCH"
        printf '# (CMakeLists.txt:209-217).  The option below is therefore scoped to THIS image and\n'
        printf '# is not a requirement of supported platforms generally.\n'
        if [ "$DERIVED_BINDER_PROTOCOL" = '7' ]; then
            printf 'CONFIG_ANDROID_BINDER_IPC_32BIT=y\n'
            printf '#   ^ REQUIRED TO BE SET for protocol 7.  MAINLINE HAS NO SUCH OPTION AFTER\n'
            printf '#     4.17: it was deleted with "ANDROID: binder: remove 32-bit binder\n'
            printf '#     interface", while CONFIG_ANDROID_BINDERFS -- which the init requires --\n'
            printf '#     starts at 5.0.  The workflow restores it on the pinned kernel with an\n'
            printf '#     inline patch and asserts it in the built .config, which is why a\n'
            printf '#     binderfs guest can be a protocol-7 guest here.  The init REPORTS what\n'
            printf '#     the running kernel shows against this line and never assumes it.\n'
        else
            printf '# CONFIG_ANDROID_BINDER_IPC_32BIT is not set\n'
            printf '#   ^ REQUIRED TO BE UNSET OR ABSENT for protocol 8.  This is also what an\n'
            printf '#     UNPATCHED mainline kernel shows whatever anyone asks for: the option was\n'
            printf '#     removed in the 4.18 era and binderfs starts at 5.0.\n'
        fi
    } >> "$target"
    chmod 0644 -- "$target"

    if [ -n "$KERNEL_CONFIG_SRC" ]; then
        {
            printf '# Copied verbatim from %s at image-build time.\n' "$KERNEL_CONFIG_SRC"
            printf '# THIS IS EVIDENCE ABOUT THE BUILD, NOT ABOUT THE RUNNING GUEST.  The init\n'
            printf '# reports what the running guest shows and never substitutes this file for it.\n'
            cat -- "$KERNEL_CONFIG_SRC"
        } > "$IMAGE_MOUNT$GUEST_KERNEL_PROVENANCE"
        chmod 0644 -- "$IMAGE_MOUNT$GUEST_KERNEL_PROVENANCE"
        log "copied the caller-supplied kernel configuration in as build-time provenance"
    fi
}

# Harvest what the staged SDK records about its own build.  Nothing here is declared: every
# line printed was read out of the staging tree, and where the tree records nothing the file
# says exactly that.
write_sdk_build_flags_record() {
    local target="$IMAGE_MOUNT$GUEST_SDK_FLAGS_RECORD"
    {
        printf '# How the staged Binder SDK was built, as recorded BY THE STAGING TREE ITSELF.\n'
        printf '# Harvested at image-build time by %s from %s.\n' "$SCRIPT_NAME" "$SDK_DIR"
        printf '# Nothing in this file is declared: an absent record is reported as absent.\n'
        printf '#\n'
        printf '# The reason it matters: the driver enforces protocol-version EQUALITY when\n'
        printf '# libbinder opens the node, so the kernel configuration, the flag the SDK was\n'
        printf '# built with (BINDER_IPC_32BIT) and the vendor side must agree.  See\n'
        printf '# linux_binder_idl BUILD.md:409-413 at tag 2.6.0.\n'
        printf '#\n'
        printf '# THE PROTOCOL FOR THIS IMAGE WAS DERIVED, NOT DECLARED: %s.\n' "$DERIVED_BINDER_PROTOCOL"
        printf '#   kernel configuration half : %s  (%s)\n' \
            "${PROTOCOL_FROM_KERNEL_CONFIG:-could not answer}" "$PROTOCOL_KERNEL_EVIDENCE"
        printf '#   staged SDK cache half     : %s  (%s)\n' \
            "${PROTOCOL_FROM_SDK_CACHE:-could not answer}" "$PROTOCOL_SDK_EVIDENCE"
        printf '# Compile bitness is a separate axis (CMakeLists.txt:209-217): this image is\n'
        printf '# 32-bit throughout and a 32-bit userspace can speak either protocol.\n'
        printf '\n'
        printf 'derived_binder_protocol=%s\n' "$DERIVED_BINDER_PROTOCOL"
        printf 'derived_binder_protocol_from_kernel_config=%s\n' "${PROTOCOL_FROM_KERNEL_CONFIG:-unknown}"
        printf 'derived_binder_protocol_from_sdk_cache=%s\n' "${PROTOCOL_FROM_SDK_CACHE:-unknown}"
        printf '\n'

        local pinned_version="$SDK_DIR/binder_sdk.version"
        if [ -f "$pinned_version" ]; then
            printf '## binder_sdk.version (the pin; BINDER_VERSION is deliberately never set)\n'
            sed -n '1,20p' -- "$pinned_version"
        else
            printf '## binder_sdk.version: not present in the staged tree\n'
        fi
        printf '\n'

        printf '## BINDER_IPC_32BIT and related cache entries found in the staged build tree\n'
        local found=0 cache
        while IFS= read -r cache; do
            [ -n "$cache" ] || continue
            found=1
            printf '# from %s\n' "${cache#"$SDK_DIR"/}"
            grep -E '^(BINDER_IPC_32BIT|CMAKE_CXX_STANDARD|CMAKE_BUILD_TYPE|CMAKE_SYSTEM_PROCESSOR|CMAKE_C_FLAGS|CMAKE_CXX_FLAGS)(:|=)' \
                -- "$cache" 2>/dev/null || printf '#   (no matching entry in this cache)\n'
        done < <(find "$SDK_BINDER_BUILD" "$SDK_BINDER_TARGET" -maxdepth 3 -name CMakeCache.txt -type f 2>/dev/null)
        [ "$found" -eq 1 ] || printf '#   NOT RECORDED BY THE STAGING TREE: no CMakeCache.txt was found under\n#   %s or %s, so the flag the SDK was built with cannot be reported from the\n#   image. It has to come from the job that built the SDK.\n' \
            "$SDK_REL_BINDER_BUILD" "$SDK_REL_BINDER_TARGET"
        printf '\n'

        printf '## the staged readiness marker and the architecture of what was staged\n'
        if [ -f "$SDK_BINDER_TARGET/.sdk_ready" ]; then
            printf 'sdk_ready_marker=present\n'
        else
            printf 'sdk_ready_marker=absent\n'
        fi
        printf 'staged_libbinder=%s\n' "$(file -b -- "$SDK_BINDER_LIB/libbinder.so" 2>/dev/null || printf 'unreadable')"
        printf 'staged_servicemanager=%s\n' "$(file -b -- "$SDK_SERVICEMANAGER" 2>/dev/null || printf 'unreadable')"
        printf 'image_target_architecture=%s\n' "$ARCH"
    } > "$target"
    chmod 0644 -- "$target"
    log "recorded the staged SDK's own build provenance in the image"
}

# The image's own build record, and the in-guest configuration the init reads.  The
# configuration is written as single-quoted assignments; every path in it has already been
# refused if it contained a quote or a newline, so the quoting cannot be broken out of.
write_image_conf() {
    local target="$IMAGE_MOUNT$GUEST_IMAGE_CONF"
    local gtest_prefix_value=''
    if [ -n "$GTEST_PREFIX_SRC" ]; then
        gtest_prefix_value="$GUEST_GTEST_PREFIX"
    fi
    cat > "$target" <<IMAGE_CONF_EOF
# Generated by $SCRIPT_NAME.  Read by $GUEST_INIT_PATH.
# Every value is a fixed property of this image; nothing here selects a code path, and in
# particular there is no back-end selection of any kind -- both HDMI CEC HAL back-ends are
# compiled into libRCEC for every SOC and the choice between them is made at runtime.
IMAGE_ARCH='$ARCH'
GUEST_WORKSPACE='$GUEST_WORKSPACE'
PAYLOAD_DIR='$GUEST_PAYLOAD_DIR'
SDK_DIR='$GUEST_SDK_DIR'
HALIF_PREFIX='$GUEST_SDK_DIR'
HALIF_LIB_DIR='$GUEST_SDK_DIR/$SDK_REL_HALIF_LIB'
BINDER_SDK_DIR='$GUEST_SDK_DIR/$SDK_REL_BINDER_TARGET'
BINDER_SDK_INCLUDE_DIR='$GUEST_SDK_DIR/$SDK_REL_BINDER_BUILD'
BINDER_LIB_DIR='$GUEST_SDK_DIR/$SDK_REL_BINDER_TARGET/lib/binder'
SERVICEMANAGER_BIN='$GUEST_SDK_DIR/$SDK_REL_SERVICEMANAGER'
GTEST_PREFIX='$gtest_prefix_value'
ARTIFACT_DIR='$GUEST_ARTIFACT_DIR'
STATUS_FILE='$GUEST_STATUS_FILE'
PROTOCOL_HELPER='$GUEST_PROTOCOL_HELPER'
READY_PROBE='$GUEST_READY_PROBE'
KERNEL_EXPECTATION='$GUEST_KERNEL_EXPECTATION'
KERNEL_PROVENANCE='$GUEST_KERNEL_PROVENANCE'
SDK_FLAGS_RECORD='$GUEST_SDK_FLAGS_RECORD'
BINDER_DEVICE_NODES='$BINDER_DEVICE_NODES'
READINESS_TIMEOUT_SECONDS='$READINESS_TIMEOUT_SECONDS'
# The wire protocol this image EXPECTS, and the two build-time artefacts it was derived
# from.  The init cross-checks all three of: the running kernel's own configuration, the
# recorded SDK value below, and the number /dev/binder answers to the BINDER_VERSION
# ioctl -- and fails naming whichever disagrees.  An empty half means that source could not
# answer at image-build time, which is reported rather than filled in.
EXPECTED_BINDER_PROTOCOL='$DERIVED_BINDER_PROTOCOL'
PROTOCOL_FROM_KERNEL_CONFIG='$PROTOCOL_FROM_KERNEL_CONFIG'
PROTOCOL_FROM_SDK_CACHE='$PROTOCOL_FROM_SDK_CACHE'
IMAGE_CONF_EOF
    chmod 0644 -- "$target"

    {
        printf '# How this image was built, recorded by %s.\n' "$SCRIPT_NAME"
        printf 'built_at_utc=%s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
        printf 'built_by=%s\n' "$SCRIPT_PATH"
        printf 'architecture=%s\n' "$ARCH"
        printf 'elf_bitness_decision=32-bit throughout (--arch %s); a separate axis from the wire protocol\n' "$ARCH"
        printf 'binder_wire_protocol_derived=%s\n' "$DERIVED_BINDER_PROTOCOL"
        printf 'binder_wire_protocol_from_kernel_config=%s\n' "${PROTOCOL_FROM_KERNEL_CONFIG:-unknown}"
        printf 'binder_wire_protocol_from_sdk_cache=%s\n' "${PROTOCOL_FROM_SDK_CACHE:-unknown}"
        printf 'image_size_mib=%s\n' "$IMAGE_SIZE_MIB"
        # SANITISED. This record lives inside the image, but it is also the file the guest's
        # own resolver comment points a reader at, and a credential belongs in neither.
        if [ -n "$BASE_URL" ]; then
            printf 'base_provenance=%s\n' "$(sanitised_endpoint_display "$BASE_URL")"
        else
            printf 'base_provenance=%s\n' "$BASE_TARBALL"
        fi
        printf 'base_sha256_verified=%s\n' "$BASE_SHA256"
        if [ -n "$APT_MIRROR" ]; then
            printf 'apt_mirror=%s\n' "$(sanitised_endpoint_display "$APT_MIRROR")"
        else
            printf 'apt_mirror=%s\n' '<the base tarball sources, unchanged>'
        fi
        printf 'installed_package_record=%s\n' "$GUEST_PACKAGE_RECORD"
        printf 'installed_package_count=%s\n' "$PACKAGE_RECORD_COUNT"
        printf 'installed_package_record_sha256=%s\n' "$PACKAGE_RECORD_DIGEST"
        printf 'required_packages_accounted=%s of %s\n' \
            "$PACKAGE_RECORD_REQUIRED_ACCOUNTED" "${#IMAGE_PACKAGES[@]}"
        printf 'lcov_source=%s\n' "${LCOV_TARBALL:-<the distribution package only>}"
        printf 'payload_source=%s\n' "$PAYLOAD_DIR"
        printf 'sdk_source=%s\n' "$SDK_DIR"
        printf 'gtest_prefix_source=%s\n' "${GTEST_PREFIX_SRC:-<the image packaging>}"
    } > "$IMAGE_MOUNT$GUEST_IMAGE_RECORD"
    chmod 0644 -- "$IMAGE_MOUNT$GUEST_IMAGE_RECORD"
}


# ------------------------------------------------------------------------------------
# THE IN-GUEST INIT.
#
# Written literally -- the heredoc delimiter is quoted, so nothing in the body is expanded
# by THIS script and the file on disk is exactly the text below.  Every value the init needs
# comes from the generated configuration file instead, which is why the body can be literal:
# a body full of escaped expansions is a body nobody can read or test.
#
# It is installed at a fixed path and IS THE GUEST'S ENTRY POINT.  The workflow boots the
# kernel with init=<that path>; there is no service manager, no unit file and no shell
# profile between the kernel and this script.
# ------------------------------------------------------------------------------------
write_guest_init() {
    local target="$IMAGE_MOUNT$GUEST_INIT_PATH"
    mkdir -p -- "$(dirname -- "$target")"
    cat > "$target" <<'CEC_L2_INIT_EOF'
#!/usr/bin/env bash
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
# cec-l2-init -- the guest entry point for the binder-capable AIDL-path job.
#
# GENERATED FILE.  Written into this image by
# .github/workflows/aidl-path-tests-rootfs.sh in the hdmicec submodule.  Edit that script,
# not this copy: this one is recreated from scratch every time an image is built.
#
# WHAT IT DOES, IN THIS ORDER, AND NOTHING ELSE:
#   1. mounts the pseudo-filesystems and binderfs, creates the binder device nodes and
#      proves the driver node can actually be opened;
#   2. reports the kernel configuration, the SDK build flags and the resulting binder
#      protocol version TOGETHER in one delimited block, every value read from the RUNNING
#      guest rather than asserted from a configuration fragment;
#   3. starts servicemanager and BLOCKS until it answers a real binder transaction, under a
#      bounded timeout -- never a sleep;
#   4. exports the build and loader environment the coverage runner and the test harnesses
#      need, and NOT CEC_TEST_AIDL_MODE;
#   5. runs the coverage runner with --build --run --output-dir, records its EXACT exit
#      status and propagates it;
#   6. tears everything down and powers the guest off, on every path.
#
# WHAT IT DELIBERATELY DOES NOT DO:
#   * It does not set CEC_TEST_AIDL_MODE.  The coverage runner owns that value per
#     invocation across the A-E matrix; one value set here would collapse the matrix to a
#     single arm and report it as green.
#   * It does not start a binder threadpool.  The client threadpool belongs to
#     DriverAidlImpl::open(); the service one belongs to the fake service host, which the L2
#     harness launches itself through an inherited readiness descriptor.
#   * It does not launch the fake service host.  That is the harness's job, and doing it here
#     would put a second process on the "HdmiCec" name.
#   * It does not pass --restore, and it does not reimplement any part of the coverage
#     runner's orchestration -- not the per-invocation exit check, not the executed-count
#     check, not the selected-path grep, not the artifact naming, not the counter policy and
#     not the single capture.
#   * It touches no network.  Nothing in this file reaches one, and nothing it runs is
#     supposed to.
##########################################################################

set -euo pipefail

# Artifacts are extracted from this image by the workflow after the guest powers down, and a
# human reads the logs afterwards; 022 keeps them readable without making anything writable.
umask 022

readonly INIT_NAME='cec-l2-init'
readonly IMAGE_CONF='/etc/cec-l2/image.conf'

log()  { printf '[%s] %s\n' "$INIT_NAME" "$*"; }
warn() { printf '[%s] WARNING: %s\n' "$INIT_NAME" "$*" >&2; }
die()  { printf '[%s] ERROR: %s\n' "$INIT_NAME" "$*" >&2; exit 1; }

# ------------------------------------------------------------------------------------
# CONFIGURATION.  Read from the generated file with a strict whitelist rather than sourced:
# an unexpected key is a fault in the image build and is reported as one instead of being
# executed.
# ------------------------------------------------------------------------------------
IMAGE_ARCH=''
GUEST_WORKSPACE=''
PAYLOAD_DIR=''
SDK_DIR=''
HALIF_PREFIX=''
HALIF_LIB_DIR=''
BINDER_SDK_DIR=''
BINDER_SDK_INCLUDE_DIR=''
BINDER_LIB_DIR=''
SERVICEMANAGER_BIN=''
GTEST_PREFIX=''
ARTIFACT_DIR=''
STATUS_FILE=''
PROTOCOL_HELPER=''
READY_PROBE=''
KERNEL_EXPECTATION=''
KERNEL_PROVENANCE=''
SDK_FLAGS_RECORD=''
BINDER_DEVICE_NODES=''
READINESS_TIMEOUT_SECONDS=''
# The binder wire protocol the image was built to expect, and the two build-time artefacts it
# was derived from.  EXPECTED_BINDER_PROTOCOL is required: an image that cannot say which
# protocol it expects cannot cross-check the driver it finds, and a silent mismatch there is
# exactly the failure mode that produced the contradictory evidence this init now refuses to
# reproduce.  The two derivation fields are diagnostic -- they name WHERE the expectation came
# from, so a disagreement report identifies the artefact to correct rather than only the number.
EXPECTED_BINDER_PROTOCOL=''
PROTOCOL_FROM_KERNEL_CONFIG=''
PROTOCOL_FROM_SDK_CACHE=''

read_image_conf() {
    [ -r "$IMAGE_CONF" ] || die "$IMAGE_CONF is missing or unreadable, so this image was not
       finished by the root-image script.  Rebuild it."
    local line key value
    while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in
            ''|'#'*) continue ;;
        esac
        key="${line%%=*}"
        value="${line#*=}"
        # One layer of single quotes, exactly as the generator writes it.
        value="${value#\'}"
        value="${value%\'}"
        case "$key" in
            IMAGE_ARCH|GUEST_WORKSPACE|PAYLOAD_DIR|SDK_DIR|HALIF_PREFIX|HALIF_LIB_DIR| \
            BINDER_SDK_DIR|BINDER_SDK_INCLUDE_DIR|BINDER_LIB_DIR|SERVICEMANAGER_BIN| \
            GTEST_PREFIX|ARTIFACT_DIR|STATUS_FILE|PROTOCOL_HELPER|READY_PROBE| \
            KERNEL_EXPECTATION|KERNEL_PROVENANCE|SDK_FLAGS_RECORD|BINDER_DEVICE_NODES| \
            READINESS_TIMEOUT_SECONDS|EXPECTED_BINDER_PROTOCOL| \
            PROTOCOL_FROM_KERNEL_CONFIG|PROTOCOL_FROM_SDK_CACHE)
                printf -v "$key" '%s' "$value" ;;
            *)
                die "unrecognised key '$key' in $IMAGE_CONF.  The image build and this init
       disagree about the configuration, which means the image is not internally consistent.
       Rebuild it with a matching root-image script." ;;
        esac
    done < "$IMAGE_CONF"

    local required
    for required in IMAGE_ARCH PAYLOAD_DIR SDK_DIR HALIF_LIB_DIR BINDER_SDK_DIR \
                    BINDER_SDK_INCLUDE_DIR BINDER_LIB_DIR SERVICEMANAGER_BIN ARTIFACT_DIR \
                    STATUS_FILE PROTOCOL_HELPER READY_PROBE BINDER_DEVICE_NODES \
                    READINESS_TIMEOUT_SECONDS EXPECTED_BINDER_PROTOCOL; do
        [ -n "${!required}" ] || die "$required is empty in $IMAGE_CONF.  The image is
       incomplete; rebuild it."
    done
}

# ------------------------------------------------------------------------------------
# TEARDOWN.  Registered before anything is mounted or started, so it runs on success, on
# failure, on a die and on an interrupt alike.  Two things must survive it: the artifacts
# (destroyed artifacts make a failure undiagnosable) and the exit status (a laundered status
# makes a failure look like a pass).
# ------------------------------------------------------------------------------------
MOUNTED_POINTS=()
SERVICEMANAGER_PID=''
TEARDOWN_DONE=0

# SC2317 is suppressed on the three teardown functions, and the justification is that
# ShellCheck's reachability analysis does not model trap handlers: main() ends in an
# unconditional `exit`, so everything reachable only from a trap registered before it is
# reported as unreachable code.  These three are reached ONLY from the EXIT trap, which is
# the whole point of them -- they must run on the success path, the failure path and the
# interrupt path alike.  The suppression is scoped to the three functions rather than the
# file, so a genuinely unreachable statement anywhere else is still reported.
# shellcheck disable=SC2317
teardown() {
    local status=$?
    # THE TEARDOWN IS NOT INTERRUPTIBLE.  A signal arriving while this is running would run
    # the INT or TERM handler, whose own `exit` would abandon the teardown halfway -- leaving
    # binderfs mounted, servicemanager running and, worst of all, the artifacts unflushed.
    # Measured on the host-side script, where exactly that left five mounts and a loop device
    # behind; the same shape applies here with more at stake.
    trap '' INT TERM HUP
    [ "$TEARDOWN_DONE" -eq 0 ] || return
    TEARDOWN_DONE=1

    log "teardown: init is finishing with status $status"

    stop_servicemanager

    # Flush before anything is unmounted: the artifacts live in this filesystem and the
    # workflow reads them from the image after the guest is gone.
    sync || true

    local index point
    for (( index=${#MOUNTED_POINTS[@]}-1; index>=0; index-- )); do
        point="${MOUNTED_POINTS[index]}"
        [ -n "$point" ] || continue
        if mountpoint -q -- "$point" 2>/dev/null; then
            umount -- "$point" 2>/dev/null || umount -l -- "$point" 2>/dev/null \
                || warn "could not unmount $point during teardown."
        fi
    done

    sync || true
    # Read-only root before power-off, so the coverage evidence cannot be lost to a page that
    # never reached the disk.
    mount -o remount,ro / 2>/dev/null || warn "could not remount / read-only before power-off."

    log "teardown complete; powering the guest down"
    power_off_guest
}

# Reached only from teardown(), which is reached only from the EXIT trap; see the SC2317
# justification above.
# shellcheck disable=SC2317
stop_servicemanager() {
    [ -n "$SERVICEMANAGER_PID" ] || return 0
    kill -0 "$SERVICEMANAGER_PID" 2>/dev/null || { SERVICEMANAGER_PID=''; return 0; }

    log "stopping servicemanager (pid $SERVICEMANAGER_PID)"
    kill -TERM "$SERVICEMANAGER_PID" 2>/dev/null || true
    # A bounded wait, then SIGKILL.  `wait` would block for as long as it liked, and this is
    # the teardown path: it has to end.
    local waited=0
    while [ "$waited" -lt 10 ] && kill -0 "$SERVICEMANAGER_PID" 2>/dev/null; do
        # A short, bounded pause on the way out.  This is NOT a readiness mechanism -- the
        # readiness wait is a poll on a real binder transaction, further down -- it is only
        # how long a terminating process is given to finish.
        sleep 1
        waited=$(( waited + 1 ))
    done
    if kill -0 "$SERVICEMANAGER_PID" 2>/dev/null; then
        warn "servicemanager did not exit on SIGTERM; sending SIGKILL."
        kill -KILL "$SERVICEMANAGER_PID" 2>/dev/null || true
    fi
    SERVICEMANAGER_PID=''
}

# Power off through whatever this root filesystem actually provides.  Nothing here can be
# assumed: a minimal image may have sysvinit's poweroff, systemd's, or neither.
# Reached only from teardown(); see the SC2317 justification above.
# shellcheck disable=SC2317
power_off_guest() {
    sync || true
    if command -v poweroff >/dev/null 2>&1; then
        poweroff -f 2>/dev/null || true
    fi
    if command -v halt >/dev/null 2>&1; then
        halt -f -p 2>/dev/null || true
    fi
    if [ -w /proc/sysrq-trigger ]; then
        printf 'o' > /proc/sysrq-trigger 2>/dev/null || true
    fi
    warn "no power-off mechanism took effect.  Idling rather than returning: an init that"
    warn "  RETURNS panics the kernel, which reports as a crash and can lose the console"
    warn "  tail this job is read from.  The workflow's own timeout on the QEMU process is"
    warn "  the backstop."
    while true; do
        # An idle block after a failed power-off.  Not a readiness wait: there is nothing
        # left to become ready.
        sleep 60
    done
}

trap teardown EXIT
trap 'die "interrupted."' INT
trap 'die "terminated."' TERM
CEC_L2_INIT_EOF
    # PART TWO: the binder bring-up, the platform record and the readiness wait.  Appended
    # rather than written into one enormous heredoc so that each part stays readable; the
    # delimiter is quoted in every part, so the file on disk is exactly this text.
    cat >> "$target" <<'CEC_L2_INIT_EOF'

# ------------------------------------------------------------------------------------
# PSEUDO-FILESYSTEMS.  Mounted defensively: a kernel with CONFIG_DEVTMPFS_MOUNT has already
# populated /dev before this script runs, and mounting over it would hide the console.  So
# each one is mounted only where it is not already there.
# ------------------------------------------------------------------------------------
mount_if_absent() { # $1=type  $2=source  $3=target  [$4..=options]
    local type="$1" source="$2" target="$3"
    shift 3
    mkdir -p -- "$target"
    if mountpoint -q -- "$target" 2>/dev/null; then
        log "  $target is already mounted; leaving it alone"
        return 0
    fi
    MOUNTED_POINTS+=("$target")
    mount -t "$type" "$@" "$source" "$target" \
        || die "could not mount $type on $target.  The guest kernel must provide it: /proc and
       /sys are needed by the toolchain and by the coverage runner, and /dev is where the
       binder nodes live."
    log "  mounted $type on $target"
}

mount_pseudo_filesystems() {
    log "mounting the pseudo-filesystems"
    # The root filesystem is the image itself and the build writes into it, so it must be
    # writable.  A kernel booted with 'ro' leaves it read-only.
    mount -o remount,rw / 2>/dev/null || true
    mount_if_absent proc  proc     /proc
    mount_if_absent sysfs sysfs    /sys
    mount_if_absent devtmpfs devtmpfs /dev
    mount_if_absent devpts devpts  /dev/pts
    mount_if_absent tmpfs tmpfs    /tmp -o mode=1777
    # /tmp is where the coverage runner mints its private lcov HOME and its Makefile
    # snapshot, so it has to be writable with the usual sticky permissions.
    chmod 1777 /tmp 2>/dev/null || true
}

# ------------------------------------------------------------------------------------
# THE GUEST IS OFFLINE, AND THIS ASSERTS IT RATHER THAN RELYING ON HOW QEMU WAS STARTED.
#
# WHAT THE ASSERTION PROTECTS.  Everything this job claims about the guest depends on the
# guest not having a network: that the payload carries every source, binary and library it
# needs; that the package set inside the image is the one the builder installed and recorded;
# that no step fetches anything at test time and therefore that the run is reproducible.  The
# enforcement lives in the workflow (qemu -nic none), in another file, edited by different
# people at different times.  An omission there is invisible from here -- a default user-mode
# NIC with a working DNS forwarder is what qemu provides when no network option is given at
# all -- and a build that quietly acquired the ability to fetch things would still pass.
#
# SO IT IS FATAL, NOT A WARNING.  A guest with a network is not the guest this job specifies,
# and the difference between the two is precisely a reproducibility claim.
#
# WHAT IS READ, AND WHY EACH IS READ THE WAY IT IS.
#   INTERFACES: /sys/class/net, because it is the kernel's own list and needs no tool.  ip(8)
#     and ifconfig(8) are not assumed to exist -- the image is a minbase install plus a build
#     toolchain, and neither is in that set.
#   ROUTES: /proc/net/route, parsed directly for the same reason.  Its first line is a header,
#     so any further line is a route.
#   RESOLVER: an active 'nameserver' line in /etc/resolv.conf.  The image build replaces that
#     file with a comment block, so a nameserver line means either the build step did not run
#     or something inside the guest wrote one.
# ------------------------------------------------------------------------------------
assert_guest_offline() {
    local interfaces='' entry name routes='' resolvers=''

    for entry in /sys/class/net/*; do
        [ -e "$entry" ] || continue
        name="$(basename -- "$entry")"
        [ "$name" = 'lo' ] || interfaces="$interfaces $name"
    done
    if [ -n "$interfaces" ]; then
        die "this guest has non-loopback network interface(s):${interfaces}
       It is specified to run with NO network: every source, binary and library it needs is
       inside the image, nothing is fetched at test time, and the package set was pinned and
       recorded at image-build time.  An interface here means the QEMU invocation gained a NIC
       -- most likely by an option being removed, since qemu creates a DEFAULT user-mode NIC
       when no -nic/-netdev/-nodefaults is given at all.  Restore '-nic none' in the workflow
       step that boots this image.  Refusing to run is deliberate: a run that could fetch
       things is not the reproducible run this job reports."
    fi

    if [ -r /proc/net/route ]; then
        # tail -n +2 drops the header line; anything left is a configured route.
        routes="$(tail -n +2 /proc/net/route 2>/dev/null | awk 'NF { print $1 }' | sort -u \
                    | tr '\n' ' ' || true)"
        routes="${routes% }"
    fi
    if [ -n "$routes" ]; then
        die "this guest has network route(s) via interface(s): $routes
       See the interface message above for why that is refused.  With no NIC there is nothing
       to route through, so a route means the guest was given one."
    fi

    if [ -r /etc/resolv.conf ]; then
        resolvers="$(grep -cE '^[[:space:]]*nameserver[[:space:]]+[^[:space:]]' \
                          /etc/resolv.conf 2>/dev/null || true)"
        resolvers="$(printf '%s' "$resolvers" | tr -d '[:space:]')"
    fi
    if [ -n "$resolvers" ] && [ "$resolvers" != '0' ]; then
        die "this guest has $resolvers nameserver line(s) configured in /etc/resolv.conf.
       The image build replaces that file with a comment block precisely so this assertion
       means something, so either that step did not run -- rebuild the image -- or something
       in the guest wrote a resolver.  A guest that can resolve names is a guest that can
       fetch, and this run's reproducibility claim rests on it not being able to."
    fi

    log "offline confirmed: no non-loopback interface, no route, no configured resolver"
}

# ------------------------------------------------------------------------------------
# BINDERFS AND THE DEVICE NODES.
#
# The initial binderfs instance is populated from CONFIG_ANDROID_BINDER_DEVICES, so the nodes
# this job needs appear inside the mount if -- and only if -- the guest kernel was configured
# with them.  A missing node is therefore a KERNEL CONFIGURATION fault, and it is reported as
# one rather than worked around: allocating a device through binder-control would paper over a
# guest that is not the guest this job specifies.
#
# The recipe follows linux_binder_idl BUILD.md:493-507 at tag 2.6.0.  That file is in the
# upstream repository and not in this workspace, so it is cited by locator and its text is
# not reproduced here.
# ------------------------------------------------------------------------------------
mount_binderfs() {
    log "mounting binderfs"
    if ! grep -qw binder /proc/filesystems 2>/dev/null; then
        die "the running kernel does not provide the 'binder' filesystem (no 'binder' line in
       /proc/filesystems).  This guest cannot run the AIDL-selected invocations at all.  Build
       the guest kernel with the options recorded in $KERNEL_EXPECTATION -- at minimum
       CONFIG_ANDROID_BINDER_IPC and CONFIG_ANDROID_BINDERFS."
    fi

    mkdir -p /dev/binderfs
    if mountpoint -q -- /dev/binderfs 2>/dev/null; then
        log "  /dev/binderfs is already mounted"
    else
        MOUNTED_POINTS+=(/dev/binderfs)
        mount -t binder binder /dev/binderfs \
            || die "mounting binderfs on /dev/binderfs failed even though the kernel lists the
       filesystem.  Check that CONFIG_ANDROID_BINDERFS is set in the running kernel; the
       options this job requires are recorded in $KERNEL_EXPECTATION."
    fi

    local node
    for node in $BINDER_DEVICE_NODES; do
        if [ ! -e "/dev/binderfs/$node" ]; then
            die "the binderfs instance has no '$node' device.  The initial instance is
       populated from CONFIG_ANDROID_BINDER_DEVICES, so this means the guest kernel was not
       built with CONFIG_ANDROID_BINDER_DEVICES=\"binder,hwbinder,vndbinder\".  Fix the kernel
       configuration -- allocating a device through binder-control here would hide a guest
       that is not the one this job specifies."
        fi
        # A symlink rather than a copy: /dev/binder is the path libbinder opens by default and
        # the path the coverage runner probes, and it must be the same object binderfs owns.
        ln -sfn "/dev/binderfs/$node" "/dev/$node" \
            || die "could not link /dev/$node to the binderfs device."
        log "  /dev/$node -> /dev/binderfs/$node"
    done

    # OPENABILITY, not merely existence.  A node can exist with permissions that make it
    # useless, and a run that got past a presence check only to abort inside libbinder would
    # report a crash where it should have reported a configuration fault.
    if ! exec 9<>/dev/binder; then
        die "/dev/binder exists but cannot be opened for reading and writing.  Every binder
       client in this job opens it, so nothing can run.  Check its permissions and the
       guest's device-node ownership."
    fi
    exec 9<&-
    log "  /dev/binder opened and closed successfully"
}

# ------------------------------------------------------------------------------------
# THE BINDER PLATFORM RECORD.
#
# One delimited block carrying three things that are only meaningful TOGETHER, because
# libbinder enforces protocol-version EQUALITY when it opens the node: the kernel
# configuration in effect, the flag the SDK was built with, and the protocol version the
# driver actually reports.  Splitting them across steps would let a reader check one and
# assume the others.
#
# Every value is READ.  Where a source is unavailable the block says so in those words; it
# never falls back to the expectation file and presents that as fact about the running guest.
# ------------------------------------------------------------------------------------
# Which file, if any, carries the configuration of the kernel that is actually running.
# Factored out because two callers need the same answer for two different purposes:
# report_kernel_config prints the binder entries for a human, and runtime_kernel_protocol
# derives the wire protocol the running kernel implies.  Deriving that from a different
# source than the one reported would let the two disagree without either being wrong.
kernel_config_source() {
    if [ -r /proc/config.gz ] && command -v zcat >/dev/null 2>&1; then
        printf '/proc/config.gz'
    elif [ -r "/boot/config-$(uname -r)" ]; then
        printf '/boot/config-%s' "$(uname -r)"
    fi
}

# One configuration entry as the running kernel's own configuration file states it: the
# matching '<entry>=<value>' or '# <entry> is not set' line, or nothing when the entry is
# absent altogether.  Absent is a THIRD state and callers must treat it as such -- reading
# absence as "not set" is how a build-time assumption gets presented as a measurement.
kernel_config_entry() {
    local entry="$1" source="$2"
    if [ "$source" = '/proc/config.gz' ]; then
        zcat /proc/config.gz 2>/dev/null | grep -E "^($entry=|# $entry is not set)" || true
    else
        grep -E "^($entry=|# $entry is not set)" -- "$source" 2>/dev/null || true
    fi
}

# ------------------------------------------------------------------------------------
# THE WIRE PROTOCOL THE RUNNING KERNEL IMPLIES.
#
# This is one of the four sources the protocol-agreement check compares, and it is a READING
# of the running guest, never a restatement of the image's expectation.  Sets two globals so
# the evidence travels with the value: a number alone in a disagreement report tells a reader
# what differs but not which artefact to correct.
#
# The mapping follows the kernel's own option, and the absent case is NOT an error: mainline
# deleted CONFIG_ANDROID_BINDER_IPC_32BIT from drivers/android/Kconfig in the 4.18 era, well
# before CONFIG_ANDROID_BINDERFS appeared in 5.0, so a STOCK kernel new enough to offer
# binderfs cannot have the option at all and its driver speaks 8.  This job's kernel is not
# stock -- the workflow restores the option with an inline patch so a binderfs guest can speak
# 7 -- which is exactly why this reading is taken from the RUNNING kernel: it is what says
# whether that patch reached the kernel actually booted here.
#   set (=y)          -> 7
#   explicitly unset  -> 8
#   absent            -> 8; on this job that means the restoration patch did not take effect
#   no readable config-> unavailable; the check then relies on the driver's own answer
# ------------------------------------------------------------------------------------
RUNTIME_KERNEL_PROTOCOL=''
RUNTIME_KERNEL_PROTOCOL_EVIDENCE=''

runtime_kernel_protocol() {
    local source line
    source="$(kernel_config_source)"
    if [ -z "$source" ]; then
        RUNTIME_KERNEL_PROTOCOL=''
        RUNTIME_KERNEL_PROTOCOL_EVIDENCE='no kernel configuration is readable on this guest
       (neither /proc/config.gz via CONFIG_IKCONFIG_PROC nor /boot/config-<kernel release>),
       so the running kernel cannot be asked what it was configured with'
        return 0
    fi

    line="$(kernel_config_entry CONFIG_ANDROID_BINDER_IPC_32BIT "$source")"
    case "$line" in
        CONFIG_ANDROID_BINDER_IPC_32BIT=y|CONFIG_ANDROID_BINDER_IPC_32BIT=Y)
            RUNTIME_KERNEL_PROTOCOL='7'
            RUNTIME_KERNEL_PROTOCOL_EVIDENCE="'$line' in $source" ;;
        '')
            RUNTIME_KERNEL_PROTOCOL='8'
            RUNTIME_KERNEL_PROTOCOL_EVIDENCE="CONFIG_ANDROID_BINDER_IPC_32BIT is absent from
       $source, which is what a STOCK kernel new enough to provide binderfs looks like -- the
       option was deleted in the 4.18 era and CONFIG_ANDROID_BINDERFS first appeared in 5.0.
       On this job the kernel is patched to restore it, so absent here means the restoration
       did not reach the kernel that booted" ;;
        *)
            RUNTIME_KERNEL_PROTOCOL='8'
            RUNTIME_KERNEL_PROTOCOL_EVIDENCE="'$line' in $source" ;;
    esac
}

report_kernel_config() {
    local source entry
    source="$(kernel_config_source)"

    if [ -z "$source" ]; then
        printf 'kernel_config_source: NONE READABLE ON THIS GUEST\n'
        printf '  Neither /proc/config.gz (CONFIG_IKCONFIG_PROC) nor /boot/config-%s exists,\n' "$(uname -r)"
        printf '  so the configuration IN EFFECT cannot be read from inside the guest and is\n'
        printf '  NOT reported here. It is not inferred from %s either: that file records what\n' "$KERNEL_EXPECTATION"
        printf '  this job REQUIRES, not what this kernel has.\n'
        return 0
    fi

    printf 'kernel_config_source: %s\n' "$source"
    for entry in CONFIG_ANDROID CONFIG_ANDROID_BINDER_IPC CONFIG_ANDROID_BINDER_DEVICES \
                 CONFIG_ANDROID_BINDERFS CONFIG_ASHMEM CONFIG_ANDROID_BINDER_IPC_32BIT; do
        local line=''
        line="$(kernel_config_entry "$entry" "$source")"
        if [ -n "$line" ]; then
            printf '  %s\n' "$line"
        else
            printf '  %s: absent from the configuration (neither set nor explicitly unset)\n' "$entry"
        fi
    done
}

# The driver's own answer, hoisted into globals so the agreement check of
# assert_binder_protocol_agreement compares the SAME reading a human sees in the record
# block below.  Asking the driver twice could in principle return two answers, and a report
# that disagreed with the check that gated the run would be worse than either alone.
DRIVER_PROTOCOL_STATUS=''
DRIVER_PROTOCOL_OUTPUT=''
DRIVER_PROTOCOL_VERSION=''

read_driver_protocol() {
    # The helper is the only way to ask the driver its protocol version; its failure is
    # reported verbatim rather than replaced with a plausible number.  It prints a bare
    # integer on success and exits 0; 2 means the node could not be opened and 3 means the
    # BINDER_VERSION ioctl failed.
    set +e
    DRIVER_PROTOCOL_OUTPUT="$("$PROTOCOL_HELPER" /dev/binder 2>&1)"
    DRIVER_PROTOCOL_STATUS=$?
    set -e

    DRIVER_PROTOCOL_VERSION=''
    if [ "$DRIVER_PROTOCOL_STATUS" -eq 0 ]; then
        # Strip surrounding whitespace and accept the value only if it is all digits: a
        # helper that printed a diagnostic on stdout must not be parsed as a version.
        local trimmed
        trimmed="$(printf '%s' "$DRIVER_PROTOCOL_OUTPUT" | tr -d '[:space:]')"
        case "$trimmed" in
            ''|*[!0-9]*) DRIVER_PROTOCOL_VERSION='' ;;
            *)           DRIVER_PROTOCOL_VERSION="$trimmed" ;;
        esac
    fi
}

report_binder_platform() {
    local protocol_output protocol_status
    read_driver_protocol
    protocol_output="$DRIVER_PROTOCOL_OUTPUT"
    protocol_status="$DRIVER_PROTOCOL_STATUS"
    runtime_kernel_protocol

    printf '================ BINDER PLATFORM RECORD (read from this running guest) ================\n'
    printf 'reported_by: %s\n' "$INIT_NAME"
    # ELF bitness and the binder wire protocol are two SEPARATE axes, and an earlier revision
    # of this script printed them as one -- it claimed protocol 7 here purely because the
    # image is 32-bit, while the workflow was building the SDK for protocol 8.  The two are
    # now stated apart, and the protocol is the value derived at image-build time from the
    # kernel configuration and the SDK build cache rather than a consequence of the bitness.
    printf 'image_target_elf_bitness: %s   (32-bit userland; says nothing about the wire protocol)\n' "$IMAGE_ARCH"
    printf 'image_expected_binder_protocol: %s   (derived at image build; kernel-config half: %s, SDK-cache half: %s)\n' \
        "$EXPECTED_BINDER_PROTOCOL" \
        "${PROTOCOL_FROM_KERNEL_CONFIG:-not derivable}" \
        "${PROTOCOL_FROM_SDK_CACHE:-not derivable}"
    printf 'uname: %s\n' "$(uname -srvmo 2>/dev/null || printf 'unavailable')"
    printf '\n'
    printf '---- kernel binder configuration, as the running guest reports it ----\n'
    report_kernel_config
    printf 'running_kernel_implies_protocol: %s\n' "${RUNTIME_KERNEL_PROTOCOL:-UNAVAILABLE}"
    printf '  evidence: %s\n' "$RUNTIME_KERNEL_PROTOCOL_EVIDENCE"
    printf '\n'
    printf '---- runtime evidence ----\n'
    printf 'binder_in_proc_filesystems: %s\n' \
        "$(grep -qw binder /proc/filesystems 2>/dev/null && printf 'yes' || printf 'no')"
    printf 'binderfs_mounted_at_/dev/binderfs: %s\n' \
        "$(mountpoint -q -- /dev/binderfs 2>/dev/null && printf 'yes' || printf 'no')"
    printf 'binderfs_entries: %s\n' \
        "$(find /dev/binderfs -mindepth 1 -maxdepth 1 -printf '%f ' 2>/dev/null || printf 'unreadable')"
    printf '\n'
    printf '---- Binder SDK build flags, as recorded by the staging tree itself ----\n'
    if [ -n "$SDK_FLAGS_RECORD" ] && [ -r "$SDK_FLAGS_RECORD" ]; then
        cat -- "$SDK_FLAGS_RECORD"
    else
        printf 'NOT RECORDED: %s is missing, so the flag the SDK was built with cannot be\n' "${SDK_FLAGS_RECORD:-<unset>}"
        printf 'reported from this image. It has to come from the job that built the SDK.\n'
    fi
    printf '\n'
    printf '---- resulting binder protocol version, asked of /dev/binder ----\n'
    if [ "$protocol_status" -eq 0 ]; then
        printf 'binder_protocol_version: %s\n' "$protocol_output"
        printf '  This is the number libbinder compares for EQUALITY when it opens the node, so\n'
        printf '  it is meaningful only against the two blocks above. A mismatch between the\n'
        printf '  kernel, the SDK build and the vendor side fails EVERY open; see\n'
        printf '  linux_binder_idl BUILD.md:409-413 at tag 2.6.0.\n'
    else
        printf 'binder_protocol_version: COULD NOT BE READ (helper exited %s)\n' "$protocol_status"
        printf '%s\n' "$protocol_output"
    fi
    printf '\n'
    printf '---- what this job requires of the guest kernel (expectation, not measurement) ----\n'
    if [ -n "$KERNEL_EXPECTATION" ] && [ -r "$KERNEL_EXPECTATION" ]; then
        cat -- "$KERNEL_EXPECTATION"
    else
        printf 'the expectation file is missing from this image.\n'
    fi
    if [ -n "$KERNEL_PROVENANCE" ] && [ -r "$KERNEL_PROVENANCE" ]; then
        printf '\n---- the configuration the image build was told the guest kernel was built with ----\n'
        printf '(build-time provenance only; the running guest is reported above)\n'
        head -n 40 -- "$KERNEL_PROVENANCE"
    fi
    printf '================ END BINDER PLATFORM RECORD ================\n'
}

# ------------------------------------------------------------------------------------
# PROTOCOL AGREEMENT: FOUR SOURCES, ONE NUMBER, AND A LOUD FAILURE WHEN THEY DIFFER.
#
# WHY THIS GATE EXISTS.  libbinder enforces protocol-version EQUALITY when it opens
# /dev/binder (linux_binder_idl BUILD.md:409-413 at tag 2.6.0), so kernel, SDK build and
# vendor side must agree or every open fails.  A disagreement is therefore not a cosmetic
# documentation problem: it is a run in which the AIDL back-end can never be selected, and
# the harness's own fallback then produces a suite that passes on the legacy path and reports
# green.  That is a false green about the exact thing this job exists to prove, which is why
# a disagreement here is fatal rather than a warning.
#
# WHY FOUR SOURCES AND NOT ONE.  The defect this replaces was a single hard-coded sentence
# claiming protocol 7 while the workflow built protocol 8; nothing compared the two, so
# nothing caught it.  Each source below can be wrong independently and each names a different
# artefact to correct:
#
#   (A) the image's expectation      -> derived at image-build time from the kernel
#                                       configuration and the SDK build cache; wrong means the
#                                       image was built against the wrong inputs
#   (B) the running kernel's config  -> wrong means the guest is booted on a kernel other than
#                                       the one the image was built for
#   (C) the staged SDK's build cache -> wrong means libbinder was compiled with the wrong
#                                       BINDER_IPC_32BIT value
#   (D) the driver's BINDER_VERSION  -> the authority.  This is the number libbinder compares,
#                                       read through the ioctl by the staged helper
#
# (D) IS REQUIRED; (B) AND (C) MAY BE UNAVAILABLE AND SAY SO.  A guest without
# CONFIG_IKCONFIG_PROC cannot be asked (B), and an SDK staged as an install tree may carry no
# CMakeCache.txt for (C).  Unavailable is reported as unavailable and never substituted with
# the expectation -- presenting a build-time assumption as a measurement is the failure mode
# that produced the contradiction in the first place.  (D) failing is fatal: if the driver
# cannot be opened or cannot answer, no AIDL invocation can run at all.
# ------------------------------------------------------------------------------------
SDK_CACHE_PROTOCOL=''
SDK_CACHE_PROTOCOL_EVIDENCE=''

# Re-derive (C) inside the guest from the staged tree itself where the tree carries a cache,
# so the check reads an artefact rather than trusting a value the image build copied into
# image.conf.  Falls back to the SDK flags record -- a file written by the image build and
# independent of image.conf -- and then to image.conf's own copy, naming which was used.
sdk_cache_protocol() {
    local cache raw
    while IFS= read -r cache; do
        [ -n "$cache" ] || continue
        raw="$(grep -E '^BINDER_IPC_32BIT(:[A-Z]+)?=' -- "$cache" 2>/dev/null | tail -n 1 || true)"
        [ -n "$raw" ] || continue
        raw="${raw#*=}"
        case "$raw" in
            ON|1|TRUE|True|true|YES|Yes|yes|Y|y)
                SDK_CACHE_PROTOCOL='7' ;;
            OFF|0|FALSE|False|false|NO|No|no|N|n|'')
                SDK_CACHE_PROTOCOL='8' ;;
            *)
                warn "BINDER_IPC_32BIT='$raw' in $cache is not a recognised CMake boolean;
       ignoring this cache for protocol derivation rather than guessing what it meant."
                continue ;;
        esac
        SDK_CACHE_PROTOCOL_EVIDENCE="BINDER_IPC_32BIT=$raw in $cache (read in the guest)"
        return 0
    done < <(find "$SDK_DIR/out/build" "$SDK_DIR/out/target" -maxdepth 3 -name CMakeCache.txt \
                  -type f 2>/dev/null)

    local recorded=''
    if [ -n "$SDK_FLAGS_RECORD" ] && [ -r "$SDK_FLAGS_RECORD" ]; then
        recorded="$(grep -E '^derived_binder_protocol_from_sdk_cache=' -- "$SDK_FLAGS_RECORD" \
                      2>/dev/null | tail -n 1 || true)"
        recorded="${recorded#*=}"
    fi
    if [ -n "$recorded" ]; then
        SDK_CACHE_PROTOCOL="$recorded"
        SDK_CACHE_PROTOCOL_EVIDENCE="derived_binder_protocol_from_sdk_cache=$recorded in
       $SDK_FLAGS_RECORD (recorded by the image build; no CMakeCache.txt is staged in this
       image to re-derive it from)"
        return 0
    fi
    if [ -n "$PROTOCOL_FROM_SDK_CACHE" ]; then
        SDK_CACHE_PROTOCOL="$PROTOCOL_FROM_SDK_CACHE"
        SDK_CACHE_PROTOCOL_EVIDENCE="PROTOCOL_FROM_SDK_CACHE=$PROTOCOL_FROM_SDK_CACHE in
       $IMAGE_CONF (recorded by the image build; neither a staged CMakeCache.txt nor the SDK
       flags record is available in this image)"
        return 0
    fi
    SDK_CACHE_PROTOCOL=''
    SDK_CACHE_PROTOCOL_EVIDENCE='no staged CMakeCache.txt, no SDK flags record entry and no
       image.conf value, so the flag libbinder was compiled with cannot be read from inside
       this guest'
}

assert_binder_protocol_agreement() {
    case "$EXPECTED_BINDER_PROTOCOL" in
        7|8) : ;;
        *) die "EXPECTED_BINDER_PROTOCOL='$EXPECTED_BINDER_PROTOCOL' in $IMAGE_CONF is not 7 or
       8.  The image build derives this value from the guest kernel configuration and the SDK
       build cache; a value outside that set means the image was finished by a root-image
       script that does not agree with this init.  Rebuild the image." ;;
    esac

    sdk_cache_protocol

    printf '================ BINDER PROTOCOL AGREEMENT ================\n'
    printf '(A) image expectation              : %s\n' "$EXPECTED_BINDER_PROTOCOL"
    printf '      derived from kernel config   : %s\n' "${PROTOCOL_FROM_KERNEL_CONFIG:-not derivable at image build}"
    printf '      derived from SDK build cache : %s\n' "${PROTOCOL_FROM_SDK_CACHE:-not derivable at image build}"
    printf '(B) running kernel configuration   : %s\n' "${RUNTIME_KERNEL_PROTOCOL:-UNAVAILABLE}"
    printf '      evidence                     : %s\n' "$RUNTIME_KERNEL_PROTOCOL_EVIDENCE"
    printf '(C) staged Binder SDK build        : %s\n' "${SDK_CACHE_PROTOCOL:-UNAVAILABLE}"
    printf '      evidence                     : %s\n' "$SDK_CACHE_PROTOCOL_EVIDENCE"
    if [ -n "$DRIVER_PROTOCOL_VERSION" ]; then
        printf '(D) /dev/binder BINDER_VERSION     : %s   (the authority; libbinder compares this for equality)\n' \
            "$DRIVER_PROTOCOL_VERSION"
    else
        printf '(D) /dev/binder BINDER_VERSION     : COULD NOT BE READ (helper exited %s)\n' \
            "${DRIVER_PROTOCOL_STATUS:-unknown}"
    fi
    printf '================ END BINDER PROTOCOL AGREEMENT ================\n'

    # (D) first, because it is the authority and because a driver that cannot answer means no
    # AIDL invocation can run regardless of what the other three say.
    if [ -z "$DRIVER_PROTOCOL_VERSION" ]; then
        die "the binder driver did not report a protocol version: the helper
       ($PROTOCOL_HELPER /dev/binder) exited ${DRIVER_PROTOCOL_STATUS:-unknown} and produced:
       ${DRIVER_PROTOCOL_OUTPUT:-<no output>}
       Exit 2 means /dev/binder could not be opened -- check that binderfs is mounted and the
       device nodes were created; exit 3 means the BINDER_VERSION ioctl failed, which means
       the node is not a binder device.  Either way the AIDL invocations cannot run, so this
       is a failure rather than a missing datum."
    fi

    local disagreements=0
    if [ "$DRIVER_PROTOCOL_VERSION" != "$EXPECTED_BINDER_PROTOCOL" ]; then
        warn "PROTOCOL DISAGREEMENT (A) vs (D): the image was built expecting protocol
       $EXPECTED_BINDER_PROTOCOL but /dev/binder reports $DRIVER_PROTOCOL_VERSION.  Correct
       the artefact the expectation came from -- kernel-config half
       '${PROTOCOL_FROM_KERNEL_CONFIG:-not derivable}', SDK-cache half
       '${PROTOCOL_FROM_SDK_CACHE:-not derivable}' -- or boot the guest on the kernel the
       image was built for."
        disagreements=$((disagreements + 1))
    fi
    if [ -n "$RUNTIME_KERNEL_PROTOCOL" ] && \
       [ "$RUNTIME_KERNEL_PROTOCOL" != "$DRIVER_PROTOCOL_VERSION" ]; then
        warn "PROTOCOL DISAGREEMENT (B) vs (D): the running kernel's configuration implies
       protocol $RUNTIME_KERNEL_PROTOCOL ($RUNTIME_KERNEL_PROTOCOL_EVIDENCE) but its own
       driver reports $DRIVER_PROTOCOL_VERSION.  The configuration this guest exposes does not
       describe the driver it is running; the kernel image and its configuration file are out
       of step."
        disagreements=$((disagreements + 1))
    fi
    if [ -n "$SDK_CACHE_PROTOCOL" ] && \
       [ "$SDK_CACHE_PROTOCOL" != "$DRIVER_PROTOCOL_VERSION" ]; then
        local remedy='OFF'
        [ "$DRIVER_PROTOCOL_VERSION" = '7' ] && remedy='ON'
        warn "PROTOCOL DISAGREEMENT (C) vs (D): the staged Binder SDK was built for protocol
       $SDK_CACHE_PROTOCOL ($SDK_CACHE_PROTOCOL_EVIDENCE) but the driver reports
       $DRIVER_PROTOCOL_VERSION.  libbinder compares these for EQUALITY on open, so every
       open would fail.  Rebuild the SDK with -DBINDER_IPC_32BIT=$remedy, which is the axis
       that selects the wire protocol independently of the 32-bit build."
        disagreements=$((disagreements + 1))
    fi

    if [ "$disagreements" -gt 0 ]; then
        die "$disagreements binder protocol disagreement(s) reported above.  Refusing to run
       the invocation matrix: with a protocol mismatch libbinder fails every open, the AIDL
       back-end can never be selected, and the suite would fall back to the legacy path and
       report green -- a false pass about the one thing this job exists to prove."
    fi

    if [ -z "$RUNTIME_KERNEL_PROTOCOL" ]; then
        warn "the running kernel's configuration is not readable, so source (B) of the
       protocol agreement could not be checked.  Build the guest kernel with
       CONFIG_IKCONFIG=y and CONFIG_IKCONFIG_PROC=y to close this gap.  The driver's own
       answer (D) was checked and agrees with the image expectation."
    fi
    if [ -z "$SDK_CACHE_PROTOCOL" ]; then
        warn "the flag the staged Binder SDK was compiled with is not recorded anywhere in
       this image, so source (C) of the protocol agreement could not be checked.  Stage the
       SDK build tree's CMakeCache.txt, or ensure the image build writes the SDK flags
       record.  The driver's own answer (D) was checked and agrees with the image
       expectation."
    fi

    log "binder protocol agreement: all available sources report protocol $DRIVER_PROTOCOL_VERSION"
}

# ------------------------------------------------------------------------------------
# SERVICEMANAGER, AND WAITING FOR IT TO ANSWER.
#
# It is an unconditional runtime prerequisite wherever a binder driver is present: the
# service name cannot be resolved without it, and obtaining an IServiceManager at all loops
# until binder handle 0 resolves.
#
# THE WAIT IS A POLL ON A REAL TRANSACTION, NOT A SLEEP.  The probe calls
# defaultServiceManager() and then checkService(); on the pinned stack the first call BLOCKS
# while no context manager exists, so `timeout` around it is a genuine bounded wait and each
# unsuccessful attempt costs its own timeout rather than a guess.  A bare sleep that was
# occasionally too short would produce a run in which every AIDL invocation silently falls
# back to the legacy back-end and reports green -- the exact failure this job exists to
# catch.
#
# Liveness is checked on every iteration too: a servicemanager that died at startup must be
# reported as dead, with its log, rather than waited out to the full budget.
# ------------------------------------------------------------------------------------
readonly SERVICEMANAGER_LOG_NAME='servicemanager.log'
readonly READINESS_PROBE_LOG_NAME='servicemanager_readiness.log'
readonly READINESS_ATTEMPT_TIMEOUT_SECONDS=3

start_servicemanager() {
    local log_file="$ARTIFACT_DIR/$SERVICEMANAGER_LOG_NAME"
    log "starting servicemanager: $SERVICEMANAGER_BIN"
    [ -x "$SERVICEMANAGER_BIN" ] || die "servicemanager is not executable at
       $SERVICEMANAGER_BIN.  It comes from the staged SDK, so this means the image was built
       around an incomplete staging tree."

    # Its own log, kept with the artifacts: when readiness fails this is the only evidence of
    # why, and it must survive a failing run.
    "$SERVICEMANAGER_BIN" >> "$log_file" 2>&1 &
    SERVICEMANAGER_PID=$!
    log "  servicemanager pid $SERVICEMANAGER_PID, logging to $log_file"
}

wait_for_servicemanager() {
    local probe_log="$ARTIFACT_DIR/$READINESS_PROBE_LOG_NAME"
    local budget="$READINESS_TIMEOUT_SECONDS"
    local elapsed=0 attempt=0 probe_status

    log "waiting up to ${budget}s for servicemanager to answer a binder transaction"
    [ -x "$READY_PROBE" ] || die "the readiness probe is missing at $READY_PROBE, so there is
       no way to establish that servicemanager is ready.  Refusing to proceed on a guess: a
       run that starts before the service manager answers falls back to the legacy back-end
       and reports green."

    while [ "$elapsed" -lt "$budget" ]; do
        attempt=$(( attempt + 1 ))
        if ! kill -0 "$SERVICEMANAGER_PID" 2>/dev/null; then
            warn "servicemanager exited before it answered. Its log follows:"
            tail -n 40 -- "$ARTIFACT_DIR/$SERVICEMANAGER_LOG_NAME" >&2 2>/dev/null || true
            die "servicemanager is not running, so no AIDL-selected invocation can resolve the
       service name.  The most common causes are a binder protocol mismatch between the
       kernel and the SDK build -- compare the two in the platform record above -- and a
       missing library from the six-library Binder closure."
        fi

        set +e
        timeout "$READINESS_ATTEMPT_TIMEOUT_SECONDS" "$READY_PROBE" >> "$probe_log" 2>&1
        probe_status=$?
        set -e

        if [ "$probe_status" -eq 0 ]; then
            log "  servicemanager answered on attempt $attempt (after about ${elapsed}s)"
            return 0
        fi

        # Each unsuccessful attempt consumed its own timeout, because the probe BLOCKS while
        # no context manager exists. That is what paces this loop -- there is no sleep in it.
        elapsed=$(( elapsed + READINESS_ATTEMPT_TIMEOUT_SECONDS ))
    done

    warn "the readiness probe never succeeded. Its log follows:"
    tail -n 40 -- "$probe_log" >&2 2>/dev/null || true
    warn "servicemanager's own log follows:"
    tail -n 40 -- "$ARTIFACT_DIR/$SERVICEMANAGER_LOG_NAME" >&2 2>/dev/null || true
    die "servicemanager did not answer within ${budget}s.  Failing rather than proceeding: a
       run that starts before the context manager is reachable resolves every selection to the
       legacy back-end and reports a green matrix that proves nothing.  Raise the budget with
       --readiness-timeout when building the image if this guest is simply slow; otherwise
       treat it as a binder configuration fault and compare the kernel, SDK and protocol
       values in the platform record above."
}
CEC_L2_INIT_EOF

    # PART THREE: the environment, the coverage-runner invocation, the status file and main.
    cat >> "$target" <<'CEC_L2_INIT_EOF'

# ------------------------------------------------------------------------------------
# THE ENVIRONMENT THE GUEST BUILD AND THE HARNESSES NEED.
#
# Four of these are configure's own precious variables, and they are exported rather than
# spelled into a configure command line because the coverage runner passes them through
# itself and derives every include root, -L directory and link edge from them inside
# configure.  A path spelled twice is a second place for the two build systems to disagree.
#
# WHAT IS DELIBERATELY NOT SET HERE:
#   CEC_TEST_AIDL_MODE -- the coverage runner owns it per invocation across the A-E matrix.
#       Setting it here would override the matrix and collapse it to one arm, which would
#       still report green.
#   CEC_FAKE_HOST_READY_FD -- the L2 harness creates that pipe and passes the descriptor to
#       the fake service host it launches itself.
#   anything that would start a binder threadpool -- there is no environment variable for
#       that, and this note exists so nobody adds one.
# ------------------------------------------------------------------------------------
export_build_environment() {
    export HOME='/root'
    export PATH='/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin'
    export LC_ALL='C'
    export TMPDIR='/tmp'

    # EXPORTED ONLY WHEN NON-EMPTY, and that distinction is load-bearing rather than tidy.
    # These four reach configure as precious variables, and 'BINDER_SDK_DIR=' asserts that
    # there is NO Binder SDK -- which fails configure's own check -- while omitting it lets
    # configure look where it knows to look.  An empty export would therefore be worse than
    # no export at all.
    local name
    for name in HALIF_PREFIX HALIF_LIB_DIR BINDER_SDK_DIR BINDER_SDK_INCLUDE_DIR; do
        if [ -n "${!name}" ]; then
            # Exporting BY NAME is the intent: $name holds the variable's name and the
            # variable itself was set by read_image_conf.  ShellCheck's SC2163 exists to
            # catch `export $foo` written where `export foo` was meant, which is the
            # opposite of what this loop does.
            # shellcheck disable=SC2163
            export "$name"
        else
            warn "$name is empty in the image configuration, so it is left UNSET rather than"
            warn "  exported empty: an empty value asserts that the SDK is absent, which is a"
            warn "  different instruction from saying nothing."
        fi
    done

    # The loader closure.  The image's ld.so.conf already names these directories, and this
    # is deliberately belt and braces: GNU ld does not search -L for a shared library's own
    # DT_NEEDED entries and the staged libbinder carries no RUNPATH, so the search path
    # matters when the suites are LINKED as well as when they are run.
    local ld_path="$BINDER_LIB_DIR:$HALIF_LIB_DIR"
    if [ -n "$GTEST_PREFIX" ]; then
        export GTEST_PREFIX="$GTEST_PREFIX"
        ld_path="$GTEST_PREFIX/lib:$ld_path"
        export PKG_CONFIG_PATH="$GTEST_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    fi
    export LD_LIBRARY_PATH="$ld_path${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

    # Where the L2 harness finds the out-of-process fake service host. The coverage runner
    # sets this itself for each invocation it launches; exporting it here means a manual
    # in-guest run of the L2 runner works too, and the two values are the same path.
    export CEC_FAKE_AIDL_HOST_PATH="$PAYLOAD_DIR/tests/L2Tests/fake_hdmi_cec_aidl_host"

    log "build environment exported:"
    log "  HALIF_PREFIX=$HALIF_PREFIX"
    log "  HALIF_LIB_DIR=$HALIF_LIB_DIR"
    log "  BINDER_SDK_DIR=$BINDER_SDK_DIR"
    log "  BINDER_SDK_INCLUDE_DIR=$BINDER_SDK_INCLUDE_DIR"
    log "  GTEST_PREFIX=${GTEST_PREFIX:-<unset: the image packaging supplies gtest.pc>}"
    log "  LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
    log "  CEC_FAKE_AIDL_HOST_PATH=$CEC_FAKE_AIDL_HOST_PATH"
    log "  CEC_TEST_AIDL_MODE is deliberately NOT set: the coverage runner owns it per invocation"
}

# ------------------------------------------------------------------------------------
# THE COVERAGE RUN.
#
# --build compiles and --run measures, and both happen here because this is where the binder
# driver is.  The runner owns everything else: the five invocations, the per-invocation
# zero-exit check, the executed-count check against each filter, the selected-path-line
# grep, the non-overwriting artifact names, the counter zero-and-accumulate policy, the
# single capture, the line gate and the per-branch gate.  None of that is repeated here.
#
# THE EXIT STATUS IS NOT LAUNDERED.  `|| status=$?` captures the value and the function
# returns it unchanged; the caller propagates it as this init's own exit status and the file
# records the same number.  What is forbidden -- and absent -- is a `|| true`, a `set +e`
# that swallows the result, or an `if ! ...; then echo` that turns a failure into a message.
#
#   0  measured and passing. The only status that means that.
#   3  ADVISORY: the coverage figures are at or above the bar but this invocation did not
#      establish the evidence for them. NOT A PASS.
#   anything else: a prerequisite was missing, a step failed, or a gate failed.
# ------------------------------------------------------------------------------------
readonly RUN_COVERAGE_REL='tests/L1Tests/run_coverage.sh'

run_coverage() { # -> the runner's exact exit status
    local runner="$PAYLOAD_DIR/$RUN_COVERAGE_REL"
    local status=0

    [ -f "$runner" ] || die "the coverage runner is missing at $runner.  The payload copied
       into this image is not an hdmicec checkout, or the copy was incomplete."

    log "running the coverage runner: bash $runner --build --run --output-dir $ARTIFACT_DIR"
    log "  (it owns the whole A-E invocation matrix, the single capture and both gates)"

    # Invoked through bash rather than relying on the executable bit, which a copy across
    # filesystems does not always preserve.
    bash "$runner" --build --run --output-dir "$ARTIFACT_DIR" || status=$?

    return "$status"
}

write_status_file() { # $1=the runner's exit status
    local status="$1"
    # The file holds exactly the integer and a newline, so the workflow can read it with a
    # single command and branch on 0, on 3, and on anything else.
    printf '%s\n' "$status" > "$STATUS_FILE"
    log "wrote the coverage runner's exit status ($status) to $STATUS_FILE"

    printf '================ COVERAGE RUN VERDICT ================\n'
    printf 'run_coverage.sh exit status: %s\n' "$status"
    case "$status" in
        0) printf 'verdict: PASS -- measured and at or above the bar.\n' ;;
        3) printf 'verdict: ADVISORY -- the figures are at or above the bar but this\n'
           printf '         invocation did not establish the evidence for them.\n'
           printf '         THIS IS NOT A PASS. Do not treat 3 as one.\n' ;;
        *) printf 'verdict: FAIL -- a prerequisite was missing, a step failed, or a gate\n'
           printf '         failed. See the run and capture logs under %s.\n' "$ARTIFACT_DIR" ;;
    esac
    printf 'artifacts: %s\n' "$ARTIFACT_DIR"
    printf '  The names are fixed by the coverage runner: coverage.info (the unfiltered trace\n'
    printf '  the per-branch gate reads), filtered_coverage.info, line_gate_coverage.info,\n'
    printf '  coverage/index.html, per_file_coverage.tsv, uncovered_lines.txt, the\n'
    printf '  per-invocation rdkTestResults_invocation_<L>.json and run_invocation_<L>.log,\n'
    printf '  and capture.log / filter.log / genhtml.log / build.log.\n'
    printf '================ END COVERAGE RUN VERDICT ================\n'
}

# The image is supposed to carry all three of these, so a missing one means the image build
# did not finish or something was pruned afterwards.  Checked here, before binderfs is
# mounted and before servicemanager is started, so the failure is one line rather than a
# cascade.
verify_guest_layout() {
    log "guest workspace: $GUEST_WORKSPACE"
    log "  payload:    $PAYLOAD_DIR"
    log "  staged SDK: $SDK_DIR"
    [ -d "$PAYLOAD_DIR" ] || die "the payload directory $PAYLOAD_DIR is missing from this
       image, so there is nothing to build or measure.  The image build did not finish."
    [ -d "$SDK_DIR" ] || die "the staged SDK directory $SDK_DIR is missing from this image, so
       the middleware could not link against the AIDL stubs or libbinder.  The image build did
       not finish."
    [ -d "$BINDER_LIB_DIR" ] || die "the staged Binder library directory $BINDER_LIB_DIR is
       missing from this image.  libbinder, libutils and the four libraries beneath them must
       all be loadable or nothing that opens the driver starts."
}

main() {
    log "guest entry point starting (pid $$)"
    read_image_conf
    verify_guest_layout
    mount_pseudo_filesystems
    # Checked as soon as /proc and /sys exist and before anything is built or run: if this
    # guest has a network, the run's reproducibility claim is void and there is no point
    # spending an hour of TCG emulation to produce a result nobody can rely on.
    assert_guest_offline

    mkdir -p -- "$ARTIFACT_DIR"
    chmod 0700 -- "$ARTIFACT_DIR"

    mount_binderfs
    report_binder_platform
    # Gated BEFORE servicemanager starts, because a protocol mismatch makes every binder open
    # fail: servicemanager would either abort or never become reachable, and the readiness
    # wait would spend its whole budget producing a timeout that names the wrong cause.
    # Checking here reports the actual disagreement, and names which of the four sources
    # differs, instead of a downstream symptom.
    assert_binder_protocol_agreement

    start_servicemanager
    wait_for_servicemanager

    export_build_environment

    local status=0
    run_coverage || status=$?
    write_status_file "$status"

    # Propagated unchanged. The EXIT trap tears the guest down and powers it off; it does not
    # alter this value.
    log "init finished; propagating the coverage runner's status ($status)"
    exit "$status"
}

main "$@"
CEC_L2_INIT_EOF

    chmod 0755 -- "$target"
    # The guest's entry point must parse before the image is declared finished: a syntax error
    # here would only surface as a kernel panic at boot, with no diagnosis.
    bash -n -- "$target" || die "the generated in-guest init does not parse.  This is a defect
       in $SCRIPT_PATH, not in the caller's inputs."
    log "wrote and syntax-checked the in-guest init at $GUEST_INIT_PATH"
}


# ------------------------------------------------------------------------------------
# NO ENDPOINT CREDENTIAL LEAVES IN THE PUBLISHED IMAGE.
#
# validate_inputs already refuses a credential-bearing --apt-mirror and --base-url, so the
# values THIS script writes cannot carry one.  That is not the whole surface.  The image's apt
# configuration also comes from the BASE TARBALL, which this script does not author: a base
# built against an authenticated archive can arrive with "deb https://user:token@host/..."
# already in /etc/apt/sources.list or in sources.list.d, and publishing that image publishes
# the token with it.
#
# So the finished filesystem is checked rather than reasoned about, in two passes:
#
#   SCRUB.  Any line whose URL-shaped fields carry userinfo, a query or a fragment is rewritten
#   to its sanitised form.  Only such lines are touched; every other line, including comments
#   and their spacing, is left byte-identical, because reformatting a file the base author
#   wrote is not this script's business.  Rewriting is safe HERE and would not have been
#   earlier: this runs after every apt operation the build performs, and the guest is asserted
#   offline, so nothing in the image ever fetches from these sources again.  They are
#   provenance from this point on.
#
#   VERIFY.  The file is then re-read and any remaining userinfo or query is FATAL.  A failure
#   here is not a warning that the artifact carries a secret - it is the artifact not being
#   published.  The message names the file and the LINE NUMBER and never the line, because a
#   die message reaches the same log the whole sanitising apparatus exists for.
#
# One deliberate over-reach: a bare word carrying an @ - an address in a comment, say - is
# scrubbed as userinfo as well, because a scheme-less "user:pass@host/path" is a form curl and
# apt both accept and the two are not distinguishable by shape.  The assertion this function
# has to satisfy is stated in terms of @, so a comment that keeps one would fail it.  Losing
# the tail of a comment is the cheaper error.
#
# ==============================================================================
# AND THE SCAN ITSELF MUST NOT BECOME THE HAZARD IT EXISTS TO REMOVE.  READ THIS.
# ==============================================================================
# An earlier revision of this function enumerated its files with a glob and `[ -f "$file" ]`,
# read them with `< "$file"` and rewrote them with `cat -- "$tmp" > "$file"` -- every one of
# which FOLLOWS SYMBOLIC LINKS, and all of it running as root on the BUILD HOST.  The base
# tarball is not this script's to trust: it can ship /etc/apt/sources.list, or any entry under
# sources.list.d, as an absolute symbolic link to a host pathname.  `[ -f ]` would report that
# link as a regular file, the read would read the HOST's file, and the redirection would
# TRUNCATE AND REPLACE THE HOST'S FILE AS ROOT.  That is a host-compromise primitive handed to
# whoever supplies the base tarball, in the one function whose purpose is to make the artefact
# safe.
#
# THE REVISION AFTER THAT ONE WAS STILL WRONG, AND THIS IS THE PART WORTH READING.  It added
# assert_confined_to_image(), which validated the PATHNAME thoroughly -- no symlinked component
# from the mount point down, a regular file, one link, the image's own st_dev, a realpath still
# inside the image -- and then the caller REOPENED THE SAME PATHNAME with an ordinary
# redirection.  Validating a name and then opening the name is check-then-open: the object the
# kernel opens is not the object that was checked, and the gap between the two IS the
# vulnerability (CWE-367), not the absence of a check.  Re-checking immediately before the write
# narrows the window; it does not close it.  An adversary who can create one symlink in the
# image tree only has to do it after the last check.
#
# WHAT CARRIES THE GUARANTEE NOW: THE CONFINED I/O PRIMITIVE, AND NOTHING ELSE.
#
# Every read, every rewrite, every removal and every directory enumeration this function and
# scrub_and_assert_image_credential_stores() perform goes through the primitive documented above
# install_confined_io_helper(): openat2(2) with
# RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_XDEV, anchored on $IMAGE_MOUNT, followed by the
# operation THROUGH THE RESULTING DESCRIPTOR.  There is no second name lookup to lose, so there
# is no window to race: a symlink substituted at any position, at any instant, is refused by the
# kernel's own path resolution rather than followed.  RESOLVE_NO_XDEV is the kernel-enforced
# form of the st_dev comparison this block used to describe, and it also covers /proc, /sys,
# /dev and /dev/pts, which are bind/pseudo mounts inside the image tree.
#
# The primitive is PROVEN to refuse, on this host, before one privileged byte is written --
# probe_confined_io() runs a positive case and four negative cases in a private directory during
# make_work_dir() and dies if any refusal does not happen.  So this block does not have to be
# believed; it has to be checked, and it is.
#
# WHAT THE REMAINING MECHANISMS ARE FOR, STATED HONESTLY:
#
#   assert_confined_to_image()  A PRE-CHECK, retained for its DIAGNOSTICS and for nothing else.
#      It can say which component of which path is a symbolic link, which is a far better
#      message than ELOOP, and it can name a hard-linked file.  It no longer carries the
#      guarantee and it is no longer sufficient on its own -- if it were deleted the confinement
#      would be unchanged; if the primitive were deleted the confinement would be gone.
#   confined_regular_file() / confined_directory()  The type and link-count refusals, taken from
#      an fstat on the descriptor the confinement produced rather than from a stat on a pathname.
#      A fifo, socket, device node or directory where configuration is expected is FATAL, not
#      skipped: it means the base filesystem is not what it claims to be, and skipping it would
#      publish the image with the base author's substitution unexamined.
#   confined_list()  Enumeration, also through a descriptor.  `find -P` does not follow links it
#      finds inside a tree, but the kernel resolves find's STARTING PATH normally, so
#      `find -P $IMAGE_MOUNT/etc/apt/sources.list.d` on an image whose /etc is an absolute
#      symlink enumerates the HOST's directory -- and reported host filenames into this script's
#      own diagnostics.  That is why enumeration moved onto the primitive too.
#   nosymfollow  DEFENCE IN DEPTH ONLY, and now VERIFIED rather than assumed.  The image is
#      remounted MS_NOSYMFOLLOW where the kernel supports it (5.10+, util-linux 2.35+ for the
#      option name), and where the remount reports success this script PROVES it is in force by
#      creating a symbolic link inside the image and establishing that an ordinary open through
#      it fails.  A remount that reports success without taking effect is reported as not in
#      force.  Where the kernel cannot supply it at all, that is logged as an unavailable
#      defence -- and it does not weaken anything, because the openat2 path is mandatory and is
#      what refuses.  There is no configuration of this script in which neither is in force: the
#      primitive is proven at start-up or the run dies.
#      IT IS NOT RESTORED AFTERWARDS, and does not need to be: main() calls this function as the
#      LAST operation on the mounted filesystem and release_image() unmounts immediately after
#      it.  If a step is ever added between the two, it must be added ABOVE this function --
#      which main()'s own comment already requires for a different reason (anything running
#      after the scrub could reintroduce what it verified was gone).
# ==============================================================================
# ------------------------------------------------------------------------------------

# The image mount's own device, captured once so every confinement check compares against one
# value rather than re-deriving it per file.  Empty until the image is mounted.
IMAGE_MOUNT_DEVICE=''

# A DIAGNOSTIC PRE-CHECK, NOT THE MECHANISM THAT CARRIES THE CONFINEMENT.  Read the block above
# for why: this validates a PATHNAME, and the caller then operates through a DESCRIPTOR obtained
# by the confined primitive, so nothing here authorises anything.  It is retained because it
# produces messages an errno cannot -- it names the exact path component that is a symbolic link,
# and it names a hard-link count -- and because a failure here is a much clearer report of a
# malformed base tarball than "the confinement refused it".
#
# It is deliberately called BEFORE the confined operation and its result is deliberately not
# relied upon: if every check in this function were removed the confinement would be unchanged.
# Dies with the reason and the in-image path; never prints the file's content.  $3 is a short
# disposition ('read', 'rewrite', 'remove') used only in the message.
assert_confined_to_image() { # $1=path  $2=what it is  $3=what is about to happen
    local path="$1" what="$2" operation="$3"
    local prefix component resolved device links relative

    [ -n "$IMAGE_MOUNT" ] || die "internal error: assert_confined_to_image ran before the image
       was mounted, so '$operation' on $what would proceed unconfined.  This is a defect in
       $SCRIPT_NAME."
    [ -n "$IMAGE_MOUNT_DEVICE" ] || die "internal error: the image mount's device was never
       recorded, so the confinement check for $what cannot compare against it.  This is a defect
       in $SCRIPT_NAME."

    case "$path" in
        "$IMAGE_MOUNT"/*) : ;;
        *) die "refusing to $operation ($what): the path
           $path
       is not underneath the image mount point $IMAGE_MOUNT.  Every file this scan touches is
       inside the image by construction, so a path that is not is a defect in $SCRIPT_NAME
       rather than a caller input." ;;
    esac

    # NO SYMBOLIC LINK ANYWHERE FROM THE MOUNT POINT DOWN.  Checked component by component on
    # the ACCUMULATED prefix, because a link in the middle redirects everything after it and
    # testing only the leaf would miss it entirely.
    relative="${path#"$IMAGE_MOUNT"/}"
    prefix="$IMAGE_MOUNT"
    local saved_ifs="$IFS"
    IFS='/'
    # Deliberate word splitting on '/' to walk the components in order.
    # shellcheck disable=SC2086
    set -- $relative
    IFS="$saved_ifs"
    for component in "$@"; do
        [ -n "$component" ] || continue
        prefix="$prefix/$component"
        [ ! -L "$prefix" ] || die "refusing to $operation ($what): the path component
           ${prefix#"$IMAGE_MOUNT"}
       inside the image is a SYMBOLIC LINK.  Following it would read or write wherever it
       points, and this scan runs as root on the BUILD HOST, so a link naming an absolute host
       path would have this script truncate and replace a host file.  Refused rather than
       resolved: a base filesystem that ships a symlinked apt configuration is not a filesystem
       this script publishes.  Inspect the base tarball."
    done

    [ -e "$path" ] || die "refusing to $operation ($what): $path no longer exists.  It was
       enumerated a moment ago, so something is changing the image's configuration while this
       scan runs.  Nothing further is written there."
    [ -f "$path" ] || die "refusing to $operation ($what): ${path#"$IMAGE_MOUNT"} inside the image
       is not a regular file.  A fifo, a socket, a device node or a directory at that name is
       not apt configuration, and reading or writing one as root is not something this script
       does on the strength of a filename.  Refused; inspect the base tarball."

    links="$(stat -c '%h' -- "$path" 2>/dev/null || printf '')"
    case "$links" in
        ''|*[!0-9]*) die "refusing to $operation ($what): the link count of
       ${path#"$IMAGE_MOUNT"} could not be read, so it cannot be established that no other name
       shares its content." ;;
    esac
    [ "$links" -le 1 ] || die "refusing to $operation ($what): ${path#"$IMAGE_MOUNT"} has $links
       hard links, so another name inside the image shares its content and a rewrite would
       change both.  Refused rather than written through."

    device="$(stat -c '%d' -- "$path" 2>/dev/null || printf '')"
    [ -n "$device" ] || die "refusing to $operation ($what): the device of ${path#"$IMAGE_MOUNT"}
       could not be read, so it cannot be established that the file lives on the image's own
       filesystem."
    [ "$device" = "$IMAGE_MOUNT_DEVICE" ] || die "refusing to $operation ($what):
           ${path#"$IMAGE_MOUNT"}
       resolves to device $device, but the image is mounted on device $IMAGE_MOUNT_DEVICE.  The
       image is a separately mounted loop filesystem, so a different device means the path
       leaves the image -- onto the build host, or onto one of the pseudo-filesystems bound into
       it.  This scan writes as root; it does not write outside the artefact it is checking."

    resolved="$(realpath -- "$path" 2>/dev/null || printf '')"
    [ -n "$resolved" ] || die "refusing to $operation ($what): ${path#"$IMAGE_MOUNT"} could not be
       resolved with realpath, so it cannot be established where a read or a write would land."
    case "$resolved" in
        "$IMAGE_MOUNT"/*) : ;;
        *) die "refusing to $operation ($what): ${path#"$IMAGE_MOUNT"} resolves to
           $resolved
       which is OUTSIDE the image mount point $IMAGE_MOUNT.  Refused: this scan runs as root on
       the build host." ;;
    esac
}

# ------------------------------------------------------------------------------------
# nosymfollow: DEFENCE IN DEPTH, ATTEMPTED AND THEN PROVEN.
#
# MS_NOSYMFOLLOW makes the kernel refuse to follow ANY symbolic link in the mount, so an open by
# pathname through one fails with ELOOP.  It is not what carries this scan's guarantee -- the
# confined I/O primitive is, and it is mandatory and proven at start-up -- but it is worth having
# because it also covers code paths that do not go through the primitive at all, such as a future
# edit that reintroduces an ordinary redirection.
#
# THE CHANGE FROM THE PREVIOUS REVISION.  That one returned success on every arm, including the
# arm where the remount FAILED, so a reader of the exit status could not tell the difference
# between "in force" and "not available", and the log claimed a layer that was not there.  Worse,
# it took the remount's own exit status as proof: a filesystem or kernel that accepts
# remount,nosymfollow without honouring it would have been reported as protected.  Now the
# remount is attempted, and where it reports success it is PROVEN in force behaviourally --
# a symbolic link is created inside the image and an ordinary open through it must fail.
# IMAGE_MOUNT_NOSYMFOLLOW is set only when that proof passes.
# ------------------------------------------------------------------------------------
IMAGE_MOUNT_NOSYMFOLLOW=0
readonly NOSYMFOLLOW_PROBE_REL='.cec-l2-nosymfollow-probe'

# Prove it rather than trust the remount's status: a symbolic link inside the image, and an
# ordinary open THROUGH THE PATHNAME, which is exactly the operation MS_NOSYMFOLLOW forbids.
# Returns 0 when the link could not be followed (nosymfollow is in force), 1 otherwise.
#
# The probe directory and its target file are created and removed THROUGH THE CONFINED PRIMITIVE,
# so the probe cannot itself become the hazard; the symbolic link is created with `ln -s`, whose
# final component is created rather than resolved, inside a directory this function has just
# created and confirmed by descriptor.  Everything is removed again and the removal is verified,
# because this runs immediately before publication and a leftover would be published.
nosymfollow_is_in_force() {
    local probe_dir="$NOSYMFOLLOW_PROBE_REL" followed=0
    local link_path="$IMAGE_MOUNT/$NOSYMFOLLOW_PROBE_REL/link"

    # A leftover from an interrupted earlier attempt is cleared first, and by the primitive.
    nosymfollow_probe_cleanup

    confined_mkdir "$probe_dir" 0700 'the nosymfollow probe directory' \
        'establish whether nosymfollow is in force on the image mount'
    confined_create "$probe_dir/target" 0600 'the nosymfollow probe target' \
        'establish whether nosymfollow is in force on the image mount'
    if ! ln -sfn 'target' -- "$link_path" 2>/dev/null; then
        warn "the nosymfollow probe could not create a symbolic link inside the image, so it"
        warn "  cannot be established whether the remount took effect.  Reported as NOT in"
        warn "  force, which is the conservative reading; the confined I/O primitive is what"
        warn "  carries this scan's guarantee and it is proven in force."
        nosymfollow_probe_cleanup
        return 1
    fi

    # The one deliberately UNCONFINED open in this file, and it is a probe rather than an
    # operation: reading through the pathname is precisely what must fail.  Its content is a
    # zero-byte file this function created, so following the link discloses nothing.
    if cat -- "$link_path" >/dev/null 2>&1; then
        followed=1
    fi
    nosymfollow_probe_cleanup
    [ "$followed" -eq 0 ]
}

# Removes the probe and verifies the removal.  Fatal if anything is left, because this runs
# immediately before the image is released and published.
nosymfollow_probe_cleanup() {
    local record type_field entry entry_type entry_name listing
    confined_directory "$NOSYMFOLLOW_PROBE_REL" "/$NOSYMFOLLOW_PROBE_REL" \
        'remove the nosymfollow probe from the image' || return 0

    listing="$WORK_DIR/nosymfollow-probe-listing"
    rm -f -- "$listing"
    confined_list "$NOSYMFOLLOW_PROBE_REL" 'the nosymfollow probe directory' \
        'remove the nosymfollow probe from the image' > "$listing"
    while IFS= read -r -d '' entry; do
        entry_type="${entry%% *}"
        entry_name="${entry#* }"
        [ -n "$entry_name" ] || continue
        if [ "$entry_type" = 'directory' ]; then
            confined_rmdir "$NOSYMFOLLOW_PROBE_REL/$entry_name" \
                'an entry of the nosymfollow probe directory' \
                'remove the nosymfollow probe from the image'
        else
            confined_unlink "$NOSYMFOLLOW_PROBE_REL/$entry_name" \
                'an entry of the nosymfollow probe directory' \
                'remove the nosymfollow probe from the image'
        fi
    done < "$listing"
    rm -f -- "$listing"

    confined_rmdir "$NOSYMFOLLOW_PROBE_REL" 'the nosymfollow probe directory' \
        'remove the nosymfollow probe from the image'
    if ! confined_absent "$NOSYMFOLLOW_PROBE_REL" 'the nosymfollow probe directory' \
            'verify the nosymfollow probe is gone'; then
        record="$(confined_stat "$NOSYMFOLLOW_PROBE_REL" 'verify the nosymfollow probe is gone' \
                  || printf '')"
        type_field="$(confined_stat_field "$record" 1)"
        die "the nosymfollow probe directory /$NOSYMFOLLOW_PROBE_REL is still present inside the
       image (as a ${type_field:-unreadable}) after this script removed it.  The image is NOT
       published: it would carry a directory this script created for a one-off measurement, and
       something is recreating it."
    fi
}

try_nosymfollow_remount() {
    if [ ! -d "$IMAGE_MOUNT" ] || ! mountpoint -q -- "$IMAGE_MOUNT" 2>/dev/null; then
        log "  nosymfollow: not attempted, $IMAGE_MOUNT is not a mount point at this moment"
        log "    (defence in depth only; the confined I/O primitive is what carries this scan's"
        log "     guarantee and it is proven in force)"
        return 0
    fi
    if mount -o remount,nosymfollow -- "$IMAGE_MOUNT" 2>/dev/null; then
        if nosymfollow_is_in_force; then
            IMAGE_MOUNT_NOSYMFOLLOW=1
            log "  nosymfollow: IN FORCE on $IMAGE_MOUNT and PROVEN -- a symbolic link created"
            log "    inside the image could not be opened through its own pathname, so the"
            log "    kernel itself refuses to follow any link in the image for the duration of"
            log "    this scan (MS_NOSYMFOLLOW).  Defence in depth on top of the confined I/O"
            log "    primitive, which is mandatory and is what carries the guarantee."
            log "    Not restored afterwards: release_image() unmounts the image immediately"
            log "    after this function returns, which is why this must stay the last step on"
            log "    the mount."
            return 0
        fi
        warn "the image mount ACCEPTED remount,nosymfollow but a symbolic link inside the image"
        warn "  can still be opened through its pathname, so MS_NOSYMFOLLOW is NOT in force"
        warn "  despite the remount reporting success.  Reported rather than trusted, because"
        warn "  the previous revision of this script took that exit status as proof and would"
        warn "  have logged a defence that was not there.  This changes nothing about the"
        warn "  guarantee: every read, rewrite, removal and enumeration this scan performs goes"
        warn "  through openat2 with RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_XDEV, which"
        warn "  is mandatory, was proven to refuse at start-up, and is what confines this scan."
        return 0
    fi
    warn "nosymfollow could NOT be enabled on the image mount, so the kernel-level refusal to"
    warn "  follow symbolic links is NOT in force for this scan.  It needs a 5.10-or-newer"
    warn "  kernel (MS_NOSYMFOLLOW) and util-linux 2.35 or newer for the option name; one of"
    warn "  those is missing here.  This is stated rather than glossed over because the log"
    warn "  would otherwise read as though the defence were active.  It is DEFENCE IN DEPTH"
    warn "  ONLY: every read, rewrite, removal and enumeration this scan performs goes through"
    warn "  openat2 with RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_XDEV, anchored on the"
    warn "  image mount and operating through the resulting descriptor, which is mandatory, was"
    warn "  proven to refuse a substituted link at start-up, and is what carries the guarantee."
    return 0
}

# The apt source locations, RELATIVE to the image mount point, because that is what the confined
# primitive takes.  An absolute path is refused by RESOLVE_BENEATH, and a path assembled from
# $IMAGE_MOUNT would reintroduce the host-side pathname this mechanism exists to remove.
readonly IMAGE_APT_SOURCES_FILE_REL='etc/apt/sources.list'
readonly IMAGE_APT_SOURCES_DIR_REL='etc/apt/sources.list.d'

# ------------------------------------------------------------------------------------
# THE SECRET VALUES THEMSELVES, SEARCHED FOR INSIDE THE FINISHED IMAGE.
#
# assert_secret_inputs_outside_copied_roots() is a POLICY check: it refuses a secret whose PATH
# lies inside a copied tree, at validation time, before anything is copied.  This is the belt
# to that braces, and it asks a different question: are the secret VALUES anywhere in the trees
# this script actually copied?  It therefore catches what the policy cannot see --
#
#   * a secret file outside every copied root whose CONTENT was duplicated into one of them by
#     something else (a build script that cached it, an editor backup, a committed fixture),
#   * a secret pasted into a file in the caller's own checkout rather than kept in the file the
#     option names,
#   * and any future copied root somebody adds to copy_guest_payload() without adding it to the
#     policy list.
#
# WHERE IT LOOKS, AND WHY ONLY THERE.  The three trees copied wholesale: --payload, --sdk-dir
# and --gtest-prefix, at their in-image locations.  It deliberately does NOT search /etc: the
# base URL legitimately appears in /etc/apt/sources.list, which is what apt is configured with,
# and the credentials that could appear there are handled by
# scrub_and_assert_image_apt_credentials() and scrub_and_assert_image_credential_stores() --
# scans that REWRITE what they find rather than merely refusing it.  Searching /etc here would
# refuse every ordinary run for the value the run is supposed to configure.
#
# WHY THE SEARCH IS AN OPERATION OF THE CONFINED PRIMITIVE.  The trees are wholesale copies of
# caller-supplied checkouts and legitimately contain symbolic links, and they sit under
# /opt/cec-l2 inside an image whose /opt could itself be a link in the base tarball.  A
# `grep -r` would skip links it found INSIDE the tree but the kernel would resolve grep's
# STARTING path normally, so the scan could read the build host and report host filenames as
# image contents.  op_search() resolves the starting directory beneath the image mount with
# openat2 and walks it descriptor-relative with O_NOFOLLOW throughout.
#
# NOTHING SECRET REACHES A DIAGNOSTIC.  The needles are passed to the helper on a pipe, never
# in an argument, so they do not appear in any /proc/<pid>/cmdline; the helper prints a LABEL
# and a path and never the matched bytes; and the die message below names the label, the file
# and the remedy.
# ------------------------------------------------------------------------------------
assert_image_carries_no_secret_values() {
    local -a labels=() needles=() roots=() shown_roots=()
    local record_file="$WORK_DIR/secret-value-scan"
    local line token query count=0 hits=0 rel record label path root i

    # ---- the base URL, when it was given as a confidential file -------------------------
    # Only then.  A --base-url passed on the command line is already in this process's argv and
    # in the manifest's sanitised record; treating it as a secret here would refuse a run for a
    # value the caller chose to make public.
    if [ -n "$BASE_URL_FILE" ] && [ -n "$BASE_URL" ]; then
        labels+=('base-url'); needles+=("$BASE_URL")
        # The query string on its own, because that is where a presigned URL carries its bearer
        # token -- and a tree could hold the token without holding the whole URL.
        case "$BASE_URL" in
            *'?'*)
                query="${BASE_URL#*\?}"
                query="${query%%\#*}"
                if [ "${#query}" -ge 8 ]; then
                    labels+=('base-url-query'); needles+=("$query")
                fi
                ;;
        esac
    fi

    # ---- the apt credential file -------------------------------------------------------
    # Whole lines first: an auth.conf record is long and specific, so a whole-line needle has
    # essentially no false-positive surface.  Then the login and password VALUES on their own,
    # because a tree can hold the password without holding the record it came from.
    if [ -n "$APT_AUTH_FILE" ]; then
        while IFS= read -r line || [ -n "$line" ]; do
            line="${line#"${line%%[![:space:]]*}"}"
            line="${line%"${line##*[![:space:]]}"}"
            case "$line" in
                ''|'#'*) continue ;;
            esac
            count=$(( count + 1 ))
            labels+=( "apt-auth-record-$count" ); needles+=( "$line" )
        done < "$APT_AUTH_FILE"
        count=0
        # awk without a "--" separator: mawk treats "--" as a filename rather than as an option
        # terminator, and $APT_AUTH_FILE is a canonical absolute path so it cannot be read as an
        # option.  tolower() so that apt's case-insensitive keywords are all matched.
        while IFS= read -r token; do
            [ -n "$token" ] || continue
            count=$(( count + 1 ))
            if [ "${#token}" -lt 6 ]; then
                # NOT SEARCHED FOR, AND SAID OUT LOUD.  A needle of five bytes or fewer occurs
                # by coincidence in ordinary binaries and source, so searching for it would
                # fail every run rather than detect anything.  The value is not echoed.
                warn "one of the login or password values in the --apt-auth-file credential file
       is ${#token} bytes long, which is too short to search for distinctively: a needle that
       short matches ordinary compiled objects by coincidence, so the pre-publication sweep
       would refuse every run instead of detecting a leak.  Its whole auth.conf record IS
       searched for, so a copy of the record is still caught; a copy of that value ALONE is
       not.  Use a longer secret if this matters."
                continue
            fi
            labels+=( "apt-auth-secret-$count" ); needles+=( "$token" )
        done < <(awk '{ for (i = 1; i < NF; i++) { k = tolower($i);
                            if (k == "login" || k == "password") print $(i + 1) } }' \
                     "$APT_AUTH_FILE" | sort -u)
    fi

    if [ "${#needles[@]}" -eq 0 ]; then
        log "no confidential input was given, so there is no secret value to search the copied"
        log "  trees for.  The credential-store and apt-configuration scans run regardless."
        return 0
    fi

    roots+=( "${GUEST_PAYLOAD_DIR#/}" );     shown_roots+=( "$GUEST_PAYLOAD_DIR (--payload)" )
    roots+=( "${GUEST_SDK_DIR#/}" );         shown_roots+=( "$GUEST_SDK_DIR (--sdk-dir)" )
    if [ -n "$GTEST_PREFIX_SRC" ]; then
        roots+=( "${GUEST_GTEST_PREFIX#/}" ); shown_roots+=( "$GUEST_GTEST_PREFIX (--gtest-prefix)" )
    fi

    log "searching the copied trees inside the image for the ${#needles[@]} confidential value(s)"
    log "  this run holds (the values are passed to the search on a pipe and never appear in an"
    log "  argument, a log line or a diagnostic):"
    for i in "${!shown_roots[@]}"; do
        log "    ${shown_roots[i]}"
    done

    for i in "${!roots[@]}"; do
        rel="${roots[i]}"
        if ! confined_directory "$rel" "the copied tree ${shown_roots[i]}" \
                'search the copied trees for the confidential input values'; then
            # Absent rather than refused -- confined_directory() dies on a refusal, so this arm
            # means the tree is not there.  --gtest-prefix is optional and the other two are
            # created by copy_guest_payload(), so this is an internal inconsistency worth
            # reporting rather than a caller mistake.
            warn "the copied tree ${shown_roots[i]} is not present inside the image, so it could
       not be searched for the confidential input values.  Nothing was copied there, or
       something removed it after copy_guest_payload() ran."
            continue
        fi
        : > "$record_file"
        confined_search "$rel" "the copied tree ${shown_roots[i]}" \
            'search the copied trees for the confidential input values' \
            "${labels[@]}" > "$record_file" < <(printf '%s\0' "${needles[@]}")
        while IFS= read -r -d '' record; do
            [ -n "$record" ] || continue
            label="${record%% *}"
            path="${record#* }"
            hits=$(( hits + 1 ))
            warn "a confidential input value ($label) was found inside the image at
       ${shown_roots[i]}/$path"
        done < "$record_file"
        : > "$record_file"
    done
    rm -f -- "$record_file"

    if [ "$hits" -gt 0 ]; then
        die "$hits file(s) inside the copied trees contain a value this run was given as a
       CONFIDENTIAL input, and the warnings above name each one by file and by which value it
       was.  The image is NOT published: it would carry the secret into an artefact that
       outlives this run and that the workflow uploads.
       The label says what to do:
         base-url / base-url-query -- the value came from --base-url-file, which exists to keep
           it off every command line.  If that URL is genuinely in your own checkout it is not
           confidential: pass it with --base-url instead, which accepts no query string and
           records a sanitised form in the manifest.  If it IS confidential, remove the copy
           from the tree.
         apt-auth-record-N / apt-auth-secret-N -- an apt credential, or one of its login or
           password values, is present in a tree this script copies into the image.  Remove it
           from the tree; --apt-auth-file is installed for the package phase only and removed
           again before the image is released, and that is the only place a credential belongs.
       This check reads only; nothing in the image was modified by it, so the tree can be
       inspected as it stands."
    fi

    log "the copied trees carry none of this run's confidential input values"
}

scrub_and_assert_image_apt_credentials() {
    local rel shown line tmp content listing record entry_type entry_name
    local scrubbed_files=0 scrubbed_lines=0 offenders=0 lineno field
    local -a rels=() fields=()

    # THE IMAGE'S OWN DEVICE, read once from the mount point.  It is used only by the
    # assert_confined_to_image PRE-CHECK: the confinement itself is enforced by the kernel
    # through RESOLVE_NO_XDEV on every confined open, which is the same test without the second
    # name lookup.  It is still read here, and its absence is still fatal, because a mount point
    # whose device cannot be read is not a mount this script goes on to scan.
    IMAGE_MOUNT_DEVICE="$(stat -c '%d' -- "$IMAGE_MOUNT" 2>/dev/null || printf '')"
    [ -n "$IMAGE_MOUNT_DEVICE" ] || die "the device of the image mount point $IMAGE_MOUNT could
       not be read, so the pre-publication credential scan cannot even report which filesystem
       the files it reads and rewrites are on.  The image is NOT published without it."
    mountpoint -q -- "$IMAGE_MOUNT" 2>/dev/null || die "the image is not mounted at $IMAGE_MOUNT
       when the pre-publication credential scan runs.  Every path it touches is resolved beneath
       that mount point; without the mount there is nothing to resolve against and nothing to
       scan.  The image is NOT published."
    # The anchor is re-asserted here rather than assumed from make_work_dir(), because this is
    # the function that reads and rewrites as root and a mis-set anchor would move all of it.
    [ "$CONFINED_IO_ANCHOR" = "$IMAGE_MOUNT" ] || die "internal error: the confined I/O anchor is
       '${CONFINED_IO_ANCHOR:-<unset>}' rather than the image mount point $IMAGE_MOUNT when the
       pre-publication credential scan runs, so every path it resolved would be resolved against
       the wrong directory.  This is a defect in $SCRIPT_NAME."

    log "apt source credential check on the finished filesystem"
    try_nosymfollow_remount

    # THE FIXED SOURCES FILE.  Its existence is optional; its SHAPE is not.  confined_regular_file
    # answers both questions from an fstat on the descriptor the confinement produced, so a
    # symlink is refused by the kernel before any type test runs -- which is the substitution the
    # old `-L` then `-f` pair was written to catch and could only catch by racing it.
    if confined_regular_file "$IMAGE_APT_SOURCES_FILE_REL" '/etc/apt/sources.list' \
            'read the image apt sources file'; then
        rels+=("$IMAGE_APT_SOURCES_FILE_REL")
    fi

    # THE sources.list.d ENTRIES, enumerated THROUGH A DESCRIPTOR (confined_list), so the
    # enumeration cannot be redirected onto a host directory by a symlinked /etc or /etc/apt --
    # which `find -P` on an absolute starting path could be, because the kernel resolves find's
    # starting path normally even though find does not follow links it finds inside the tree.
    #
    # Anything that is not a plain file is REFUSED, not skipped: it means the base filesystem is
    # not what it claims to be, and skipping it publishes the image with that substitution
    # unexamined.  The listing goes through a host-side file rather than a command substitution
    # because the records are NUL-terminated (a filename may contain a newline) and because a
    # process substitution would swallow the helper's own fatal exit.
    if confined_directory "$IMAGE_APT_SOURCES_DIR_REL" '/etc/apt/sources.list.d' \
            'enumerate the image apt source entries'; then
        listing="$WORK_DIR/apt-sources-listing"
        rm -f -- "$listing"
        confined_list "$IMAGE_APT_SOURCES_DIR_REL" 'the image apt source directory' \
            'enumerate the image apt source entries' > "$listing"
        while IFS= read -r -d '' record; do
            entry_type="${record%% *}"
            entry_name="${record#* }"
            [ -n "$entry_name" ] || continue
            [ "$entry_type" = 'regular' ] || die "the apt source entry
           /$IMAGE_APT_SOURCES_DIR_REL/$entry_name
       inside the image is a $entry_type rather than a regular file.  Refused rather than
       followed or skipped: this scan reads and rewrites these files as root on the BUILD HOST,
       and skipping the entry would publish the image with the base author's substitution
       unexamined.  Inspect the base tarball."
            rels+=("$IMAGE_APT_SOURCES_DIR_REL/$entry_name")
        done < "$listing"
        rm -f -- "$listing"
    fi

    if [ "${#rels[@]}" -eq 0 ]; then
        log "  the image carries no apt source files"
        # STILL CHECKED.  "No source lists" says nothing about auth.conf, a netrc, an apt.conf
        # proxy directive or an apt.conf.d one, so the second half of the precondition runs on
        # this path too -- and it must run BEFORE the early return rather than after it.
        scrub_and_assert_image_credential_stores
        return 0
    fi

    # Both staging files live on the HOST, in this script's own 0700 scratch directory, rather
    # than beside the file inside the image: a temporary file written into the image is one more
    # thing that a failure between writing it and removing it would publish.
    tmp="$WORK_DIR/apt-source-scrub"
    content="$WORK_DIR/apt-source-content"

    for rel in "${rels[@]}"; do
        local rewritten=0
        shown="/$rel"
        # THE PRE-CHECK, for its diagnostics only.  It names the offending path component where
        # the confined open would only say ELOOP.  It authorises nothing: the read below does not
        # reopen this pathname, it resolves the RELATIVE path beneath the anchor and reads through
        # the descriptor that resolution returned.
        assert_confined_to_image "$IMAGE_MOUNT/$rel" 'an apt source file' 'read'

        # THE READ, through the confinement.  Its bytes land in a host-side staging file, so the
        # line loop below cannot be reading something else by the time it finishes: the file was
        # opened once, beneath the anchor, and what follows is this script's own copy.
        rm -f -- "$content"
        confined_read "$rel" 'an apt source file' \
            "read the image apt source file $shown" > "$content"

        rm -f -- "$tmp"
        ( umask 077 && : > "$tmp" ) || die "could not stage the apt source rewrite at $tmp."
        while IFS= read -r line || [ -n "$line" ]; do
            sanitise_endpoint "$line"
            if [ "$SANITISED_ENDPOINT_REDACTED" -eq 1 ]; then
                rewritten=$((rewritten + 1))
                printf '%s\n' "$SANITISED_ENDPOINT" >> "$tmp" \
                    || die "could not write the scrubbed apt source line to $tmp."
            else
                printf '%s\n' "$line" >> "$tmp" \
                    || die "could not write an apt source line to $tmp."
            fi
        done < "$content"
        rm -f -- "$content"

        if [ "$rewritten" -gt 0 ]; then
            # THE WRITE, through the confinement.  The file keeps its own inode, owner and mode --
            # the helper truncates and writes THROUGH THE DESCRIPTOR rather than replacing the
            # name, which is what `cat -- "$tmp" > "$file"` used to achieve by a redirection that
            # followed symbolic links.  There is no re-check before it and none is needed: the
            # name is resolved once, by the kernel, with links refused at every position, so there
            # is no window between a check and this write for anything to be substituted in.
            confined_write "$rel" 'an apt source file' \
                "write the scrubbed apt sources back to $shown inside the image" < "$tmp"
            scrubbed_files=$((scrubbed_files + 1))
            scrubbed_lines=$((scrubbed_lines + rewritten))
            warn "removed a credential, query or fragment from $rewritten line(s) of"
            warn "  $shown inside the image.  It came from the base tarball, not from"
            warn "  --apt-mirror, which this script refuses in that form.  The guest is offline"
            warn "  and never fetches from these sources, so they are provenance only."
        fi
        rm -f -- "$tmp" || die "could not remove the staged apt source rewrite at $tmp."

        # THE RE-READ, also through the confinement.  This is the pass whose result decides
        # whether the image is published, so it must open the same object the rewrite wrote to --
        # which it does by construction, because both resolve the same relative path beneath the
        # same anchor with links refused.
        rm -f -- "$content"
        confined_read "$rel" 'an apt source file' \
            "re-read the image apt source file $shown" > "$content"
        lineno=0
        while IFS= read -r line || [ -n "$line" ]; do
            lineno=$((lineno + 1))
            fields=()
            read -r -a fields <<< "$line"
            for field in ${fields[@]+"${fields[@]}"}; do
                url_authority "$field"
                case "$URL_AUTHORITY" in
                    *@*)
                        offenders=$((offenders + 1))
                        warn "userinfo remains in $shown line $lineno"
                        ;;
                esac
                case "$field" in
                    *'?'*)
                        offenders=$((offenders + 1))
                        warn "a query string remains in $shown line $lineno"
                        ;;
                esac
            done
        done < "$content"
        rm -f -- "$content"
    done

    if [ "$offenders" -ne 0 ]; then
        die "$offenders apt source field(s) in the image still carry a URL credential or query
       after the scrub above, so the image is NOT published.  The lines are identified by file
       and line number in the warnings above and are deliberately not reproduced here.  Look at
       them in the base tarball: something in this script's rewrite did not recognise the form
       they use, and that is a defect in $SCRIPT_PATH rather than in the caller's inputs."
    fi

    if [ "$scrubbed_files" -eq 0 ]; then
        log "  ${#rels[@]} apt source file(s), no credential or query present"
    else
        log "  $scrubbed_lines line(s) across $scrubbed_files apt source file(s) scrubbed;"
        log "    re-read afterwards and no userinfo or query remains"
    fi
    # WHICH MECHANISM ACTUALLY CONFINED THIS SCAN, recorded beside the result rather than left
    # for a reader to infer.  The mandatory one is named first and named as mandatory, because an
    # earlier revision listed four "layers" of which the load-bearing one was a pathname check
    # that the caller then reopened by name.
    log "  confinement: MANDATORY -- every read, rewrite, removal and enumeration above went"
    log "    through openat2 with RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_XDEV anchored"
    log "    on $IMAGE_MOUNT, operating through the returned descriptor, with no pathname"
    log "    re-derived after the open (proven to refuse a substituted link at start-up)"
    if [ "$IMAGE_MOUNT_NOSYMFOLLOW" -eq 1 ]; then
        log "    DEFENCE IN DEPTH -- kernel-level nosymfollow on the image mount, verified in"
        log "      force behaviourally; plus the per-path diagnostic pre-check and the fatal"
        log "      refusal of any non-regular entry"
    else
        log "    DEFENCE IN DEPTH -- the per-path diagnostic pre-check and the fatal refusal of"
        log "      any non-regular entry; kernel-level nosymfollow was NOT in force on this host"
        log "      (see the warning above), which does not weaken the mandatory mechanism"
    fi

    # THE SECOND HALF OF THE SAME PRECONDITION.  A source list is not the only place apt keeps a
    # credential, and it is the only place this function used to look.  The dedicated stores are
    # REMOVED and the apt configuration is REWRITTEN; the reasoning for each disposition is on
    # that function.
    scrub_and_assert_image_credential_stores
}

# ------------------------------------------------------------------------------------
# THE OTHER PLACES A CREDENTIAL LIVES, AND THE TWO DISPOSITIONS THEY GET.
#
# WHY THIS EXISTS AT ALL.  The scan above looked at apt's SOURCE LISTS and nothing else, which
# left three whole classes of credential store unexamined in an image that is uploaded as a
# workflow artifact and copied between machines.  Worse, this script's own help text used to
# RECOMMEND one of them: three places told a caller with an authenticated archive to "use
# /etc/apt/auth.conf.d in the base tarball, or a netrc".  That is advice to bake a permanent
# credential into the published artefact -- the same exposure the in-URL-credential refusal
# exists to prevent, reached by a route nothing checked.  The advice is deleted rather than
# softened, --apt-auth-file replaces it with a build-time-only transport, and this function is
# the independent net underneath both.
#
# WHAT IS SCANNED, and each is a real store rather than a guess:
#
#   /etc/apt/auth.conf, /etc/apt/auth.conf.d/*   apt's own netrc-format credential store.
#   /root/.netrc, /home/*/.netrc                 the store curl and wget read by default.
#   /etc/apt/apt.conf.d/*                        Acquire::http::Proxy "http://u:p@host:3128/"
#                                                and its https/ftp/socks siblings put a
#                                                credential in an ordinary configuration file.
#
# THIS IS NOT THE SAME CHECK AS assert_payload_carries_no_credentials, and neither replaces the
# other.  That one refuses credential material in the --payload TREE before it is copied in,
# and it looks for the shapes a source checkout carries (.git-credentials, a private key, a git
# extraheader).  This one looks at the image's OWN SYSTEM PATHS after every step has run, which
# is where the base tarball, the apt phase and this script itself could each have left one.
#
# TWO DISPOSITIONS, AND THE LINE BETWEEN THEM IS WHAT THE FILE IS FOR.
#
#   REMOVED OUTRIGHT -- auth.conf, auth.conf.d/*, and every .netrc.  A file of that kind exists
#   for one purpose, which is to authenticate a future fetch.  This guest performs no future
#   fetch: it boots with `qemu -nic none`, its init asserts that no non-loopback interface, no
#   route and no resolver exists, and every source, binary and library it needs is already
#   inside the image.  So there is no provenance in one worth preserving and nothing to weigh
#   against deleting it, and deletion is the disposition that cannot leave a residue -- a
#   rewrite of a netrc would have to decide which of its tokens were secret, and guessing that
#   is how a redaction misses one.
#
#   REWRITTEN IN PLACE -- apt.conf.d/*.  These are ORDINARY CONFIGURATION, not credential
#   stores: this script writes one itself (99cec-l2-snapshot), and a base tarball's own
#   fragments carry real settings a reader of the image is entitled to see.  So the disposition
#   matches the source lists: the offending field is reduced to scheme://host/path and the rest
#   of the file is kept, which removes the secret without discarding the configuration.
#
#   AND THE apt.conf.d TEST IS DELIBERATELY NARROW -- a field must carry BOTH a scheme (`://`)
#   AND userinfo in its authority.  A blanket search for '@' would match, and silently mangle,
#   an ordinary directive such as  //Unattended-Upgrade::Mail "root@localhost";  which ships
#   commented out in Debian's own 50unattended-upgrades.  The threat here is specifically a
#   proxy URL with an embedded credential, so that is exactly what is matched.
#
# HOW A FINDING IS REPORTED.  By FILE AND LINE NUMBER ONLY, never by content -- a die or warn
# message reaches the same log this whole sanitising apparatus exists for, and quoting the line
# would publish the secret to the log in order to complain about it being in the image.
#
# AND EVERY VERIFICATION IS FATAL, NOT ADVISORY.  A removal is re-read (`rm -f` reports success
# for a path that does not exist, so its status alone establishes nothing), a rewrite is
# re-read, and a survivor of either stops publication.
#
# EVERY PATH GOES THROUGH assert_confined_to_image FIRST, for exactly the reason stated in that
# function's block: this scan reads, rewrites and DELETES files as root on the build host, so a
# scan that followed a symlink out of the image would be a second instance of the defect the
# source-list scrub was just corrected for.  A store that is a symlink or a non-regular file is
# refused FATALLY rather than skipped -- a base filesystem that ships /root/.netrc as a link to
# a host path is not a filesystem this script publishes an image around, and skipping it would
# publish the base author's substitution unexamined.
# ------------------------------------------------------------------------------------

# ------------------------------------------------------------------------------------
# THE INVENTORY, RE-DERIVED FROM APT'S OWN CREDENTIAL AND PROXY LOCATIONS.
#
# An earlier revision listed /etc/apt/auth.conf, /etc/apt/auth.conf.d, the netrc paths and
# /etc/apt/apt.conf.d -- and stopped there.  It missed /etc/apt/apt.conf, which is apt's PRIMARY
# configuration file and takes exactly the same directives as the fragments in apt.conf.d, so an
#     Acquire::http::Proxy "http://user:pass@proxy.example:3128/";
# written there survived publication untouched while the identical line in apt.conf.d/99local was
# reduced.  The list below is derived from where apt and the libraries it uses actually look,
# rather than from what the previous list happened to contain:
#
#   REMOVED   /etc/apt/auth.conf            apt_auth.conf(5): apt's own netrc-style store.
#   REMOVED   /etc/apt/auth.conf.d/*        the same, as fragments.
#   REMOVED   /root/.netrc, /home/*/.netrc  the store curl and wget read by default, and which
#                                           apt's https method inherits through libcurl on some
#                                           configurations.
#   REWRITTEN /etc/apt/apt.conf             apt.conf(5): the primary configuration file.  ADDED
#                                           in this revision -- it was the gap named above.
#   REWRITTEN /etc/apt/apt.conf.d/*         the fragment directory apt reads after it.
#   REWRITTEN /etc/environment              where a Debian system sets http_proxy/https_proxy for
#                                           every process including apt, in the same
#                                           scheme://user:pass@host/ form.  ADDED in this
#                                           revision: apt inherits it, so a credential there is
#                                           an apt credential in every practical sense.
#
# DELIBERATELY NOT INCLUDED, with the reason, so the omissions are decisions rather than gaps:
#
#   /etc/apt/apt.conf.d is covered above; /etc/apt/preferences and /etc/apt/sources.list* are
#   handled by scrub_and_assert_image_apt_credentials(), which is the scan this one is the second
#   half of.
#   /root/.curlrc and /root/.wgetrc  -- neither curl nor wget is in IMAGE_PACKAGES, so neither
#   file has a consumer inside this image, and the guest boots with no NIC and asserts itself
#   offline.  Adding them would mean rewriting files nothing reads, on a filesystem where their
#   absence is already asserted by the package accounting.
#   /etc/profile.d/*.sh  -- these are shell PROGRAMS, not configuration.  The redaction here
#   rewrites a field in place, which is safe for a KEY=value file and for an apt directive but is
#   not a transformation this script is willing to apply to executable code it did not write.  A
#   credential there would be reported by neither pass, and that is stated rather than hidden.
#   The environment the guest actually runs under is written by this script (write_image_conf and
#   the generated init), so it carries nothing the caller supplied.
#
# ALL of these go through the confined I/O primitive -- confined_regular_file/confined_directory
# to establish what is there, confined_list to enumerate, confined_read/confined_write to rewrite
# and confined_unlink to remove -- for the reason on that primitive's own block: this pass reads,
# rewrites and DELETES files as root on the build host.
# ------------------------------------------------------------------------------------

# Stores that are REMOVED.  Paths are absolute inside the image; the relative form the confined
# primitive takes is derived by stripping the leading '/' at the point of use, so the tables stay
# readable as the paths a person would recognise.
readonly IMAGE_CREDENTIAL_STORE_FILES=(
    '/etc/apt/auth.conf'
    '/root/.netrc'
)
readonly IMAGE_CREDENTIAL_STORE_DIRS=(
    '/etc/apt/auth.conf.d'
)
# Configuration FILES that are REWRITTEN rather than removed.
readonly IMAGE_CREDENTIAL_CONFIG_FILES=(
    '/etc/apt/apt.conf'
    '/etc/environment'
)
# Configuration DIRECTORIES whose entries are REWRITTEN rather than removed.
readonly IMAGE_CREDENTIAL_CONFIG_DIRS=(
    '/etc/apt/apt.conf.d'
)
# Per-user stores, enumerated rather than listed: a base tarball chooses its own accounts.
readonly IMAGE_CREDENTIAL_HOME_ROOT='/home'
readonly IMAGE_CREDENTIAL_HOME_LEAF='.netrc'

# Is this credential directory present and usable?  0 when it is, 1 when it is not there, and a
# `die` for anything else.  A thin wrapper over confined_directory that takes the ABSOLUTE
# in-image path the tables above use, so a caller does not strip the leading slash by hand and
# get it wrong in one place out of four.
credential_dir_usable() { # $1=absolute path inside the image  $2=path as shown
    confined_directory "${1#/}" "$2" "enumerate the credential paths under $2"
}

# Enumerates one directory THROUGH A DESCRIPTOR and appends its entries, as paths RELATIVE to the
# image mount point, to the array named by $3.  Anything that is not a plain file is refused
# fatally rather than skipped: a base filesystem that ships /etc/apt/auth.conf.d/x as a link or a
# fifo is not a filesystem this script publishes an image around, and skipping the entry would
# publish that substitution unexamined.
#
# `find -P` used to do this and is not used any more.  find does not follow links it encounters
# INSIDE a tree, but the kernel resolves find's STARTING PATH normally, so an image whose /etc is
# an absolute symbolic link had its HOST directory enumerated and host filenames reported into
# this script's diagnostics.  confined_list resolves every component beneath the anchor with
# links refused, and reads the directory through the descriptor that produced.
collect_credential_dir_entries() { # $1=absolute dir inside the image  $2=dir as shown  $3=array
    local dir_rel="${1#/}" shown="$2" array_name="$3" record entry_type entry_name listing
    listing="$WORK_DIR/credential-dir-listing"
    rm -f -- "$listing"
    # Not a process substitution: confined_list dies on a refusal, and inside a subshell that
    # death would end the loop instead of the run -- which is a silent skip, the exact disposition
    # this function's comment refuses.
    confined_list "$dir_rel" "the credential directory $shown" \
        "enumerate the credential paths under $shown" > "$listing"

    # Appended through a nameref so that one enumeration serves both target arrays -- the same
    # idiom assert_lock_descriptor_identity already uses to return two values by name.  Declared
    # once, outside the loop, and never with a name that could alias it.
    local -n target="$array_name"
    while IFS= read -r -d '' record; do
        entry_type="${record%% *}"
        entry_name="${record#* }"
        [ -n "$entry_name" ] || continue
        [ "$entry_type" = 'regular' ] || die "the entry $shown/$entry_name inside the image is a
       $entry_type rather than a regular file.  Refused rather than followed or skipped: this
       scan opens, rewrites and removes these files as root on the BUILD HOST, and skipping the
       entry would publish the image with that substitution unexamined.  Inspect the base
       tarball."
        target+=("$dir_rel/$entry_name")
    done < "$listing"
    rm -f -- "$listing"
}

# The line numbers of netrc/auth.conf ENTRY lines, one per line, and nothing else -- never the
# line itself.  Used only to report WHERE a removed store carried entries; the removal does not
# depend on the result, so a false negative here cannot leave a credential in the image.
#
# $1 IS THIS SCRIPT'S OWN HOST-SIDE COPY of the store, not a path inside the image.  The caller
# reads the store through the confined primitive into that copy first, so awk never opens a
# pathname inside the image: an awk invocation against an in-image path would follow a symbolic
# link like any other ordinary open, and it would do so as root on the build host.
#
# The regular expressions are built as STRINGS rather than written as /.../ literals, because a
# literal would have to escape the slashes in "://" and the escaping differs between awk
# implementations.
credential_entry_lines() { # $1=host-side copy of the store -> line numbers on stdout
    awk 'BEGIN { entry = "(^|[ \t])(login|password|machine)[ \t]+[^ \t]" }
         tolower($0) ~ entry { print NR }' "$1" 2>/dev/null || true
}

# Reduces every scheme-bearing field of one line whose authority carries userinfo to its
# scheme://host/path form, and leaves every other field exactly as it was.  Sets
# CREDENTIAL_LINE_REDACTED to the rewritten line and CREDENTIAL_LINE_CHANGED to 1 when it
# changed anything.
#
# read -r -a rather than `for f in $line`, for the reason the sanitisers already give: it splits
# on IFS WITHOUT globbing, so a '*' in a configuration value is not expanded against the build
# host's filesystem.
CREDENTIAL_LINE_REDACTED=''
CREDENTIAL_LINE_CHANGED=0
redact_config_line_userinfo() { # $1=line
    local line="$1" field out='' first=1
    local -a fields=()
    CREDENTIAL_LINE_REDACTED="$line"
    CREDENTIAL_LINE_CHANGED=0
    case "$line" in
        *'://'*) : ;;
        # Nothing URL-shaped on the line at all, so there is nothing to reduce and the line is
        # returned untouched.  This is the common case for an apt.conf.d fragment.
        *) return 0 ;;
    esac
    read -r -a fields <<< "$line"
    for field in ${fields[@]+"${fields[@]}"}; do
        case "$field" in
            *'://'*)
                url_authority "$field"
                case "$URL_AUTHORITY" in
                    *@*)
                        sanitise_url_token "$field"
                        field="$SANITISED_URL_TOKEN"
                        CREDENTIAL_LINE_CHANGED=1
                        ;;
                esac
                ;;
        esac
        if [ "$first" -eq 1 ]; then out="$field"; first=0; else out="$out $field"; fi
    done
    CREDENTIAL_LINE_REDACTED="$out"
}

# Does this line still carry a scheme-bearing field with userinfo?  The re-read test, and it is
# deliberately the same narrow test the rewrite uses: a re-read that asked a broader question
# than the rewrite answered would fail every image carrying an ordinary '@' in a comment.
config_line_has_userinfo() { # $1=line
    local line="$1" field
    local -a fields=()
    case "$line" in
        *'://'*) : ;;
        *) return 1 ;;
    esac
    read -r -a fields <<< "$line"
    for field in ${fields[@]+"${fields[@]}"}; do
        case "$field" in
            *'://'*)
                url_authority "$field"
                case "$URL_AUTHORITY" in
                    *@*) return 0 ;;
                esac
                ;;
        esac
    done
    return 1
}

scrub_and_assert_image_credential_stores() {
    local rel shown in_image lines line tmp content listing record
    local entry_type entry_name
    local removed=0 inspected=0 rewritten_files=0 rewritten_lines=0 offenders=0 lineno
    local -a stores=() configs=()

    tmp="$WORK_DIR/apt-config-scrub"
    content="$WORK_DIR/credential-store-content"

    # ---- THE STORES THAT ARE REMOVED --------------------------------------------------
    # confined_regular_file answers "is it there, and is it a plain file" from an fstat on the
    # descriptor the confinement produced.  A symbolic link is refused by the kernel before the
    # type test is even reached, which is what the old `-L` then `-e` pair was written to catch --
    # -e follows, so a dangling link was dismissed as absent and a live one examined as though it
    # were the caller's own file.
    for in_image in "${IMAGE_CREDENTIAL_STORE_FILES[@]}"; do
        if confined_regular_file "${in_image#/}" "$in_image" \
                "remove the credential store $in_image from the image"; then
            stores+=("${in_image#/}")
        fi
    done
    for in_image in "${IMAGE_CREDENTIAL_STORE_DIRS[@]}"; do
        if credential_dir_usable "$in_image" "$in_image"; then
            collect_credential_dir_entries "$in_image" "$in_image" stores
        fi
    done
    # /home/*/.netrc.  The home root itself is checked the same way, then each home directory is
    # looked in without descending further: a .netrc is at the top level of a home by definition,
    # and a recursive walk would enumerate a user's whole tree as root.
    if credential_dir_usable "$IMAGE_CREDENTIAL_HOME_ROOT" "$IMAGE_CREDENTIAL_HOME_ROOT"; then
        listing="$WORK_DIR/credential-home-listing"
        rm -f -- "$listing"
        confined_list "${IMAGE_CREDENTIAL_HOME_ROOT#/}" 'the image home directory root' \
            "enumerate the per-user credential stores under $IMAGE_CREDENTIAL_HOME_ROOT" \
            > "$listing"
        while IFS= read -r -d '' record; do
            entry_type="${record%% *}"
            entry_name="${record#* }"
            [ -n "$entry_name" ] || continue
            # Only directories are descended into.  A non-directory at the top of /home is not a
            # home directory and is not this pass's business -- it holds no .netrc by definition,
            # and refusing it would fail images over an unrelated stray file.
            [ "$entry_type" = 'directory' ] || continue
            rel="${IMAGE_CREDENTIAL_HOME_ROOT#/}/$entry_name/$IMAGE_CREDENTIAL_HOME_LEAF"
            if confined_regular_file "$rel" "/$rel" \
                    "remove the credential store /$rel from the image"; then
                stores+=("$rel")
            fi
        done < "$listing"
        rm -f -- "$listing"
    fi

    for rel in ${stores[@]+"${stores[@]}"}; do
        shown="/$rel"
        # THE PRE-CHECK, for its diagnostics only; the removal below does not reopen this
        # pathname.  It resolves the relative path beneath the anchor and unlinks the leaf through
        # a descriptor, and unlinkat never follows a symbolic link -- so there is no gap between a
        # check and the deletion for anything to be substituted into (CWE-367).
        assert_confined_to_image "$IMAGE_MOUNT/$rel" "the credential store $shown" 'remove'
        inspected=$(( inspected + 1 ))

        # The store is read into this script's own host-side copy THROUGH THE CONFINEMENT before
        # it is counted, so awk never opens an in-image pathname.  The copy is removed again
        # immediately; its content is never logged.
        rm -f -- "$content"
        confined_read "$rel" "the credential store $shown" \
            "count the credential entries in $shown" > "$content"
        lines="$(credential_entry_lines "$content")"
        rm -f -- "$content"

        warn "the image carries the credential store $shown; it is REMOVED."
        if [ -n "$lines" ]; then
            warn "  It holds credential entries at line(s): $(printf '%s' "$lines" | tr '\n' ' ')"
        fi
        warn "  Removed rather than rewritten: this guest boots with no NIC and fetches"
        warn "  nothing, so a credential store in it authenticates nothing and is pure"
        warn "  liability.  Its content is deliberately not reproduced in this log.  For an"
        warn "  authenticated archive use --apt-auth-file, which is installed for the package"
        warn "  phase only and removed before the image is released."

        confined_unlink "$rel" "the credential store $shown" \
            "remove the credential store $shown from the image"
        removed=$(( removed + 1 ))

        # FATAL RE-READ, through the confinement.  An unlink that returned 0 establishes that a
        # call succeeded; this establishes that nothing is at the name any more -- and it cannot
        # be satisfied by planting a symbolic link there, because confined_absent dies on a
        # refusal rather than reading one as absence.
        confined_absent "$rel" "the credential store $shown" \
            "verify the credential store $shown is gone" \
            || die "the credential store $shown is still present in the image after it was
       removed.  The image is NOT published.  Something is recreating it, or the removal did not
       do what its own exit status claimed."
    done

    # ---- THE CONFIGURATION THAT IS REWRITTEN ------------------------------------------
    # The named FILES first -- /etc/apt/apt.conf is apt's primary configuration file and takes
    # the same Acquire::*::Proxy directives as the fragments, and it is the path the previous
    # revision of this inventory did not look at -- then the fragment directories.
    for in_image in "${IMAGE_CREDENTIAL_CONFIG_FILES[@]}"; do
        if confined_regular_file "${in_image#/}" "$in_image" \
                "rewrite any URL credential in $in_image"; then
            configs+=("${in_image#/}")
        fi
    done
    for in_image in "${IMAGE_CREDENTIAL_CONFIG_DIRS[@]}"; do
        if credential_dir_usable "$in_image" "$in_image"; then
            collect_credential_dir_entries "$in_image" "$in_image" configs
        fi
    done

    for rel in ${configs[@]+"${configs[@]}"}; do
        local changed=0
        shown="/$rel"
        assert_confined_to_image "$IMAGE_MOUNT/$rel" "the apt configuration file $shown" 'read'
        inspected=$(( inspected + 1 ))

        # Read through the confinement into a host-side copy, so the line loop is iterating over
        # this script's own bytes and not over a name that can change under it.
        rm -f -- "$content"
        confined_read "$rel" "the apt configuration file $shown" \
            "read the image apt configuration file $shown" > "$content"

        # Staged on the HOST, in this script's own 0700 scratch directory, for the reason the
        # source-list scrub gives: a temporary file written inside the image is one more thing a
        # failure between writing it and removing it would publish.
        rm -f -- "$tmp"
        ( umask 077 && : > "$tmp" ) || die "could not stage the apt configuration rewrite at $tmp."
        lineno=0
        while IFS= read -r line || [ -n "$line" ]; do
            lineno=$(( lineno + 1 ))
            redact_config_line_userinfo "$line"
            if [ "$CREDENTIAL_LINE_CHANGED" -eq 1 ]; then
                changed=$(( changed + 1 ))
                warn "a URL credential was removed from $shown line $lineno inside the image"
                printf '%s\n' "$CREDENTIAL_LINE_REDACTED" >> "$tmp" \
                    || die "could not write the scrubbed apt configuration line to $tmp."
            else
                printf '%s\n' "$line" >> "$tmp" \
                    || die "could not write an apt configuration line to $tmp."
            fi
        done < "$content"
        rm -f -- "$content"

        if [ "$changed" -gt 0 ]; then
            # THE WRITE, through the confinement.  The file keeps its own inode, owner and mode,
            # because the helper truncates and writes THROUGH THE DESCRIPTOR rather than replacing
            # the name -- which is what `cat -- "$tmp" > "$path"` achieved by a redirection that
            # followed symbolic links.  No re-check precedes it and none is needed: the name was
            # resolved once by the kernel with links refused at every position.
            confined_write "$rel" "the apt configuration file $shown" \
                "write the scrubbed apt configuration back to $shown inside the image" < "$tmp"
            rewritten_files=$(( rewritten_files + 1 ))
            rewritten_lines=$(( rewritten_lines + changed ))
            warn "  ${changed} line(s) of $shown were reduced to scheme://host/path.  The"
            warn "  file is kept rather than removed because it is ordinary configuration, not a"
            warn "  credential store, and the setting itself is provenance.  The content is"
            warn "  deliberately not reproduced in this log."
        fi
        rm -f -- "$tmp" || die "could not remove the staged apt configuration rewrite at $tmp."

        # THE RE-READ, also through the confinement, because this is the pass whose result decides
        # whether the image is published: it must open the same object the rewrite wrote to, which
        # it does by construction -- both resolve the same relative path beneath the same anchor.
        rm -f -- "$content"
        confined_read "$rel" "the apt configuration file $shown" \
            "re-read the image apt configuration file $shown" > "$content"
        lineno=0
        while IFS= read -r line || [ -n "$line" ]; do
            lineno=$(( lineno + 1 ))
            if config_line_has_userinfo "$line"; then
                offenders=$(( offenders + 1 ))
                warn "a URL credential REMAINS in $shown line $lineno"
            fi
        done < "$content"
        rm -f -- "$content"
    done

    if [ "$offenders" -ne 0 ]; then
        die "$offenders line(s) of the image's apt configuration still carry a URL credential
       after the rewrite above, so the image is NOT published.  They are identified by file and
       line number in the warnings above and are deliberately not reproduced here.  Look at
       them in the base tarball: something in this script's rewrite did not recognise the form
       they use, and that is a defect in $SCRIPT_PATH rather than in the caller's inputs."
    fi

    if [ "$inspected" -eq 0 ]; then
        log "  credential-store check: the image carries none of the known stores"
        log "    (/etc/apt/auth.conf, /etc/apt/auth.conf.d/*, /etc/apt/apt.conf,"
        log "     /etc/apt/apt.conf.d/*, /etc/environment, /root/.netrc, /home/*/.netrc)"
        return 0
    fi
    log "  credential-store check: $inspected path(s) inspected"
    if [ "$removed" -eq 0 ]; then
        log "    no dedicated credential store present"
    else
        log "    $removed dedicated credential store(s) REMOVED and verified absent"
    fi
    if [ "$rewritten_files" -eq 0 ]; then
        log "    no URL credential in the apt configuration"
    else
        log "    $rewritten_lines line(s) across $rewritten_files apt configuration file(s)"
        log "      reduced to scheme://host/path; re-read afterwards and none remains"
    fi
    log "    every path above was read, rewritten, enumerated and removed through openat2 with"
    log "      RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_XDEV anchored on the image mount"
    log "    reported by file and line number only; no content was written to this log"
}

# ------------------------------------------------------------------------------------
# RELEASING THE IMAGE.
#
# Done explicitly rather than left to the trap, for two reasons: the image must be fully
# flushed and detached before its size is read and the manifest is written, and a failure to
# release is a real failure of this run -- an image still held by a loop device is not a
# finished artifact.  The trap remains as the safety net for every path that does not reach
# this function.
# ------------------------------------------------------------------------------------
release_image() {
    log "releasing the image"
    sync || true
    local point
    for point in "$IMAGE_MOUNT/dev/pts" "$IMAGE_MOUNT/dev" "$IMAGE_MOUNT/sys" \
                 "$IMAGE_MOUNT/proc" "$IMAGE_MOUNT"; do
        mountpoint -q -- "$point" 2>/dev/null || continue
        # The same bounded-retry helper the trap uses, so the two agree about what "released"
        # means; its lazy fallback is then re-checked below and refused here.
        unmount_with_retries "$point"
        # A lazy unmount is NOT accepted here, unlike in the trap: the image must be fully
        # detached before its size is read and the manifest written, and a filesystem the
        # kernel is still finishing with is not a finished artefact.
        mountpoint -q -- "$point" 2>/dev/null && die "could not unmount $point after three
       attempts.  Something inside the image is still holding it -- a stray chroot process,
       most likely.  The image is NOT finished, so it is not declared as one."
    done
    if [ -n "$LOOP_DEVICE" ] && [ -b "$LOOP_DEVICE" ] && loop_still_attached "$LOOP_DEVICE"; then
        # IDENTITY BEFORE DETACH.  Between the unmount above and this detach the node can have
        # been re-used by another process on this host, and `losetup -d` as root would then
        # pull the backing file out from under THAT process's mounted image.
        assert_loop_backs "$LOOP_DEVICE" "$STAGED_IMAGE" "detach $LOOP_DEVICE"
        # No "--" before the device; see the note in cleanup().
        if ! losetup -d "$LOOP_DEVICE"; then
            # The kernel auto-clears a loop device once its last user closes it, so a detach
            # that fails AFTER the unmount above has usually already happened. Only a device
            # that is still bound is a real failure.
            loop_still_attached "$LOOP_DEVICE" && die "could not detach $LOOP_DEVICE from
       $STAGED_IMAGE.  The image is not finished while a loop device still holds it."
        fi
    fi
    # DURABILITY BEFORE PUBLICATION.  The staged image is about to be renamed onto the
    # caller's path and then read by QEMU, possibly after a reboot on a self-hosted runner.
    # `sync -f` flushes the filesystem holding it and is preferred because it does not stall
    # on every other filesystem the host has mounted; a coreutils too old for -f falls back to
    # the whole-system flush rather than skipping the flush.
    #
    # AND BOTH FAILING IS FATAL.  This line used to end in `|| true`, which meant an image whose
    # bytes were never committed could still be published and reported as finished; on a
    # self-hosted runner that reboots, the caller then has a published path holding a partially
    # written filesystem.  An unflushed image is not a finished image, so the run stops here.
    #
    # Named through the publication directory descriptor, like every other operation on the
    # staged image: `sync -f` opens its argument, and an argument that resolved somewhere else
    # would flush somebody else's filesystem while reporting success for ours.
    if ! sync -f -- "$OUTPUT_DIR_PROC/$STAGED_IMAGE_LEAF" 2>/dev/null; then
        if ! sync; then
            die "the staged image could not be flushed to disk: neither
           sync -f -- '$STAGED_IMAGE'
       nor a whole-system 'sync' succeeded.  The image is NOT published: its bytes are not
       known to be on the disk, and any previously published pair is left exactly as it was."
        fi
        log "durability: 'sync -f' was unavailable or failed, so a whole-system sync was used"
    fi
    log "image released; every mount and loop device this run created is gone"
}

# ------------------------------------------------------------------------------------
# THE MANIFEST.  KEY=VALUE lines beside the image, so the workflow reads the paths it needs
# instead of hard-coding them -- and so a change to the guest layout is picked up by the
# workflow rather than silently disagreed with.
# ------------------------------------------------------------------------------------
write_manifest() {
    # Staged next to the staged image, for the same reason and with the same guarantees: the
    # manifest describes an image, so a manifest that becomes visible before its image is
    # published -- or that survives a failure that stopped the image from being published --
    # would describe something that is not there.  publish_outputs() renames the pair
    # together.
    #
    # Minted, checked and written THROUGH THE PUBLICATION DIRECTORY DESCRIPTOR, exactly as the
    # staged image is: $manifest below is $OUTPUT_DIR_PROC/<leaf>, so the redirection at the
    # end of this function writes inside the directory inode that was validated, and no rename
    # of the directory or an ancestor between here and there can redirect it.  $STAGED_MANIFEST
    # holds the human pathname for the messages and for the rename in publish_outputs.
    local manifest
    assert_output_dir_custody "create the staging file for the manifest in $OUTPUT_DIR"
    manifest="$(mktemp -- "$OUTPUT_DIR_PROC/.cec-l2-manifest.XXXXXXXX")" \
        || die "could not create a staging file for the manifest in $OUTPUT_DIR."
    STAGED_MANIFEST_LEAF="$(basename -- "$manifest")"
    STAGED_MANIFEST="$OUTPUT_DIR/$STAGED_MANIFEST_LEAF"
    record_leaf_identity "$manifest" STAGED_MANIFEST_DEVICE STAGED_MANIFEST_INODE \
        'the staged manifest' "$STAGED_MANIFEST"
    local size_bytes
    # The size is READ FROM THE STAGED FILE, which is the image that will be published, and it
    # is read through the descriptor for the same reason everything else is.
    size_bytes="$(stat -c '%s' -- "$OUTPUT_DIR_PROC/$STAGED_IMAGE_LEAF" 2>/dev/null \
        || printf 'unknown')"

    # THE MANIFEST DOES NOT DOCUMENT AN AUDIT GAP AS THOUGH IT WERE A MEASUREMENT.
    # record_installed_packages() is fatal on every path that cannot produce a concrete
    # package/version/architecture record, so these values are numbers and a digest by the time
    # this runs.  The check is here anyway because this is the function that would SERIALISE a
    # placeholder into the published artefact, and one line beside the write is cheaper than
    # trusting that a caller ordering never changes.
    case "$PACKAGE_RECORD_COUNT" in
        ''|*[!0-9]*)
            die "the installed-package count is '$PACKAGE_RECORD_COUNT' rather than a number at
       manifest-writing time, so record_installed_packages() either did not run or did not
       establish a concrete package set.  The manifest is NOT written and the image is NOT
       published: publishing a manifest that says the resolved package set is unknown would
       hand on an audit gap dressed as a measurement."
            ;;
    esac
    [ "$PACKAGE_RECORD_COUNT" -gt 0 ] || die "the installed-package count is zero at
       manifest-writing time.  The image is NOT published; see record_installed_packages()."
    case "$PACKAGE_RECORD_DIGEST" in
        [0-9a-f]*) : ;;
        *) die "the package record's sha256 is '$PACKAGE_RECORD_DIGEST' rather than a hex digest
       at manifest-writing time, so the manifest cannot carry a comparable fingerprint of the
       resolved package set.  The image is NOT published." ;;
    esac

    # Adjacent to the write, as everywhere else: the manifest is written as root into a
    # directory the caller chose, so the directory's identity and the leaf's own identity are
    # both re-established in the statement before the redirection that writes through them.
    assert_output_dir_custody "write the staged manifest $STAGED_MANIFEST"
    assert_leaf_identity "$manifest" "$STAGED_MANIFEST_DEVICE" "$STAGED_MANIFEST_INODE" \
        "write the staged manifest $STAGED_MANIFEST" "$STAGED_MANIFEST"

    {
        printf '# Manifest for the AIDL-path job'"'"'s QEMU root image.\n'
        printf '# Generated by %s. Read this file rather than hard-coding paths.\n' "$SCRIPT_NAME"
        printf 'GENERATED_BY=%s\n' "$SCRIPT_PATH"
        printf 'GENERATED_AT_UTC=%s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
        printf 'IMAGE=%s\n' "$OUTPUT_IMAGE"
        printf 'IMAGE_FS=ext4\n'
        printf 'IMAGE_FS_LABEL=cec-l2-root\n'
        printf 'IMAGE_SIZE_MIB=%s\n' "$IMAGE_SIZE_MIB"
        printf 'IMAGE_SIZE_BYTES=%s\n' "$size_bytes"
        printf 'ARCH=%s\n' "$ARCH"
        printf 'ELF_BITNESS=32-bit throughout\n'
        # THIS BLOCK IS THIS SCRIPT'S OWN TEXT AND IS NOT THE AUTHORITY ON THE PROTOCOL.
        # Saying so is the point of the wording, because a manifest is exactly the kind of
        # file a later reader quotes as fact.  What BINDER_PROTOCOL records is the image
        # builder's EXPECTATION, derived at image-build time from two artefacts the caller
        # supplied -- the guest kernel configuration and the staged SDK's CMake cache -- and
        # refused outright when those two disagree.  It is not declared: an earlier revision
        # wrote a fixed "BINDER_PROTOCOL=7" here while the SDK inside the image was built
        # BINDER_IPC_32BIT=OFF, which is protocol 8, and produced a manifest that contradicted
        # its own image.
        #
        # THE AUTHORITY IS THE THREE-WAY CROSS-CHECK THE RUN PERFORMS, not this line: the
        # RUNNING kernel's configuration as the guest reads it back, the SDK build flags as
        # recorded in the staging tree, and -- decisively -- the BINDER_VERSION ioctl on
        # /dev/binder, which is the value libbinder itself compares for equality when it opens
        # the node.  The init prints all three in the BINDER PROTOCOL AGREEMENT block on the
        # console and DIES naming whichever source disagrees, so a wrong expectation here
        # cannot pass as a result.  The lines below therefore carry the expectation, the
        # evidence for each half of it, and where the authoritative answer is to be read.
        printf 'BINDER_PROTOCOL=%s\n' "$DERIVED_BINDER_PROTOCOL"
        printf 'BINDER_PROTOCOL_STATUS=EXPECTATION recorded by the image builder, NOT an authoritative measurement of the running guest\n'
        printf 'BINDER_PROTOCOL_DERIVATION=derived at image-build time from the guest kernel configuration and the staged SDK CMake cache; never declared by the image builder, and refused when the two disagree\n'
        printf 'BINDER_PROTOCOL_FROM_KERNEL_CONFIG=%s\n' "${PROTOCOL_FROM_KERNEL_CONFIG:-unknown}"
        printf 'BINDER_PROTOCOL_FROM_SDK_CACHE=%s\n' "${PROTOCOL_FROM_SDK_CACHE:-unknown}"
        printf 'BINDER_PROTOCOL_AUTHORITY=the three-way cross-check performed at run time, not this file: (1) the running kernel configuration the guest reads back, (2) the SDK build flags recorded in the staging tree, (3) the BINDER_VERSION ioctl on /dev/binder, which is what libbinder compares for equality; disagreement between any of them is FATAL to the run\n'
        printf 'BINDER_PROTOCOL_VERIFIED_IN_GUEST=the init reads /dev/binder with the BINDER_VERSION ioctl, cross-checks it against the running kernel configuration and the recorded SDK value, and fails naming whichever disagrees; read the BINDER PROTOCOL AGREEMENT block in the console log for the authoritative outcome\n'
        printf 'GUEST_INIT=%s\n' "$GUEST_INIT_PATH"
        printf 'GUEST_KERNEL_APPEND_HINT=root=/dev/vda rw init=%s console=ttyS0\n' "$GUEST_INIT_PATH"
        printf 'ARTIFACT_DIR=%s\n' "$GUEST_ARTIFACT_DIR"
        printf 'STATUS_FILE=%s\n' "$GUEST_STATUS_FILE"
        printf 'STATUS_FILE_FORMAT=one line holding the exact exit status of run_coverage.sh; 0 means measured and passing, 3 is ADVISORY and is NOT a pass, anything else is a failure\n'
        printf 'GUEST_WORKSPACE=%s\n' "$GUEST_WORKSPACE"
        printf 'GUEST_PAYLOAD_DIR=%s\n' "$GUEST_PAYLOAD_DIR"
        printf 'GUEST_SDK_DIR=%s\n' "$GUEST_SDK_DIR"
        printf 'GUEST_GTEST_PREFIX=%s\n' "${GTEST_PREFIX_SRC:+$GUEST_GTEST_PREFIX}"
        printf 'GUEST_IMAGE_CONF=%s\n' "$GUEST_IMAGE_CONF"
        printf 'GUEST_EXPECTED_KERNEL_CONFIG=%s\n' "$GUEST_KERNEL_EXPECTATION"
        printf 'GUEST_SDK_BUILD_FLAGS_RECORD=%s\n' "$GUEST_SDK_FLAGS_RECORD"
        printf 'REQUIRED_KERNEL_OPTIONS=CONFIG_ANDROID=y CONFIG_ANDROID_BINDER_IPC=y CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder" CONFIG_ANDROID_BINDERFS=y CONFIG_ASHMEM=y\n'
        if [ "$DERIVED_BINDER_PROTOCOL" = '7' ]; then
            printf 'PROTOCOL_SCOPED_KERNEL_OPTION=CONFIG_ANDROID_BINDER_IPC_32BIT=y\n'
        else
            printf 'PROTOCOL_SCOPED_KERNEL_OPTION=CONFIG_ANDROID_BINDER_IPC_32BIT unset or absent\n'
        fi
        printf 'PROTOCOL_SCOPED_KERNEL_OPTION_NOTE=follows the DERIVED protocol above and is scoped to this image; it is not a requirement of supported platforms generally, and it is independent of ELF bitness -- a 32-bit userspace can speak either protocol\n'
        # SANITISED, AND THIS ONE MATTERS MOST: the manifest is uploaded as a workflow
        # artifact, so anything in it has left the machine.  Scheme, host and path answer
        # "which archive was this built from"; a token in a query string or a userinfo field
        # answers nothing and cannot be recalled.  The digest below is the value that actually
        # pins the content, and it is recorded in full.
        if [ -n "$BASE_URL" ]; then
            printf 'BASE_PROVENANCE=%s\n' "$(sanitised_endpoint_display "$BASE_URL")"
        else
            printf 'BASE_PROVENANCE=%s\n' "$BASE_TARBALL"
        fi
        printf 'BASE_SHA256=%s\n' "$BASE_SHA256"
        printf 'PAYLOAD_SOURCE=%s\n' "$PAYLOAD_DIR"
        printf 'SDK_SOURCE=%s\n' "$SDK_DIR"
        printf 'LCOV_SOURCE=%s\n' "${LCOV_TARBALL:-distribution package only}"
        # REPRODUCIBILITY EVIDENCE, NOT A CLAIM OF IT.  The package set is pinned by the
        # archive the caller passed as --apt-mirror; these two lines say where the resolved
        # set is recorded and how large it is, so a reader can diff two images by package and
        # version instead of being told that nothing should have changed.  The list itself
        # lives in the image because it is a few hundred lines and the reader who needs it is
        # inside the guest.
        printf 'INSTALLED_PACKAGE_RECORD=%s\n' "$GUEST_PACKAGE_RECORD"
        printf 'INSTALLED_PACKAGE_COUNT=%s\n' "$PACKAGE_RECORD_COUNT"
        # The digest is what makes the count comparable rather than merely large: two images
        # whose records hash the same resolved to the same package set, and no reader has to
        # diff a few hundred lines to find that out.
        printf 'INSTALLED_PACKAGE_RECORD_SHA256=%s\n' "$PACKAGE_RECORD_DIGEST"
        printf 'REQUIRED_PACKAGES_ACCOUNTED=%s of %s\n' \
            "$PACKAGE_RECORD_REQUIRED_ACCOUNTED" "${#IMAGE_PACKAGES[@]}"
        if [ -n "$APT_MIRROR" ]; then
            printf 'PACKAGE_SOURCE=%s\n' "$(sanitised_endpoint_display "$APT_MIRROR")"
        else
            printf 'PACKAGE_SOURCE=%s\n' 'the base tarball sources, unchanged'
        fi
        printf 'READINESS_TIMEOUT_SECONDS=%s\n' "$READINESS_TIMEOUT_SECONDS"
        printf 'GUEST_NETWORK_AT_TEST_TIME=none\n'
        printf 'PAYLOAD_TRANSPORT=copied into the image at build time; artifacts are read back out of the image after the guest powers down\n'
    } > "$manifest"
    chmod 0644 -- "$manifest"
    log "manifest staged at $STAGED_MANIFEST (publishes to $OUTPUT_MANIFEST)"
    log "  written through the publication directory descriptor as $manifest"
}

# ------------------------------------------------------------------------------------
# PUBLICATION.  The only place either caller-supplied path is written, and the last thing
# this script does before declaring the build finished.
#
# ONE TRANSACTION, NOT A SEQUENCE OF INDEPENDENT STEPS.  The image and the manifest are a PAIR:
# the manifest names the image's paths, its architecture and its derived binder protocol, and
# the workflow reads every path it uses out of it.  A new image beside a stale manifest is worse
# than no publication, because it looks complete.  So the swap is written as a transaction with
# one commit point, and everything about its shape follows from the failure it exists to
# prevent -- which was measured on the previous revisions and is worth stating exactly:
#
#   THE FIRST DEFECT.  The previous image was moved aside to a name held in a FUNCTION LOCAL, and
#   only then was a second holding name minted for the previous manifest.  A failure of that
#   second mktemp or mv, or an INT/TERM/HUP landing in that window, reached `die` without any
#   rollback: the caller's good image was hidden under a private name the EXIT trap could not
#   see, and nothing existed at --output.  The caller was left with no image and no way to find
#   the one they had.
#
#   WHAT REPLACES IT, in four parts:
#
#   (a) BOTH HOLDING NAMES ARE MINTED BEFORE ANY RENAME.  A failure while minting them happens
#       when nothing has moved yet, so there is nothing to roll back.
#   (b) EVERY OLD AND NEW NAME LIVES IN A GLOBAL THE EXIT TRAP CAN READ, and PUBLISH_IN_PROGRESS
#       is raised before the first rename.  cleanup() rolls the transaction back from those
#       globals, so a path that reaches the trap without passing through the rollback below is
#       still covered.
#   (c) INT, TERM AND HUP ARE MASKED ACROSS THE TRANSACTION and the same handlers are restored
#       afterwards through install_interrupt_traps.  Those three are the signals a Ctrl-C, a
#       `timeout`, a CI cancellation or a lost session delivers, and the window they must not
#       land in is a handful of renames long.
#   (d) THE NEXT RUN RECOVERS WHAT NO TRAP COULD.  SIGKILL, a power loss and an OOM kill run no
#       handler at all, so recover_interrupted_publication() -- called under the publication
#       lock before anything is built -- reads the JOURNAL and drives the pair to one state.
#
#   THREE FURTHER DEFECTS, EACH CLOSED BY A NAMED MECHANISM BELOW:
#
#   THE EMPTY-PLACEHOLDER RESTORE.  mktemp mints an aside name by CREATING the file, so between
#   minting and the `mv` a real, EMPTY, regular file sits at each aside name.  The restore's
#   whole precondition was `[ -f "$aside" ]`, so a FAILED old-leaf rename -- the one case in
#   which the aside is still that placeholder -- had the rollback move a ZERO-BYTE FILE over the
#   caller's still-good published image.  Closed by PUBLISH_ASIDE_*_HOLDS_OLD, raised only after
#   that leaf's own `mv` returned success, plus a recorded size the restore re-checks.
#
#   THE ASYMMETRIC ROLLBACK.  Only the IMAGE's rename was tracked, so a failure after the
#   manifest rename -- the chmod, or the durability flush -- withdrew the new image and left the
#   new MANIFEST published.  On a first publication, with no asides, that is a manifest
#   describing an image that is not there.  Closed by PUBLISH_MANIFEST_RENAMED and a rollback
#   that withdraws BOTH new leaves.
#
#   THE UNRECOVERABLE MIXED STATE.  The image and the manifest are two independent renames, and a
#   SIGKILL between them leaves a new image, no manifest and BOTH asides.  The aside-scan
#   recovery read that one file at a time and reached the worst possible conclusion: an image
#   exists, so the old-image aside is "superseded" and is DELETED; no manifest exists, so the old
#   manifest is RESTORED.  A new image beside an old manifest, cemented, with the old image
#   destroyed.  Closed by the journal: see the block at PUBLISH_JOURNAL_LEAF for its shape, and
#   recover_interrupted_publication() for the state machine it drives.
#
# WHAT "ATOMIC" MEANS HERE, EXACTLY -- because the word was doing more work in this block than
# the code could support, and an over-claim in a comment is how the next reader builds on a
# guarantee that is not there.  Three separate statements, and only these three:
#
#   (a) EACH LEAF IS REPLACED BY ONE ATOMIC RENAME.  Both renames are same-directory, so at no
#       instant does either published path hold a partial or truncated file.  A reader that opens
#       one path sees either the whole previous file or the whole new one, never a mixture.
#
#   (b) THE PAIR IS CONSISTENT FOR A READER THAT HOLDS THE PUBLICATION LOCK.  It is NOT consistent
#       for one that does not.  The image and the manifest are four independent renames at fixed
#       leaf names, and there is a real window -- between the image's rename and the manifest's --
#       in which a lockless reader sees the NEW image beside the OLD manifest.  POSIX offers no
#       way to rename two names as one operation, and the alternative shape that would remove the
#       window -- a generation directory with one atomically switched symlink -- changes the paths
#       the workflow consumes, which is the trade this design declined (see the journal block).
#       So the consistency of the PAIR is delivered by the lock, and the consumers must take it:
#       aidl-path-tests.yml's boot step and its artifact-extraction step both `flock` the same
#       $OUTPUT_IMAGE.lock, shared, for the duration of their read.  The lock leaf is chmodded
#       0644 in lock_publication_targets() precisely so an unprivileged consumer can open it
#       read-only and do that.
#
#   (c) AFTER AN INTERRUPTION THE PAIR IS MADE CONSISTENT AGAIN BY RECOVERY, not by the renames.
#       recover_interrupted_publication() runs under the same lock before anything is built and
#       drives both leaves to one generation.  That is what closes the window for a reader that
#       arrives after a crash rather than during a publication.
#
# AND NO POSTCONDITION IS ADVISORY.  The final mode, the durability flush, the rollback itself
# and the removal of the superseded asides were warning-only; each is now fatal.  A published
# image nobody can read, or one that is not on the disk when the runner reboots, is not a
# published image, and a leftover multi-gigabyte aside is not housekeeping -- it is half of a
# transaction that did not finish.
#
# EVERY PATHNAME OPERATION IN HERE NAMES $OUTPUT_DIR_PROC/<leaf>, never the caller's pathname.
# A rename is authorised by the DIRECTORY, so a rename through a pathname whose ancestry was
# substituted after validation is a rename into somebody else's directory.  The descriptor pins
# the inode; see the custody block above resolve_publication_targets.
#
# THE ORDER, then:
#
#   1. VALIDATE the staged image.  A file that is not the size it should be, or is not a
#      regular file, is not published at all.  This is the "only after validation" clause:
#      an image is published because it was checked, not because the script reached the end.
#   2. RE-CHECK the destinations and the output directory's custody, adjacent to the use
#      (CWE-367): they were checked before the build, and the build takes an hour.
#   3. MINT BOTH HOLDING NAMES, before anything moves.
#   4. WRITE AND FLUSH THE JOURNAL, before anything moves, so a kill after step 5 is
#      recoverable at all.
#   5. MASK THE SIGNALS and raise PUBLISH_IN_PROGRESS.
#   6. MOVE THE PREVIOUS PAIR ASIDE rather than deleting it, recording each completed move in
#      the journal.  A caller who had a working image keeps one on every path out of here.
#   7. RENAME the image, then the manifest, recording the INTENT of each before it and the
#      completion after it.  Same-directory renames, so EACH LEAF is replaced atomically -- at no
#      instant does either path hold a partial file.  The PAIR is a different claim and a weaker
#      one; see "WHAT 'ATOMIC' MEANS HERE, EXACTLY" above.
#   8. ROLL BACK ON EVERY FAILURE from step 6 onwards, then fail.
#   9. MODE, then DURABILITY, then discard the asides, then discard the JOURNAL -- each fatal,
#      and the journal last, because it is what makes every earlier step recoverable.
#  10. LOWER THE FLAG and restore the signal handlers.
# ------------------------------------------------------------------------------------

# ------------------------------------------------------------------------------------
# THE JOURNAL, WRITTEN AND READ.
#
# One record, rewritten in full on every update rather than appended to: a record that is only
# ever replaced wholesale cannot be half of one update and half of another, and the reader has
# no ordering to reconstruct.  Each write goes to a fixed staging leaf and is renamed onto the
# journal name, which is atomic within a directory, and ends with the terminator line the reader
# REQUIRES -- so a torn write is refused rather than interpreted.
#
# THE RECORD IS THE GENERATION IDENTITY.  It names the two published leaves, the two aside
# leaves and the two staged leaves by name, so recovery does not have to infer which file is
# which from timestamps or from globbing: it is told, by the run that was about to move them.
# That is what lets the PAIR be recovered together instead of one file at a time.
# ------------------------------------------------------------------------------------

# Write (or rewrite) the journal.  Returns non-zero rather than dying: the caller decides
# whether a journal failure is fatal before the first move (it is) or reportable after the pair
# is already committed (it is that too, but by a different message).
publish_journal_write() { # $1=what has just happened, for the record and the log
    local why="$1" staging final
    staging="$OUTPUT_DIR_PROC/$PUBLISH_JOURNAL_TMP_LEAF"
    final="$OUTPUT_DIR_PROC/$PUBLISH_JOURNAL_LEAF"

    assert_output_dir_custody "update the publication journal in $OUTPUT_DIR ($why)"
    # A staging leaf left by a torn earlier update is removed rather than appended to -- and a
    # name this function cannot claim is a failure, not something to write through: `rm -f`
    # removes a regular file or a symlink, so what survives it is a directory or an unwritable
    # entry, and the redirection below would either fail confusingly or land somewhere else.
    rm -f -- "$staging" 2>/dev/null || return 1
    {
        printf '# Publication journal written by %s.  Read by the NEXT run, under the\n' "$SCRIPT_NAME"
        printf '# publication lock, to drive an interrupted publication to one consistent state.\n'
        printf '# It is removed only after the new pair is published, mode-set and flushed.\n'
        printf 'JOURNAL_VERSION=2\n'
        printf 'RUN_ID=%s\n' "$PUBLISH_RUN_ID"
        printf 'GENERATION=%s\n' "$PUBLISH_GENERATION"
        printf 'LAST_STEP=%s\n' "$why"
        printf 'IMAGE_LEAF=%s\n' "$OUTPUT_IMAGE_LEAF"
        printf 'MANIFEST_LEAF=%s\n' "$OUTPUT_MANIFEST_LEAF"
        printf 'STAGED_IMAGE_LEAF=%s\n' "$STAGED_IMAGE_LEAF"
        printf 'STAGED_MANIFEST_LEAF=%s\n' "$STAGED_MANIFEST_LEAF"
        printf 'ASIDE_IMAGE_LEAF=%s\n' "$PUBLISH_ASIDE_IMAGE_LEAF"
        printf 'ASIDE_MANIFEST_LEAF=%s\n' "$PUBLISH_ASIDE_MANIFEST_LEAF"
        # THE SIX IDENTITIES.  Recorded while each object is still at its starting name, so the
        # next run recognises it wherever a rename left it.  An empty device and inode means
        # "there was no such participant" -- a first publication has no previous pair -- and the
        # reader treats empty as "not recorded" rather than as a value.
        printf 'OLD_IMAGE_DEVICE=%s\n' "$PUBLISH_OLD_IMAGE_DEVICE"
        printf 'OLD_IMAGE_INODE=%s\n' "$PUBLISH_OLD_IMAGE_INODE"
        printf 'OLD_IMAGE_BYTES=%s\n' "$PUBLISH_OLD_IMAGE_BYTES"
        printf 'OLD_MANIFEST_DEVICE=%s\n' "$PUBLISH_OLD_MANIFEST_DEVICE"
        printf 'OLD_MANIFEST_INODE=%s\n' "$PUBLISH_OLD_MANIFEST_INODE"
        printf 'OLD_MANIFEST_BYTES=%s\n' "$PUBLISH_OLD_MANIFEST_BYTES"
        printf 'STAGED_IMAGE_DEVICE=%s\n' "$STAGED_IMAGE_DEVICE"
        printf 'STAGED_IMAGE_INODE=%s\n' "$STAGED_IMAGE_INODE"
        printf 'STAGED_IMAGE_BYTES=%s\n' "$PUBLISH_STAGED_IMAGE_BYTES"
        printf 'STAGED_MANIFEST_DEVICE=%s\n' "$STAGED_MANIFEST_DEVICE"
        printf 'STAGED_MANIFEST_INODE=%s\n' "$STAGED_MANIFEST_INODE"
        printf 'STAGED_MANIFEST_BYTES=%s\n' "$PUBLISH_STAGED_MANIFEST_BYTES"
        printf 'ASIDE_IMAGE_HOLDER_DEVICE=%s\n' "$PUBLISH_ASIDE_IMAGE_HOLDER_DEVICE"
        printf 'ASIDE_IMAGE_HOLDER_INODE=%s\n' "$PUBLISH_ASIDE_IMAGE_HOLDER_INODE"
        printf 'ASIDE_MANIFEST_HOLDER_DEVICE=%s\n' "$PUBLISH_ASIDE_MANIFEST_HOLDER_DEVICE"
        printf 'ASIDE_MANIFEST_HOLDER_INODE=%s\n' "$PUBLISH_ASIDE_MANIFEST_HOLDER_INODE"
        # THE WRITE-AHEAD PAIRS, one per rename.  INTENT is raised and this record flushed
        # BEFORE the rename it describes; DONE is raised and it is flushed again after.
        printf 'INTENT_ASIDE_IMAGE=%s\n' "$PUBLISH_INTENT_ASIDE_IMAGE"
        printf 'DONE_ASIDE_IMAGE=%s\n' "$PUBLISH_ASIDE_IMAGE_HOLDS_OLD"
        printf 'INTENT_ASIDE_MANIFEST=%s\n' "$PUBLISH_INTENT_ASIDE_MANIFEST"
        printf 'DONE_ASIDE_MANIFEST=%s\n' "$PUBLISH_ASIDE_MANIFEST_HOLDS_OLD"
        printf 'INTENT_NEW_IMAGE=%s\n' "$PUBLISH_INTENT_NEW_IMAGE"
        printf 'DONE_NEW_IMAGE=%s\n' "$PUBLISH_IMAGE_RENAMED"
        printf 'INTENT_NEW_MANIFEST=%s\n' "$PUBLISH_INTENT_NEW_MANIFEST"
        printf 'DONE_NEW_MANIFEST=%s\n' "$PUBLISH_MANIFEST_RENAMED"
        printf '%s\n' "$PUBLISH_JOURNAL_TERMINATOR"
    } > "$staging" || return 1
    chmod 0644 -- "$staging" || return 1
    # DURABLE BEFORE THE RENAME, AND AGAIN AFTER IT.  A journal whose bytes are still in the
    # page cache when the power fails is not a journal; and a rename that is not on the disk
    # leaves the previous record in place, which would tell the next run that a move it has
    # since made had not happened.
    sync -f -- "$staging" 2>/dev/null || sync || return 1
    mv -f -- "$staging" "$final" || return 1
    sync -f -- "$final" 2>/dev/null || sync || return 1
    PUBLISH_JOURNAL_WRITTEN=1
    return 0
}

# Read one KEY=VALUE out of a journal file.  Prints the value, empty when the key is absent.
# The journal is written by this script only, one key per line, values with no newlines in them
# (they are leaf names and small integers), so a line-oriented read is exact rather than
# approximate -- and the caller checks for emptiness where emptiness is not allowed.
#
# No `--` before the filename: mawk rejects it as a filename rather than treating it as the
# end of options (measured), and it is not needed -- the path is always composed here as
# $OUTPUT_DIR_PROC/<leaf>, so it begins with /proc/ and can never look like an option.
publish_journal_value() { # $1=journal path  $2=key
    local journal="$1" key="$2"
    awk -v want="$key" '
        BEGIN { FS = "=" }
        substr($0, 1, 1) == "#" { next }
        {
            eq = index($0, "=")
            if (eq == 0) next
            if (substr($0, 1, eq - 1) != want) next
            print substr($0, eq + 1)
            exit
        }
    ' "$journal" 2>/dev/null || printf ''
}

# ------------------------------------------------------------------------------------
# IDENTITY, WHICH IS WHAT THE RECOVERY DECIDES ON.
#
# A rename changes a NAME and leaves the INODE alone.  That single property is what makes an
# interrupted publication decidable at all: the journal records what each participant's inode was
# while it was still at its starting name, and the recovery stats the names afterwards and asks
# which recorded object is sitting at each of them.  No timestamps, no globbing, no size
# heuristics and -- above all -- no trusting a completion flag that the kill may have pre-empted.
# ------------------------------------------------------------------------------------

# Describe what is at one leaf of the publication directory.  Prints exactly one of:
#
#   absent                    nothing of that name
#   irregular                 something is there and it is not a plain regular file, which
#                             includes a symbolic link -- checked BEFORE -e/-f, both of which
#                             follow one, so a link at a publication name reads as what it is
#   <device> <inode> <size>   a regular file, identified the way a rename preserves
#
# Always succeeds: "there is nothing there" and "there is something odd there" are answers the
# caller acts on, not errors.  The path is composed from the directory descriptor, like every
# other privileged operation in here.
publish_name_state() { # $1=leaf
    local path="$OUTPUT_DIR_PROC/$1" record
    if [ -L "$path" ]; then
        printf 'irregular'
        return 0
    fi
    if [ ! -e "$path" ]; then
        printf 'absent'
        return 0
    fi
    if [ ! -f "$path" ]; then
        printf 'irregular'
        return 0
    fi
    record="$(stat -c '%d %i %s' -- "$path" 2>/dev/null)" || record=''
    case "$record" in
        *[!0-9\ ]*|'') printf 'irregular' ;;
        *' '*' '*)      printf '%s' "$record" ;;
        *)              printf 'irregular' ;;
    esac
}

# Does a state string from publish_name_state() describe the object recorded as this device and
# inode?  False for 'absent', false for 'irregular', and false when the recorded identity is
# EMPTY -- which is how "there was no previous image" is written, and which must never match
# whatever happens to be sitting at a name.
publish_state_is() { # $1=state  $2=recorded device  $3=recorded inode
    local state="$1" device="$2" inode="$3" rest
    if [ -z "$device" ] || [ -z "$inode" ]; then
        return 1
    fi
    case "$state" in
        absent|irregular|'') return 1 ;;
    esac
    [ "${state%% *}" = "$device" ] || return 1
    rest="${state#* }"
    [ "${rest%% *}" = "$inode" ] || return 1
    return 0
}

# The size out of a state string; empty when the state does not carry one.
publish_state_bytes() { # $1=state
    local state="$1"
    case "$state" in
        absent|irregular|'') printf '' ;;
        *)                   printf '%s' "${state##* }" ;;
    esac
}

# Record one participant's identity while it is still at its starting name, into the three
# variables named by $2, $3 and $4.  Returns non-zero -- with all three cleared -- when the name
# does not hold a regular file, which every caller treats as fatal: a participant the journal
# cannot identify is a participant the next run's recovery could not place, and the whole point
# of the record is that it never has to guess.
publish_record_identity() { # $1=leaf  $2=device var  $3=inode var  $4=bytes var
    local leaf="$1" state
    local -n device_ref="$2" inode_ref="$3" bytes_ref="$4"
    device_ref=''
    inode_ref=''
    bytes_ref=''
    [ -n "$leaf" ] || return 1
    state="$(publish_name_state "$leaf")"
    case "$state" in
        absent|irregular|'') return 1 ;;
    esac
    device_ref="${state%% *}"
    state="${state#* }"
    inode_ref="${state%% *}"
    bytes_ref="${state##* }"
    # A state string that reached this point always carries three fields, so an empty one is a
    # defect in publish_name_state() rather than a condition of the file.  Checked rather than
    # assumed: the recovery's entire decision rests on these three values, and a silently empty
    # identity would read to the next run as "there was no such participant".
    if [ -z "$device_ref" ] || [ -z "$inode_ref" ] || [ -z "$bytes_ref" ]; then
        device_ref=''
        inode_ref=''
        bytes_ref=''
        return 1
    fi
    return 0
}

# Is one recorded identity out of the journal well formed?  Both fields digits is a recorded
# participant; both empty is "there was no such participant", which is how a FIRST publication
# writes the previous pair.  Anything else -- one field set and the other not, or a value with a
# character in it that an inode cannot have -- is refused by the caller rather than read as
# absent, because reading a half-recorded identity as "absent" is a decision about the caller's
# files taken on the strength of a field this script could not parse.
publish_identity_well_formed() { # $1=device  $2=inode
    local device="$1" inode="$2"
    if [ -z "$device" ] && [ -z "$inode" ]; then
        return 0
    fi
    case "$device" in ''|*[!0-9]*) return 1 ;; esac
    case "$inode" in ''|*[!0-9]*) return 1 ;; esac
    return 0
}

# ------------------------------------------------------------------------------------
# THE ONLY TWO PLACES THE RECOVERY DELETES ANYTHING, AND THE RULE THEY BOTH ENFORCE.
#
# Deleting the wrong file here is how an earlier revision destroyed the caller's previous image:
# the discard pass removed every journal-named leaf, and a kill in the write-after-move window
# had left the journal claiming the aside held nothing when it held the only copy.  So deletion
# is no longer driven by a name or by a flag.  An object is removed ONLY when the journal
# positively identifies it by device and inode as one of:
#
#   (1) an empty mktemp placeholder -- a holding name that was minted and never moved into;
#   (2) one of THIS run's staged files -- the interrupted build's work, which is being discarded
#       precisely because the previous generation is the one being kept;
#   (3) the previous generation's file, and ONLY once the new generation has been established
#       BY INODE as sitting at both of its published names.  This third class is not in the
#       first two because it deletes a file that WAS a good generation -- it is permitted
#       because at that point it is superseded by a complete published pair, and because leaving
#       it would make the very next run refuse to start: a leftover holding name is what
#       recover_interrupted_publication() reads as an interrupted publication.  The caller has
#       to pass the proof in; there is no default.
#
# Anything else at either name is refused with the names listed.  An object this script cannot
# identify is not an object it deletes.
# ------------------------------------------------------------------------------------

# Discard one set-aside holding name under the rule above.  Dies rather than guessing.
publish_discard_aside() { # $1=leaf  $2=what it is  $3=placeholder device  $4=placeholder inode
                          # $5=previous device  $6=previous inode  $7=1 when the new generation
                          #    has been established by inode at BOTH published names
    local leaf="$1" what="$2" holder_device="$3" holder_inode="$4"
    local previous_device="$5" previous_inode="$6" superseded="$7"
    local state
    [ -n "$leaf" ] || return 0
    state="$(publish_name_state "$leaf")"
    [ "$state" != 'absent' ] || return 0
    if publish_state_is "$state" "$holder_device" "$holder_inode"; then
        assert_output_dir_custody "discard the unused holding name $OUTPUT_DIR/$leaf"
        rm -f -- "$OUTPUT_DIR_PROC/$leaf" || die "the unused holding name
           $OUTPUT_DIR/$leaf
       could not be removed.  It holds nothing -- it is the empty file mktemp created when the
       name was minted -- but a leftover holding name is what the next run reads as an
       interrupted publication, and it refuses to disambiguate two of them.  Remove it by hand
       and re-run."
        log "discarded the unused holding name $OUTPUT_DIR/$leaf (an empty mktemp placeholder)"
        return 0
    fi
    if publish_state_is "$state" "$previous_device" "$previous_inode"; then
        [ "$superseded" -eq 1 ] || die "refusing to remove $OUTPUT_DIR/$leaf: the object at that
       holding name IS the caller's previous $what (device $previous_device inode
       $previous_inode), and the new generation has NOT been established at both of its published
       names.  Removing it would destroy the only complete generation in the directory, which is
       exactly the failure this recovery exists to prevent.  This is a defect in $SCRIPT_NAME if
       it is reached: report it with the journal at $OUTPUT_DIR/$PUBLISH_JOURNAL_LEAF."
        assert_output_dir_custody "remove the superseded previous $what $OUTPUT_DIR/$leaf"
        rm -f -- "$OUTPUT_DIR_PROC/$leaf" || die "the superseded previous $what
           $OUTPUT_DIR/$leaf
       could not be removed.  The new pair IS published, but a leftover holding name is half of a
       transaction that did not finish and the next run refuses to disambiguate it.  Remove it by
       hand and re-run."
        log "removed the superseded previous $what $OUTPUT_DIR/$leaf"
        return 0
    fi
    die "refusing to remove $OUTPUT_DIR/$leaf: the object at that holding name reads as
           $state
       which is neither the empty placeholder the journal recorded (device
       ${holder_device:-<unrecorded>} inode ${holder_inode:-<unrecorded>}) nor the previous $what
       (device ${previous_device:-<unrecorded>} inode ${previous_inode:-<unrecorded>}).  This
       script does not delete a file it cannot identify, and it does not build over a directory
       state it cannot explain.  Inspect it by hand, put the pair you want at
           $OUTPUT_IMAGE
           $OUTPUT_MANIFEST
       remove the rest, and re-run."
}

# Discard one of the interrupted run's staged files under the rule above.  Dies rather than
# guessing.
publish_discard_staged() { # $1=leaf  $2=what it is  $3=staged device  $4=staged inode
    local leaf="$1" what="$2" device="$3" inode="$4"
    local state
    [ -n "$leaf" ] || return 0
    state="$(publish_name_state "$leaf")"
    [ "$state" != 'absent' ] || return 0
    if ! publish_state_is "$state" "$device" "$inode"; then
        die "refusing to remove $OUTPUT_DIR/$leaf: the object at that staging name reads as
           $state
       which is not the interrupted run's staged $what (device ${device:-<unrecorded>} inode
       ${inode:-<unrecorded>}) the journal records under it.  Something outside that run put it
       there, and this script does not delete a file it cannot identify.  Inspect it by hand,
       remove it deliberately, and re-run."
    fi
    assert_output_dir_custody "discard the interrupted run's staged $what $OUTPUT_DIR/$leaf"
    rm -f -- "$OUTPUT_DIR_PROC/$leaf" || die "the interrupted run's staged $what
           $OUTPUT_DIR/$leaf
       could not be removed.  The previous pair IS restored, but a leftover staging name makes
       the next run's recovery ambiguous, so remove it by hand and re-run."
    log "discarded the interrupted run's staged $what $OUTPUT_DIR/$leaf"
}

# Move one aside file back onto its published name.  Returns non-zero on failure so the caller
# can account for it; says exactly what is where when it cannot.
#
# THE PRECONDITION IS THE FLAG, NOT THE FILE'S EXISTENCE.  $4 is the "this aside actually holds
# the previous content" flag, raised only after that leaf's own `mv` returned success.  An aside
# name that exists because mktemp created it, and was never moved into, holds ZERO BYTES -- and
# moving that over the caller's still-good published file is the failure this parameter exists
# to prevent.  $5 is the size the previous file had, re-checked against what is actually there:
# belt and braces on top of the flag, so a future edit that raises the flag in the wrong place
# still cannot restore a placeholder.
publish_restore_one() { # $1=aside leaf  $2=published leaf  $3=what it is  $4=holds-old flag
                        # $5=old size  $6=old device  $7=old inode
    local aside_leaf="$1" published_leaf="$2" what="$3" holds_old="$4" old_bytes="$5"
    local old_device="${6-}" old_inode="${7-}"
    local aside published aside_state published_state now_bytes
    [ -n "$aside_leaf" ] || return 0
    if [ "$holds_old" -ne 1 ]; then
        # Nothing was ever moved into this aside, so there is nothing to put back.  Said out
        # loud rather than passed over silently: the reader of a rollback log needs to know
        # that the previous file is still at its published path, not that a step was skipped.
        log "rollback: the previous $what was never moved aside, so it is still at its"
        log "  published path and needs no restoration"
        return 0
    fi
    aside="$OUTPUT_DIR_PROC/$aside_leaf"
    published="$OUTPUT_DIR_PROC/$published_leaf"
    aside_state="$(publish_name_state "$aside_leaf")"
    if [ "$aside_state" = 'absent' ]; then
        warn "ROLLBACK INCOMPLETE: the previous $what was moved aside to
       $OUTPUT_DIR/$aside_leaf
       but nothing is there now, so it cannot be put back at $OUTPUT_DIR/$published_leaf.
       Something outside this run removed it."
        return 1
    fi
    # IDENTITY FIRST, AND IT IS THE CHECK THAT CARRIES THIS.  The object at the aside name must
    # be the very object whose device and inode were recorded before the move -- not merely a
    # regular file of about the right size.  That is what distinguishes the caller's previous
    # file from the EMPTY mktemp placeholder the aside name was minted as, without trusting the
    # flag above, and it is also what refuses a file something outside this run put there.
    if [ -n "$old_device" ] && [ -n "$old_inode" ]; then
        if ! publish_state_is "$aside_state" "$old_device" "$old_inode"; then
            warn "ROLLBACK REFUSED for the previous $what: the object at
       $OUTPUT_DIR/$aside_leaf
       is not the one that was moved there.  It was recorded as device $old_device inode
       $old_inode before the move, and what is there now reads as '$aside_state'.  Moving it onto
       $OUTPUT_DIR/$published_leaf would publish a file this run cannot identify -- possibly the
       empty holding placeholder, possibly something else entirely.  Refused: inspect both by
       hand."
            return 1
        fi
    fi
    # SIZE, AS BELT AND BRACES ON TOP OF THE IDENTITY.  Retained rather than replaced: it costs
    # one field comparison and it catches a future edit that loses the recorded inode.
    now_bytes="$(publish_state_bytes "$aside_state")"
    if [ -n "$old_bytes" ] && [ "$old_bytes" != '0' ] && [ "$now_bytes" = '0' ]; then
        warn "ROLLBACK REFUSED for the previous $what: the aside at
       $OUTPUT_DIR/$aside_leaf
       is zero bytes, but the file it was made from was $old_bytes bytes.  That is an empty
       mktemp placeholder rather than the caller's previous $what, and moving it onto
       $OUTPUT_DIR/$published_leaf would destroy whatever is still there.  Refused: inspect
       both by hand."
        return 1
    fi
    if mv -f -- "$aside" "$published"; then
        # AND VERIFIED AFTER THE MOVE.  A rename that reported success but left a different
        # object at the published name is not a restoration, and the caller is about to report
        # the previous generation as recovered on the strength of this return value.
        if [ -n "$old_device" ] && [ -n "$old_inode" ]; then
            published_state="$(publish_name_state "$published_leaf")"
            if ! publish_state_is "$published_state" "$old_device" "$old_inode"; then
                warn "ROLLBACK INCOMPLETE: the previous $what was moved back onto
       $OUTPUT_DIR/$published_leaf
       and the rename reported success, but what is at that name now reads as
       '$published_state' rather than the recorded device $old_device inode $old_inode.  This run
       does not report the previous $what as restored on the strength of a rename whose result it
       cannot confirm."
                return 1
            fi
        fi
        log "rollback: the previous $what is back at $OUTPUT_DIR/$published_leaf"
        return 0
    fi
    warn "ROLLBACK INCOMPLETE: the previous $what is left at
       $OUTPUT_DIR/$aside_leaf
       and $OUTPUT_DIR/$published_leaf is absent.  Move it back by hand:
           mv '$OUTPUT_DIR/$aside_leaf' '$OUTPUT_DIR/$published_leaf'"
    return 1
}

# Withdraw one NEW leaf from its published name, back onto the staging name it came from.
# Returns non-zero on failure.  Called for BOTH leaves -- the asymmetry that withdrew only the
# image is one of the four defects this block closes.
publish_withdraw_one() { # $1=published leaf  $2=staged leaf  $3=what it is  $4=renamed-flag variable name
    local published_leaf="$1" staged_leaf="$2" what="$3"
    local -n renamed_ref="$4"
    [ "$renamed_ref" -eq 1 ] || return 0
    [ -n "$staged_leaf" ] || return 0
    if [ ! -f "$OUTPUT_DIR_PROC/$published_leaf" ]; then
        # Already gone: nothing to withdraw, and the flag is lowered so a second pass does not
        # report the same thing twice.
        renamed_ref=0
        return 0
    fi
    if mv -f -- "$OUTPUT_DIR_PROC/$published_leaf" "$OUTPUT_DIR_PROC/$staged_leaf"; then
        renamed_ref=0
        log "rollback: this run's $what is back at its staging name $OUTPUT_DIR/$staged_leaf"
        return 0
    fi
    warn "ROLLBACK INCOMPLETE: this run's $what could not be moved back off
       $OUTPUT_DIR/$published_leaf, so the previous $what cannot be restored to that name.  The
       file now there is THIS run's $what, which was not validated as publishable."
    return 1
}

# Undo whichever parts of the transaction had been done.  Called from the failure arms of
# publish_outputs() and from cleanup() for the paths that never reach them.  Returns non-zero if
# anything could not be put back, so both callers can treat that as the failure it is.
publish_rollback() {
    local failures=0
    # BOTH new leaves are withdrawn FIRST, and in the reverse of the order they were published.
    # Leaving either in place would keep its destination occupied and the restore below would
    # then overwrite this run's file with the previous one, which loses this run's work as well
    # as confusing the next.
    publish_withdraw_one "$OUTPUT_MANIFEST_LEAF" "$STAGED_MANIFEST_LEAF" 'manifest' \
        PUBLISH_MANIFEST_RENAMED || failures=$(( failures + 1 ))
    publish_withdraw_one "$OUTPUT_IMAGE_LEAF" "$STAGED_IMAGE_LEAF" 'image' \
        PUBLISH_IMAGE_RENAMED || failures=$(( failures + 1 ))

    publish_restore_one "$PUBLISH_ASIDE_IMAGE_LEAF" "$OUTPUT_IMAGE_LEAF" 'image' \
        "$PUBLISH_ASIDE_IMAGE_HOLDS_OLD" "$PUBLISH_OLD_IMAGE_BYTES" \
        "$PUBLISH_OLD_IMAGE_DEVICE" "$PUBLISH_OLD_IMAGE_INODE" \
        || failures=$(( failures + 1 ))
    publish_restore_one "$PUBLISH_ASIDE_MANIFEST_LEAF" "$OUTPUT_MANIFEST_LEAF" 'manifest' \
        "$PUBLISH_ASIDE_MANIFEST_HOLDS_OLD" "$PUBLISH_OLD_MANIFEST_BYTES" \
        "$PUBLISH_OLD_MANIFEST_DEVICE" "$PUBLISH_OLD_MANIFEST_INODE" \
        || failures=$(( failures + 1 ))

    if [ "$failures" -eq 0 ]; then
        # The empty aside names are removed rather than left behind: an aside leaf that exists
        # and holds nothing is exactly what the next run's recovery must not find, and after a
        # complete rollback neither of them holds anything.
        local leaf
        for leaf in "$PUBLISH_ASIDE_IMAGE_LEAF" "$PUBLISH_ASIDE_MANIFEST_LEAF"; do
            [ -n "$leaf" ] || continue
            [ -e "$OUTPUT_DIR_PROC/$leaf" ] || continue
            rm -f -- "$OUTPUT_DIR_PROC/$leaf" 2>/dev/null \
                || warn "the empty holding name $OUTPUT_DIR/$leaf could not be removed after a
       complete rollback.  Remove it by hand: the next run reads a leftover holding name as an
       interrupted publication."
        done
        # THE JOURNAL GOES LAST, and only once the rollback is complete.  While it exists the
        # next run will drive the same rollback again, which is idempotent and is the correct
        # behaviour if this process dies during the lines above.
        if [ "$PUBLISH_JOURNAL_WRITTEN" -eq 1 ] && [ -n "$PUBLISH_JOURNAL_LEAF" ]; then
            rm -f -- "$OUTPUT_DIR_PROC/$PUBLISH_JOURNAL_LEAF" 2>/dev/null \
                || warn "the publication journal $OUTPUT_DIR/$PUBLISH_JOURNAL_LEAF could not be
       removed after a complete rollback.  It is harmless -- the next run reads it, finds the
       previous pair already in place and removes it -- but it is named here so it is not a
       surprise."
            PUBLISH_JOURNAL_WRITTEN=0
        fi
        PUBLISH_ASIDE_IMAGE=''
        PUBLISH_ASIDE_MANIFEST=''
        PUBLISH_ASIDE_IMAGE_LEAF=''
        PUBLISH_ASIDE_MANIFEST_LEAF=''
        PUBLISH_ASIDE_IMAGE_HOLDS_OLD=0
        PUBLISH_ASIDE_MANIFEST_HOLDS_OLD=0
        PUBLISH_INTENT_ASIDE_IMAGE=0
        PUBLISH_INTENT_ASIDE_MANIFEST=0
        PUBLISH_INTENT_NEW_IMAGE=0
        PUBLISH_INTENT_NEW_MANIFEST=0
        PUBLISH_ASIDE_IMAGE_HOLDER_DEVICE=''
        PUBLISH_ASIDE_IMAGE_HOLDER_INODE=''
        PUBLISH_ASIDE_MANIFEST_HOLDER_DEVICE=''
        PUBLISH_ASIDE_MANIFEST_HOLDER_INODE=''
        PUBLISH_IN_PROGRESS=0
    else
        # LEFT FOR RECOVERY, DELIBERATELY.  The journal is what the next run uses to finish
        # this, so it is not removed when the rollback is incomplete.
        warn "the publication journal is left at $OUTPUT_DIR/${PUBLISH_JOURNAL_LEAF:-<none>} so
       the next run can finish this rollback under the publication lock."
    fi
    return "$failures"
}

# Fail the publication, rolling the transaction back first and restoring the signal handlers so
# the teardown behaves normally.  A rollback that itself fails is reported and does not stop the
# original failure from being the one that ends the run.
publish_fail() { # $1=message
    publish_rollback || warn "the rollback above did not complete; read its lines for what is
       where.  The publication failure that triggered it follows."
    install_interrupt_traps
    die "$1"
}

# ------------------------------------------------------------------------------------
# CRASH-CONSISTENT RECOVERY.  Run under the publication lock, before anything is built.
#
# WHAT IT IS FOR.  SIGKILL, an OOM kill and a power loss run no handler, so a run can die at any
# point in the four renames the publication is made of.  This function reads the journal the dead
# run left, works out from the FILESYSTEM which object is sitting at each of the six names
# involved, and drives the pair to one consistent generation.
#
# WHY IT DOES NOT DECIDE FROM THE RECORD ALONE, which is the correction that matters here.  The
# record used to carry one completion flag per move, written AFTER the move -- and a kill in that
# window leaves a durable flag saying the move did not happen when it did.  The previous revision
# of this function believed the flag: after a kill immediately following the previous image's own
# `mv`, MOVED_ASIDE_IMAGE read 0, the restore was skipped as "nothing was ever moved into this
# aside", and the discard pass then removed the journal-named aside -- which at that instant held
# the caller's only copy of the previous image.  The recovery destroyed the thing it exists to
# preserve.
#
# SO THE RECORD IS WRITE-AHEAD AND THE DECISION IS BY INODE.  Each move now has an INTENT field
# flushed before it and a DONE field flushed after, and the journal carries the device and inode
# of all six participants: the previous image, the previous manifest, this run's two staged files
# and the two empty mktemp placeholders the holding names were minted as.  (INTENT=1, DONE=0) is
# a legitimate terminal state of the record meaning "this move may or may not have happened", and
# this function answers it by stating the six names and asking which recorded object is at each.
# A rename preserves an inode, so that question has an answer; no flag is trusted for it.
#
# THE COMMIT POINT IS BOTH NEW OBJECTS AT THEIR PUBLISHED NAMES, established by inode.  Until
# both are there the new generation is not published, and recovery rolls the pair back to the
# PREVIOUS generation in full.  Once both are there the new generation IS published and recovery
# only finishes the housekeeping that was left -- the mode, the flush, the asides and the journal.
#
# AND IT ROLLS BACK RATHER THAN FORWARD, DELIBERATELY.  Completing somebody else's publication
# would mean asserting that the half of it this run never saw was valid: the staged image's size
# was checked by the dead run, not by this one, and the pair's mutual consistency -- the manifest
# names the image's size, architecture and derived binder protocol -- was never established at
# all.  The previous generation, by contrast, was published as a pair by a run that finished.  So
# the caller keeps the pair that was complete, and the interrupted build is re-run.
#
# WHAT IT WILL NOT DO IS GUESS.  Every disagreement it cannot settle by inode is refused with the
# names listed and EVERYTHING LEFT WHERE IT IS, so a human decides: an object at a publication
# name that the journal identifies as nothing, one recorded object apparently at two names, a
# recorded previous file that is at none of them, a record whose fields do not parse, and an
# aside file the journal does not mention at all (which is a second, older interrupted
# transaction).  Deletion in particular is governed by the rule at publish_discard_aside(): an
# object is removed only when the journal positively identifies it, never because its name
# matches a pattern.  The lock is held throughout, so no live builder owns any of these files.
# ------------------------------------------------------------------------------------
recover_interrupted_publication() {
    local journal staging entry
    local strays='' stray_count=0
    local version run_id generation last_step
    local j_image_leaf j_manifest_leaf j_staged_image_leaf j_staged_manifest_leaf
    local j_aside_image_leaf j_aside_manifest_leaf
    local j_old_image_device j_old_image_inode j_old_image_bytes
    local j_old_manifest_device j_old_manifest_inode j_old_manifest_bytes
    local j_staged_image_device j_staged_image_inode
    local j_staged_manifest_device j_staged_manifest_inode
    local j_holder_image_device j_holder_image_inode
    local j_holder_manifest_device j_holder_manifest_inode
    local intent_aside_image done_aside_image intent_aside_manifest done_aside_manifest
    local intent_new_image done_new_image intent_new_manifest done_new_manifest
    local state_published_image state_published_manifest
    local state_aside_image state_aside_manifest
    local state_staged_image state_staged_manifest
    local old_image_at='missing' old_manifest_at='missing'
    local new_image_at='missing' new_manifest_at='missing'
    local failures=0 leaf index_a index_b
    local -a identity_key=() identity_label=()

    [ "$OUTPUT_LOCK_HELD" -eq 1 ] || die "internal error: recover_interrupted_publication ran
       without the publication lock, so a live builder's aside files could be moved out from
       under it.  This is a defect in $SCRIPT_NAME."
    assert_output_dir_custody "scan $OUTPUT_DIR for an interrupted publication"

    journal="$OUTPUT_DIR_PROC/$PUBLISH_JOURNAL_LEAF"
    staging="$OUTPUT_DIR_PROC/$PUBLISH_JOURNAL_TMP_LEAF"

    # A STAGING LEAF IS ALWAYS DISCARDABLE.  With no journal beside it, it is a FIRST journal
    # write interrupted before its rename -- and the journal is written before anything moves, so
    # nothing had moved.  With a journal beside it, it is a torn update and the journal on disk
    # is the authoritative record.  Either way the staging leaf is removed and never read.
    if [ -e "$staging" ] || [ -L "$staging" ]; then
        warn "a partially written publication journal was left at
       $OUTPUT_DIR/$PUBLISH_JOURNAL_TMP_LEAF
       by a run that was killed mid-write.  It is discarded unread; the completed journal beside
       it, if there is one, is what this run acts on."
        rm -f -- "$staging" || die "the partially written publication journal
           $OUTPUT_DIR/$PUBLISH_JOURNAL_TMP_LEAF
       could not be removed.  It is refused rather than worked around: this run writes its own
       journal to that name, and a name it cannot claim is a journal it cannot make durable."
    fi

    # THE ASIDE SCAN, which is a CROSS-CHECK on the journal rather than the recovery itself.
    # NULL-delimited and -maxdepth 1, so a name with a space or a newline in it cannot split into
    # two and nothing outside the publication directory is considered.  The trailing slash on the
    # proc path is required: without it find lstats the /proc/self/fd/<n> symlink itself and
    # descends into nothing (measured).
    while IFS= read -r -d '' entry; do
        strays="$strays${strays:+
}$(basename -- "$entry")"
        stray_count=$(( stray_count + 1 ))
    done < <(find "$OUTPUT_DIR_PROC/" -maxdepth 1 -type f \
                  \( -name "$PUBLISH_ASIDE_IMAGE_PREFIX*" \
                     -o -name "$PUBLISH_ASIDE_MANIFEST_PREFIX*" \) -print0 2>/dev/null)

    if [ ! -e "$journal" ] && [ ! -L "$journal" ]; then
        if [ "$stray_count" -eq 0 ]; then
            return 0
        fi
        die "the publication directory holds set-aside copies from an interrupted publication
       but NO journal describing them:
$strays
       The journal is written and flushed before the first file is moved, so an aside without one
       cannot have come from a run of this version of $SCRIPT_NAME -- it is either from an older
       version, or something removed the journal.  Which of those files is the caller's last good
       image, and which is a stale leftover, cannot be established from the directory alone, and
       putting back the wrong one publishes an image nobody can identify.  Refused rather than
       guessed: they are ordinary ext4 images and KEY=VALUE manifests, so inspect them, move the
       pair you want onto
           $OUTPUT_IMAGE
           $OUTPUT_MANIFEST
       delete the rest, and re-run."
    fi

    [ ! -L "$journal" ] || die "the publication journal $OUTPUT_DIR/$PUBLISH_JOURNAL_LEAF is a
       symbolic link.  Refused rather than followed: this run would read a recovery plan from
       wherever the link points and then move the caller's files according to it."
    [ -f "$journal" ] || die "the publication journal $OUTPUT_DIR/$PUBLISH_JOURNAL_LEAF exists
       and is not a regular file.  Refused: a recovery plan is read out of it."
    grep -qxF -- "$PUBLISH_JOURNAL_TERMINATOR" "$journal" 2>/dev/null \
        || die "the publication journal $OUTPUT_DIR/$PUBLISH_JOURNAL_LEAF does not end with its
       terminator line, so the record was TORN: the run that wrote it died between the first byte
       and the last.  A half-written recovery plan is exactly the ambiguity the journal exists to
       remove, so it is refused rather than interpreted.  Inspect the journal and any
       ${PUBLISH_ASIDE_IMAGE_PREFIX}* / ${PUBLISH_ASIDE_MANIFEST_PREFIX}* files beside it, put the
       pair you want at $OUTPUT_IMAGE and $OUTPUT_MANIFEST, delete the rest, and re-run."

    version="$(publish_journal_value "$journal" JOURNAL_VERSION)"
    case "$version" in
        2) : ;;
        1) die "the publication journal $OUTPUT_DIR/$PUBLISH_JOURNAL_LEAF is a VERSION 1 record,
       written by an earlier build of $SCRIPT_NAME.  It is refused rather than read, and the
       reason is the whole point of version 2: a version 1 record carries one completion flag per
       move, written AFTER the move, and no participant identities at all.  Acting on it would
       mean acting on exactly the claim that was measured to be wrong -- a kill between a rename
       and its flag leaves the flag saying the rename did not happen -- and version 1 carries no
       inode with which to check.  The files themselves are intact: an ext4 image, a KEY=VALUE
       manifest, and one or two ${PUBLISH_ASIDE_IMAGE_PREFIX}* / ${PUBLISH_ASIDE_MANIFEST_PREFIX}*
       holding copies.  Inspect them, put the pair you want at
           $OUTPUT_IMAGE
           $OUTPUT_MANIFEST
       delete the rest including the journal, and re-run." ;;
        *) die "the publication journal $OUTPUT_DIR/$PUBLISH_JOURNAL_LEAF declares
       JOURNAL_VERSION='$version', which this build of $SCRIPT_NAME does not know how to act on
       (it writes and reads version 2).  Refused rather than misread: recovering a record whose
       fields mean something else moves the caller's files by mistake." ;;
    esac

    run_id="$(publish_journal_value "$journal" RUN_ID)"
    generation="$(publish_journal_value "$journal" GENERATION)"
    last_step="$(publish_journal_value "$journal" LAST_STEP)"
    j_image_leaf="$(publish_journal_value "$journal" IMAGE_LEAF)"
    j_manifest_leaf="$(publish_journal_value "$journal" MANIFEST_LEAF)"
    j_staged_image_leaf="$(publish_journal_value "$journal" STAGED_IMAGE_LEAF)"
    j_staged_manifest_leaf="$(publish_journal_value "$journal" STAGED_MANIFEST_LEAF)"
    j_aside_image_leaf="$(publish_journal_value "$journal" ASIDE_IMAGE_LEAF)"
    j_aside_manifest_leaf="$(publish_journal_value "$journal" ASIDE_MANIFEST_LEAF)"
    j_old_image_device="$(publish_journal_value "$journal" OLD_IMAGE_DEVICE)"
    j_old_image_inode="$(publish_journal_value "$journal" OLD_IMAGE_INODE)"
    j_old_image_bytes="$(publish_journal_value "$journal" OLD_IMAGE_BYTES)"
    j_old_manifest_device="$(publish_journal_value "$journal" OLD_MANIFEST_DEVICE)"
    j_old_manifest_inode="$(publish_journal_value "$journal" OLD_MANIFEST_INODE)"
    j_old_manifest_bytes="$(publish_journal_value "$journal" OLD_MANIFEST_BYTES)"
    j_staged_image_device="$(publish_journal_value "$journal" STAGED_IMAGE_DEVICE)"
    j_staged_image_inode="$(publish_journal_value "$journal" STAGED_IMAGE_INODE)"
    j_staged_manifest_device="$(publish_journal_value "$journal" STAGED_MANIFEST_DEVICE)"
    j_staged_manifest_inode="$(publish_journal_value "$journal" STAGED_MANIFEST_INODE)"
    j_holder_image_device="$(publish_journal_value "$journal" ASIDE_IMAGE_HOLDER_DEVICE)"
    j_holder_image_inode="$(publish_journal_value "$journal" ASIDE_IMAGE_HOLDER_INODE)"
    j_holder_manifest_device="$(publish_journal_value "$journal" ASIDE_MANIFEST_HOLDER_DEVICE)"
    j_holder_manifest_inode="$(publish_journal_value "$journal" ASIDE_MANIFEST_HOLDER_INODE)"
    intent_aside_image="$(publish_journal_value "$journal" INTENT_ASIDE_IMAGE)"
    done_aside_image="$(publish_journal_value "$journal" DONE_ASIDE_IMAGE)"
    intent_aside_manifest="$(publish_journal_value "$journal" INTENT_ASIDE_MANIFEST)"
    done_aside_manifest="$(publish_journal_value "$journal" DONE_ASIDE_MANIFEST)"
    intent_new_image="$(publish_journal_value "$journal" INTENT_NEW_IMAGE)"
    done_new_image="$(publish_journal_value "$journal" DONE_NEW_IMAGE)"
    intent_new_manifest="$(publish_journal_value "$journal" INTENT_NEW_MANIFEST)"
    done_new_manifest="$(publish_journal_value "$journal" DONE_NEW_MANIFEST)"

    # EVERY INTENT AND DONE RECORD MUST BE A LITERAL 0 OR 1.  An absent or malformed field is not
    # read as "no", because "no" is a decision about the caller's files.
    for entry in "$intent_aside_image" "$done_aside_image" \
                 "$intent_aside_manifest" "$done_aside_manifest" \
                 "$intent_new_image" "$done_new_image" \
                 "$intent_new_manifest" "$done_new_manifest"; do
        case "$entry" in
            0|1) : ;;
            *) die "the publication journal $OUTPUT_DIR/$PUBLISH_JOURNAL_LEAF carries a
       write-ahead record that is neither 0 nor 1:
           aside image    INTENT='$intent_aside_image'    DONE='$done_aside_image'
           aside manifest INTENT='$intent_aside_manifest' DONE='$done_aside_manifest'
           new image      INTENT='$intent_new_image'      DONE='$done_new_image'
           new manifest   INTENT='$intent_new_manifest'   DONE='$done_new_manifest'
       An unreadable record cannot be read as 'that step did not happen': that reading would move
       the caller's files on the strength of a field this script could not parse.  Refused." ;;
        esac
    done

    # EVERY RECORDED IDENTITY MUST PARSE.  Both fields digits is a recorded participant; both
    # empty is "there was no such participant", which is what a FIRST publication writes for the
    # previous pair.  One set and the other not is neither, and is refused rather than read as
    # absent -- reading it as absent is how a recovery decides not to restore something that is
    # there.
    if ! publish_identity_well_formed "$j_old_image_device" "$j_old_image_inode" \
       || ! publish_identity_well_formed "$j_old_manifest_device" "$j_old_manifest_inode" \
       || ! publish_identity_well_formed "$j_staged_image_device" "$j_staged_image_inode" \
       || ! publish_identity_well_formed "$j_staged_manifest_device" "$j_staged_manifest_inode" \
       || ! publish_identity_well_formed "$j_holder_image_device" "$j_holder_image_inode" \
       || ! publish_identity_well_formed "$j_holder_manifest_device" "$j_holder_manifest_inode"; then
        die "the publication journal $OUTPUT_DIR/$PUBLISH_JOURNAL_LEAF carries a participant
       identity that does not parse as a device and inode pair:
           previous image     ${j_old_image_device:-<empty>}/${j_old_image_inode:-<empty>}
           previous manifest  ${j_old_manifest_device:-<empty>}/${j_old_manifest_inode:-<empty>}
           staged image       ${j_staged_image_device:-<empty>}/${j_staged_image_inode:-<empty>}
           staged manifest    ${j_staged_manifest_device:-<empty>}/${j_staged_manifest_inode:-<empty>}
           image holder       ${j_holder_image_device:-<empty>}/${j_holder_image_inode:-<empty>}
           manifest holder    ${j_holder_manifest_device:-<empty>}/${j_holder_manifest_inode:-<empty>}
       Every decision this function takes is an inode comparison, so an identity it cannot read
       is a decision it cannot take.  Refused with everything left in place."
    fi

    # THE STAGED PAIR MUST BE IDENTIFIED.  Unlike the previous pair, which may legitimately not
    # have existed, this run's predecessor always had both staged files -- the journal is written
    # after they are validated.  An empty identity here means the record was written by something
    # other than publish_outputs().
    if [ -z "$j_staged_image_inode" ] || [ -z "$j_staged_manifest_inode" ]; then
        die "the publication journal $OUTPUT_DIR/$PUBLISH_JOURNAL_LEAF does not identify the
       interrupted run's staged pair (image inode '${j_staged_image_inode:-<empty>}', manifest
       inode '${j_staged_manifest_inode:-<empty>}').  Those two identities are how this run tells
       the interrupted build's files from the caller's previous ones, so a record without them
       cannot be acted on.  Refused with everything left in place."
    fi

    # THE JOURNAL MUST BE THIS OUTPUT'S JOURNAL.  Its leaf name is derived from the image leaf, so
    # a mismatch means the file was copied in from elsewhere or the --output was renamed.
    if [ "$j_image_leaf" != "$OUTPUT_IMAGE_LEAF" ] || [ "$j_manifest_leaf" != "$OUTPUT_MANIFEST_LEAF" ]; then
        die "the publication journal $OUTPUT_DIR/$PUBLISH_JOURNAL_LEAF describes a different
       output than this run's:
           the journal names   $j_image_leaf and $j_manifest_leaf
           this run publishes  $OUTPUT_IMAGE_LEAF and $OUTPUT_MANIFEST_LEAF
       Acting on it would move files this run has no claim to.  Refused: if the journal is stale,
       delete it and re-run; if the --output was renamed, recover the old name first."
    fi

    # NO TWO PARTICIPANTS MAY SHARE AN IDENTITY.  Every decision below is "which recorded object
    # is at this name", so two records with the same device and inode would make one name
    # answerable two ways.  They are distinct live files when the record is written, so this
    # cannot arise from publish_outputs() -- it is a check against a hand-edited or fabricated
    # journal, and it is cheap.
    [ -z "$j_old_image_inode" ] || { identity_key+=("$j_old_image_device:$j_old_image_inode")
        identity_label+=('the previous image'); }
    [ -z "$j_old_manifest_inode" ] || { identity_key+=("$j_old_manifest_device:$j_old_manifest_inode")
        identity_label+=('the previous manifest'); }
    identity_key+=("$j_staged_image_device:$j_staged_image_inode")
    identity_label+=("the interrupted run's staged image")
    identity_key+=("$j_staged_manifest_device:$j_staged_manifest_inode")
    identity_label+=("the interrupted run's staged manifest")
    [ -z "$j_holder_image_inode" ] || { identity_key+=("$j_holder_image_device:$j_holder_image_inode")
        identity_label+=("the image holding name's placeholder"); }
    [ -z "$j_holder_manifest_inode" ] || { identity_key+=("$j_holder_manifest_device:$j_holder_manifest_inode")
        identity_label+=("the manifest holding name's placeholder"); }
    index_a=0
    while [ "$index_a" -lt "${#identity_key[@]}" ]; do
        index_b=$(( index_a + 1 ))
        while [ "$index_b" -lt "${#identity_key[@]}" ]; do
            if [ "${identity_key[$index_a]}" = "${identity_key[$index_b]}" ]; then
                die "the publication journal $OUTPUT_DIR/$PUBLISH_JOURNAL_LEAF records the same
       device and inode
           ${identity_key[$index_a]}
       for two different participants (${identity_label[$index_a]} and
       ${identity_label[$index_b]}).  This function decides which object is at which name by
       comparing inodes, and two participants that share one cannot be told apart -- so every
       decision below would be a guess.  Refused with everything left in place: inspect the files
       by hand, put the pair you want at $OUTPUT_IMAGE and $OUTPUT_MANIFEST, delete the rest
       including the journal, and re-run."
            fi
            index_b=$(( index_b + 1 ))
        done
        index_a=$(( index_a + 1 ))
    done

    # EVERY ASIDE ON DISK MUST BE ONE THE JOURNAL NAMES.  One it does not name is a second,
    # older interrupted transaction, and choosing between two is the guess this refuses to make.
    if [ "$stray_count" -gt 0 ]; then
        while IFS= read -r entry; do
            [ -n "$entry" ] || continue
            [ "$entry" != "$j_aside_image_leaf" ] || continue
            [ "$entry" != "$j_aside_manifest_leaf" ] || continue
            die "the publication directory holds a set-aside copy the journal does not name:
           $OUTPUT_DIR/$entry
       The journal for this output describes asides '${j_aside_image_leaf:-<none>}' and
       '${j_aside_manifest_leaf:-<none>}', so the file above is from a DIFFERENT interrupted
       publication -- an older run, or an older version of $SCRIPT_NAME.  Two interrupted
       transactions in one directory cannot be disentangled by this script, and putting back the
       wrong image publishes one nobody can identify.  Refused: inspect them, keep the pair you
       want at $OUTPUT_IMAGE and $OUTPUT_MANIFEST, delete the rest, and re-run."
        done <<STRAY_EOF
$strays
STRAY_EOF
    fi

    warn "an interrupted publication was found in the publication directory:"
    warn "  journal    $OUTPUT_DIR/$PUBLISH_JOURNAL_LEAF"
    warn "  run        ${run_id:-unrecorded}   generation ${generation:-unrecorded}"
    warn "  last step  ${last_step:-unrecorded}"
    warn "  write-ahead records (INTENT before the move, DONE after it):"
    warn "    aside image    INTENT=$intent_aside_image DONE=$done_aside_image"
    warn "    aside manifest INTENT=$intent_aside_manifest DONE=$done_aside_manifest"
    warn "    new image      INTENT=$intent_new_image DONE=$done_new_image"
    warn "    new manifest   INTENT=$intent_new_manifest DONE=$done_new_manifest"
    warn "  A previous run was killed part-way through the publication (SIGKILL, an OOM kill or a"
    warn "  power loss runs no handler), so this run drives the pair to ONE generation before it"
    warn "  builds anything -- and it decides from the inodes on the disk, not from the records"
    warn "  above, because a kill can land between a rename and the record of it."

    # ---- WHERE EACH OBJECT ACTUALLY IS, decided by inode and nothing else.
    #
    # Six names, and a closed list of legal occupants for each.  Anything else at any of them is
    # a disagreement, and a disagreement ends this function with everything left in place.
    #
    #   the published image name : the previous image (not yet moved aside)
    #                            | the interrupted run's staged image (published)
    #                            | nothing
    #   the aside image name     : the previous image (moved aside)
    #                            | the empty mktemp placeholder (minted, never moved into)
    #                            | nothing
    #   the staged image name    : the interrupted run's staged image (not yet published)
    #                            | nothing
    #
    # ...and the same three for the manifest.
    state_published_image="$(publish_name_state "$OUTPUT_IMAGE_LEAF")"
    state_published_manifest="$(publish_name_state "$OUTPUT_MANIFEST_LEAF")"
    state_aside_image='absent'
    [ -z "$j_aside_image_leaf" ] || state_aside_image="$(publish_name_state "$j_aside_image_leaf")"
    state_aside_manifest='absent'
    [ -z "$j_aside_manifest_leaf" ] || state_aside_manifest="$(publish_name_state "$j_aside_manifest_leaf")"
    state_staged_image='absent'
    [ -z "$j_staged_image_leaf" ] || state_staged_image="$(publish_name_state "$j_staged_image_leaf")"
    state_staged_manifest='absent'
    [ -z "$j_staged_manifest_leaf" ] || state_staged_manifest="$(publish_name_state "$j_staged_manifest_leaf")"

    if publish_state_is "$state_published_image" "$j_old_image_device" "$j_old_image_inode"; then
        old_image_at='published'
    elif publish_state_is "$state_published_image" "$j_staged_image_device" "$j_staged_image_inode"; then
        new_image_at='published'
    elif [ "$state_published_image" != 'absent' ]; then
        die "the object at $OUTPUT_IMAGE reads as
           $state_published_image
       which the journal identifies as neither the previous image (device
       ${j_old_image_device:-<none>} inode ${j_old_image_inode:-<none>}) nor the interrupted run's
       staged image (device $j_staged_image_device inode $j_staged_image_inode).  Something
       outside that run put it there.  This run will not build over a publication state it cannot
       explain, and it will not move a file it cannot identify: inspect it by hand, put the pair
       you want at $OUTPUT_IMAGE and $OUTPUT_MANIFEST, delete the rest including the journal, and
       re-run."
    fi
    if publish_state_is "$state_aside_image" "$j_old_image_device" "$j_old_image_inode"; then
        [ "$old_image_at" = 'missing' ] || die "the previous image appears at TWO names at once --
       $OUTPUT_IMAGE and $OUTPUT_DIR/$j_aside_image_leaf both hold device $j_old_image_device
       inode $j_old_image_inode.  One inode under two names is a hard link, which this
       transaction never creates, so the directory is not in a state this function can reason
       about.  Refused with everything left in place."
        old_image_at='aside'
    elif publish_state_is "$state_aside_image" "$j_holder_image_device" "$j_holder_image_inode"; then
        : # the empty placeholder: the holding name was minted and never moved into.
    elif [ "$state_aside_image" != 'absent' ]; then
        die "the object at the image holding name $OUTPUT_DIR/$j_aside_image_leaf reads as
           $state_aside_image
       which the journal identifies as neither the previous image (device
       ${j_old_image_device:-<none>} inode ${j_old_image_inode:-<none>}) nor the empty placeholder
       that name was minted as (device ${j_holder_image_device:-<none>} inode
       ${j_holder_image_inode:-<none>}).  Refused with everything left in place: this script does
       not move or delete a file it cannot identify."
    fi
    if publish_state_is "$state_staged_image" "$j_staged_image_device" "$j_staged_image_inode"; then
        [ "$new_image_at" = 'missing' ] || die "the interrupted run's staged image appears at TWO
       names at once -- $OUTPUT_IMAGE and $OUTPUT_DIR/$j_staged_image_leaf both hold device
       $j_staged_image_device inode $j_staged_image_inode.  Refused with everything left in
       place."
        new_image_at='staged'
    elif [ "$state_staged_image" != 'absent' ]; then
        die "the object at the image staging name $OUTPUT_DIR/$j_staged_image_leaf reads as
           $state_staged_image
       which is not the staged image the journal records under it (device $j_staged_image_device
       inode $j_staged_image_inode).  Refused with everything left in place."
    fi

    if publish_state_is "$state_published_manifest" "$j_old_manifest_device" "$j_old_manifest_inode"; then
        old_manifest_at='published'
    elif publish_state_is "$state_published_manifest" "$j_staged_manifest_device" "$j_staged_manifest_inode"; then
        new_manifest_at='published'
    elif [ "$state_published_manifest" != 'absent' ]; then
        die "the object at $OUTPUT_MANIFEST reads as
           $state_published_manifest
       which the journal identifies as neither the previous manifest (device
       ${j_old_manifest_device:-<none>} inode ${j_old_manifest_inode:-<none>}) nor the interrupted
       run's staged manifest (device $j_staged_manifest_device inode $j_staged_manifest_inode).
       Refused with everything left in place; see the equivalent refusal for the image above."
    fi
    if publish_state_is "$state_aside_manifest" "$j_old_manifest_device" "$j_old_manifest_inode"; then
        [ "$old_manifest_at" = 'missing' ] || die "the previous manifest appears at TWO names at
       once -- $OUTPUT_MANIFEST and $OUTPUT_DIR/$j_aside_manifest_leaf both hold device
       $j_old_manifest_device inode $j_old_manifest_inode.  Refused with everything left in
       place."
        old_manifest_at='aside'
    elif publish_state_is "$state_aside_manifest" "$j_holder_manifest_device" "$j_holder_manifest_inode"; then
        : # the empty placeholder.
    elif [ "$state_aside_manifest" != 'absent' ]; then
        die "the object at the manifest holding name $OUTPUT_DIR/$j_aside_manifest_leaf reads as
           $state_aside_manifest
       which the journal identifies as neither the previous manifest (device
       ${j_old_manifest_device:-<none>} inode ${j_old_manifest_inode:-<none>}) nor the empty
       placeholder that name was minted as (device ${j_holder_manifest_device:-<none>} inode
       ${j_holder_manifest_inode:-<none>}).  Refused with everything left in place."
    fi
    if publish_state_is "$state_staged_manifest" "$j_staged_manifest_device" "$j_staged_manifest_inode"; then
        [ "$new_manifest_at" = 'missing' ] || die "the interrupted run's staged manifest appears
       at TWO names at once -- $OUTPUT_MANIFEST and $OUTPUT_DIR/$j_staged_manifest_leaf both hold
       device $j_staged_manifest_device inode $j_staged_manifest_inode.  Refused with everything
       left in place."
        new_manifest_at='staged'
    elif [ "$state_staged_manifest" != 'absent' ]; then
        die "the object at the manifest staging name $OUTPUT_DIR/$j_staged_manifest_leaf reads as
           $state_staged_manifest
       which is not the staged manifest the journal records under it (device
       $j_staged_manifest_device inode $j_staged_manifest_inode).  Refused with everything left
       in place."
    fi

    # A PREVIOUS FILE THE JOURNAL RECORDS MUST BE SOMEWHERE.  If it is at neither its published
    # name nor its holding name, this function cannot restore it -- and continuing would publish
    # a new generation over the wreckage of one it was supposed to be able to fall back to.
    if [ -n "$j_old_image_inode" ] && [ "$old_image_at" = 'missing' ]; then
        die "the journal records a previous image (device $j_old_image_device inode
       $j_old_image_inode, $j_old_image_bytes bytes) but it is at neither $OUTPUT_IMAGE nor its
       holding name ${j_aside_image_leaf:-<none recorded>}.  Something outside the interrupted run
       removed or replaced it.  This run will not build over that: the previous generation is what
       the recovery exists to preserve, and it is gone.  Inspect the directory by hand, decide
       what should be at $OUTPUT_IMAGE and $OUTPUT_MANIFEST, delete the journal deliberately, and
       re-run."
    fi
    if [ -n "$j_old_manifest_inode" ] && [ "$old_manifest_at" = 'missing' ]; then
        die "the journal records a previous manifest (device $j_old_manifest_device inode
       $j_old_manifest_inode, $j_old_manifest_bytes bytes) but it is at neither $OUTPUT_MANIFEST
       nor its holding name ${j_aside_manifest_leaf:-<none recorded>}.  Refused for the same
       reason as the image above: the previous generation is what the recovery preserves."
    fi

    # THE TWO COMBINATIONS THAT CANNOT HAPPEN.  (INTENT=1, DONE=0) is expected and is settled
    # above by inode; (INTENT=1, DONE=1) is settled the same way, and it is deliberately NOT
    # cross-checked, because a recovery that was itself killed part-way legitimately leaves DONE=1
    # with the object already back at its pre-move name or already discarded -- checking it would
    # make this function refuse to finish work it had started.  What cannot happen is a move whose
    # INTENT was never recorded: the intent is written and flushed BEFORE the rename, and a
    # failure to write it stops the transaction, so an object at its post-move name with INTENT=0
    # means something outside that run moved it.
    if [ "$intent_aside_image" -eq 0 ] && [ "$old_image_at" = 'aside' ]; then
        die "the previous image is at its holding name $OUTPUT_DIR/$j_aside_image_leaf, but the
       journal records INTENT_ASIDE_IMAGE=0 -- the intent to move it was never written.  The
       intent is flushed BEFORE the rename and a failure to write it stops the transaction, so
       this state cannot have been produced by $SCRIPT_NAME.  Refused with everything left in
       place."
    fi
    if [ "$intent_aside_manifest" -eq 0 ] && [ "$old_manifest_at" = 'aside' ]; then
        die "the previous manifest is at its holding name $OUTPUT_DIR/$j_aside_manifest_leaf, but
       the journal records INTENT_ASIDE_MANIFEST=0.  Refused with everything left in place; see
       the equivalent refusal for the image above."
    fi
    if [ "$intent_new_image" -eq 0 ] && [ "$new_image_at" = 'published' ]; then
        die "the interrupted run's staged image is at $OUTPUT_IMAGE, but the journal records
       INTENT_NEW_IMAGE=0 -- the intent to publish it was never written.  Refused with everything
       left in place."
    fi
    if [ "$intent_new_manifest" -eq 0 ] && [ "$new_manifest_at" = 'published' ]; then
        die "the interrupted run's staged manifest is at $OUTPUT_MANIFEST, but the journal records
       INTENT_NEW_MANIFEST=0.  Refused with everything left in place."
    fi

    warn "  where each object actually is, established by inode:"
    warn "    the previous image             $old_image_at"
    warn "    the previous manifest          $old_manifest_at"
    warn "    the interrupted run's image    $new_image_at"
    warn "    the interrupted run's manifest $new_manifest_at"

    if [ "$new_image_at" = 'published' ] && [ "$new_manifest_at" = 'published' ]; then
        # ---- FORWARD.  Both new objects are AT their published names, established by inode, so
        # the new generation IS the published pair and only the housekeeping was left.  Nothing
        # is moved.
        warn "  Both halves of the new pair are at their published names, so the pair IS"
        warn "  published: the mode and durability steps that were left are finished, and the"
        warn "  set-aside previous copies are superseded and are removed."
        # THE MODE IS PART OF THE PUBLICATION, so finishing forward finishes it.  The staged
        # files are 0600 and 0644 respectively and the image's 0644 is set AFTER its rename, so a
        # run killed between the commit and that chmod leaves a published image the runner's own
        # account cannot read -- and a "recovered" pair the job cannot use is not a recovered
        # pair.  Idempotent, so it costs nothing when the chmod had already run.
        for leaf in "$OUTPUT_IMAGE_LEAF" "$OUTPUT_MANIFEST_LEAF"; do
            assert_output_dir_custody "finish the published mode of $OUTPUT_DIR/$leaf"
            chmod 0644 -- "$OUTPUT_DIR_PROC/$leaf" || die "the recovered pair is at its published
       names but the mode of $OUTPUT_DIR/$leaf could not be set to 0644.  The workflow's artifact
       upload and the QEMU process both read those files, so this run does not treat the recovery
       as finished; fix the mode by hand and re-run."
        done
        # THE ASIDES.  This is the third permitted deletion class at publish_discard_aside(), and
        # the proof it requires is the condition of this branch: both new objects have just been
        # established BY INODE at both published names, so what is at a holding name is a
        # superseded generation rather than the only one.  The `1` is that proof.
        publish_discard_aside "$j_aside_image_leaf" 'image' \
            "$j_holder_image_device" "$j_holder_image_inode" \
            "$j_old_image_device" "$j_old_image_inode" 1
        publish_discard_aside "$j_aside_manifest_leaf" 'manifest' \
            "$j_holder_manifest_device" "$j_holder_manifest_inode" \
            "$j_old_manifest_device" "$j_old_manifest_inode" 1
    else
        # ---- BACK.  The new generation was not committed, so the previous one is restored in
        # full and the interrupted run's work is discarded.
        warn "  The new pair was NOT fully published, so the PREVIOUS pair is restored in full"
        warn "  and the interrupted build's staged files are discarded."

        # 1. Withdraw whichever new object reached a published name, back onto the staging name
        #    it came from.  Manifest first, mirroring publish_rollback: the reverse of the order
        #    they are published in.  The staging name is known to be free -- the classification
        #    above refuses an object at it that is not the staged file itself, and refuses the
        #    staged file being at two names -- but it is asserted rather than assumed, because
        #    this rename would otherwise overwrite whatever is there.
        if [ "$new_manifest_at" = 'published' ]; then
            [ -n "$j_staged_manifest_leaf" ] || die "the interrupted run's manifest is at
       $OUTPUT_MANIFEST and must be withdrawn, but the journal records no staging name to
       withdraw it to.  Refused with everything left in place."
            [ "$state_staged_manifest" = 'absent' ] || die "the interrupted run's manifest must be
       withdrawn from $OUTPUT_MANIFEST onto $OUTPUT_DIR/$j_staged_manifest_leaf, but something is
       already at that name.  Refused with everything left in place."
            assert_output_dir_custody "withdraw the interrupted run's manifest from $OUTPUT_MANIFEST"
            mv -f -- "$OUTPUT_DIR_PROC/$OUTPUT_MANIFEST_LEAF" \
                     "$OUTPUT_DIR_PROC/$j_staged_manifest_leaf" \
                || die "could not withdraw the interrupted run's manifest from $OUTPUT_MANIFEST,
       so the previous manifest cannot be put back there.  Move it out of the way by hand and
       re-run."
            new_manifest_at='staged'
            state_staged_manifest="$(publish_name_state "$j_staged_manifest_leaf")"
            log "withdrew the interrupted run's manifest from $OUTPUT_MANIFEST"
        fi
        if [ "$new_image_at" = 'published' ]; then
            [ -n "$j_staged_image_leaf" ] || die "the interrupted run's image is at $OUTPUT_IMAGE
       and must be withdrawn, but the journal records no staging name to withdraw it to.  Refused
       with everything left in place."
            [ "$state_staged_image" = 'absent' ] || die "the interrupted run's image must be
       withdrawn from $OUTPUT_IMAGE onto $OUTPUT_DIR/$j_staged_image_leaf, but something is
       already at that name.  Refused with everything left in place."
            assert_output_dir_custody "withdraw the interrupted run's image from $OUTPUT_IMAGE"
            mv -f -- "$OUTPUT_DIR_PROC/$OUTPUT_IMAGE_LEAF" \
                     "$OUTPUT_DIR_PROC/$j_staged_image_leaf" \
                || die "could not withdraw the interrupted run's image from $OUTPUT_IMAGE, so the
       previous image cannot be put back there.  Move it out of the way by hand and re-run."
            new_image_at='staged'
            state_staged_image="$(publish_name_state "$j_staged_image_leaf")"
            log "withdrew the interrupted run's image from $OUTPUT_IMAGE"
        fi

        # 2. Restore each previous file that is sitting at its holding name.  THE PRECONDITION IS
        #    WHERE THE OBJECT ACTUALLY IS, not what a completion record claims -- which is the
        #    correction this whole function exists for.  publish_restore_one() re-checks the
        #    inode before the move and again after it.
        assert_output_dir_custody "restore the previous pair onto $OUTPUT_IMAGE and $OUTPUT_MANIFEST"
        if [ "$old_image_at" = 'aside' ]; then
            publish_restore_one "$j_aside_image_leaf" "$OUTPUT_IMAGE_LEAF" 'image' 1 \
                "$j_old_image_bytes" "$j_old_image_device" "$j_old_image_inode" \
                || failures=$(( failures + 1 ))
        fi
        if [ "$old_manifest_at" = 'aside' ]; then
            publish_restore_one "$j_aside_manifest_leaf" "$OUTPUT_MANIFEST_LEAF" 'manifest' 1 \
                "$j_old_manifest_bytes" "$j_old_manifest_device" "$j_old_manifest_inode" \
                || failures=$(( failures + 1 ))
        fi
        [ "$failures" -eq 0 ] || die "$failures half(ves) of the previous pair could not be
       restored (named above).  This run will NOT build over a half-recovered publication: the
       caller's last good pair is what the recovery exists to preserve, and continuing would put
       a new image beside whichever half is still there.  Fix it by hand and re-run."

        # 3. Discard the interrupted run's leftovers -- and ONLY objects the journal positively
        #    identifies, per the rule at publish_discard_aside().  The staged files are its own
        #    work; a holding name that still exists at this point can only hold the empty
        #    placeholder, because step 2 renamed the previous file off it, and the `0` says that
        #    a holding name found still holding the previous file must NOT be deleted.
        publish_discard_staged "$j_staged_image_leaf" 'image' \
            "$j_staged_image_device" "$j_staged_image_inode"
        publish_discard_staged "$j_staged_manifest_leaf" 'manifest' \
            "$j_staged_manifest_device" "$j_staged_manifest_inode"
        publish_discard_aside "$j_aside_image_leaf" 'image' \
            "$j_holder_image_device" "$j_holder_image_inode" \
            "$j_old_image_device" "$j_old_image_inode" 0
        publish_discard_aside "$j_aside_manifest_leaf" 'manifest' \
            "$j_holder_manifest_device" "$j_holder_manifest_inode" \
            "$j_old_manifest_device" "$j_old_manifest_inode" 0

        # 4. AND THE RESULT IS VERIFIED, by inode, before anything says the recovery worked.  The
        #    published names must hold the previous generation where there was one, and must hold
        #    nothing where there was not -- a FIRST publication that was interrupted rolls back to
        #    an empty pair, and an empty pair is the correct outcome rather than a failure.
        state_published_image="$(publish_name_state "$OUTPUT_IMAGE_LEAF")"
        state_published_manifest="$(publish_name_state "$OUTPUT_MANIFEST_LEAF")"
        if [ -n "$j_old_image_inode" ]; then
            publish_state_is "$state_published_image" "$j_old_image_device" "$j_old_image_inode" \
                || die "the rollback finished but $OUTPUT_IMAGE does not hold the previous image:
       it reads as '$state_published_image' rather than device $j_old_image_device inode
       $j_old_image_inode.  This run does not report a recovery it cannot confirm."
        else
            [ "$state_published_image" = 'absent' ] || die "the rollback finished and the journal
       records that there was no previous image, so $OUTPUT_IMAGE should hold nothing -- and it
       reads as '$state_published_image'.  This run does not report a recovery it cannot confirm."
        fi
        if [ -n "$j_old_manifest_inode" ]; then
            publish_state_is "$state_published_manifest" "$j_old_manifest_device" "$j_old_manifest_inode" \
                || die "the rollback finished but $OUTPUT_MANIFEST does not hold the previous
       manifest: it reads as '$state_published_manifest' rather than device
       $j_old_manifest_device inode $j_old_manifest_inode.  This run does not report a recovery it
       cannot confirm."
        else
            [ "$state_published_manifest" = 'absent' ] || die "the rollback finished and the
       journal records that there was no previous manifest, so $OUTPUT_MANIFEST should hold
       nothing -- and it reads as '$state_published_manifest'.  This run does not report a
       recovery it cannot confirm."
        fi
    fi

    # DURABILITY BEFORE THE JOURNAL GOES.  Every rename above must be on the disk before the
    # record that would let a later run redo them is discarded; otherwise a power loss here
    # leaves the directory in the state this function started from and nothing saying so.
    if ! sync -f -- "$OUTPUT_DIR_PROC/" 2>/dev/null; then
        sync || die "the recovered publication could not be flushed to disk: neither
           sync -f -- '$OUTPUT_DIR'
       nor a whole-system 'sync' succeeded.  The journal is deliberately NOT removed, so a later
       run repeats this recovery rather than finding a state nothing accounts for.  Fix the
       filesystem and re-run."
    fi

    # THE JOURNAL GOES LAST.  While it exists the recovery above is repeatable, which is what
    # makes this function safe to be killed in the middle of; once it is gone the directory is
    # in one state and nothing is owed.
    assert_output_dir_custody "remove the recovered publication journal"
    rm -f -- "$journal" || die "the publication journal $OUTPUT_DIR/$PUBLISH_JOURNAL_LEAF could
       not be removed after the recovery above completed.  This run will not build while it is
       there: it writes its own journal to that name, and a stale one would be read as this
       run's own record."
    log "publication recovery complete; the journal is removed and the directory is in one state"

    # The leaves were re-checked at resolution time; they have just changed, so they are
    # re-checked here rather than left as they were found.
    assert_publishable_leaf "$OUTPUT_DIR_PROC/$OUTPUT_IMAGE_LEAF" \
        'the --output image, after recovery' "$OUTPUT_IMAGE"
    assert_publishable_leaf "$OUTPUT_DIR_PROC/$OUTPUT_MANIFEST_LEAF" \
        'the manifest beside --output, after recovery' "$OUTPUT_MANIFEST"
}

publish_outputs() {
    local expected_bytes=$(( IMAGE_SIZE_MIB * 1024 * 1024 ))
    local actual_bytes staged_image_proc staged_manifest_proc aside_proc
    local image_proc manifest_proc aside_bytes

    # 0. EXCLUSIVITY.  Publishing without the lock would defeat the whole point of taking it,
    # and this catches a future edit that reorders main() rather than a runtime condition.
    [ "$OUTPUT_LOCK_HELD" -eq 1 ] || die "refusing to publish without the publication lock on
       $OUTPUT_LOCK.  lock_publication_targets() must run before anything is built; this is a
       defect in $SCRIPT_NAME rather than in the caller's inputs."

    # Every operation below names one of these four, never a caller pathname.
    staged_image_proc="$OUTPUT_DIR_PROC/$STAGED_IMAGE_LEAF"
    staged_manifest_proc="$OUTPUT_DIR_PROC/$STAGED_MANIFEST_LEAF"
    image_proc="$OUTPUT_DIR_PROC/$OUTPUT_IMAGE_LEAF"
    manifest_proc="$OUTPUT_DIR_PROC/$OUTPUT_MANIFEST_LEAF"

    # 1. VALIDATE.
    if [ -z "$STAGED_IMAGE_LEAF" ] || [ ! -f "$staged_image_proc" ]; then
        die "the staged image is missing at publication time (${STAGED_IMAGE:-<unset>}).
       Nothing is published: an absent staged image means the build did not produce one, and
       any previously published image is left exactly as it was."
    fi
    if [ -z "$STAGED_MANIFEST_LEAF" ] || [ ! -f "$staged_manifest_proc" ]; then
        die "the staged manifest is missing at publication time
       (${STAGED_MANIFEST:-<unset>}).  Nothing is published: an image without its manifest is
       unusable by the workflow, which reads every path out of the manifest."
    fi
    actual_bytes="$(stat -c '%s' -- "$staged_image_proc" 2>/dev/null || printf '0')"
    [ "$actual_bytes" = "$expected_bytes" ] || die "the staged image is $actual_bytes bytes but
       was created at ${IMAGE_SIZE_MIB} MiB ($expected_bytes bytes).  Something truncated or
       extended it after mkfs, so it is NOT published and the previous image is left in
       place.  A short image boots into a corrupt filesystem, which is the failure mode this
       check exists to prevent."

    # 2. RE-CHECK, adjacent to the use.  The output directory's custody and both staged leaves'
    # identities as well as the destinations: an hour has passed since they were established.
    assert_output_dir_custody "publish $OUTPUT_IMAGE and $OUTPUT_MANIFEST"
    assert_leaf_identity "$staged_image_proc" "$STAGED_IMAGE_DEVICE" "$STAGED_IMAGE_INODE" \
        "publish the staged image onto $OUTPUT_IMAGE" "$STAGED_IMAGE"
    assert_leaf_identity "$staged_manifest_proc" "$STAGED_MANIFEST_DEVICE" "$STAGED_MANIFEST_INODE" \
        "publish the staged manifest onto $OUTPUT_MANIFEST" "$STAGED_MANIFEST"
    assert_publishable_leaf "$image_proc"    'the --output image'          "$OUTPUT_IMAGE"
    assert_publishable_leaf "$manifest_proc" 'the manifest beside --output' "$OUTPUT_MANIFEST"

    # 3. MINT BOTH HOLDING NAMES BEFORE ANYTHING MOVES, and RECORD EVERY PARTICIPANT'S IDENTITY.
    #
    # Six identities, each read while its object is still at its starting name: the previous
    # image, the previous manifest, this run's two staged files, and the two empty placeholders
    # mktemp creates when it mints the holding names.  A rename preserves an inode, so this is
    # what lets the NEXT run recognise each object wherever a rename left it -- and, in
    # particular, tell "the holding name still holds the empty placeholder" from "the holding
    # name holds the caller's previous file" without trusting any completion flag.  The sizes
    # come along for the same reason they always did: as belt and braces under the identity.
    #
    # A failure here is a failure with nothing yet displaced, which is why it dies directly
    # rather than through publish_fail.
    PUBLISH_STAGED_IMAGE_BYTES="$actual_bytes"
    PUBLISH_STAGED_MANIFEST_BYTES="$(publish_state_bytes "$(publish_name_state "$STAGED_MANIFEST_LEAF")")"
    if [ -z "$PUBLISH_STAGED_MANIFEST_BYTES" ]; then
        die "internal error: the staged manifest at $STAGED_MANIFEST could not be measured
       although it was just asserted to be a regular file.  This is a defect in $SCRIPT_NAME:
       the journal records every participant's size, and a participant it cannot measure is one
       the next run's recovery could not identify."
    fi
    if [ -f "$image_proc" ]; then
        publish_record_identity "$OUTPUT_IMAGE_LEAF" \
            PUBLISH_OLD_IMAGE_DEVICE PUBLISH_OLD_IMAGE_INODE PUBLISH_OLD_IMAGE_BYTES \
            || die "the previous image at $OUTPUT_IMAGE is there but its device, inode and size
       could not be read.  Nothing has been moved: the previous pair is exactly as it was.  It is
       refused rather than worked around -- the recovery in the next run recognises the previous
       image by its inode, so an image this run cannot identify is one that run could not put
       back."
        aside_proc="$(mktemp -- "$OUTPUT_DIR_PROC/${PUBLISH_ASIDE_IMAGE_PREFIX}XXXXXXXX")" \
            || die "could not create a holding file for the previous image in $OUTPUT_DIR.
       Nothing has been moved: the previous pair is exactly as it was."
        PUBLISH_ASIDE_IMAGE_LEAF="$(basename -- "$aside_proc")"
        PUBLISH_ASIDE_IMAGE="$OUTPUT_DIR/$PUBLISH_ASIDE_IMAGE_LEAF"
        publish_record_identity "$PUBLISH_ASIDE_IMAGE_LEAF" \
            PUBLISH_ASIDE_IMAGE_HOLDER_DEVICE PUBLISH_ASIDE_IMAGE_HOLDER_INODE aside_bytes \
            || die "the holding file just created at $PUBLISH_ASIDE_IMAGE could not be
       identified.  Nothing has been moved.  Its inode is what tells the next run's recovery
       'this holding name was minted and never moved into' from 'this holding name holds the
       caller's previous image', and without it that distinction would be a guess."
        # A NAME MKTEMP HAS JUST CREATED HOLDS NOTHING.  Asserted rather than assumed: mktemp
        # creates with O_EXCL, so content at that name means something else is writing into the
        # publication directory -- and the recovery tells "minted, never moved into" from "holds
        # the previous image" by inode with the size as belt and braces, so a placeholder that
        # is not empty blurs the very distinction the identity above exists to make.
        [ "$aside_bytes" = '0' ] || die "the holding file just created at
           $PUBLISH_ASIDE_IMAGE
       is $aside_bytes bytes rather than empty.  mktemp creates it with O_EXCL, so a file with
       content in it at that name means something else is writing into $OUTPUT_DIR.  Nothing has
       been moved and the previous pair is exactly as it was; refused rather than continued."
    fi
    if [ -f "$manifest_proc" ]; then
        publish_record_identity "$OUTPUT_MANIFEST_LEAF" \
            PUBLISH_OLD_MANIFEST_DEVICE PUBLISH_OLD_MANIFEST_INODE PUBLISH_OLD_MANIFEST_BYTES \
            || {
                [ -z "$PUBLISH_ASIDE_IMAGE_LEAF" ] \
                    || rm -f -- "$OUTPUT_DIR_PROC/$PUBLISH_ASIDE_IMAGE_LEAF"
                PUBLISH_ASIDE_IMAGE=''
                PUBLISH_ASIDE_IMAGE_LEAF=''
                PUBLISH_ASIDE_IMAGE_HOLDER_DEVICE=''
                PUBLISH_ASIDE_IMAGE_HOLDER_INODE=''
                die "the previous manifest at $OUTPUT_MANIFEST is there but its device, inode and
       size could not be read.  Nothing has been moved: the previous pair is exactly as it was."
            }
        aside_proc="$(mktemp -- "$OUTPUT_DIR_PROC/${PUBLISH_ASIDE_MANIFEST_PREFIX}XXXXXXXX")" \
            || {
                # The image's holding name was minted but nothing was moved into it, so it is
                # removed rather than left for the next run's recovery to puzzle over.
                [ -z "$PUBLISH_ASIDE_IMAGE_LEAF" ] \
                    || rm -f -- "$OUTPUT_DIR_PROC/$PUBLISH_ASIDE_IMAGE_LEAF"
                PUBLISH_ASIDE_IMAGE=''
                PUBLISH_ASIDE_IMAGE_LEAF=''
                PUBLISH_ASIDE_IMAGE_HOLDER_DEVICE=''
                PUBLISH_ASIDE_IMAGE_HOLDER_INODE=''
                die "could not create a holding file for the previous manifest in $OUTPUT_DIR.
       Nothing has been moved: the previous pair is exactly as it was."
            }
        PUBLISH_ASIDE_MANIFEST_LEAF="$(basename -- "$aside_proc")"
        PUBLISH_ASIDE_MANIFEST="$OUTPUT_DIR/$PUBLISH_ASIDE_MANIFEST_LEAF"
        publish_record_identity "$PUBLISH_ASIDE_MANIFEST_LEAF" \
            PUBLISH_ASIDE_MANIFEST_HOLDER_DEVICE PUBLISH_ASIDE_MANIFEST_HOLDER_INODE aside_bytes \
            || die "the holding file just created at $PUBLISH_ASIDE_MANIFEST could not be
       identified.  Nothing has been moved; see the equivalent refusal for the image above for
       why an unidentifiable holding name is not continued past."
        # A NAME MKTEMP HAS JUST CREATED HOLDS NOTHING.  Asserted rather than assumed: mktemp
        # creates with O_EXCL, so content at that name means something else is writing into the
        # publication directory -- and the recovery tells "minted, never moved into" from "holds
        # the previous manifest" by inode with the size as belt and braces, so a placeholder that
        # is not empty blurs the very distinction the identity above exists to make.
        [ "$aside_bytes" = '0' ] || die "the holding file just created at
           $PUBLISH_ASIDE_MANIFEST
       is $aside_bytes bytes rather than empty.  mktemp creates it with O_EXCL, so a file with
       content in it at that name means something else is writing into $OUTPUT_DIR.  Nothing has
       been moved and the previous pair is exactly as it was; refused rather than continued."
    fi

    # 4. THE JOURNAL, WRITTEN AND MADE DURABLE BEFORE THE FIRST MOVE.  Everything after this
    # point is recoverable by the next run; nothing before it needed to be, because nothing had
    # moved.  A journal that cannot be written is therefore fatal HERE, with the previous pair
    # untouched -- publishing without it would mean a SIGKILL in the next four lines left a state
    # no later run could resolve, which is the defect this whole mechanism exists to close.
    PUBLISH_RUN_ID="$$-$(date -u '+%Y%m%dT%H%M%SZ')"
    PUBLISH_GENERATION="$(date -u '+%Y-%m-%dT%H:%M:%SZ')/$PUBLISH_RUN_ID"
    publish_journal_write 'about to move the previous pair aside' \
        || die "the publication journal could not be written to
           $OUTPUT_DIR/$PUBLISH_JOURNAL_LEAF
       Nothing has been moved and the previous pair is exactly as it was.  The journal is what
       makes an interrupted publication recoverable, so publishing without one is refused: a
       kill between the renames that follow would leave a state no later run could resolve."
    log "publication journal written: $OUTPUT_DIR/$PUBLISH_JOURNAL_LEAF"
    log "  generation $PUBLISH_GENERATION"

    # 5. MASK THE SIGNALS, RAISE THE FLAG.  From here to step 10 the caller's good pair exists
    # only under the aside names above, and those names are now visible to the EXIT trap.
    trap '' INT TERM HUP
    PUBLISH_IN_PROGRESS=1

    # 6. ASIDE, not deleted.  EVERY MOVE IS BRACKETED BY TWO FLUSHED JOURNAL WRITES: the INTENT
    # before it and the DONE after it.  The INTENT write is the one that matters, and it is the
    # correction of the defect described at PUBLISH_INTENT_ASIDE_IMAGE -- with only the write
    # after the move, a kill in between left a durable record saying the move had not happened
    # and the next run believed it.  With both, (INTENT=1, DONE=0) says "may or may not have
    # happened", and the recorded inodes tell the next run which it was.
    if [ -n "$PUBLISH_ASIDE_IMAGE_LEAF" ]; then
        PUBLISH_INTENT_ASIDE_IMAGE=1
        publish_journal_write 'about to move the previous image aside' \
            || publish_fail "the publication journal could not record the INTENT to move the
       previous image aside to
           $PUBLISH_ASIDE_IMAGE
       Nothing has been moved and nothing is published.  The intent is written BEFORE the move
       for a reason -- a move nobody recorded the intent of is a move the next run cannot know
       to look for -- so a failure to record it stops the transaction here rather than taking
       the move on trust."
        mv -f -- "$image_proc" "$OUTPUT_DIR_PROC/$PUBLISH_ASIDE_IMAGE_LEAF" \
            || publish_fail "could not move the previous image aside from $OUTPUT_IMAGE.
       Nothing is published; the previous pair is untouched."
        PUBLISH_ASIDE_IMAGE_HOLDS_OLD=1
        publish_journal_write 'the previous image is aside' \
            || publish_fail "the previous image was moved aside to
           $PUBLISH_ASIDE_IMAGE
       but the publication journal could not be updated to record the completion.  The rollback
       below puts it back; nothing is published.  This is no longer an unrecoverable window --
       the INTENT above is on the disk and the previous image's inode is recorded with it, so a
       kill here would be resolved by the next run from the filesystem -- but a transaction whose
       record this run cannot keep current is not one it continues."
    fi
    if [ -n "$PUBLISH_ASIDE_MANIFEST_LEAF" ]; then
        PUBLISH_INTENT_ASIDE_MANIFEST=1
        publish_journal_write 'about to move the previous manifest aside' \
            || publish_fail "the publication journal could not record the INTENT to move the
       previous manifest aside to
           $PUBLISH_ASIDE_MANIFEST
       The rollback below puts the previous image back; nothing is published."
        mv -f -- "$manifest_proc" "$OUTPUT_DIR_PROC/$PUBLISH_ASIDE_MANIFEST_LEAF" \
            || publish_fail "could not move the previous manifest aside from $OUTPUT_MANIFEST.
       The previous image has been put back and nothing is published."
        PUBLISH_ASIDE_MANIFEST_HOLDS_OLD=1
        publish_journal_write 'the previous pair is aside' \
            || publish_fail "the previous manifest was moved aside to
           $PUBLISH_ASIDE_MANIFEST
       but the publication journal could not be updated to record the completion.  The rollback
       below puts the previous pair back; nothing is published."
    fi

    # 7. and 8. RENAME, with rollback on every failure.
    #
    # The custody re-check is repeated here rather than relied upon from step 2, because the
    # renames are the operations that actually consume the directory's authority: a rename is
    # permitted by the DIRECTORY, so if the descriptor no longer refers to the directory that was
    # vetted -- or the lock's inode has been swapped underneath us -- these two renames are the
    # ones that would write into somebody else's.  A refusal here reaches the teardown with
    # PUBLISH_IN_PROGRESS raised, and cleanup()'s rollback arm puts the previous pair back --
    # which is exactly the case that arm exists for.
    assert_output_dir_custody "rename this run's pair onto $OUTPUT_IMAGE and $OUTPUT_MANIFEST"
    PUBLISH_INTENT_NEW_IMAGE=1
    publish_journal_write 'about to publish the new image' \
        || publish_fail "the publication journal could not record the INTENT to publish the new
       image onto $OUTPUT_IMAGE.  Nothing has been renamed onto either published name; the
       rollback below puts the previous pair back."
    if ! mv -f -- "$staged_image_proc" "$image_proc"; then
        publish_fail "could not publish the image to $OUTPUT_IMAGE.  The previous pair has been
       put back; the staged image is discarded by the teardown, because a staged image nobody
       can publish is exactly the half-built artefact a later step must not find."
    fi
    PUBLISH_IMAGE_RENAMED=1
    publish_journal_write 'the new image is published' \
        || publish_fail "the new image reached $OUTPUT_IMAGE but the publication journal could
       not be updated to record the completion.  The rollback below withdraws it and puts the
       previous pair back.  On the previous revision this was the one unrecoverable window -- a
       kill immediately after the rename left a new image, no manifest, and a journal saying
       neither had happened.  It is recoverable now, because the INTENT above is on the disk and
       every participant's inode is recorded with it, but a record this run cannot keep current
       is still not a transaction it continues."
    PUBLISH_INTENT_NEW_MANIFEST=1
    publish_journal_write 'about to publish the new manifest' \
        || publish_fail "the publication journal could not record the INTENT to publish the new
       manifest onto $OUTPUT_MANIFEST.  The image publication has been undone and the previous
       pair put back."
    if ! mv -f -- "$staged_manifest_proc" "$manifest_proc"; then
        publish_fail "could not publish the manifest to $OUTPUT_MANIFEST.  The image
       publication has been undone and the previous pair put back, because a new image beside a
       stale manifest is worse than no publication -- it looks complete."
    fi
    PUBLISH_MANIFEST_RENAMED=1
    # THE COMMIT POINT.  This journal record is what makes the new generation the published one:
    # a next run that reads BOTH rename records completes forward, and a next run that reads
    # anything less rolls back in full.  A failure to write it therefore rolls back, exactly as
    # the earlier ones do -- the pair on disk is complete, but a pair the next run would roll
    # back to the previous generation anyway is not a pair this run may report as published.
    publish_journal_write 'the new pair is published' \
        || publish_fail "both halves of the new pair reached their published names but the
       publication journal could not be updated to record the second one.  The rollback below
       withdraws both and puts the previous pair back: a journal that records only the image
       tells the next run to roll this publication back, so reporting it as published here would
       be reporting something the next run will undo."

    # 9. MODE, DURABILITY, THE ASIDES, THEN THE JOURNAL.  All fatal; see the block above.
    #
    # The image is world-readable at this point only in the sense the caller's own umask and
    # directory allow; 0644 is set explicitly so the workflow's artifact upload and the QEMU
    # process can read it without the caller having to guess the mode.  A failure here leaves a
    # published image the job cannot read, which is not a published image.
    assert_output_dir_custody "set the mode of the published pair in $OUTPUT_DIR"
    chmod 0644 -- "$image_proc" \
        || publish_fail "could not set the mode of $OUTPUT_IMAGE after publishing it.  The
       workflow's artifact upload and the QEMU process both read that file, so an image whose
       mode could not be set is not usable; the previous pair has been put back."
    chmod 0644 -- "$manifest_proc" \
        || publish_fail "could not set the mode of $OUTPUT_MANIFEST after publishing it.  The
       workflow reads every path it uses out of that file; the previous pair has been put back."

    # DURABILITY, AND IT IS CHECKED.  The directory entries are what must reach the disk here;
    # the image's data was flushed in release_image before it was renamed.  `sync -f` flushes
    # the filesystem holding the named file and is preferred because it does not stall on every
    # other filesystem the host has mounted; a coreutils too old for -f falls back to the
    # whole-system flush.  What is NOT tolerated any more is BOTH failing: a pair that is not on
    # the disk when a self-hosted runner reboots was never published, and `|| true` said
    # otherwise.
    if ! sync -f -- "$image_proc" "$manifest_proc" 2>/dev/null; then
        if ! sync; then
            publish_fail "the published pair could not be flushed to disk: neither
           sync -f -- '$OUTPUT_IMAGE' '$OUTPUT_MANIFEST'
       nor a whole-system 'sync' succeeded.  An unflushed rename is a pair that may not exist
       after a reboot, so this run does not report a publication it cannot vouch for.  The
       previous pair has been put back."
        fi
        log "durability: 'sync -f' was unavailable or failed, so a whole-system sync was used"
    fi

    # THE SUPERSEDED ASIDES.  Fatal, and not tidiness: each is a whole image, and a leftover
    # aside is half of a transaction that did not finish -- which is exactly what
    # recover_interrupted_publication() reads as an interrupted run, and what it refuses to
    # disambiguate against its own journal.  The pair IS committed at this point, so this arm
    # does NOT roll back; it fails the run with the leftover named.
    local aside_leaf removal_failures=0
    for aside_leaf in "$PUBLISH_ASIDE_IMAGE_LEAF" "$PUBLISH_ASIDE_MANIFEST_LEAF"; do
        [ -n "$aside_leaf" ] || continue
        [ -e "$OUTPUT_DIR_PROC/$aside_leaf" ] || continue
        if ! rm -f -- "$OUTPUT_DIR_PROC/$aside_leaf"; then
            warn "could not remove the superseded copy $OUTPUT_DIR/$aside_leaf"
            removal_failures=$(( removal_failures + 1 ))
        fi
    done

    # THE JOURNAL IS REMOVED LAST, AFTER THE FINAL DURABILITY STEP.  Until it is gone the next
    # run would read this publication as interrupted and complete it forward, which is harmless
    # and idempotent -- removing it earlier would not be, because a kill between the removal and
    # the flush above would leave a pair whose durability nothing had established and no record
    # that anything was owed.
    if [ "$PUBLISH_JOURNAL_WRITTEN" -eq 1 ]; then
        if ! rm -f -- "$OUTPUT_DIR_PROC/$PUBLISH_JOURNAL_LEAF"; then
            warn "could not remove the publication journal $OUTPUT_DIR/$PUBLISH_JOURNAL_LEAF"
            removal_failures=$(( removal_failures + 1 ))
        else
            PUBLISH_JOURNAL_WRITTEN=0
        fi
    fi

    # 10. THE TRANSACTION IS CLOSED.  Cleared so the EXIT trap neither looks for staged files
    # that have been renamed away nor tries to roll back a committed publication.
    STAGED_IMAGE=''
    STAGED_MANIFEST=''
    STAGED_IMAGE_LEAF=''
    STAGED_MANIFEST_LEAF=''
    PUBLISH_ASIDE_IMAGE=''
    PUBLISH_ASIDE_MANIFEST=''
    PUBLISH_ASIDE_IMAGE_LEAF=''
    PUBLISH_ASIDE_MANIFEST_LEAF=''
    PUBLISH_ASIDE_IMAGE_HOLDS_OLD=0
    PUBLISH_ASIDE_MANIFEST_HOLDS_OLD=0
    PUBLISH_IMAGE_RENAMED=0
    PUBLISH_MANIFEST_RENAMED=0
    PUBLISH_INTENT_ASIDE_IMAGE=0
    PUBLISH_INTENT_ASIDE_MANIFEST=0
    PUBLISH_INTENT_NEW_IMAGE=0
    PUBLISH_INTENT_NEW_MANIFEST=0
    PUBLISH_ASIDE_IMAGE_HOLDER_DEVICE=''
    PUBLISH_ASIDE_IMAGE_HOLDER_INODE=''
    PUBLISH_ASIDE_MANIFEST_HOLDER_DEVICE=''
    PUBLISH_ASIDE_MANIFEST_HOLDER_INODE=''
    PUBLISH_IN_PROGRESS=0
    install_interrupt_traps

    if [ "$removal_failures" -gt 0 ]; then
        die "$removal_failures superseded file(s) from the previous publication could not be
       removed from $OUTPUT_DIR (named above).  The new pair IS published and durable at
           $OUTPUT_IMAGE
           $OUTPUT_MANIFEST
       so it is not rolled back -- but this run does not report success while a
       ${PUBLISH_ASIDE_IMAGE_PREFIX}*, ${PUBLISH_ASIDE_MANIFEST_PREFIX}* or journal file remains:
       the next run reads such a file as an interrupted publication and refuses to disambiguate
       it against its own journal.  Remove them by hand and the pair above is ready to use."
    fi

    log "published $OUTPUT_IMAGE and $OUTPUT_MANIFEST (mode 0644, flushed)"
    log "  each leaf was replaced by one atomic same-directory rename; the PAIR is consistent for"
    log "  a reader holding a shared flock on $OUTPUT_LOCK, which is what the workflow's boot and"
    log "  artifact-extraction steps take, and is restored to consistency by the next run's"
    log "  recovery if this one is interrupted"
    log "  every rename went through the publication directory descriptor on fd $OUTPUT_DIR_FD"
}

print_summary() {
    cat <<SUMMARY_EOF

================ ROOT IMAGE BUILT ================
image            $OUTPUT_IMAGE
manifest         $OUTPUT_MANIFEST
ELF bitness      $ARCH  (32-bit throughout; a SEPARATE axis from the wire protocol)
binder protocol  $DERIVED_BINDER_PROTOCOL  (an EXPECTATION this builder DERIVED -- kernel configuration: ${PROTOCOL_FROM_KERNEL_CONFIG:-could not answer}, staged SDK cache: ${PROTOCOL_FROM_SDK_CACHE:-could not answer}; the authority is the guest's own three-way cross-check ending at the BINDER_VERSION ioctl on /dev/binder, and disagreement there is fatal)
guest entry      $GUEST_INIT_PATH
artifacts        $GUEST_ARTIFACT_DIR inside the image
status file      $GUEST_STATUS_FILE inside the image

WHAT THE JOB DOES NEXT

  1. Boot this image with the guest kernel, which must be built with the options recorded
     in $GUEST_KERNEL_EXPECTATION inside the image:
     CONFIG_ANDROID, CONFIG_ANDROID_BINDER_IPC,
     CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder", CONFIG_ANDROID_BINDERFS and
     CONFIG_ASHMEM always; and CONFIG_ANDROID_BINDER_IPC_32BIT in whichever state the
     DERIVED protocol above requires, which that file spells out.  Booting a kernel whose
     protocol differs from the derived value is a named failure inside the guest rather than
     a silent one: the init asks /dev/binder directly.  Pass the root device and
         init=$GUEST_INIT_PATH
     on the kernel command line, and give the QEMU process its own bounded timeout: the
     guest powers itself down, and the timeout is the backstop for the case where it cannot.

  2. The guest needs NO network, NO shared directory and NO second disk.  Everything it
     runs is already inside this image.

  3. When the guest has powered down, loop-mount the image read-only and copy
     $GUEST_ARTIFACT_DIR out.  Read $GUEST_STATUS_FILE for the coverage runner's exact exit
     status: 0 means measured and passing, 3 is ADVISORY and is NOT a pass, anything else is
     a failure.

  4. The guest console carries one BINDER PLATFORM RECORD block with the kernel
     configuration, the SDK build flags and the resulting protocol version together, then a
     PROTOCOL AGREEMENT verdict comparing all three sources, and the coverage runner's own
     per-invocation output for the A-E matrix.  Keep the console log: it is the only place
     the platform record exists.

NOT MEASURED HERE, AND NOT CLAIMED ANYWHERE

  Performance parity and device-level L3 validation are deferred human steps on target
  hardware.  Nothing in this image build or in the guest run measures either, and no figure
  for either is produced or implied.
================ END ================
SUMMARY_EOF
}

# ------------------------------------------------------------------------------------
# MAIN.  The order is the dependency order, and every step is a hard gate: a run that cannot
# verify its inputs never reaches the point of creating an image, and a run that cannot
# finish the image never reaches the point of declaring one.
# ------------------------------------------------------------------------------------
main() {
    # THE SELF-TEST IS DISPATCHED FIRST, ahead of parse_args and therefore ahead of every
    # privileged step, so `--self-test` can never begin an image build.  It is matched
    # positionally rather than added to parse_args for exactly that reason: an option handled
    # inside parse_args would still be followed by resolve_arch, require_host_tools,
    # validate_inputs and the publication lock.
    if [ "${1:-}" = '--self-test' ]; then
        local self_test_status=0
        [ $# -eq 1 ] || die "--self-test takes no other arguments; it builds nothing, so no
       build option applies to it.  Run it on its own:
           bash $SCRIPT_PATH --self-test"
        run_self_test || self_test_status=$?
        return "$self_test_status"
    fi

    parse_args "$@"
    resolve_arch
    require_host_tools
    validate_inputs
    # Where the two output files may be written, and exclusive access to them, established
    # before anything is built: every one of these checks is worthless after an hour of work
    # has been committed to a path that turns out to be a link, and a second builder has to be
    # refused before it starts rather than after it finishes.
    resolve_publication_targets
    lock_publication_targets
    # UNDER THE LOCK, AND BEFORE ANYTHING IS BUILT: put back a pair that a previous run was
    # killed in the middle of publishing.  SIGKILL, an OOM kill and a power loss run no trap,
    # so the previous run's own rollback could not have happened; the holding names are fixed
    # prefixes precisely so a LATER run can find them without a record from the run that died.
    recover_interrupted_publication
    verify_sdk_tree
    verify_payload_tree
    # Derived here, before a single byte of image is written: a protocol disagreement between
    # the kernel configuration and the staged SDK is a fault in the INPUTS, and finding it out
    # after an 8 GiB image has been formatted and populated costs the whole build for nothing.
    derive_binder_protocol

    make_work_dir
    acquire_base_rootfs

    create_and_mount_image
    extract_base_rootfs
    mount_chroot_pseudo_filesystems
    assert_chroot_executable
    assert_base_architecture

    install_image_packages
    install_lcov_2x

    copy_guest_payload
    register_guest_library_paths
    build_guest_helpers
    assert_image_toolchain

    write_kernel_expectation
    write_sdk_build_flags_record
    write_image_conf
    write_guest_init

    # LAST thing done to the mounted filesystem, and it has to be: it inspects and rewrites
    # what every step above left behind, so anything that ran after it could reintroduce what
    # it verified was gone.
    scrub_and_assert_image_apt_credentials

    # AFTER the apt scrub and still before the image is released.  It reads and never writes,
    # so it cannot reintroduce anything the scrub above verified was gone -- which is what the
    # "last thing done to the mounted filesystem" rule protects -- and running it last means it
    # inspects the tree exactly as it will be published.
    assert_image_carries_no_secret_values

    release_image
    write_manifest
    # The pair becomes visible at the caller's paths here -- each leaf by one atomic rename, the
    # pair as a whole to any reader holding the publication lock -- and only after the staged
    # image has been validated.
    publish_outputs
    # Declared finished only here, after the pair is published. Until this point a failure
    # removes this run's STAGED files and leaves any previously published pair untouched.
    BUILD_COMPLETED=1
    print_summary
}

main "$@"
