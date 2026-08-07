# HDMI-CEC L1 Tests

This directory contains the L1 unit tests for the hdmicec library using Google Test (gtest/gmock).

## Framework

- **Test Framework**: Google Test (gtest) v1.10.0+
- **Mocking Framework**: Google Mock (gmock)
- **Language**: C++14 — set by `AM_CXXFLAGS = -Wall -std=c++14 -g` in `Makefile.am`
- **Build System**: Autotools
- **Coverage**: gcov/lcov via `run_coverage.sh`, gated at 80% line coverage — see [Coverage](#coverage)

## Prerequisites

Install the Google Test development packages.  Elevated privilege is needed for the package
manager and for nothing else:

```bash
# Ubuntu/Debian
sudo apt-get install libgtest-dev libgmock-dev
```

If the distribution packages ship headers without the static libraries, build GoogleTest from
source **unprivileged** and install it into a prefix you own.  Compiling as root is unnecessary,
and writing archives straight into `/usr/lib` puts files the package manager does not know about
where they can shadow a later distribution update:

```bash
cmake -S /usr/src/googletest -B "$HOME/gtest-build" \
      -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build "$HOME/gtest-build"
cmake --install "$HOME/gtest-build"        # writes only under $HOME/.local
```

Point `configure` at that prefix so the build finds it without any system-wide change:

```bash
CPPFLAGS="-I$HOME/.local/include" LDFLAGS="-L$HOME/.local/lib" ./configure --enable-l1tests
```

and put `$HOME/.local/lib` on `LD_LIBRARY_PATH` when running `run_L1Tests` if the shared
libraries were built there.

## Building Tests

**The short answer:** `./run_coverage.sh --build --run` from this directory does the whole
sequence below — build, run, capture, report and gate — and is the recipe this suite is
maintained against.

The long answer, for building by hand.  Four prerequisites are easy to miss, and every one of
them is fatal rather than degrading:

1. **`stubs/` must exist.**  The middleware includes IARM bus headers it does not ship.
2. **The HAL driver mock must be symlinked over the driver header.**  That symlink is the entire
   mocking seam; without it the build looks for a real HDMI-CEC driver.
3. **`CPPFLAGS` must carry `mocks/` and `stubs/`, and `PKG_CONFIG_PATH` must reach a `gtest.pc`.**
4. **`make` at the top level does not build this suite.**  `tests/L1Tests` needs its own
   `make` pass — see [Running Tests](#running-tests) for why root `make check` does not help
   either.

```bash
# From the hdmicec submodule root
export GTEST_PREFIX="$HOME/.local/gtest-1.15.0"       # or wherever gtest.pc lives

# 1+2. Stub headers, and the HAL driver mock injected over the driver header
mkdir -p stubs/rdk/iarmbus stubs/ccec/drivers/iarmbus
touch stubs/rdk/iarmbus/libIARM.h \
      stubs/rdk/iarmbus/libIBus.h \
      stubs/rdk/iarmbus/libIBusDaemon.h \
      stubs/ccec/drivers/iarmbus/CecIARMBusMgr.h
ln -sf ../../../mocks/hdmicec/hdmi_cec_driver.h stubs/ccec/drivers/hdmi_cec_driver.h

# 3. Generate and configure.  Drop the two coverage flags for a plain build.
autoreconf -if
PKG_CONFIG_PATH="$GTEST_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}" \
CPPFLAGS="-I$PWD/mocks -I$PWD/stubs -I$GTEST_PREFIX/include" \
LDFLAGS="-L$GTEST_PREFIX/lib -fprofile-arcs -ftest-coverage" \
CXXFLAGS="-fprofile-arcs -ftest-coverage" \
  ./configure --enable-l1tests

# 4. Build the libraries, then the suite
make -j"$(nproc)" all
make -C tests/L1Tests all
```

`autoreconf`/`configure` rewrite six git-tracked `Makefile` files in this submodule.  They are
local toolchain output and must never be committed; `./run_coverage.sh --restore` puts them back.

## Test Structure

```
tests/L1Tests/
├── Makefile.am           # Autotools build configuration; run_L1Tests_SOURCES is the ONLY
│                         #   gate on what gets compiled, and its order is significant
├── run_coverage.sh       # gcov/lcov runner, per-file table and >=80% line-coverage gate
├── .lcovrc_l1            # lcov configuration with branch collection enabled
├── test_main.cpp         # Test runner entry point; installs the single global
│                         #   testing::Environment that creates the driver mock
├── ccec/                 # CCEC library tests (426 tests)
│   ├── test_CECFrame.cpp        # 31 tests - frame construction, accessors, boundary sizes
│   ├── test_Connection.cpp      # 60 tests - listener registration, address filtering
│   ├── test_Bus.cpp             # 37 tests - listener dispatch, filtering, send retries
│   ├── test_LibCCEC.cpp         # 13 tests - init/term, addresses, uninitialised-state guards
│   ├── test_MessageEncoder.cpp  # 10 tests - CEC message encoding
│   ├── test_MessageDecoder.cpp  # 54 tests - all CEC opcodes, edge cases, tracking
│   ├── test_OpCode.cpp          # 82 tests - all opcodes, GetOpName coverage
│   ├── test_Operands.cpp        # 69 tests - all operand types, methods
│   ├── test_Driver_Mock.cpp     # 10 tests - mock driver verification
│   ├── test_Driver.cpp          # 22 tests - open/close, write, address management
│   ├── test_DriverImpl_Async.cpp# 26 tests - async transmit, invalid state, error paths
│   └── test_Util.cpp            # 12 tests - log-level configuration, buffer dump
└── osal/                 # OSAL library tests (26 tests)
    ├── test_ConditionVariable.cpp  # 4 tests - wait/signal/timed wait, native handle
    ├── test_Mutex.cpp              # 11 tests - lock/unlock, copy semantics
    └── test_Thread.cpp             # 11 tests - named construction, dispatch, detach
```

**452 test cases in 17 fixtures**, all passing, none disabled.  The counts above are
measured with `./run_L1Tests --gtest_list_tests`, not estimated; re-measure with that
command rather than trusting this list after adding tests.  Two fixtures come out of
`test_LibCCEC.cpp` (`LibCCECTest`, 8 cases, and `LibCCECUninitializedTest`, 5) and two out of
`test_MessageDecoder.cpp` (`MessageDecoderTest`, 44, and `MessageDecoderTrackingTest`, 10),
which is why 17 fixtures come from 15 translation units.

One further translation unit is compiled into `run_L1Tests` from outside this directory:
`../../mocks/hdmicec/hdmi_cec_driver_mock.cpp`, the GoogleMock HDMI-CEC HAL driver mock.  It
defines no test cases of its own, so it contributes none of the 452.

### Adding a test file

A new translation unit is compiled ONLY if it appears in `run_L1Tests_SOURCES`, so a file
added without amending `Makefile.am` changes nothing and produces no coverage movement.  That
list is the only compile gate: an unregistered test file is silently not compiled, with no
warning and no build error to tell you, which makes it indistinguishable from a test that was
never written.  Place the entry so that it does not reorder the order-fragile suites: GoogleTest
registers each `TEST_F` during static initialisation in link order and runs the suites in
registration order, so inserting a file *ahead* of an existing suite reorders that suite, and
this suite contains pre-existing order-fragile tests that pass only in the established order.
The four translation units added by this pass are grouped with their siblings but placed *after*
every pre-existing entry of their layer — the two `ccec/` files after `ccec/test_Driver.cpp` and
the two `osal/` files after `osal/test_ConditionVariable.cpp` — which keeps the list readable
while leaving the established registration order of every pre-existing suite intact.  Verify any
new placement with a full run before relying on it.

## Running Tests

Run from **this directory**, and run the `run_L1Tests` that sits here — it is the libtool wrapper
script, and it sets the library search path the real binary under `.libs/` needs.  Invoking
`.libs/run_L1Tests` directly is the usual cause of a loader error that looks like a build failure.

Root `make check` does **not** run this suite.  The top-level `Makefile.am` declares
`SUBDIRS = osal ccec`, so a `check` at the top level never descends into `tests/`.  The two
invocations that do work are `make -C tests/L1Tests check` from the submodule root (this
directory's `Makefile.am` declares `TESTS = run_L1Tests`) and running the wrapper directly:

```bash
# Run all tests -- from tests/L1Tests
./run_L1Tests

# ...or through automake's check target, from the hdmicec submodule root
make -C tests/L1Tests check

# Run specific test with filter
./run_L1Tests --gtest_filter="CECFrameTest.*"

# Run with per-test output and timings (there is no --gtest_verbose flag; see note below)
./run_L1Tests --gtest_print_time=1 --gtest_brief=0

# Generate XML report
./run_L1Tests --gtest_output=xml:test_results.xml

# List all tests
./run_L1Tests --gtest_list_tests

# Diagnostic only: expose inter-test order dependencies (see Known Issues)
./run_L1Tests --gtest_shuffle --gtest_random_seed=12345

# Coverage capture, report and the 80% line gate
./run_coverage.sh --run
```

> `--gtest_verbose` is **not** a GoogleTest flag.  Passing it makes `run_L1Tests` print its help
> text, run **zero** tests and still exit `0` — a silently green no-op.  The two real flags are
> `--gtest_print_time=0|1` and `--gtest_brief=0|1`; `./run_L1Tests --help` is the authority.

## Writing New Tests

1. Create test file in appropriate directory (ccec/ or osal/)
2. Add to `run_L1Tests_SOURCES` in Makefile.am — **mandatory, and the step that is easy to
   miss.**  See [Adding a test file](#adding-a-test-file) for why, and for why the entry must be
   appended rather than inserted.
3. Use Google Test macros:

```cpp
#include <gtest/gtest.h>
#include "your_header.hpp"

class YourTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup before each test
    }
    
    void TearDown() override {
        // Cleanup after each test
    }
};

TEST_F(YourTest, TestName) {
    EXPECT_EQ(actual, expected);
    ASSERT_TRUE(condition);
}
```

Then, before you call it done:

- **Confirm the file was actually picked up.**  `./run_L1Tests --gtest_list_tests` must show the
  new cases, and the total must rise by exactly the number you added.  That check is what catches
  a missed step 2.
- **Make the test self-sufficient.**  Establish its own preconditions and leave shared state — the
  LibCCEC singleton, the driver mock's expectations, any file it writes — exactly as it found it.
  GoogleTest isolates fixture state only, never process-global or filesystem state, and test
  execution order is undefined.  Verify the new case both in isolation
  (`--gtest_filter=YourTest.TestName`) and in the full suite.

## Test Categories

- **Unit Tests**: Test individual classes/functions in isolation
- **Driver-dependent tests**: exercised through the GoogleMock HDMI-CEC HAL driver mock in
  `../../mocks/hdmicec/`, which is registered in `run_L1Tests_SOURCES` and injected by
  symlinking `mocks/hdmicec/hdmi_cec_driver.h` over `stubs/ccec/drivers/hdmi_cec_driver.h`.
  No test requires real CEC hardware, and none is skipped for the want of it.
- **Disabled tests**: there are none.  No test name in `ccec/` or `osal/` carries GoogleTest's
  disable prefix, all 452 cases are enabled and run, and
  `./run_L1Tests --gtest_also_run_disabled_tests --gtest_list_tests` lists the same 452 as a
  plain listing.  See [Known Issues](#known-issues-and-notes).

## Common Assertions

```cpp
EXPECT_EQ(val1, val2)     // val1 == val2
EXPECT_NE(val1, val2)     // val1 != val2
EXPECT_LT(val1, val2)     // val1 < val2
EXPECT_GT(val1, val2)     // val1 > val2
EXPECT_TRUE(condition)    // condition is true
EXPECT_FALSE(condition)   // condition is false
EXPECT_NO_THROW({code})   // code doesn't throw
EXPECT_THROW({code}, ex)  // code throws exception ex
```

## Test Coverage Details

### CCEC Library Tests (426 tests)

- **test_CECFrame.cpp** (31 tests): Frame construction, copy operations, serialization, hex dump, boundary sizes
- **test_Connection.cpp** (60 tests): Connection lifecycle, open/close, listener registration, address filtering, broadcast handling
- **test_Bus.cpp** (37 tests): Listener dispatch and filtering, send retries, timeout behaviour
- **test_LibCCEC.cpp** (13 tests): Singleton pattern, initialization/termination, logical/physical addresses, and the guards that throw when the library is not initialised. Split across two fixtures: `LibCCECTest` (8 cases) runs against the initialised library and never terminates it, and `LibCCECUninitializedTest` (5 cases) owns the uninitialised state at SUITE level - one `term()` in the first `SetUp()`, one `init()` in `TearDownTestSuite()` - so that no case pairs an `init()` with a following `term()`
- **test_MessageEncoder.cpp** (10 tests): Encoding CEC messages through MessageEncoder
- **test_MessageDecoder.cpp** (54 tests): Decoding all 60+ CEC opcodes, polling messages, edge cases, opcode tracking; split across the `MessageDecoderTest` and `MessageDecoderTrackingTest` fixtures
- **test_OpCode.cpp** (82 tests): Complete GetOpName() coverage for all CEC opcodes, OpCode class methods
- **test_Operands.cpp** (69 tests): All operand classes (PhysicalAddress, LogicalAddress, DeviceType, Version, PowerStatus, AbortReason, OSDString, OSDName, Language, VendorID, UICommand, SystemAudioStatus, AudioStatus, RequestAudioFormat, ShortAudioDescriptor, AllDeviceTypes, RcProfile, DeviceFeatures, LatencyInfo)
- **test_Driver.cpp** (22 tests) / **test_Driver_Mock.cpp** (10 tests): Open/close and reopen, synchronous write including NACK and transmit-failure returns, address management, frame-detail printing, asynchronous write with both its success and failure returns, and direct mock verification
- **test_DriverImpl_Async.cpp** (26 tests): Asynchronous transmit, the transmit-completion callback, invalid-state guards, error-injection paths
- **test_Util.cpp** (12 tests): Log-level configuration parsing and the debug buffer dump. Each case body runs in a **forked child process**: production `cec_log_level` is a non-atomic file-static that `check_cec_log_status()` writes and `CCEC_LOG()`/`dump_buffer()` read, and the Bus reader and writer threads live for the whole program, so writing it on the test thread would race their reads. The parent only reads the level, and asserts it is unchanged on the way out. The child calls `__gcov_reset()`/`__gcov_dump()` so the production lines it drove are still counted, which is why `run_L1Tests_LDADD` names `-lgcov`. The file header carries the full rationale and records the blocked production change — make the level atomic, or expose a seam for setting and reading it — that would remove the need for a child process

### OSAL Library Tests (26 tests)

- **test_ConditionVariable.cpp** (4 tests): Notify/wait synchronization patterns, timed wait, native-handle accessor
- **test_Mutex.cpp** (11 tests): Lock/unlock including the recursive case, native-handle retrieval and its usability checked with `pthread_mutex_trylock` through that handle, copy construction and copy assignment.  `Mutex` exposes no try-lock operation of its own — its API is `lock()`, `unlock()` and `getNativeHandle()` — so none is tested
- **test_Thread.cpp** (11 tests): Named construction, dispatch through Runnable, detach, destruction

## Coverage

Two files in this directory carry the coverage setup:

- **`run_coverage.sh`** — captures gcov data, writes an HTML report, prints a per-file table of
  line, function and branch figures read out of the lcov trace, enumerates every file below the
  bar together with its uncovered line numbers, and **exits non-zero when line coverage misses
  the threshold**.  It reproduces the capture, filter and report recipe of
  `.github/workflows/L1-tests.yml` — including that workflow's exclusion globs verbatim, which is
  what keeps the coverage denominator production-source-only — and adds the two things the
  workflow lacks: branch data and a numeric gate.
- **`.lcovrc_l1`** — the LCOV configuration for this suite, with `lcov_branch_coverage = 1` and
  `lcov_function_coverage = 1`.  Stated plainly: **CI does not read this file.**  The workflow
  passes neither `--config-file` nor `--rc`, so the CI coverage step inherits the system default
  and middleware branch visibility is developer-opt-in.  `run_coverage.sh` is the in-tree consumer;
  it passes this file to `lcov` and `genhtml` and additionally forces `--rc branch_coverage=1`.

```bash
# Build (instrumented), run the suite, capture, report and gate
./run_coverage.sh --build --run

# Measure whatever profile data already exists
./run_coverage.sh

# Raise the bar for a diagnostic run
./run_coverage.sh --threshold 90

# Gate on the aggregate ONLY -- a diagnostic opt-out, because per-file gating is the default
./run_coverage.sh --no-per-file-gate

# Full option and environment reference
./run_coverage.sh --help
```

**Per-file gating is on by default** (`COVERAGE_PER_FILE_GATE` defaults to `1`), so any single file
below the bar fails the run even when the aggregate is healthy — which is what the requirement's
per-target wording asks for.  `--per-file-gate` therefore only restates the default and exists for
symmetry; `--no-per-file-gate` (or `COVERAGE_PER_FILE_GATE=0`) is the opt-out, for a deliberate
diagnostic run such as watching the aggregate while a new production file's tests are still being
written.  Files below the bar are enumerated either way, so turning the gate off never hides a
figure — it only stops it failing the run.

Notes worth knowing before you reach for `lcov` directly:

- **The gate must be spelled with an operation.**  `lcov --summary <trace> --fail-under-lines 80`
  works — exit 0 at or above the bar, exit 1 below it.  The intuitive
  `lcov --fail-under-lines 80 <trace>` is rejected outright by lcov 2.x and exits 2, which looks
  like a working gate while never once consulting the coverage figure.
- **Branch coverage is evidence, not a gate.**  gcov models branches as control-flow-graph arcs,
  including compiler-generated exception and static-destruction arcs no test can reach, so a
  branch percentage is reported to show movement and never used as a pass/fail criterion.  Line
  coverage is the gate.
- **Coverage counters accumulate.**  A stale `*.gcda` keeps a line marked hit long after the test
  that hit it stopped running.  `--run` zeroes the counters before the suite for exactly that
  reason; without it the script reports how many counter files it found and when they were
  written.
- **The build is a prerequisite.**  `--enable-l1tests` plus `-fprofile-arcs -ftest-coverage`, and
  `libglib2.0-dev` is a hard requirement of `configure`.  `autoreconf`/`configure` also rewrite six
  git-tracked `Makefile` files in this submodule; those are local toolchain output and must never
  be committed.  `./run_coverage.sh --restore` restores them.

### Coverage output is a build artifact — never commit it

`run_coverage.sh` writes `coverage.info`, `filtered_coverage.info`, `coverage/`,
`per_file_coverage.tsv`, `uncovered_lines.txt`, `rdkL1TestResults.json` and its per-step logs.
`.gitignore` in this directory covers object files, the `run_L1Tests` binary and `*.log`/`*.trs`/
`*.xml`, but **not** those coverage names — so by default the script writes them to
`${TMPDIR:-/tmp}/hdmicec-l1-coverage/<workspace-root-basename>/`, outside the git tree entirely,
where they cannot be staged even by `git add -A`.  If you override that with `--output-dir .` to
reproduce the CI layout, keeping them out of a commit becomes yours to manage.

## Known Issues and Notes

### No Disabled Tests

**This suite has no disabled tests.**  `./run_L1Tests` reports 452 tests run, 452 passed,
0 disabled.  Verify with:

```bash
./run_L1Tests --gtest_list_tests | grep DISABLED_    # expect no output
```

Earlier revisions of this document listed three disabled `LibCCECTest` cases — a `Term`-throws
case, a `Term`-succeeds case and a multiple-init/term-cycles case — and attributed their
disablement to thread-safety concerns.  Both halves of that entry are corrected here rather than
quietly dropped:

- **The list was stale, and it had been stale for a while.**  Those three cases were real: they
  were added to `ccec/test_LibCCEC.cpp` on 2026-02-19 (commit `e48fa73`) and removed again on
  2026-03-17 (commit `0f00d4e`).  This README's last content change predates that removal, so it
  went on describing tests the file no longer contained.  From then until this project's tests
  landed, the only cases in the suite actually carrying GoogleTest's disable prefix were two
  asynchronous-write cases in `ccec/test_Driver.cpp` — and both are now enabled and passing, as
  `DriverTest.WriteAsync` and `DriverTest.WriteAsyncWithFailure`.
- **The stated reason no longer justifies disabling anything.**  The rationale given was that
  LibCCEC's Bus reader/writer threads race when repeatedly started and stopped, so the suite
  avoided init/term cycling altogether.  That cycling is now covered and green:
  the four `LibCCECUninitializedTest.*ThrowsWhenNotInitialized` guard cases — for `term()`,
  `addLogicalAddress()`, `getLogicalAddress()` and `getPhysicalAddress()` — plus
  `InitWithNullNameClearsLogPrefix` all assert against a deliberately terminated library, and all
  five are green.

The underlying race is real; what changed is that it is now **structurally avoided rather than
timed around**.  There is no pause anywhere in this suite's new tests.  Two things replace it, and
both are the pattern to reuse if you add another case that terminates the library:

- **The uninitialised state is owned at suite level.**  `LibCCECUninitializedTest` performs ONE
  `term()` in the first `SetUp()` and ONE `init()` in `TearDownTestSuite()`; every case in between
  simply asserts, because "terminated" is already the state.  No case pairs an `init()` with a
  following `term()`, which is the only ordering in which `Bus::Reader`'s RUNNING/STOPPING
  transition can be lost, so the window is never opened rather than being waited out.
- **The close() sentinel is drained deterministically.**  `DriverImpl::close()` offers a NULL
  sentinel to the receive queue and `DriverImpl::read()` is what consumes it, but `Bus::stop()`
  only waits for the reader to leave its loop - it can leave with the sentinel still queued, and a
  second `close()` then queues another one, which is what the flush-arm dereference below needs.
  Every case that terminates the shared library therefore calls `Driver::read()` itself
  immediately afterwards (`drainClosedDriverQueue()` in `ccec/test_LibCCEC.cpp` and
  `ccec/test_Driver.cpp`, and an inline `EXPECT_THROW` in `ccec/test_DriverImpl_Async.cpp`).  It is
  safe from the test thread because `term()` has already waited for the reader to be gone, so there
  is no second consumer, and it is a real assertion in its own right: it reaches `read()`'s
  invalid-state arm.

### Intermittent SIGSEGV in the Bus reader thread — a PRODUCTION defect, reported not worked around

`./run_L1Tests` USED to exit 139 (SIGSEGV) intermittently — measured on this tree at 1 crash in
60 consecutive full-suite runs, and 2 in 60 when only the LibCCEC and Driver fixtures were run.
The fault is a null dereference in PRODUCTION source and the tests merely reached it, so the
production defect below is reported rather than fixed; what the tests now do is stop creating the
queue state it needs (see the sentinel drain in [No Disabled Tests](#no-disabled-tests)).  With
that in place the measurement is **0 crashes in 120 consecutive full-suite runs** and 0 in 40 for
each fixture in isolation.  The defect is still there and a future case that cycles the shared
library without draining will find it again.

Core dump, symbolised (`gdb ./.libs/run_L1Tests core.<pid>`):

```
#0  DriverImpl::read (...) at DriverImpl.cpp:182     ->  frame = *inFrame;
#1  Bus::Reader::run ()   at Bus.cpp:158
#2  CCEC_OSAL::Thread::CEntry at Thread.cpp:41
```

`DriverImpl::read()`'s flush-and-throw arm (`ccec/src/DriverImpl.cpp:180-184`) runs when the
driver is closed while a frame is still queued:

```cpp
while (rQueue.size() > 0) {
    inFrame = rQueue.poll();
    frame = *inFrame;          // line 182: inFrame may be NULL
    delete inFrame;
}
```

`EventQueue::poll()` is DOCUMENTED to return a default-constructed value -- for
`CECFrame *`, a null pointer -- when it is woken with the condition unset, expressly so a
waiting thread can close the queue out (`osal/include/osal/EventQueue.hpp:101-115`).  The
happy path at `DriverImpl.cpp:172` null-checks the same call; the flush arm does not, and
trusts `size()` instead.  Between the `size()` check and the `poll()` the condition can be
reset, so `poll()` legitimately hands back null and line 182 dereferences it.

**Required production change (NOT made here: production source is out of scope for this
pass, so this is reported rather than fixed):** null-check the flush arm exactly as the
happy path does, for example

```cpp
while (rQueue.size() > 0) {
    inFrame = rQueue.poll();
    if (inFrame == 0) { break; }   // woken with the condition unset
    frame = *inFrame;
    delete inFrame;
}
```

Until that lands, treat a lone exit-139 with no `[  FAILED  ]` line as this defect and re-run;
`run_coverage.sh` fails closed on it and reports no coverage, which is the correct behaviour.
Note that this arm was UNCOVERED before this project's tests reached it, which is why the
latent dereference had never been observed.

### Test Order Dependence (diagnostic)

The suite is green in its default order — 452 of 452, exit `0`.  Under randomised order it is
not:

```bash
./run_L1Tests --gtest_shuffle --gtest_random_seed=12345
```

Measured with that seed on this tree: 163 cases reported OK, `BusTest.ListenerFiltering` and
`BusTest.UnregisteredConnectionReceivesAll` reported `[  FAILED  ]`, and the run then ended in the
production SIGSEGV described above — shuffling moves a `close()` next to a fresh `init()`, which is
exactly the queue state that dereference needs.  Both halves of that outcome are pre-existing
conditions of the suite, not consequences of the tests added here.

The failing set is specific to the seed *and* to the test inventory: re-derive it after adding
tests rather than trusting a list quoted here, because shuffling reorders the whole program and a
different seed exposes a different subset of the same underlying fragility.  The seed above is
simply a known reproducer.  Corroborating evidence sits in the driver file:
`DriverTest.AAA_DriverSingletonAccess` is commented "runs first alphabetically", and
`DriverTest.ZZZ_OpenWithFailure` and `DriverTest.ZZZ_CloseWithFailure` are commented "runs late
to avoid breaking other tests" — deliberate alphabetical-ordering prefixes, which only make
sense as a workaround for this fragility.

These cases pass in the default order and are deliberately left as they are.  **Randomised-order
runs are diagnostic here, not an acceptance gate.**  Two consequences for anyone adding tests:

- Place new translation units in `run_L1Tests_SOURCES` *after* every pre-existing entry of their
  layer, so no established suite is reordered.  GoogleTest registers each `TEST_F` during static
  initialisation, in link order, and runs the suites in registration order, so a file inserted
  ahead of an existing suite reorders that suite.
- A new test must establish its own preconditions and leave shared state untouched, and must be
  verified both in isolation (`--gtest_filter=YourFixture.*`) and in the full suite.

### Dead Skip Guards

`ccec/test_Driver.cpp` contains 14 `GTEST_SKIP()` guards — 13 spelled
`"Mock is nullptr - test environment not initialized"` and one
`"Driver not in valid state for this test"`.  Measurement shows **none of them ever fires**: both
the default run and the shuffled run above report zero skipped tests.  They are dead defensive code
rather than silent skips, so the suite's pass count is its real pass count.  New tests deliberately
do not copy that idiom — if a precondition genuinely cannot be met, the test is not written and
the gap is reported instead.

### LibCCEC Singleton Behavior

LibCCEC is a singleton shared across all test suites. The test fixture SetUp() catches `InvalidStateException` to handle cases where LibCCEC has already been initialized by other test suites (e.g., DriverTest).

### Thread Timing

- Thread-related tests may need timing adjustments on slow systems
- Bus thread cleanup can take time.  Init/term cycling is nonetheless covered — see
  [No Disabled Tests](#no-disabled-tests) — and it is covered **without any wall-clock pause**:
  the uninitialised state is owned at suite level so no `init()` is ever followed by a `term()`,
  and the `close()` sentinel is drained through a real `Driver::read()` assertion.  There is no
  `sleep`, `usleep` or `sleep_for` anywhere in the tests this project added

### Hardware Dependencies

- **None.** No test in this suite requires real CEC hardware or a real driver.  A complete
  GoogleMock HDMI-CEC HAL driver mock already exists at `../../mocks/hdmicec/`
  (`hdmi_cec_driver_mock.h` / `.cpp`), is registered in `run_L1Tests_SOURCES`, and is injected by
  symlinking `mocks/hdmicec/hdmi_cec_driver.h` over `stubs/ccec/drivers/hdmi_cec_driver.h` during
  the build.  In the default order all 60 `ConnectionTest` and all 23 `DriverTest` cases pass
  against the mock with no hardware present
- Failure and error paths are provoked by making the mock return non-success codes, and the
  asynchronous completion path by invoking the callback the driver registered during open —
  never by waiting on real hardware
