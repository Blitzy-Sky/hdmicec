# HDMI-CEC L1 Tests

This directory contains the L1 unit tests for the hdmicec library using Google Test (gtest/gmock).

## Framework

- **Test Framework**: Google Test (gtest) v1.10.0+
- **Mocking Framework**: Google Mock (gmock)
- **Language**: C++11
- **Build System**: Autotools

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

### Adding a test file

A new translation unit is compiled ONLY if it appears in `run_L1Tests_SOURCES`, so a file
added without amending `Makefile.am` changes nothing and produces no coverage movement.
**Append** the entry rather than inserting it: GoogleTest registers each `TEST_F` during
static initialisation in link order, so inserting a file in the middle of that list
reorders every suite after it, and this suite contains pre-existing order-fragile tests
that pass only in the established order.  See the comment in `Makefile.am`.

## Running Tests

```bash
# Run all tests
make check

# Run specific test with filter
./run_L1Tests --gtest_filter="CECFrameTest.*"

# Run with verbose output
./run_L1Tests --gtest_verbose

# Generate XML report
./run_L1Tests --gtest_output=xml:test_results.xml

# List all tests
./run_L1Tests --gtest_list_tests
```

## Writing New Tests

1. Create test file in appropriate directory (ccec/ or osal/)
2. Add to `run_L1Tests_SOURCES` in Makefile.am
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

## Test Categories

- **Unit Tests**: Test individual classes/functions in isolation
- **DISABLED_** prefix: Tests requiring hardware/driver mocking (currently disabled)

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
- **test_MessageDecoder.cpp** (54 tests): Decoding all 60+ CEC opcodes, polling messages, edge cases, opcode tracking
- **test_OpCode.cpp** (82 tests): Complete GetOpName() coverage for all CEC opcodes, OpCode class methods
- **test_Operands.cpp** (69 tests): All operand classes (PhysicalAddress, LogicalAddress, DeviceType, Version, PowerStatus, AbortReason, OSDString, OSDName, Language, VendorID, UICommand, SystemAudioStatus, AudioStatus, RequestAudioFormat, ShortAudioDescriptor, AllDeviceTypes, RcProfile, DeviceFeatures, LatencyInfo)
- **test_Operand.cpp** (11 tests): The Operand BASE class - the default `toString()`, `name()` and `validate()` bodies inherited by any operand that declines to override them, and the non-virtual `serialize(void)` convenience overload
- **test_Exception.cpp** (16 tests): Every exception type's `what()` message, dispatch through base and `std::exception` references, and that sibling types do not catch each other
- **test_Driver.cpp** (23 tests) / **test_Driver_Mock.cpp** (10 tests): Open/close, synchronous write, address management, mock verification
- **test_DriverImpl_Async.cpp** (26 tests): Asynchronous transmit, the transmit-completion callback, invalid-state guards, error-injection paths
- **test_Util.cpp** (12 tests): Log-level configuration parsing and the debug buffer dump

### OSAL Library Tests (26 tests)

- **test_ConditionVariable.cpp** (4 tests): Notify/wait synchronization patterns, timed wait, native-handle accessor
- **test_Mutex.cpp** (11 tests): Lock/unlock, tryLock, copy construction and copy assignment
- **test_Thread.cpp** (11 tests): Named construction, dispatch through Runnable, detach, destruction

## Known Issues and Notes

### Disabled Tests

**There are none.**  `./run_L1Tests` reports 520 tests run, 520 passed, 0 disabled.  Earlier
revisions of this document listed three `DISABLED_Term*` LibCCEC tests; no test of those names
has ever existed in this suite, and the two async driver tests that WERE disabled are now
enabled and passing.  Verify with:

```bash
./run_L1Tests --gtest_list_tests | grep DISABLED_    # expect no output
```

Repeated LibCCEC init/term cycles remain untested on purpose: LibCCEC uses Bus reader/writer
threads that race when repeatedly started and stopped, so the suite initialises the library
once, in the single global `testing::Environment` installed by `test_main.cpp`, and every
fixture shares that one initialisation.  The uninitialised-state guards are covered instead by
asserting the exception each API throws before `init()`, which needs no init/term cycle.

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

### Test Ordering

The suite contains pre-existing order-fragile tests: they pass in the established order and a
`--gtest_shuffle` run is expected to fail.  Two consequences for anyone adding tests:

- **Append** new translation units to `run_L1Tests_SOURCES`; do not insert them.  GoogleTest
  registers each `TEST_F` during static initialisation, in link order, and runs the suites in
  registration order, so inserting a file reorders every suite after it.
- Verify a new test both in isolation (`--gtest_filter=YourFixture.*`) and in the full suite.

### LibCCEC Singleton Behavior

LibCCEC is a singleton shared across all test suites. The test fixture SetUp() catches `InvalidStateException` to handle cases where LibCCEC has already been initialized by other test suites (e.g., DriverTest).

### Thread Timing

- Thread-related tests may need timing adjustments on slow systems
- Bus thread cleanup can take time; avoid rapid init/term cycles

### Hardware Dependencies

- Some Connection tests require actual CEC hardware/driver
- Hardware-dependent tests may need driver mocking implementation for full automation
