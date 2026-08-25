# L1 Unit Test Framework - Quick Reference

## Directory Structure

```text
tests/
├── Makefile.am                   # Conditionally includes L1Tests and L2Tests
├── BasicTest.cpp                 # Existing integration tests
├── CECCmd.cpp
├── CECMonitor.cpp
├── CECCmdTest.cpp
└── L1Tests/                      # L1 Unit Tests
    ├── Makefile.am               # L1 test build configuration
    ├── README.md                 # L1 test documentation
    ├── QUICK_START.md            # This quick reference
    ├── test_main.cpp             # Test runner entry point
    ├── run_coverage.sh           # coverage runner with 80% line gate
    ├── .lcovrc_l1                # lcov configuration (branch collection enabled)
    ├── .gitignore                # Ignore build artifacts
    ├── ccec/                     # CCEC library tests (13 files, 520 tests)
    │   ├── test_CECFrame.cpp
    │   ├── test_Connection.cpp
    │   ├── test_Bus.cpp
    │   ├── test_LibCCEC.cpp
    │   ├── test_MessageEncoder.cpp
    │   ├── test_MessageDecoder.cpp
    │   ├── test_OpCode.cpp
    │   ├── test_Operands.cpp
    │   ├── test_Driver_Mock.cpp
    │   ├── test_Driver.cpp
    │   ├── test_DriverImpl_Async.cpp
    │   ├── test_Util.cpp
    │   └── test_DriverAidl.cpp    # AIDL back-end + runtime selection (64 tests)
    └── osal/                     # OSAL library tests (3 files, 27 tests)
        ├── test_ConditionVariable.cpp
        ├── test_Mutex.cpp
        └── test_Thread.cpp
```

`tests/L2Tests/` sits beside this directory and is the second tier: `run_L2Tests` plus the
out-of-process `fake_hdmi_cec_aidl_host`.  See [The L2 tier](#the-l2-tier) below.

The two test subdirectories list exactly the entries in `Makefile.am`'s
`run_L1Tests_SOURCES`, each subdirectory in that list's relative order.  A
test file missing from that list is silently not compiled — no warning, no
build error — and the order matters, because GoogleTest registers fixtures
during static initialisation in link order, so append new entries rather
than inserting them (see the comment in `Makefile.am`).

## Build System Changes

### 1. configure.ac
- Added `--enable-l1tests` option
- Added Google Test dependency check
- Added `tests/L1Tests/Makefile` and `tests/L2Tests/Makefile` to configuration
- Added the **unconditional** AIDL/Binder dependency checks, which fail `configure` outright when
  the staging roots do not resolve — see [The AIDL/Binder dependency](#the-aidlbinder-dependency)

### 2. Makefile.am (root)
```makefile
DIST_SUBDIRS = cfg osal ccec tests
```

### 3. tests/Makefile.am
```makefile
if ENABLE_L1TESTS
SUBDIRS = L1Tests L2Tests
endif
```

### 4. tests/L1Tests/Makefile.am
- Defines `run_L1Tests` executable
- Links test sources with libRCEC.la and libRCECOSHal.la, plus the four AIDL/Binder edges
- `AM_CXXFLAGS = -Wall -std=c++17 -g @AIDL_CXXFLAGS@`

### 5. tests/L2Tests/Makefile.am
- Defines **two** executables: `run_L2Tests` and `fake_hdmi_cec_aidl_host`

## Quick Start

### Build and run, in one command
```bash
cd tests/L1Tests
./run_coverage.sh --build --run     # build, run every invocation, capture, report, gate
```

That is the maintained recipe.  Everything below is the same sequence by hand.

### The AIDL/Binder dependency
`libRCEC` links the generated HDMI-CEC AIDL client stubs and the Binder client library
**unconditionally** — both HAL back-ends are compiled in and one is chosen at run time — so there
is no switch to turn this off and nothing here builds without it.  **Two staging prefixes are the
whole contract**, and `configure` derives every path from them:

| Input | Spelling | Notes |
|--------|-------------|---------|
| HALIF staging root | `--with-halif-prefix=DIR` or `HALIF_PREFIX=DIR` | Omit when the `rdk-halif-aidl` checkout sits beside this submodule; `configure` falls back to it |
| AIDL stub libraries | `HALIF_LIB_DIR=DIR` | Needed whenever the built stubs are not at `HALIF_PREFIX/lib/halif` |
| Binder SDK root | `--with-binder-sdk=DIR` or `BINDER_SDK_DIR=DIR` | Holds `lib/binder` and `include/binder_sdk` |
| Binder headers | `BINDER_SDK_INCLUDE_DIR=DIR` | Only when they are staged away from `BINDER_SDK_DIR` |

From those, `configure` derives **four include roots** — `hdmicec/0.1.0.0/include`,
`common/0.2.0.0/include`, `common/current` and the Binder SDK header root — and **four link edges,
in dependency order**:

```text
-lhdmicec-v0.1.0.0-cpp -lcommon-v0.2.0.0-cpp -lbinder -lutils
```

Four things about that set are worth knowing before you debug it, and `README.md` carries the rest:

- **`common/current` is the only root that supplies `halcompat.h`** — it is *not* inside
  `common/0.2.0.0/include`, so omitting it surfaces as `halcompat.h: No such file or directory`.
- **`common` must be `0.2.0.0`**, the ABI the released `hdmicec` snapshot was pinned against; a
  newer `common` is not a substitute.
- **`-std=c++17` is required**, not preferred: `halcompat.h:73` opens
  `namespace com::rdk::hal::halcompat {`, a nested-namespace definition that does not compile at
  `c++14` at all.  `configure` checks for it and `AM_CXXFLAGS` sets it.
- **`libbinder`'s closure — `liblog`, `libbase`, `libcutils`, `libcutils_sockets` — must be staged
  and on the loader path**, at link time as well as at run time, but must never be added as direct
  edges.

Do not spell the roots or the edges into `CPPFLAGS`/`LDFLAGS` by hand; pass the prefixes and let
`configure` derive them, so that this build and the hand-written `ccec/src/Makefile` cannot end up
configured differently.

### Build with L1 Tests, by hand
Five steps are prerequisites rather than optional extras: the stub headers, the HAL-mock symlink,
`CPPFLAGS`/`PKG_CONFIG_PATH`, the two AIDL/Binder staging prefixes, and a **separate** `make` pass
for `tests/L1Tests` — a third for `tests/L2Tests`.  Omit any one and the build fails or silently
produces no test binary.

```bash
# From the hdmicec submodule root.  $GTEST_PREFIX must hold lib/pkgconfig/gtest.pc:
# configure.ac finds GoogleTest with PKG_CHECK_MODULES, which ignores -I and -L.
export GTEST_PREFIX="$HOME/.local/gtest-1.15.0"

# The two AIDL/Binder staging prefixes.  Omit HALIF_PREFIX when the rdk-halif-aidl
# checkout sits beside this submodule -- configure falls back to ../rdk-halif-aidl.
export HALIF_PREFIX="${HALIF_PREFIX:-$PWD/../rdk-halif-aidl}"
: "${HALIF_LIB_DIR:?the directory holding libhdmicec-v0.1.0.0-cpp}"
: "${BINDER_SDK_DIR:?the staged Binder SDK root holding lib/binder}"
: "${BINDER_SDK_INCLUDE_DIR:=$BINDER_SDK_DIR}"

mkdir -p stubs/rdk/iarmbus stubs/ccec/drivers/iarmbus
touch stubs/rdk/iarmbus/libIARM.h \
      stubs/rdk/iarmbus/libIBus.h \
      stubs/rdk/iarmbus/libIBusDaemon.h \
      stubs/ccec/drivers/iarmbus/CecIARMBusMgr.h
ln -sf ../../../mocks/hdmicec/hdmi_cec_driver.h stubs/ccec/drivers/hdmi_cec_driver.h

autoreconf -if
PKG_CONFIG_PATH="$GTEST_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}" \
CPPFLAGS="-I$PWD/mocks -I$PWD/stubs -I$GTEST_PREFIX/include" \
LDFLAGS="-L$GTEST_PREFIX/lib" \
  ./configure --enable-l1tests \
      --with-halif-prefix="$HALIF_PREFIX" \
      --with-binder-sdk="$BINDER_SDK_DIR" \
      BINDER_SDK_INCLUDE_DIR="$BINDER_SDK_INCLUDE_DIR" \
      HALIF_LIB_DIR="$HALIF_LIB_DIR"

make -j"$(nproc)" all
make -C tests/L1Tests all
make -C tests/L2Tests all
```

That recipe is deliberately **uninstrumented** — no `-fprofile-arcs`, no `-ftest-coverage`.  A
first build does not need them; `./run_coverage.sh --build` adds them when coverage is wanted.
Read the configure output back before trusting it: it prints one `AIDL HDMI CEC back-end:` line per
resolved path plus the C++17 flag it settled on, so a prefix that resolved to the wrong place is
visible there rather than in a link error much later.  Also put the Binder library directory on
`LD_LIBRARY_PATH` — `libbinder.so` carries no `RUNPATH`, so its closure has to be findable to
*link* against `libRCEC`, not merely to run.

`libglib2.0-dev` is a hard requirement of `configure`.

#### Those six regenerated `Makefile` files
`autoreconf`/`configure` rewrite six git-tracked `Makefile` files in this submodule.  Five are pure
toolchain output; **`ccec/src/Makefile` is not** — it is hand-written RDK build source, carrying
this migration's `-std=c++17`, `DriverAidlImpl.o` and rewritten link command, and it is in
`AC_CONFIG_FILES` all the same, so `configure` clobbers it by design.  `--build` therefore
snapshots all six from the working tree immediately before `autoreconf` and restores them from that
snapshot on the way out of the **same** invocation, so a cancelled build restores too.  A
standalone `./run_coverage.sh --restore` restores from that invocation's snapshot; with the six
dirty and no snapshot it **refuses and exits non-zero** rather than reaching for git.  **Never
revert those six with `git checkout`** — that restores whatever the *index* holds and silently
destroys the hand-written `ccec/src/Makefile`.  `README.md` has the full explanation.

### Build without L1 Tests (default)
```bash
./configure
make
```

`--enable-l1tests` is what is optional here; the AIDL/Binder prefixes are not.  A plain
`./configure` still needs them to resolve, because `libRCEC` carries both back-ends in every build.

### Run Tests Manually
```bash
cd tests/L1Tests
# Invocation A -- the legacy back-end, which an unset CEC_TEST_AIDL_MODE already selects.
# The negative filter is the one part that is NOT optional.
./run_L1Tests --gtest_filter='-DriverAidlSessionTest.*:DriverAidlTransmitTest.*'
```

`./run_L1Tests` is the libtool wrapper in this directory, **not** `.libs/run_L1Tests`.  Run it bare
and it exits non-zero: the two AIDL-only fixtures assert in `SetUp` that the AIDL back-end is the
resolved one and **fail rather than skip** when it is not, deliberately, because a skipped arm is
indistinguishable from a passing one in an aggregate count.  See
[Back-End Selection](#back-end-selection-five-invocations).

Root `make check` does **not** run this suite: the top-level `Makefile.am` declares
`SUBDIRS = osal ccec`, so `check` never descends into `tests/`.  Use
`make -C tests/L1Tests check` from the submodule root when you want automake to drive it.

### Run Specific Tests
```bash
./run_L1Tests --gtest_filter="CECFrameTest.*"
./run_L1Tests --gtest_filter="*Mutex*"
```

The second filter matches the 11 `MutexTest` cases in `osal/test_Mutex.cpp`.  Use
`--gtest_list_tests` to see the fixture names a filter can select.

### Measure Coverage
```bash
cd tests/L1Tests
./run_coverage.sh --build --run     # build, run every invocation, capture, report, gate
```

Artifacts (`coverage.info`, `filtered_coverage.info`, `coverage/index.html`
and the per-file tables) have **no fixed default path**.  With no
`--output-dir` the script mints a fresh directory per run with
`mktemp -d "${TMPDIR:-/tmp}/hdmicec-l1-coverage.XXXXXXXX"` at mode 0700 and
prints it as the `artifacts:` line at startup — read that line to find the
files.  An unpredictable name cannot be pre-created by another local
account, and it is outside the git tree because `.gitignore` does not cover
these names.  Pass `--output-dir DIR` (or set `COVERAGE_OUTPUT_DIR`) when
you need a stable location; `--output-dir .` reproduces the CI layout
in-tree, at which point they are build artifacts that must not be
committed.  The per-file gate is on by default, so a single file below 80%
fails the run.  See `./run_coverage.sh --help` for the full option and
environment reference.

On a host that can only run some of the invocations, expect a non-zero exit: the AIDL-path
production files are reachable only from the invocations being deferred, so they stay below the
per-file bar.  That is the gate working, not a threshold to lower — do not pass
`--no-per-file-gate` or exempt a file to make it green.

## Back-End Selection: Five Invocations

`Driver::getInstance()` chooses between the two HAL back-ends **once per process**, inside
`LibCCEC::init` — the first thing this harness does that forces the factory.  By the time any
`TEST_F` body runs the choice is made, so **each outcome needs its own process** and no filter can
make one run exercise both arms.  **Ordering is load-bearing:** on B and C the fake must be
registered, and on E the host must have signalled ready, **before** `init` — afterwards the
selection is already on legacy and a green run proves nothing.

`./run_coverage.sh --run` drives all five, one process each, each with its own
`run_invocation_<letter>.log` that a later invocation cannot overwrite:

| Inv | Binary | `CEC_TEST_AIDL_MODE` | Selection | Cases |
|--------|-------------|---------|---------|---------|
| A | `run_L1Tests` | `absent` | Legacy | 516, measured — the pre-existing 483 plus 33 contract cases |
| B | `run_L1Tests` | `compatible` | AIDL (local interface) | the AIDL contract arms plus the 308 back-end-neutral cases |
| C | `run_L1Tests` | `incompatible` | Legacy, rejection logged | 25 contract cases plus the same 308 |
| D | `run_L2Tests` | `absent` | Legacy | the `DualPath*` round trip through the in-process legacy mock |
| E | `run_L2Tests` | `remote` | AIDL (remote proxy) | the same round trip over real Binder IPC |

Only A carries a measured total; **483 is the pre-existing legacy-path baseline**, and the binary
now registers 547 cases in 25 fixtures.  No total is quoted for B, C or E because none has been
measured.  The counts are documentation, not the gate — `run_coverage.sh` asks the binary how many
cases each filter selects, every time.

### `CEC_TEST_AIDL_MODE`
The only switch.  Read by `tests/L1Tests/test_main.cpp` and `tests/L2Tests/test_main.cpp` and **by
no production source**:

- **`absent`** — register nothing; the legacy back-end is selected and libbinder is never touched.
  **Unset and empty both mean this.**
- **`compatible`** — register an in-process fake reporting its real, frozen metadata; AIDL is
  selected.
- **`incompatible`** — register an in-process fake whose interface hash is `"-1"`; the service is
  *present* and rejected, so legacy is selected and the rejection is logged.
- **`remote`** — launch the out-of-process fake host.  `run_L2Tests` only; in `run_L1Tests` it is a
  hard failure naming the other runner.

An unrecognised value is a hard failure, never a quiet fall back to `absent`.

### The L2 tier
`tests/L2Tests` builds `run_L2Tests` and `fake_hdmi_cec_aidl_host`, and they **must** be separate
processes: an in-process registration yields no proxy, no driver transaction and no
client-threadpool involvement, so it cannot prove the transport works.  `CEC_FAKE_AIDL_HOST_PATH`
tells the harness where the host binary is and is set by the build; `CEC_FAKE_HOST_READY_FD` is the
inherited pipe descriptor the host reports readiness on and is set by the harness — do not set it
yourself.  `README.md` has the detail.

### What this host can actually run
**Compiling and linking works anywhere** — that needs only headers and libraries.  *Executing* an
AIDL-path case needs three more things: a kernel with Binder support, a Binder protocol version
matching the one `libbinder` was built for, and a running `servicemanager`.  Measured on the host
this file was last revised on — **kernel 6.12.85+, `CONFIG_ANDROID_BINDER_IPC` not set, no
`/dev/binder*`** — so **A and D are the two invocations runnable here**, and **B, C and E cannot
run at all**: registering a name needs `defaultServiceManager()`, which opens the driver and
*aborts the process* when the node is missing.  `run_coverage.sh` reports those three as
**DEFERRED**, never attempts them and never counts them as passed.  **No result for B, C or E is
claimed anywhere in this file**; they belong to the Binder-capable CI job.

## Test Executable

- **Name**: `run_L1Tests`
- **Location**: `tests/L1Tests/`
- **Type**: Google Test executable
- **Sources**: 16 test translation units + test_main.cpp + the two mock sources (all listed in
  `run_L1Tests_SOURCES`, which is the only gate on what gets compiled — and the order in that
  list is significant, so append at the end of a group rather than inserting)
- **Total Tests**: 547 cases registered in 25 fixtures, none disabled — measured with
  `./run_L1Tests --gtest_list_tests`.  That is the pre-existing **483 in 18 fixtures**, still
  unmodified, plus the 64 contract cases in `ccec/test_DriverAidl.cpp`.  No single invocation runs
  all 547: see [Back-End Selection](#back-end-selection-five-invocations).  There are more fixtures
  than translation units because some files declare more than one — `ccec/test_LibCCEC.cpp`
  (`LibCCECTest` and `LibCCECUninitializedTest`) and `ccec/test_MessageDecoder.cpp`
  (`MessageDecoderTest` and `MessageDecoderTrackingTest`) among them

## Key Features

- ✅ **Conditional Build**: Only builds when `--enable-l1tests` is specified
- ✅ **Backward Compatible**: Existing tests (BasicTest, CECCmd, etc.) unchanged
- ✅ **Clean Separation**: L1 tests in dedicated subdirectory
- ✅ **Google Test Framework**: Industry-standard C++ testing
- ✅ **Automated Testing**: `TESTS = run_L1Tests` in this directory's `Makefile.am`, so
  `make -C tests/L1Tests check` drives the suite (root `make check` does not — see above)

## Configuration Options

| Option | Description | Default |
|--------|-------------|---------|
| `--enable-l1tests` | Enable L1 unit tests | `no` |
| `--disable-l1tests` | Disable L1 unit tests | N/A |
| `--with-halif-prefix=DIR` | Staged `rdk-halif-aidl` tree | sibling `../rdk-halif-aidl` |
| `--with-binder-sdk=DIR` | Staged Binder SDK root | none — must resolve |

`HALIF_PREFIX`, `HALIF_LIB_DIR`, `BINDER_SDK_DIR` and `BINDER_SDK_INCLUDE_DIR` are the equivalent
`configure` variables.  Neither `--with-` option is optional in effect: both back-ends are in every
build, so `configure` fails naming the roots it tried when a prefix does not resolve.

## Next Steps

1. Provide GoogleTest so that `pkg-config` can find a `gtest.pc` — either
   `sudo apt-get install libgtest-dev libgmock-dev`, or a source build into a private prefix
   exported as `GTEST_PREFIX` (see [Quick Start](#quick-start))
2. Stage `rdk-halif-aidl` and the Binder SDK, and know the two prefixes (see
   [The AIDL/Binder dependency](#the-aidlbinder-dependency))
3. Generate the stubs and the HAL-mock symlink, then `autoreconf -if` and
   `./configure --enable-l1tests` with `CPPFLAGS`/`PKG_CONFIG_PATH` and the two prefixes set
4. Build: `make all` **and then** `make -C tests/L1Tests all` **and then**
   `make -C tests/L2Tests all`
5. Test: `./run_L1Tests` from `tests/L1Tests`, or `make -C tests/L1Tests check`

Or let `./run_coverage.sh --build --run` do all five.

See **UNIT_TEST_SETUP.md** for complete documentation.
