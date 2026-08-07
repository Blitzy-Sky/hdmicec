# Unit Test Framework Setup Guide

## Overview

A comprehensive L1 unit test framework has been created for the hdmicec library using **Google Test (gtest/gmock)**.

## Why Google Test?

**Google Test** was selected as the best framework for this project because:

1. ✅ **C++ Native**: Perfect for your C++ codebase (ccec/*.cpp, osal/*.cpp)
2. ✅ **Industry Standard**: Widely adopted, excellent documentation and community support
3. ✅ **Rich Features**: Built-in mocking (gmock), fixtures, parameterized tests, death tests
4. ✅ **RDK Ecosystem**: Commonly used in RDK projects
5. ✅ **Easy Integration**: Works seamlessly with autotools build system
6. ✅ **Modern**: Supports C++11/14/17 features used in your code

### Alternatives Considered

- **CUnit**: C-only framework, awkward for C++ classes and namespaces ❌
- **CppUnit**: Older, less maintained, verbose syntax ❌
- **Catch2**: Good but header-only increases compile times ⚠️
- **Boost.Test**: Heavy dependency, overkill for this project ⚠️

## Framework Structure

```
tests/L1Tests/
├── README.md                     # Documentation
├── QUICK_START.md                # Build-and-run quick reference
├── Makefile.am                   # Build configuration; run_L1Tests_SOURCES is the only
│                                 #   gate on what is compiled, and its order matters
├── run_coverage.sh               # gcov/lcov runner with the >=80% line-coverage gate
├── .lcovrc_l1                    # lcov configuration, branch collection enabled
├── .gitignore                    # Ignores the build output: *.o, .deps/, .libs/, Makefile,
│                                 #   run_L1Tests and the *.log/*.trs/*.xml result files
├── test_main.cpp                 # Test runner entry point
├── ccec/                         # CCEC library tests
│   ├── test_CECFrame.cpp         # CECFrame class tests
│   ├── test_Connection.cpp       # Connection class tests
│   ├── test_Bus.cpp              # Bus dispatch, filtering and send-retry tests
│   ├── test_LibCCEC.cpp          # LibCCEC singleton tests
│   ├── test_MessageEncoder.cpp   # Message encoding tests, every message type
│   ├── test_MessageDecoder.cpp   # Message decoding tests
│   ├── test_OpCode.cpp           # OpCode enum/class tests
│   ├── test_Operands.cpp         # PhysicalAddress/LogicalAddress tests
│   ├── test_Operand.cpp          # Operand BASE class default-virtual tests
│   ├── test_Exception.cpp        # Exception hierarchy what()/dispatch tests
│   ├── test_Driver_Mock.cpp      # Mock driver verification
│   ├── test_Driver.cpp           # Driver open/close/write/address tests
│   ├── test_DriverImpl_Async.cpp # Async transmit, invalid state, error paths
│   └── test_Util.cpp             # Log-level configuration and buffer dump
└── osal/                         # OSAL library tests
    ├── test_ConditionVariable.cpp # Condition variable tests
    ├── test_Mutex.cpp             # Mutex lock/unlock and copy-semantics tests
    └── test_Thread.cpp            # Thread construction, dispatch and detach tests
```

Every file listed above exists on disk and is git-tracked: `git ls-files tests/L1Tests`
returns exactly these 24 files and nothing else (`ccec/` and `osal/` are the two
directories holding them).  Anything else you see in that directory after a build --
`*.o`, `*.gcno`, `*.gcda`, `.deps/`, `.libs/`, `Makefile`, `Makefile.in`, `run_L1Tests`,
`*.log`, `*.trs` -- is generated output matched by `.gitignore`.

## Test Coverage

### CCEC Library Tests (14 test translation units, 494 tests)
Counts below are measured with `./run_L1Tests --gtest_list_tests`; re-measure with that
command after adding tests rather than trusting this list.
- **CECFrame** (31 tests): Constructor, copy operations, serialization, buffer management, hex dump, boundary sizes
- **Connection** (60 tests): Object creation, lifecycle management, open/close, listener registration, address filtering
- **Bus** (37 tests): Listener dispatch and filtering, send retries and timeout behaviour
- **LibCCEC** (14 tests): Singleton pattern, initialization/termination, logical/physical address management, and the uninitialised-state guards that throw
- **MessageEncoder** (49 tests): Encoding every message type in Messages.hpp, including the two-armed serializers and the variable-length audio-descriptor and latency messages
- **MessageDecoder** (54 tests): Comprehensive decoding of all 60+ CEC opcodes, polling messages, error handling, opcode tracking
- **OpCode** (82 tests): Complete GetOpName() coverage for all CEC opcodes, OpCode class methods (constructor, serialize, print)
- **Operands** (69 tests): All operand types including PhysicalAddress, LogicalAddress, DeviceType, Version, PowerStatus, AbortReason, OSDString, OSDName, Language, VendorID, UICommand, SystemAudioStatus, AudioStatus, RequestAudioFormat, ShortAudioDescriptor, AllDeviceTypes, RcProfile, DeviceFeatures, LatencyInfo
- **Operand base class** (11 tests): The default `toString()`, `name()` and `validate()` bodies the base supplies to any operand that declines to override them, plus the non-virtual `serialize(void)` convenience overload
- **Exception hierarchy** (16 tests): Every exception type's `what()` message, dispatch through base and `std::exception` references, and that sibling types do not catch each other
- **Driver** (23 tests) and **mock driver** (10 tests): Open/close, synchronous write, address management, mock verification
- **DriverImpl async** (26 tests): Asynchronous transmit, the transmit-completion callback, invalid-state guards and error-injection paths
- **Util** (12 tests): The log-level configuration read path -- every recognised level mapped to its numeric setting, plus the missing-file, empty-file, unrecognised-key and short-prefix arms that leave it unchanged -- and the hex buffer dump: emitted at debug and trace, silent below them, with zero-length and maximum-length buffers

The counts above sum to the 494 in the heading -- 14 figures across 13 bullets, because
**Driver** and **mock driver** are separate translation units counted on one line.

### OSAL Library Tests (3 test translation units, 26 tests)
- **ConditionVariable** (4 tests): Notify/wait synchronization patterns, signalling, timed wait and timeout behavior, and the native-handle accessor
- **Mutex** (11 tests): Default construction, lock/unlock including the recursive case where each `lock()` needs a matching `unlock()`, native-handle retrieval, copy construction, copy assignment, chained and self-assignment, and a copy owning a distinct handle usable independently of the original. `Mutex` has no try-lock operation, so none is tested
- **Thread** (11 tests): Both constructor overloads (unnamed, and named with empty and long names), `run()` dispatching to the `Runnable`, `start()` dispatching it on another thread, `detach()`, and destruction -- at scope exit, after `detach()`, and without a `start()`. `stop()` and `getNativeHandle()` are declared in `Thread.hpp` but never defined, so neither links; there is no join operation

**520 test cases in 18 fixtures, all passing, none disabled.**

## Installation Steps

### 1. Install Google Test

#### Ubuntu/Debian:
```bash
sudo apt-get update
sudo apt-get install libgtest-dev libgmock-dev cmake
```

#### Build from source (if packages don't include libraries):
```bash
cd /usr/src/gtest
sudo cmake .
sudo make
sudo cp lib/*.a /usr/lib

cd /usr/src/gmock
sudo cmake .
sudo make
sudo cp lib/*.a /usr/lib
```

#### RHEL/CentOS:
```bash
sudo yum install gtest-devel gmock-devel
```

### 2. Build System Configuration

The build system has been configured with the following changes:

#### configure.ac
- Added `--enable-l1tests` configure option
- Added Google Test dependency check
- Added `tests/L1Tests/Makefile` to AC_CONFIG_FILES

#### Makefile.am (root)
- Added `tests` to DIST_SUBDIRS

#### tests/Makefile.am
- Conditionally includes L1Tests subdirectory when `--enable-l1tests` is used
- Maintains backward compatibility with existing test applications

#### tests/L1Tests/Makefile.am
- Defines `run_L1Tests` test executable
- Links against libRCEC.la and libRCECOSHal.la
- Includes all test source files

### 3. Configure and Build

```bash
# Generate build scripts
autoreconf -fi

# Configure with L1 tests enabled
./configure --enable-l1tests

# Build the library and tests
make

# Run all tests
make check
```

## Running Tests

### Basic Usage

```bash
# Run all tests
make check

# Run tests directly
./tests/L1Tests/run_L1Tests

# Turn the output UP: print each test's elapsed time
./tests/L1Tests/run_L1Tests --gtest_print_time=1

# Turn the output DOWN: report failures only, suppressing the per-test progress lines
./tests/L1Tests/run_L1Tests --gtest_brief=1
```

> **Always take flag names from `./run_L1Tests --help`.**  GoogleTest has no general
> "verbose" switch -- the two flags above are how you raise and lower its output -- and it
> handles an unrecognised `--gtest_*` argument by printing its help text, running **no tests
> at all**, and still exiting `0`.  A misspelled flag therefore produces a run that looks
> successful but tested nothing, which no exit-status check will catch.

### Advanced Usage

```bash
# Navigate to test directory
cd tests/L1Tests

# Run specific test suite
./run_L1Tests --gtest_filter="CECFrameTest.*"

# Run multiple test patterns (this one selects the 22 OSAL Mutex and Thread cases)
./run_L1Tests --gtest_filter="*Mutex*:*Thread*"

# List all available tests
./run_L1Tests --gtest_list_tests

# Generate XML report (for CI/CD)
./run_L1Tests --gtest_output=xml:test_results.xml

# Repeat tests for flakiness detection
./run_L1Tests --gtest_repeat=100

# Shuffle test execution order (diagnostic only -- see "Order independence" below;
# --gtest_random_seed pins the order so a finding is reproducible)
./run_L1Tests --gtest_shuffle --gtest_random_seed=12345
```

`--gtest_shuffle` is a diagnostic, not a gate: this suite has pre-existing order-fragile
cases, so a shuffled run is expected to fail while the default-order run is green.  Measured
with seed `12345`: 520 ran, 511 passed, 9 failed, exit 1 -- against 520/520 and exit 0 in
default order.  Use it to check that a test you just wrote is self-sufficient, and compare
against that baseline rather than reading any shuffled failure as a new regression.

## Writing New Tests

### Example Test File

Create `tests/L1Tests/ccec/test_NewClass.cpp`:

```cpp
#include <gtest/gtest.h>
#include "ccec/NewClass.hpp"



class NewClassTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup before each test
        obj = new NewClass();
    }
    
    void TearDown() override {
        // Cleanup after each test
        delete obj;
    }
    
    NewClass* obj;
};

TEST_F(NewClassTest, BasicFunctionality) {
    EXPECT_EQ(obj->getValue(), 42);
    EXPECT_TRUE(obj->isValid());
}

TEST_F(NewClassTest, EdgeCase) {
    obj->setValue(-1);
    EXPECT_THROW(obj->process(), std::invalid_argument);
}
```

Add to `tests/L1Tests/Makefile.am`:
```makefile
run_L1Tests_SOURCES = \
    ... \
    ccec/test_NewClass.cpp
```

## CI/CD Integration

### GitHub Actions Example

```yaml
name: L1 Unit Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y libgtest-dev libgmock-dev libglib2.0-dev
      - name: Build and test
        run: |
          autoreconf -fi
          ./configure --enable-l1tests
          make check
      - name: Upload test results
        if: always()
        uses: actions/upload-artifact@v2
        with:
          name: test-results
          path: tests/L1Tests/*.xml
```

## Next Steps

1. **Integration tests**: Consider adding integration tests in a separate directory
2. **Order independence**: this suite contains pre-existing order-fragile tests -- a
   `--gtest_shuffle` run is expected to fail, and new translation units must be APPENDED to
   `run_L1Tests_SOURCES` rather than inserted, because static-initialisation order decides
   the order the suites run in
3. **Continuous testing**: the L1 workflow already runs this suite; `run_coverage.sh`
   reproduces its capture, filter and report steps locally with branch data enabled and a
   machine-checked >=80% line-coverage gate

Hardware mocking is already in place (`mocks/hdmicec/hdmi_cec_driver_mock.h`, injected by
symlinking it over the driver header the production code includes) and no test in this suite
is disabled.  gcov/lcov coverage reporting is wired: see `run_coverage.sh`.

## Troubleshooting

### gtest not found
```bash
# Check if gtest is installed
pkg-config --modversion gtest

# If not found, install or build from source
```

### Link errors
```bash
# Ensure libraries are in library path
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# Or add to configure
./configure --enable-l1tests LDFLAGS="-L/usr/local/lib"
```

### Test failures
```bash
# Run with per-test timings
./run_L1Tests --gtest_print_time=1

# Stop at the first failure instead of running the whole suite
./run_L1Tests --gtest_break_on_failure

# Capture a machine-readable result file to inspect the failure detail
./run_L1Tests --gtest_output=json:l1_results.json

# Debug specific test
gdb --args ./run_L1Tests --gtest_filter="FailingTest.*"
```

## Resources

- [Google Test Documentation](https://google.github.io/googletest/)
- [Google Mock Documentation](https://google.github.io/googletest/gmock_for_dummies.html)
- [Google Test Primer](https://google.github.io/googletest/primer.html)
- [Advanced Testing Topics](https://google.github.io/googletest/advanced.html)

## Summary

The L1 unit test framework provides:
- ✅ 18 test fixtures with 520 individual tests, all passing, none disabled
- ✅ Comprehensive coverage for both CCEC and OSAL libraries
  - Complete CEC opcode coverage (60+ opcodes tested)
  - All operand types tested (19 classes, 69 tests)
  - Message encoding/decoding thoroughly tested
  - All OpCode GetOpName() cases covered
- ✅ Easy to extend and maintain
- ✅ Integrated with build system via `--enable-l1tests`
- ✅ CI/CD ready with XML output support
- ✅ Production-grade testing infrastructure
- ✅ Singleton and global-state handling documented: `test_main.cpp` registers a single
  `CecTestEnvironment` (a `testing::Environment`) that installs the HAL driver mock and
  calls `LibCCEC::getInstance().init("CEC_TEST")` once for the whole program, so no fixture
  re-initialises the library. A test that must observe an uninitialised-state guard uses the
  `term()` -> provoke -> `init()` idiom and restores the singleton before it returns, which
  is what lets the async and `LibCCEC` guard cases run inside the shared environment

