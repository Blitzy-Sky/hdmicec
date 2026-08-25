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
# WHY THIS FILE EXISTS -- AND WHY IT IS THE TWENTY-FIFTH IN-SUBMODULE PATH
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
#   THE FILE-COUNT CONSEQUENCE, STATED SO A REVIEWER DOES NOT HAVE TO INFER IT.  The
#   migration's close-out check counts the in-submodule diff against an explicit allowlist
#   of TWENTY-FOUR paths, and no root-image script appears in that table.  This file makes
#   the count TWENTY-FIVE.  It is AUTHORISED by the provisioning requirement rather than by
#   that table, and it is NOT an out-of-scope addition: the workflow it serves cannot exist
#   without it.  .github/workflows/L1-tests.yml records the same twenty-fifth path in its
#   own header, and the two statements are kept in step deliberately.
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
#     3. NOTHING IS CREATED OUTSIDE THE OUTPUT IMAGE AND A RUN-SCOPED TEMPORARY DIRECTORY.
#        Every mount this script makes, every loop device it attaches and every temporary
#        directory it creates is registered and released by an EXIT/INT/TERM trap, in
#        reverse order, whether the run succeeded, failed or was cancelled.  A leaked loop
#        device or a leaked bind mount wedges the runner for every job after it.
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
# BITNESS: THIS IS A DELIBERATELY ALL-32-BIT, PROTOCOL-7 JOB
# ==============================================================================
#   The pinned Binder stack supports two layouts, and the kernel option
#   CONFIG_ANDROID_BINDER_IPC_32BIT DIFFERS BETWEEN THEM:
#
#     * ALL-32-BIT                        -- speaks binder protocol 7, option SET.
#     * 32-bit middleware / 64-bit vendor -- speaks binder protocol 8, option UNSET, with
#                                            the middleware's own libbinder built
#                                            BINDER_IPC_32BIT=OFF.
#
#   linux_binder_idl BUILD.md:409-413 (upstream repository, tag 2.6.0 -- NOT a file in this
#   workspace, cited by locator and never quoted here) documents a strict protocol-version
#   EQUALITY check when libbinder opens the driver node: kernel, SDK build and vendor side
#   must all agree, and a mismatch fails EVERY open.
#
#   THIS JOB IS THE ALL-32-BIT ONE.  CONFIG_ANDROID_BINDER_IPC_32BIT=y is therefore scoped
#   to THIS JOB and is NOT a requirement of supported platforms generally -- a platform
#   with a 64-bit vendor side needs it unset, and writing it down as a universal
#   requirement would be wrong for that platform.  The always-required options are separate
#   and are recorded in the image at ${GUEST_KERNEL_EXPECTATION} for the guest to report
#   against.
#
#   THE CONSEQUENCE THIS SCRIPT HAS TO HANDLE.  All-32-bit means the ENTIRE guest userspace
#   is 32-bit: the middleware, the staged SDK, servicemanager and every test binary.  So
#   the image's own toolchain and libraries are 32-bit too, and this script REFUSES a
#   staged SDK whose ELF class or machine does not match the architecture selected -- a
#   64-bit libbinder inside a 32-bit guest is a link failure at best and a protocol
#   mismatch at worst, and finding out at boot costs a whole QEMU cycle to diagnose.
#   i386 is the default because it is the straightforward choice for QEMU on an x86_64
#   host and needs no foreign-architecture emulation to populate; armhf is equally valid
#   and is accepted, at the cost of a registered binfmt_misc handler on the build host.
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
TEMP_DIRS=()
CLEANUP_IN_PROGRESS=0
# Whether an output image exists yet, and whether the run got far enough to declare it
# finished.  The pair is what lets cleanup remove a HALF-BUILT image: an incomplete image left
# on disk is worse than no image, because a workflow step that only checks for the file's
# existence would boot it.
IMAGE_CREATED=0
BUILD_COMPLETED=0

register_mount()  { MOUNT_POINTS+=("$1"); }
register_loop()   { LOOP_DEVICES+=("$1"); }
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

    local index mount_point loop_device temp_dir

    for (( index=${#MOUNT_POINTS[@]}-1; index>=0; index-- )); do
        mount_point="${MOUNT_POINTS[index]}"
        [ -n "$mount_point" ] || continue
        unmount_with_retries "$mount_point"
    done

    for (( index=${#LOOP_DEVICES[@]}-1; index>=0; index-- )); do
        loop_device="${LOOP_DEVICES[index]}"
        [ -n "$loop_device" ] || continue
        if [ -b "$loop_device" ] && loop_still_attached "$loop_device"; then
            # NO `--` BEFORE THE DEVICE, and that is measured rather than stylistic:
            # util-linux 2.41 takes the "--" in `losetup -d -- /dev/loopN` to BE a device name
            # and fails with "losetup: /dev/--: detach failed", leaving the real device
            # attached.  The value is always a /dev/loopN path losetup printed itself, so there
            # is no leading-dash argument to guard against here.
            if ! losetup -d "$loop_device" 2>/dev/null; then
                # Re-checked rather than reported: between the test above and the detach the
                # kernel may have auto-cleared it, and warning about that would be noise.
                if loop_still_attached "$loop_device"; then
                    warn "could not detach $loop_device; run 'losetup -d $loop_device' by hand."
                fi
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
        rm -rf -- "$temp_dir" 2>/dev/null \
            || warn "could not remove the temporary directory $temp_dir."
    done

    # A HALF-BUILT IMAGE IS REMOVED.  Only when this run created it and only when this run
    # failed: an incomplete image is not a smaller version of a finished one, it is an image
    # that boots into something undefined, and a workflow step that tests for the file's
    # existence would use it.  The logs on stdout are the diagnosis; the image carries none.
    if [ "$status" -ne 0 ] && [ "$IMAGE_CREATED" -eq 1 ] && [ "$BUILD_COMPLETED" -eq 0 ]; then
        if [ -n "$OUTPUT_IMAGE" ] && [ -f "$OUTPUT_IMAGE" ]; then
            rm -f -- "$OUTPUT_IMAGE" "$OUTPUT_IMAGE.manifest" 2>/dev/null || true
            warn "removed the incomplete image $OUTPUT_IMAGE, so it cannot be mistaken for a"
            warn "  finished one. The failure is diagnosed from the output above."
        fi
    fi

    return "$status"
}

trap cleanup EXIT
trap 'die "interrupted; releasing every mount, loop device and temporary directory."' INT
trap 'die "terminated; releasing every mount, loop device and temporary directory."' TERM

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
BASE_SHA256=''
LCOV_TARBALL=''
LCOV_SHA256=''
GTEST_PREFIX_SRC=''
KERNEL_CONFIG_SRC=''
APT_MIRROR=''
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
                           implausibly short, or inside a tree the operating system owns.
  -p, --payload DIR        The hdmicec checkout, and any binaries already built, to make
                           available to the guest.  Copied to $GUEST_PAYLOAD_DIR.
                           Must contain configure.ac and tests/L1Tests/run_coverage.sh.
  -s, --sdk-dir DIR        The staged rdk-halif-aidl tree: the Binder SDK and the AIDL
                           client stub snapshots.  Copied to $GUEST_SDK_DIR.  The four
                           staging prefixes are derived from it, not asked for separately.
      --base-tarball FILE  Base root filesystem tarball for the target architecture.
      --base-url URL       Alternative to --base-tarball: fetched ON THE BUILD HOST.  The
                           guest never reaches the network.
      --base-sha256 HEX    SHA256 of the base tarball.  MANDATORY unless a sidecar
                           <tarball>.sha256 is present.  Verified before extraction; a
                           mismatch is fatal.  No digest is hard-coded in this script.

OPTIONAL
  -a, --arch ARCH          i386 (default) or armhf.  Both are 32-bit: this job is
                           deliberately all-32-bit and speaks binder protocol 7.  The
                           resolved value is echoed, recorded in the image and written to
                           the manifest.
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
      --kernel-config FILE The guest kernel's configuration, copied in as BUILD-TIME
                           PROVENANCE only.  The init reports what the RUNNING guest shows
                           and never asserts a value from this file.
      --apt-mirror URL     Pin the package archive the image is populated from, for a
                           reproducible package set (a snapshot archive, for instance).
                           Omitted, the base tarball's own sources are used as they stand.
      --size MIB           Image size in MiB (default $DEFAULT_IMAGE_SIZE_MIB).  It holds the
                           toolchain, the payload, the staged SDK, a full build tree and the
                           coverage artifacts, so it is not a place to economise.
      --readiness-timeout S  Seconds the in-guest init waits for servicemanager to ANSWER
                           (default $DEFAULT_READINESS_TIMEOUT_SECONDS).  The wait is a bounded
                           poll on a real binder transaction, never a sleep.
  -h, --help               This message.

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


# ------------------------------------------------------------------------------------
# ARCHITECTURE RESOLUTION.
#
# Only 32-bit targets are accepted, and the refusal of a 64-bit one is a design statement
# rather than a limitation of this script: the job is all-32-bit so that kernel, SDK and
# vendor side agree on binder protocol 7, and a 64-bit guest userspace would need the other
# supported layout -- protocol 8, CONFIG_ANDROID_BINDER_IPC_32BIT unset and libbinder built
# BINDER_IPC_32BIT=OFF.  That is a different job with a different kernel, not an option
# here.
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
            die "unsupported --arch '$ARCH'.  This job is deliberately all-32-bit so that the
       kernel, the Binder SDK build and the vendor side agree on binder protocol 7, so the
       accepted values are the 32-bit ones: i386 (the default, and the straightforward choice
       for QEMU on an x86_64 host) or armhf (which additionally needs a registered
       binfmt_misc handler on this build host).  A 64-bit target belongs to the other
       supported layout -- protocol 8, with CONFIG_ANDROID_BINDER_IPC_32BIT unset -- which is
       a different job with a different kernel." ;;
    esac
    readonly EXPECTED_ELF_CLASS EXPECTED_ELF_MACHINE DEBIAN_ARCH
    readonly ELF_CLASS_REGEX ELF_MACHINE_REGEX ELF_REJECT_REGEX
    log "target architecture: $ARCH (all-32-bit, binder protocol 7; expecting $EXPECTED_ELF_CLASS $EXPECTED_ELF_MACHINE binaries)"
}

# ------------------------------------------------------------------------------------
# HOST PREREQUISITES.  Checked as one set so the caller learns everything missing at once
# rather than one package per failed run.
# ------------------------------------------------------------------------------------
require_host_tools() {
    local missing='' tool
    for tool in tar mkfs.ext4 losetup mount umount mountpoint chroot file readelf sha256sum \
                truncate mktemp install cp find rm sync ldconfig; do
        command -v -- "$tool" >/dev/null 2>&1 || missing="$missing $tool"
    done
    [ -z "$missing" ] || die "missing host tooling:$missing.
       This script populates the image through a loop device, a mount and a chroot, and it
       verifies what it copies in.  On a Debian/Ubuntu build host:
           sudo apt-get install -y e2fsprogs util-linux coreutils tar file binutils libc-bin
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

    if [ -n "$BASE_TARBALL" ] && [ -n "$BASE_URL" ]; then
        die "--base-tarball and --base-url are alternatives; both were given.  Pass the one
       that describes where the base actually comes from, so the manifest records a single
       provenance for it."
    fi
    [ -n "$BASE_TARBALL" ] || [ -n "$BASE_URL" ] || die "a base root filesystem is required:
       pass --base-tarball FILE (preferred: no network at all) or --base-url URL (fetched on
       THIS host, never from inside the guest), together with its SHA256.  There is no
       built-in default, because an unpinned base would make this job irreproducible."

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
       This job is all-32-bit -- kernel, SDK build and vendor side must agree on binder
       protocol 7 -- so a 64-bit staged artefact cannot be used.  Rebuild the Binder SDK and
       the AIDL stub snapshots for $ARCH, or select the architecture they were built for with
       --arch."
    fi
    printf '%s\n' "$description" | grep -Eq -- "$ELF_CLASS_REGEX" || die "$2 is not a 32-bit
       object:
           $1
           $description
       This job is all-32-bit; rebuild it for $ARCH or pass the --arch it was built for."
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
    log "payload verified: an hdmicec checkout with run_coverage.sh and no foreign build output"
}


# ------------------------------------------------------------------------------------
# RUN-SCOPED SCRATCH.  One directory, minted with mktemp -d so its name cannot be
# pre-created, registered with the trap before anything is written into it.
# ------------------------------------------------------------------------------------
WORK_DIR=''
IMAGE_MOUNT=''
BASE_ARCHIVE=''

make_work_dir() {
    WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cec-l2-rootfs.XXXXXXXX")"
    register_tempdir "$WORK_DIR"
    chmod 0700 -- "$WORK_DIR"
    IMAGE_MOUNT="$WORK_DIR/mnt"
    mkdir -p -- "$IMAGE_MOUNT"
    log "scratch directory: $WORK_DIR"
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
        log "fetching the base root filesystem on THIS host from $BASE_URL"
        log "  (host-side acquisition at image-build time; the guest never reaches the network)"
        if command -v curl >/dev/null 2>&1; then
            curl --fail --silent --show-error --location --output "$BASE_ARCHIVE" -- "$BASE_URL" \
                || die "could not fetch the base root filesystem from $BASE_URL.  Check the URL
       and this host's network access, or fetch it separately and pass --base-tarball."
        else
            wget --quiet --output-document="$BASE_ARCHIVE" -- "$BASE_URL" \
                || die "could not fetch the base root filesystem from $BASE_URL.  Check the URL
       and this host's network access, or fetch it separately and pass --base-tarball."
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
    log "creating a ${IMAGE_SIZE_MIB} MiB ext4 image at $OUTPUT_IMAGE"
    rm -f -- "$OUTPUT_IMAGE"
    truncate -s "${IMAGE_SIZE_MIB}M" -- "$OUTPUT_IMAGE" \
        || die "could not create $OUTPUT_IMAGE.  Check free space on its filesystem: the image
       is ${IMAGE_SIZE_MIB} MiB and holds a toolchain, the payload, the staged SDK, a full
       build tree and the coverage artifacts."
    # -m 0 keeps no reserved-blocks margin: nothing else shares this filesystem, and the
    # build tree wants every block.
    IMAGE_CREATED=1
    mkfs.ext4 -q -F -m 0 -L 'cec-l2-root' -- "$OUTPUT_IMAGE" \
        || die "mkfs.ext4 failed on $OUTPUT_IMAGE."

    LOOP_DEVICE="$(losetup --find --show -- "$OUTPUT_IMAGE")" \
        || die "could not attach a loop device to $OUTPUT_IMAGE.  If the host has run out of
       loop devices, free them with 'losetup -a' and 'losetup -d'."
    register_loop "$LOOP_DEVICE"
    readonly LOOP_DEVICE
    log "loop device: $LOOP_DEVICE"

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

install_image_packages() {
    if [ -n "$APT_MIRROR" ]; then
        log "pinning the package archive to $APT_MIRROR"
        printf '%s\n' "$APT_MIRROR" > "$IMAGE_MOUNT/etc/apt/sources.list"
        # A pinned snapshot archive is by definition not the latest, so let apt use it.
        mkdir -p -- "$IMAGE_MOUNT/etc/apt/apt.conf.d"
        printf '%s\n' 'Acquire::Check-Valid-Until "false";' \
            > "$IMAGE_MOUNT/etc/apt/apt.conf.d/99cec-l2-snapshot"
    fi

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

    # The archive metadata and the downloaded packages are dead weight in a boot image, and
    # the guest never uses apt.
    chroot_run apt-get clean || true
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
# SCOPED TO THIS JOB ONLY -- NOT a requirement of supported platforms generally:
# this is a deliberately all-32-bit job speaking binder protocol 7, and the option must be
# SET for that layout.  A platform with a 32-bit middleware and a 64-bit vendor side speaks
# protocol 8 and needs it UNSET, with its own libbinder built BINDER_IPC_32BIT=OFF.  The
# driver enforces protocol-version EQUALITY when libbinder opens the node, so kernel, SDK
# build and vendor side must agree; see linux_binder_idl BUILD.md:409-413 at tag 2.6.0.
CONFIG_ANDROID_BINDER_IPC_32BIT=y
#
# Also assumed by the init, and worth having for diagnosis rather than strictly required:
#   CONFIG_DEVTMPFS / CONFIG_DEVTMPFS_MOUNT  -- so /dev is populated before init runs
#   CONFIG_IKCONFIG / CONFIG_IKCONFIG_PROC   -- so /proc/config.gz exists and the init can
#                                               report the configuration ACTUALLY in effect
#                                               instead of reporting that it is unreadable
KERNEL_EXPECTATION_EOF
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
        printf '# built with (BINDER_IPC_32BIT) and the vendor side must agree.  This job is the\n'
        printf '# all-32-bit, protocol-7 one.  See linux_binder_idl BUILD.md:409-413 at tag 2.6.0.\n'
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
IMAGE_CONF_EOF
    chmod 0644 -- "$target"

    {
        printf '# How this image was built, recorded by %s.\n' "$SCRIPT_NAME"
        printf 'built_at_utc=%s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
        printf 'built_by=%s\n' "$SCRIPT_PATH"
        printf 'architecture=%s\n' "$ARCH"
        printf 'bitness_decision=all-32-bit, binder protocol 7 (scoped to this job)\n'
        printf 'image_size_mib=%s\n' "$IMAGE_SIZE_MIB"
        printf 'base_provenance=%s\n' "${BASE_URL:-$BASE_TARBALL}"
        printf 'base_sha256_verified=%s\n' "$BASE_SHA256"
        printf 'apt_mirror=%s\n' "${APT_MIRROR:-<the base tarball sources, unchanged>}"
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
            READINESS_TIMEOUT_SECONDS)
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
                    READINESS_TIMEOUT_SECONDS; do
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
report_kernel_config() {
    local source='' entry
    if [ -r /proc/config.gz ] && command -v zcat >/dev/null 2>&1; then
        source='/proc/config.gz'
    elif [ -r "/boot/config-$(uname -r)" ]; then
        source="/boot/config-$(uname -r)"
    fi

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
        if [ "$source" = '/proc/config.gz' ]; then
            line="$(zcat /proc/config.gz 2>/dev/null | grep -E "^($entry=|# $entry is not set)" || true)"
        else
            line="$(grep -E "^($entry=|# $entry is not set)" -- "$source" 2>/dev/null || true)"
        fi
        if [ -n "$line" ]; then
            printf '  %s\n' "$line"
        else
            printf '  %s: absent from the configuration (neither set nor explicitly unset)\n' "$entry"
        fi
    done
}

report_binder_platform() {
    local protocol_output protocol_status
    # The helper is the only way to ask the driver its protocol version; its failure is
    # reported verbatim rather than replaced with a plausible number.
    set +e
    protocol_output="$("$PROTOCOL_HELPER" /dev/binder 2>&1)"
    protocol_status=$?
    set -e

    printf '================ BINDER PLATFORM RECORD (read from this running guest) ================\n'
    printf 'reported_by: %s\n' "$INIT_NAME"
    printf 'image_target_architecture: %s   (all-32-bit; this job speaks binder protocol 7)\n' "$IMAGE_ARCH"
    printf 'uname: %s\n' "$(uname -srvmo 2>/dev/null || printf 'unavailable')"
    printf '\n'
    printf '---- kernel binder configuration, as the running guest reports it ----\n'
    report_kernel_config
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

    mkdir -p -- "$ARTIFACT_DIR"
    chmod 0700 -- "$ARTIFACT_DIR"

    mount_binderfs
    report_binder_platform

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
        # No "--" before the device; see the note in cleanup().
        if ! losetup -d "$LOOP_DEVICE"; then
            # The kernel auto-clears a loop device once its last user closes it, so a detach
            # that fails AFTER the unmount above has usually already happened. Only a device
            # that is still bound is a real failure.
            loop_still_attached "$LOOP_DEVICE" && die "could not detach $LOOP_DEVICE from
       $OUTPUT_IMAGE.  The image is not finished while a loop device still holds it."
        fi
    fi
    sync || true
    log "image released; every mount and loop device this run created is gone"
}

# ------------------------------------------------------------------------------------
# THE MANIFEST.  KEY=VALUE lines beside the image, so the workflow reads the paths it needs
# instead of hard-coding them -- and so a change to the guest layout is picked up by the
# workflow rather than silently disagreed with.
# ------------------------------------------------------------------------------------
write_manifest() {
    local manifest="$OUTPUT_IMAGE.manifest"
    local size_bytes
    size_bytes="$(stat -c '%s' -- "$OUTPUT_IMAGE" 2>/dev/null || printf 'unknown')"

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
        printf 'BITNESS=all-32-bit\n'
        printf 'BINDER_PROTOCOL=7\n'
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
        printf 'JOB_SCOPED_KERNEL_OPTION=CONFIG_ANDROID_BINDER_IPC_32BIT=y\n'
        printf 'JOB_SCOPED_KERNEL_OPTION_NOTE=scoped to this all-32-bit protocol-7 job only; a 32-bit-middleware/64-bit-vendor platform needs it unset\n'
        printf 'BASE_PROVENANCE=%s\n' "${BASE_URL:-$BASE_TARBALL}"
        printf 'BASE_SHA256=%s\n' "$BASE_SHA256"
        printf 'PAYLOAD_SOURCE=%s\n' "$PAYLOAD_DIR"
        printf 'SDK_SOURCE=%s\n' "$SDK_DIR"
        printf 'LCOV_SOURCE=%s\n' "${LCOV_TARBALL:-distribution package only}"
        printf 'READINESS_TIMEOUT_SECONDS=%s\n' "$READINESS_TIMEOUT_SECONDS"
        printf 'GUEST_NETWORK_AT_TEST_TIME=none\n'
        printf 'PAYLOAD_TRANSPORT=copied into the image at build time; artifacts are read back out of the image after the guest powers down\n'
    } > "$manifest"
    chmod 0644 -- "$manifest"
    log "manifest written to $manifest"
}

print_summary() {
    cat <<SUMMARY_EOF

================ ROOT IMAGE BUILT ================
image            $OUTPUT_IMAGE
manifest         $OUTPUT_IMAGE.manifest
architecture     $ARCH  (all-32-bit; this job speaks binder protocol 7)
guest entry      $GUEST_INIT_PATH
artifacts        $GUEST_ARTIFACT_DIR inside the image
status file      $GUEST_STATUS_FILE inside the image

WHAT THE JOB DOES NEXT

  1. Boot this image with the guest kernel, which must be built with the options recorded
     in $GUEST_KERNEL_EXPECTATION inside the image:
     CONFIG_ANDROID, CONFIG_ANDROID_BINDER_IPC,
     CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder", CONFIG_ANDROID_BINDERFS and
     CONFIG_ASHMEM always; plus CONFIG_ANDROID_BINDER_IPC_32BIT=y for THIS job, because it
     is the all-32-bit, protocol-7 one.  Pass the root device and
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
     configuration, the SDK build flags and the resulting protocol version together, and the
     coverage runner's own per-invocation output for the A-E matrix.  Keep the console log:
     it is the only place the platform record exists.

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
    parse_args "$@"
    resolve_arch
    require_host_tools
    validate_inputs
    verify_sdk_tree
    verify_payload_tree

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

    release_image
    write_manifest
    # Declared finished only here, after the image is released and the manifest written. Until
    # this point a failure removes the image rather than leaving a half-built one behind.
    BUILD_COMPLETED=1
    print_summary
}

main "$@"
