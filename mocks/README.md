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
- Each SendMessageStatus value, and a settable non-ok binder Status for every call the middleware actually makes: open, close, getLogicalAddresses, addLogicalAddresses, removeLogicalAddresses and sendMessage
- A call counter for each of those calls, and a last-argument capture for each one that carries an argument worth asserting: the listener passed to open, the controller passed to close, the address vector passed to addLogicalAddresses and to removeLogicalAddresses, and the frame passed to sendMessage. The four calls the middleware deliberately never makes (getState, getProperty, registerEventListener and unregisterEventListener) carry a counter only: they always report an ok status and have no settable response, because the one thing a test wants from them is the assertion that their count stayed at zero
- Three triggers that make the fake call back into the listener captured by open(): a received message, a state change and a message-sent report. All three are called in process by the L1 AIDL suite, the last two by DriverAidlSessionTest.DiagnosticCallbacksAreReportedWithoutDisturbingTheSession; only the received-message trigger is additionally reachable from another process, through the host's control channel described below
- One metadata control, setInterfaceHash on the service class. Its only consumer is the L1 harness in tests/L1Tests/test_main.cpp, whose incompatible mode installs the broken hash "-1" before the middleware's selection resolves, so that the factory-level fallback from a present but incompatible service can be observed. The remaining compatibility-rejection arms - the empty hash, the "notfrozen" development hash and every version arm - are exercised at unit level by locally constructed doubles in tests/L1Tests/ccec/test_DriverAidl.cpp, not through this fake, because a Bn*-derived object that is served remotely cannot report divergent metadata at all: its generated onTransact answers the metadata transactions from the compiled-in constants. There is deliberately no version setter on either class and no metadata setter on the controller, since the middleware's compatibility check reads the service interface's metadata alone and a divergent value reported anywhere else could not change an outcome under test

### hdmicec/fake_hdmi_cec_aidl_service_host.cpp
A small main() that hosts the fake in a separate process. A service name registered in the calling process resolves to the local BBinder, so an in-process registration produces no proxy and no transaction across the binder driver; only a separate process makes the middleware hold a real proxy and receive callbacks on a binder thread. It starts a binder threadpool for its own service side, signals readiness to its parent, and exits on request. The test harness launches it, waits for that readiness signal, and terminates and reaps it on teardown.

#### The control and observation channel

A separate process is opaque to the test that launched it: the parent cannot reach into it to make the fake deliver an inbound message, and it cannot read back what the middleware actually sent. The host therefore carries a line-oriented control and observation channel that does not travel over binder, which is the point of it - the observation stays trustworthy even when the binder path under test is the thing that is broken. The channel is optional and off by default. The host reads two environment variables alongside the existing CEC_FAKE_HOST_READY_FD, and with both unset it behaves exactly as it did before the channel existed:

- CEC_FAKE_HOST_CONTROL_FD: an inherited read end, from which the host reads newline-terminated ASCII commands
- CEC_FAKE_HOST_OBSERVE_FD: an inherited write end, to which the host writes exactly one newline-terminated reply per command, every reply beginning either "OK " or "ERR "

The command vocabulary:

```
ping                             -> OK pong
deliver <lowercase-hex>          -> OK delivered <byteCount> | ERR no-listener | ERR bad-hex
sent-count                       -> OK sent-count <n>
last-sent                        -> OK last-sent <lowercase-hex>   (empty capture: empty hex field)
open-count                       -> OK open-count <n>      (consumed by DualPathAidlFlowTest)
close-count                      -> OK close-count <n>     (consumed by DualPathAidlFlowTest)
listener                         -> OK listener present | OK listener absent
shutdown                         -> OK shutdown, then the same teardown the signal path performs
anything else                    -> ERR unknown-command <verb>
```

That table is the whole vocabulary; a verb outside it, including one the host used to carry, is answered "ERR unknown-command <verb>" rather than silently served. Three verbs are deliberately absent. state-changed and message-sent would have fired the fake's two diagnostic callbacks over IPC, and the adapter's only obligation for those callbacks - log them and act on them in no other way - is already asserted in process by DriverAidlSessionTest.DiagnosticCallbacksAreReportedWithoutDisturbingTheSession, which calls the two triggers directly and reads what they logged, so an out-of-process variant would have added a verb without adding evidence. reset is absent because it cannot be made safe: FakeHdmiCecService::reset() clears the captured listener, so a reset issued mid-session would destroy the live session's receive path and turn every later deliver into "ERR no-listener" for a reason no assertion would explain. Per-case isolation belongs to the in-process fixtures, which call reset() directly and re-open afterwards.

Three further replies exist outside the table above. A command carrying the wrong number of arguments is answered "ERR bad-args <verb>". A line longer than the 4096-byte cap is answered "ERR command-too-long" and is never parsed, so how the parent's bytes happened to be split can never decide whether its command was accepted. The two commands that read through the controller answer "ERR no-controller" if the fake is somehow holding none, which the host's own construction order makes unreachable today but which is answered rather than crashed. A blank or whitespace-only line is ignored without a reply, a trailing carriage return is tolerated, and the loop continues after every ERR, so a malformed command never costs the parent its channel.

deliver reaches the same listener the middleware handed to IHdmiCec::open(), and the observation commands report the fake's own counters and captured bytes rather than a second copy of them, so what the channel answers is what the middleware actually did. Nothing in the vocabulary clears, rewinds or reconfigures the hosted fake.

Failures are loud rather than silent. One variable present without the other, a value that is not a plain non-negative descriptor number, a descriptor that is not open in the child or is open in the wrong direction, and both variables naming the same descriptor are all hard failures: the host exits 8 and writes no readiness token, so a parent that mis-wires the channel sees a failed launch instead of a host that quietly ignores its commands. A channel that breaks while in use, through a poll or read error or a reply that cannot be delivered within five seconds, exits 9. The parent must clear FD_CLOEXEC on the child's copies of both descriptors between fork() and exec(), exactly as it already does for the readiness descriptor. The host serves the channel with a poll() over both the control descriptor and its shutdown self-pipe, so a signal and a queued command are each answered promptly, and end of file on the control descriptor means the parent has gone away and is treated as a clean shutdown.

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
