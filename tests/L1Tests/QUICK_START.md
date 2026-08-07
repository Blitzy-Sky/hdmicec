# L1 Unit Test Framework - Quick Reference

## Directory Structure

```
tests/
├── Makefile.am                   # Conditionally includes L1Tests
├── BasicTest.cpp                 # Existing integration tests
├── CECCmd.cpp
├── CECMonitor.cpp
├── CECCmdTest.cpp
└── L1Tests/                      # NEW: L1 Unit Tests
    ├── Makefile.am               # L1 test build configuration
    ├── README.md                 # L1 test documentation
    ├── QUICK_START.md            # This quick reference
    ├── test_main.cpp             # Test runner entry point
    ├── run_coverage.sh           # coverage runner with 80% line gate
    ├── .lcovrc_l1                # lcov configuration (branch collection enabled)
    ├── .gitignore                # Ignore build artifacts
    ├── ccec/                     # CCEC library tests (14 files, 494 tests)
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
    │   ├── test_Operand.cpp
    │   └── test_Exception.cpp
    └── osal/                     # OSAL library tests (3 files, 26 tests)
        ├── test_ConditionVariable.cpp
        ├── test_Mutex.cpp
        └── test_Thread.cpp
```

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
- Added `tests/L1Tests/Makefile` to configuration

### 2. Makefile.am (root)
```makefile
DIST_SUBDIRS = cfg osal ccec tests
```

### 3. tests/Makefile.am
```makefile
if ENABLE_L1TESTS
SUBDIRS = L1Tests
endif
```

### 4. tests/L1Tests/Makefile.am
- Defines `run_L1Tests` executable
- Links test sources with libRCEC.la and libRCECOSHal.la

## Quick Start

### Build with L1 Tests
```bash
autoreconf -fi
./configure --enable-l1tests
make
make check
```

### Build without L1 Tests (default)
```bash
./configure
make
```

### Run Tests Manually
```bash
cd tests/L1Tests
./run_L1Tests
```

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
./run_coverage.sh --build --run     # build, run, capture, report, gate at 80% lines
```

Artifacts (`coverage.info`, `filtered_coverage.info`, `coverage/index.html`
and the per-file tables) default to
`${TMPDIR:-/tmp}/hdmicec-l1-coverage/<workspace>`, deliberately outside the
git tree because `.gitignore` does not cover them.  `--output-dir .`
reproduces the CI layout in-tree instead, at which point they are build
artifacts that must not be committed.  See `./run_coverage.sh --help` for
the full option and environment reference.

## Test Executable

- **Name**: `run_L1Tests`
- **Location**: `tests/L1Tests/`
- **Type**: Google Test executable
- **Sources**: 17 test translation units + test_main.cpp (all listed in
  `run_L1Tests_SOURCES`, which is the only gate on what gets compiled)
- **Total Tests**: 520 individual test cases in 18 fixtures — all passing,
  none disabled — measured with `./run_L1Tests --gtest_list_tests`

## Key Features

✅ **Conditional Build**: Only builds when `--enable-l1tests` is specified  
✅ **Backward Compatible**: Existing tests (BasicTest, CECCmd, etc.) unchanged  
✅ **Clean Separation**: L1 tests in dedicated subdirectory  
✅ **Google Test Framework**: Industry-standard C++ testing  
✅ **Automated Testing**: Integrated with `make check`  

## Configuration Options

| Option | Description | Default |
|--------|-------------|---------|
| `--enable-l1tests` | Enable L1 unit tests | `no` |
| `--disable-l1tests` | Disable L1 unit tests | N/A |

## Next Steps

1. Install Google Test: `sudo apt-get install libgtest-dev libgmock-dev`
2. Configure: `./configure --enable-l1tests`
3. Build: `make`
4. Test: `make check`

See **UNIT_TEST_SETUP.md** for complete documentation.
