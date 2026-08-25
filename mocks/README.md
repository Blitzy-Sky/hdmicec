# HDMI CEC Mocks

This directory contains mock implementations of external dependencies for HDMI CEC L1 testing.

## Structure

```
mocks/
├── hdmicec/                    # HDMI CEC specific mocks
│   ├── hdmi_cec_driver.h       # HAL driver interface
│   ├── hdmi_cec_driver_mock.h  # Mock class header
│   ├── hdmi_cec_driver_mock.cpp # Mock implementation
│   ├── fake_hdmi_cec_aidl_service.h # Fake AIDL service header
│   ├── fake_hdmi_cec_aidl_service.cpp # Fake AIDL service implementation
│   └── fake_hdmi_cec_aidl_service_host.cpp # Out-of-process fake host binary
├── safec_lib.h                 # safec API compatibility shim
├── telemetry_busmessage_sender.h # Telemetry stub
└── README.md
```

## Files

### hdmicec/hdmi_cec_driver.h
Header file defining the HDMI CEC HAL driver interface. This matches the interface expected by the CEC implementation.

### hdmicec/hdmi_cec_driver_mock.h / hdmi_cec_driver_mock.cpp
Mock implementation of the HDMI CEC driver. This provides:
- Controllable driver behavior for testing
- Methods to inject received CEC messages
- Methods to simulate transmission results
- Verification of driver state and logical addresses

### hdmicec/fake_hdmi_cec_aidl_service.h / fake_hdmi_cec_aidl_service.cpp
Test-scope fake implementation of the existing HDMI CEC AIDL service. The header declares the fake service and controller classes, derived from the generated BnHdmiCec and BnHdmiCecController bases; the implementation supplies their behaviour and registers the fake under the production service name "HdmiCec", which it takes from IHdmiCec::serviceName() rather than from a literal of its own, so the middleware's own lookup resolves to it. It implements no new interface: the interface it implements is the one the .aidl files already define. This provides:
- Settable canned responses for the logical address, transmit and open/close calls, so tests can drive each outcome
- Reporting of a multi-entry or an empty logical address vector
- Each SendMessageStatus value, and a non-ok binder Status per method
- A trigger that makes the fake call back into the registered event listener
- Overridable interface hash and version, so the compatibility rejection paths can be exercised in process

### hdmicec/fake_hdmi_cec_aidl_service_host.cpp
A small main() that hosts the fake in a separate process. A service name registered in the calling process resolves to the local BBinder, so an in-process registration produces no proxy and no transaction across the binder driver; only a separate process makes the middleware hold a real proxy and receive callbacks on a binder thread. It starts a binder threadpool for its own service side, signals readiness to its parent, and exits on request. The test harness launches it, waits for that readiness signal, and terminates and reaps it on teardown.

### telemetry_busmessage_sender.h
Stub header for RDK telemetry system. Provides no-op macros for telemetry calls used in the CEC code.

## Usage in Tests

```cpp
#include "hdmi_cec_driver_mock.h"

TEST_F(YourTestFixture, TestSomething) {
    // Create mock instance
    HdmiCecDriverMock mock;

    // Configure mock behavior using Google Mock, for example:
    // ON_CALL(mock, open(::testing::_)).WillByDefault(::testing::Return(true));
    // EXPECT_CALL(mock, close()).Times(1);

    // Your test code that uses the CEC driver
    // ...

    // Inject a received message into the mock
    unsigned char msg[] = {0x40, 0x04}; // Example CEC message
    mock.injectReceivedMessage(msg, sizeof(msg));

    // Optionally, simulate a transmit result being reported by the driver
    mock.simulateTxResult(/* txId */ 1, /* success */ true);

    // Verify that the mock's callbacks/handles have been set up as expected
    EXPECT_NE(mock.currentHandle, nullptr);
    EXPECT_TRUE(mock.rxCallback);

    // Additional EXPECT_CALL/ASSERT_* on your system-under-test can go here.
}
```

## Building

These mocks are built as part of the L1 and L2 test build process. The Makefile.am in tests/L1Tests includes these source files, compiling the fake AIDL service implementation into the run_L1Tests runner alongside the legacy driver mock, which stays last in that source list. The Makefile.am in tests/L2Tests builds two programs: the run_L2Tests runner, which carries the legacy driver mock, and a separate fake_hdmi_cec_aidl_host binary, which carries both fake sources. The fake belongs to the host, never to the runner, because the two have to be separate processes for real binder IPC to occur.

Neither production library source list references mocks/: not libRCEC_la_SOURCES in ccec/src/Makefile.am, and not OBJS in ccec/src/Makefile. That is why nothing in this directory can reach the shipped libRCEC library. The test Makefiles named above do reference it, and they are the only things that do, which is why a mocks/ reference must never be added to either production source list.
