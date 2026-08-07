# HDMI-CEC L1 Tests

This directory contains the L1 unit tests for the hdmicec library using Google Test (gtest/gmock).

## Framework

- **Test Framework**: Google Test (gtest) v1.10.0+
- **Mocking Framework**: Google Mock (gmock)
- **Language**: C++14 — set by `AM_CXXFLAGS = -Wall -std=c++14 -g` in `Makefile.am`
- **Build System**: Autotools
- **Coverage**: gcov/lcov via `run_coverage.sh`, gated at 80% line coverage — see [Coverage](#coverage)

## Prerequisites

Install Google Test development package:

```bash
# Ubuntu/Debian
sudo apt-get install libgtest-dev libgmock-dev

# Build from source if needed
cd /usr/src/gtest
sudo cmake .
sudo make
sudo cp lib/*.a /usr/lib
```

## Building Tests

```bash
# Configure with L1 tests enabled
./configure --enable-l1tests

# Build and run tests
make check

# Or build and run explicitly
cd tests/L1Tests
make
./run_L1Tests
```

## Test Structure

```
tests/L1Tests/
├── Makefile.am           # Autotools build configuration; run_L1Tests_SOURCES is the ONLY
│                         #   gate on what gets compiled, and its order is significant
├── run_coverage.sh       # gcov/lcov runner, per-file table and >=80% line-coverage gate
├── .lcovrc_l1            # lcov configuration with branch collection enabled
├── test_main.cpp         # Test runner entry point; installs the single global
│                         #   testing::Environment that creates the driver mock
├── ccec/                 # CCEC library tests (494 tests)
│   ├── test_CECFrame.cpp        # 31 tests - frame construction, accessors, boundary sizes
│   ├── test_Connection.cpp      # 60 tests - listener registration, address filtering
│   ├── test_Bus.cpp             # 37 tests - listener dispatch, filtering, send retries
│   ├── test_LibCCEC.cpp         # 14 tests - init/term, addresses, uninitialised-state guards
│   ├── test_MessageEncoder.cpp  # 49 tests - CEC message encoding, every message type
│   ├── test_MessageDecoder.cpp  # 54 tests - all CEC opcodes, edge cases, tracking
│   ├── test_OpCode.cpp          # 82 tests - all opcodes, GetOpName coverage
│   ├── test_Operands.cpp        # 69 tests - all operand types, methods
│   ├── test_Operand.cpp         # 11 tests - the Operand BASE class's default virtuals
│   ├── test_Exception.cpp       # 16 tests - every exception type's what() and dispatch
│   ├── test_Driver_Mock.cpp     # 10 tests - mock driver verification
│   ├── test_Driver.cpp          # 23 tests - open/close, write, address management
│   ├── test_DriverImpl_Async.cpp# 26 tests - async transmit, invalid state, error paths
│   └── test_Util.cpp            # 12 tests - log-level configuration, buffer dump
└── osal/                 # OSAL library tests (26 tests)
    ├── test_ConditionVariable.cpp  # 4 tests - wait/signal/timed wait, native handle
    ├── test_Mutex.cpp              # 11 tests - lock/unlock, copy semantics
    └── test_Thread.cpp             # 11 tests - named construction, dispatch, detach
```

**520 test cases in 18 fixtures**, all passing, none disabled.  The counts above are
measured with `./run_L1Tests --gtest_list_tests`, not estimated; re-measure with that
command rather than trusting this list after adding tests.

One further translation unit is compiled into `run_L1Tests` from outside this directory:
`../../mocks/hdmicec/hdmi_cec_driver_mock.cpp`, the GoogleMock HDMI-CEC HAL driver mock.  It
defines no test cases of its own, so it contributes none of the 520.

### Adding a test file

A new translation unit is compiled ONLY if it appears in `run_L1Tests_SOURCES`, so a file
added without amending `Makefile.am` changes nothing and produces no coverage movement.  That
list is the only compile gate: an unregistered test file is silently not compiled, with no
warning and no build error to tell you, which makes it indistinguishable from a test that was
never written.  **Append** the entry rather than inserting it: GoogleTest registers each
`TEST_F` during static initialisation in link order, so inserting a file in the middle of that
list reorders every suite after it, and this suite contains pre-existing order-fragile tests
that pass only in the established order.  See the comment in `Makefile.am`.

## Running Tests

```bash
# Run all tests
make check

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
  disable prefix, all 520 cases are enabled and run, and
  `./run_L1Tests --gtest_also_run_disabled_tests --gtest_list_tests` lists the same 520 as a
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

### CCEC Library Tests (494 tests)

- **test_CECFrame.cpp** (31 tests): Frame construction, copy operations, serialization, hex dump, boundary sizes
- **test_Connection.cpp** (60 tests): Connection lifecycle, open/close, listener registration, address filtering, broadcast handling
- **test_Bus.cpp** (37 tests): Listener dispatch and filtering, send retries, timeout behaviour
- **test_LibCCEC.cpp** (14 tests): Singleton pattern, initialization/termination, logical/physical addresses, and the guards that throw when the library is not initialised
- **test_MessageEncoder.cpp** (49 tests): Encoding every message type in Messages.hpp, including the two-armed serializers (SystemAudioModeRequest, RequestCurrentLatency, ReportCurrentLatency) and the variable-length audio-descriptor and feature-report messages
- **test_MessageDecoder.cpp** (54 tests): Decoding all 60+ CEC opcodes, polling messages, edge cases, opcode tracking; split across the `MessageDecoderTest` and `MessageDecoderTrackingTest` fixtures
- **test_OpCode.cpp** (82 tests): Complete GetOpName() coverage for all CEC opcodes, OpCode class methods
- **test_Operands.cpp** (69 tests): All operand classes (PhysicalAddress, LogicalAddress, DeviceType, Version, PowerStatus, AbortReason, OSDString, OSDName, Language, VendorID, UICommand, SystemAudioStatus, AudioStatus, RequestAudioFormat, ShortAudioDescriptor, AllDeviceTypes, RcProfile, DeviceFeatures, LatencyInfo)
- **test_Operand.cpp** (11 tests): The Operand BASE class - the default `toString()`, `name()` and `validate()` bodies inherited by any operand that declines to override them, and the non-virtual `serialize(void)` convenience overload
- **test_Exception.cpp** (16 tests): Every exception type's `what()` message, dispatch through base and `std::exception` references, and that sibling types do not catch each other
- **test_Driver.cpp** (23 tests) / **test_Driver_Mock.cpp** (10 tests): Open/close and reopen, synchronous write including NACK and transmit-failure returns, address management, frame-detail printing, asynchronous write with both its success and failure returns, and direct mock verification
- **test_DriverImpl_Async.cpp** (26 tests): Asynchronous transmit, the transmit-completion callback, invalid-state guards, error-injection paths
- **test_Util.cpp** (12 tests): Log-level configuration parsing and the debug buffer dump

### OSAL Library Tests (26 tests)

- **test_ConditionVariable.cpp** (4 tests): Notify/wait synchronization patterns, timed wait, native-handle accessor
- **test_Mutex.cpp** (11 tests): Lock/unlock, tryLock, copy construction and copy assignment
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

# Raise the bar for a diagnostic run, or gate per file as well as on the aggregate
./run_coverage.sh --threshold 90
./run_coverage.sh --per-file-gate

# Full option and environment reference
./run_coverage.sh --help
```

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

**This suite has no disabled tests.**  `./run_L1Tests` reports 520 tests run, 520 passed,
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
  `LibCCECTest.MultipleInitTermCycles` calls `term()`, re-`init()`s and then exercises the address
  APIs end to end, and the four `*ThrowsWhenNotInitialized` guard cases — for `term()`,
  `addLogicalAddress()`, `getLogicalAddress()` and `getPhysicalAddress()` — all assert against a
  deliberately terminated library.

The underlying race is real; what changed is that it is now **managed rather than avoided**.
`ccec/test_LibCCEC.cpp` defines a documented `settleBusReaderAfterInit()` helper — a single 50 ms
pause after each `init()`, needed because the reader's state lives in a private member with no
observable condition a test could wait on.  Its in-source measurement is recorded alongside it:
3 hangs in 20 consecutive isolated runs of that fixture without the helper, 0 in 20 with it.  That
is the pattern to reuse if you add another test that terminates and re-initialises the library.

### Intermittent SIGSEGV in the Bus reader thread — a PRODUCTION defect, reported not worked around

`./run_L1Tests` exits 139 (SIGSEGV) in roughly **5-7% of full-suite runs** on a loaded host.
It is not a test defect and it is not order-dependent: the fault is a null dereference in
production source, and the tests merely reach it.  Measured at this suite's HEAD with no test
changes applied: 2 crashes in 60 consecutive runs, and 3 in 40 on a busier host.

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

The suite is green in its default order — 520 of 520, exit `0`.  Under randomised order it is
not:

```bash
./run_L1Tests --gtest_shuffle --gtest_random_seed=12345   # 511 passed, 9 failed, exit 1
```

The nine failures are deterministic for that seed and span two fixtures:

- `ConnectionTest` (`ccec/test_Connection.cpp`) — `DefaultFilterBroadcast`,
  `DefaultFilterSpecificAddress`, `DefaultFilterUnregistered`, `MatchSourceMismatchedAddress`,
  `MultipleListenersNotification` and `SendAsyncMatchSource`
- `DriverTest` (`ccec/test_Driver.cpp`) — `AddLogicalAddressSuccess`,
  `IsValidLogicalAddressTrue` and `WriteWithHdmiCecTxFailure`

That set is specific to this seed *and* to this test inventory: re-derive it after adding tests
rather than trusting a list quoted here, because shuffling reorders the whole program and a
different seed exposes a different subset of the same underlying fragility.  The seed above is
simply a known reproducer.  Corroborating evidence sits in the driver file:
`DriverTest.AAA_DriverSingletonAccess` is commented "runs first alphabetically", and
`DriverTest.ZZZ_OpenWithFailure` and `DriverTest.ZZZ_CloseWithFailure` are commented "runs late
to avoid breaking other tests" — deliberate alphabetical-ordering prefixes, which only make
sense as a workaround for this fragility.

These cases pass in the default order and are deliberately left as they are.  **Randomised-order
runs are diagnostic here, not an acceptance gate.**  Two consequences for anyone adding tests:

- **Append** new translation units to `run_L1Tests_SOURCES`; do not insert them.  GoogleTest
  registers each `TEST_F` during static initialisation, in link order, and runs the suites in
  registration order, so inserting a file reorders every suite after it.
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
  [No Disabled Tests](#no-disabled-tests) — provided the Bus reader is allowed to settle after
  each `init()`; that is what `settleBusReaderAfterInit()` in `ccec/test_LibCCEC.cpp` is for, and
  it is the only deliberate wall-clock pause in the suite

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
