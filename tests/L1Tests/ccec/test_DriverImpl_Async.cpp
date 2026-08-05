/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2016 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

/*
 * L1 unit tests for the asynchronous transmit seam of the CEC driver.
 *
 * The asynchronous path had zero hits across its entire body: the buffer read, the
 * frame logging, the lock acquisition, the state guard, the HAL asynchronous
 * transmit call and the failure arm that throws on a non-success return. The
 * transmit-completion callback registered with the HAL during open() had never been
 * invoked either, despite being bound on every open.
 *
 * Both paths are reached here through the existing HAL mock - failure-return
 * injection for the error arms, and the mock's transmit-result helper to invoke the
 * registered completion callback directly. Invoking the callback rather than waiting
 * on real hardware keeps the cases deterministic and free of wall-clock waits.
 *
 * ISOLATION: this suite shares one process-global driver with every other suite in
 * the binary, and the suite is known to be order-sensitive. The cases that need an
 * open driver therefore establish that precondition themselves rather than assuming
 * one, leave the driver in that same opened state - the state the global test
 * environment establishes at start-up - and clear their own mock expectations. The
 * cases that need a driver which is NOT open avoid the shared instance entirely and
 * construct a local one instead; see the design note further down. Exactly one case
 * in this file cycles the shared library, because it is the only one that has no
 * alternative. No case sleeps or waits on wall-clock time.
 *
 * ---------------------------------------------------------------------------------
 * TRACEABILITY (COVERAGE_GAPS.md section 6.2 rank + stable anchor + symbol name;
 * never keyed by line number, because this pass shifts line numbers)
 *
 *   rank 8,  P0  #gap-mw-driverimpl       DriverImpl seam - "the single
 *                                         highest-leverage middleware gap"
 *   rank 9,  P0  #gap-hal-settxcallback   HdmiCecSetTxCallback / DriverTransmitCallback
 *   rank 10, P0  #gap-hal-txasync         HdmiCecTxAsync
 *   rank 26, P1  #gap-mw-driver           Driver::getInstance abstraction
 *   rank 1,  P0  #gap-flow-a              inbound receive chain, whose first leg is
 *                                         DriverImpl::DriverReceiveCallback
 *
 * NOTE on the register's own numbering: section 2a carries table row numbers that
 * DIFFER from the section 6.2 ranks (HdmiCecSetTxCallback is row 8 in section 2a but
 * rank 9 in section 6.2). Everything below keys off the section 6.2 rank.
 *
 * NOTE on provenance: DriverImpl::writeAsync has NO production caller anywhere in
 * the middleware. Connection::sendTo and Connection::sendToAsync both converge on
 * Driver::write and therefore on the synchronous HdmiCecTx, as the sibling suite
 * records in its own comment in ccec/test_Connection.cpp ("dispatches via HdmiCecTx
 * (not HdmiCecTxAsync)"). Every executing line of coverage this seam will ever have
 * therefore originates in tests such as these.
 *
 * ---------------------------------------------------------------------------------
 * UNCOVERABLE LINES - enumerated, deliberately NOT chased, and NOT excluded from the
 * coverage denominator. Each entry names the production change that would be needed;
 * production source is out of scope here, so none of them is made.
 *
 * (1) DriverImpl::printFrameDetails - the body of its catch(Exception &e) handler.
 *     The only statement inside the guarded try that can throw is the Header
 *     construction, which reads frame.at(0) and therefore raises
 *     std::out_of_range. CCEC's Exception derives from std::exception, and
 *     std::out_of_range derives from std::logic_error, so the two are SIBLINGS -
 *     catch(Exception &e) provably cannot catch it, and the exception propagates
 *     out of printFrameDetails instead. CECFrame::getBuffer is two assignments and
 *     cannot throw; LogicalAddress cannot throw for a nibble; and the OpCode
 *     construction is already length-guarded. No test-only change can enter this
 *     handler.
 *     REQUIRED PRODUCTION CHANGE: broaden the handler to catch(std::exception &) or
 *     catch(...), or length-guard the frame before constructing the Header.
 *     The boundary itself IS pinned down by tests below, which assert that
 *     std::out_of_range escapes rather than being swallowed - so a future change to
 *     the handler becomes a deliberate, visible decision rather than a silent one.
 *
 * (2) DriverImpl::read - the flush arm inside the "status is no longer OPENED"
 *     branch of the read loop. read() guards on the status BEFORE entering the loop
 *     and throws immediately, so a read after close never reaches the arm. Reaching
 *     it requires the status to have left OPENED while the queue is simultaneously
 *     non-empty, but the poll that returned the sentinel already drained it, and no
 *     further frame can be enqueued once the status leaves OPENED because
 *     getIncomingQueue throws. That combination is a genuine two-thread race
 *     between the Bus reader thread and a concurrent close.
 *     REQUIRED PRODUCTION CHANGE: make the drain deterministically reachable, for
 *     example by draining under a single lock acquisition in close().
 *     NOTHING IN THIS FILE MAY CALL read(): with the queue empty EventQueue::poll
 *     blocks by its own documented contract, so a read() here would either throw
 *     immediately or hang the whole test binary.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "ccec/CECFrame.hpp"
#include "ccec/Driver.hpp"
#include "ccec/Exception.hpp"
#include "ccec/LibCCEC.hpp"

/*
 * DriverImpl.hpp lives under ccec/src and is deliberately NOT on the include path
 * (AM_CPPFLAGS covers ccec/include, osal/include and the mock directories only). A
 * relative include reaches it without adding an -I anywhere, and it resolves
 * cleanly because every header it pulls in - <list>, osal/Mutex.hpp,
 * osal/EventQueue.hpp, osal/ConditionVariable.hpp, ccec/Driver.hpp and
 * ccec/Header.hpp - is already reachable from the existing include path.
 *
 * It is needed for exactly two things that the abstract Driver interface cannot
 * express: constructing a local driver instance so its destructor actually runs
 * (the process-wide instance is a function-local static, so its destructor runs
 * only at static destruction and is unreachable from a test), and reaching the
 * invalid-state guards on a freshly constructed instance without disturbing the
 * process-global driver that every other suite in this binary shares.
 */
#include "../../../ccec/src/DriverImpl.hpp"

#include <stdexcept>
#include "hdmi_cec_driver_mock.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SaveArg;

class DriverImplAsyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock = HdmiCecDriverMock::getInstance();
        if (mock != nullptr) {
            ::testing::Mock::VerifyAndClearExpectations(mock);
        }

        // Establish this suite's own precondition instead of inheriting whatever
        // state the previously executed suite happened to leave behind. open() is a
        // no-op when the driver is already opened.
        try {
            Driver::getInstance().open();
        } catch (...) {
            // Already opened - nothing to do.
        }
    }

    void TearDown() override {
        if (mock != nullptr) {
            ::testing::Mock::VerifyAndClearExpectations(mock);
        }

        // Restore the baseline the global environment set up, so no sibling suite
        // observes a closed driver because of anything done here.
        try {
            Driver::getInstance().open();
        } catch (...) {
            // Already opened - nothing to do.
        }
    }

    // A well-formed directed frame: <header> <opcode>.
    static CECFrame directedFrame(uint8_t header = 0x40, uint8_t opcode = 0x36) {
        CECFrame frame;
        frame.append(header);
        frame.append(opcode);
        return frame;
    }

    HdmiCecDriverMock *mock = nullptr;
};

// The happy path through writeAsync: the frame's buffer and length reach the HAL's
// asynchronous entry point and nothing is thrown.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl DriverImpl::writeAsync;
// rank 10 #gap-hal-txasync HdmiCecTxAsync; rank 26 #gap-mw-driver Driver::getInstance.
TEST_F(DriverImplAsyncTest, WriteAsyncForwardsFrameToHal) {
    ASSERT_NE(mock, nullptr);

    int capturedLength = -1;
    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _))
        .Times(1)
        .WillOnce(DoAll(SaveArg<2>(&capturedLength), Return(HDMI_CEC_IO_SUCCESS)));

    CECFrame frame = directedFrame();
    EXPECT_NO_THROW({ Driver::getInstance().writeAsync(frame); });

    EXPECT_EQ(capturedLength, 2);
}

// The buffer handed to the HAL must be the frame's own bytes, unmodified.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl DriverImpl::writeAsync;
// rank 10 #gap-hal-txasync HdmiCecTxAsync.
TEST_F(DriverImplAsyncTest, WriteAsyncForwardsExactFrameBytes) {
    ASSERT_NE(mock, nullptr);

    unsigned char capturedFirstByte = 0;
    unsigned char capturedSecondByte = 0;
    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&capturedFirstByte, &capturedSecondByte](int, const unsigned char *buf, int len) {
                if (buf != nullptr && len >= 2) {
                    capturedFirstByte = buf[0];
                    capturedSecondByte = buf[1];
                }
                return HDMI_CEC_IO_SUCCESS;
            }));

    CECFrame frame = directedFrame(0x4F, 0x82);
    EXPECT_NO_THROW({ Driver::getInstance().writeAsync(frame); });

    EXPECT_EQ(capturedFirstByte, 0x4F);
    EXPECT_EQ(capturedSecondByte, 0x82);
}

// A single-byte frame is the minimum the asynchronous path can carry - it is what a
// poll looks like on the wire.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl DriverImpl::writeAsync;
// rank 10 #gap-hal-txasync HdmiCecTxAsync. Boundary case: minimum frame length.
TEST_F(DriverImplAsyncTest, WriteAsyncAcceptsMinimumLengthFrame) {
    ASSERT_NE(mock, nullptr);

    int capturedLength = -1;
    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _))
        .Times(1)
        .WillOnce(DoAll(SaveArg<2>(&capturedLength), Return(HDMI_CEC_IO_SUCCESS)));

    CECFrame frame;
    frame.append(0x44);
    EXPECT_NO_THROW({ Driver::getInstance().writeAsync(frame); });

    EXPECT_EQ(capturedLength, 1);
}

// The maximum frame the type can hold must also pass through unchanged, including
// through the frame-detail logging that runs before the HAL call.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl DriverImpl::writeAsync;
// rank 10 #gap-hal-txasync HdmiCecTxAsync. Boundary case: maximum frame length.
TEST_F(DriverImplAsyncTest, WriteAsyncAcceptsMaximumLengthFrame) {
    ASSERT_NE(mock, nullptr);

    int capturedLength = -1;
    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _))
        .Times(1)
        .WillOnce(DoAll(SaveArg<2>(&capturedLength), Return(HDMI_CEC_IO_SUCCESS)));

    CECFrame frame;
    frame.append(static_cast<uint8_t>(0x40));
    frame.append(static_cast<uint8_t>(0x36));
    for (size_t i = frame.length(); i < CECFrame::MAX_LENGTH; i++) {
        frame.append(static_cast<uint8_t>(i & 0xFF));
    }
    ASSERT_EQ(frame.length(), CECFrame::MAX_LENGTH);

    EXPECT_NO_THROW({ Driver::getInstance().writeAsync(frame); });

    EXPECT_EQ(capturedLength, static_cast<int>(CECFrame::MAX_LENGTH));
}

// Broadcast addressing takes the same asynchronous path as directed addressing; the
// distinction lives in the header nibble, not in the transmit routine.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl DriverImpl::writeAsync;
// rank 10 #gap-hal-txasync HdmiCecTxAsync. Corner case: broadcast vs directed.
TEST_F(DriverImplAsyncTest, WriteAsyncAcceptsBroadcastAddressedFrame) {
    ASSERT_NE(mock, nullptr);

    unsigned char capturedHeader = 0;
    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _))
        .Times(1)
        .WillOnce(::testing::Invoke([&capturedHeader](int, const unsigned char *buf, int len) {
            if (buf != nullptr && len >= 1) {
                capturedHeader = buf[0];
            }
            return HDMI_CEC_IO_SUCCESS;
        }));

    // Source 4 (playback device 1) to the broadcast address 0x0F.
    CECFrame frame = directedFrame(0x4F, 0x36);
    EXPECT_NO_THROW({ Driver::getInstance().writeAsync(frame); });

    EXPECT_EQ(capturedHeader, 0x4F);
}

// The failure arm: a non-success return from the HAL must surface as an IOException.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl DriverImpl::writeAsync;
// rank 10 #gap-hal-txasync HdmiCecTxAsync. Negative case: HAL general error.
TEST_F(DriverImplAsyncTest, WriteAsyncThrowsIOExceptionOnHalGeneralError) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_GENERAL_ERROR));

    CECFrame frame = directedFrame();
    EXPECT_THROW({ Driver::getInstance().writeAsync(frame); }, IOException);
}

// Traceability: section 6.2 rank 8 #gap-mw-driverimpl DriverImpl::writeAsync;
// rank 10 #gap-hal-txasync HdmiCecTxAsync. Negative case: HAL invalid argument.
TEST_F(DriverImplAsyncTest, WriteAsyncThrowsIOExceptionOnHalInvalidArgument) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_INVALID_ARGUMENT));

    CECFrame frame = directedFrame();
    EXPECT_THROW({ Driver::getInstance().writeAsync(frame); }, IOException);
}

// A failed asynchronous transmit must leave the driver usable: the next transmit
// still reaches the HAL.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl DriverImpl::writeAsync;
// rank 10 #gap-hal-txasync HdmiCecTxAsync. Recovery after a failed transmit.
TEST_F(DriverImplAsyncTest, DriverRemainsUsableAfterAsyncFailure) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _))
        .Times(2)
        .WillOnce(Return(HDMI_CEC_IO_GENERAL_ERROR))
        .WillOnce(Return(HDMI_CEC_IO_SUCCESS));

    CECFrame frame = directedFrame();
    EXPECT_THROW({ Driver::getInstance().writeAsync(frame); }, IOException);
    EXPECT_NO_THROW({ Driver::getInstance().writeAsync(frame); });
}

// The completion callback bound to the HAL during open(): invoking it with a success
// result must execute its body without disturbing the driver.
// Traceability: section 6.2 rank 9 #gap-hal-settxcallback HdmiCecSetTxCallback;
// rank 8 #gap-mw-driverimpl DriverImpl::DriverTransmitCallback. Positive result arm.
TEST_F(DriverImplAsyncTest, TransmitCompletionCallbackHandlesSuccessResult) {
    ASSERT_NE(mock, nullptr);
    ASSERT_NE(mock->txCallback, nullptr) << "open() must have registered a transmit callback";

    EXPECT_NO_THROW({ mock->simulateTxResult(HDMI_CEC_IO_SENT_AND_ACKD); });
}

// The failure arm of the completion callback - the branch that logs the result code.
// Traceability: section 6.2 rank 9 #gap-hal-settxcallback HdmiCecSetTxCallback;
// rank 8 #gap-mw-driverimpl DriverImpl::DriverTransmitCallback. Negative result arm.
TEST_F(DriverImplAsyncTest, TransmitCompletionCallbackHandlesFailureResult) {
    ASSERT_NE(mock, nullptr);
    ASSERT_NE(mock->txCallback, nullptr) << "open() must have registered a transmit callback";

    EXPECT_NO_THROW({ mock->simulateTxResult(HDMI_CEC_IO_SENT_BUT_NOT_ACKD); });
    EXPECT_NO_THROW({ mock->simulateTxResult(HDMI_CEC_IO_SENT_FAILED); });
    EXPECT_NO_THROW({ mock->simulateTxResult(HDMI_CEC_IO_GENERAL_ERROR); });
}

// The completion callback fires asynchronously with respect to the transmit that
// triggered it, so it must also be safe immediately after a transmit.
// Traceability: section 6.2 rank 9 #gap-hal-settxcallback HdmiCecSetTxCallback;
// rank 10 #gap-hal-txasync HdmiCecTxAsync; rank 8 #gap-mw-driverimpl
// DriverImpl::writeAsync + DriverImpl::DriverTransmitCallback in sequence.
TEST_F(DriverImplAsyncTest, TransmitCompletionCallbackAfterAsyncWrite) {
    ASSERT_NE(mock, nullptr);
    ASSERT_NE(mock->txCallback, nullptr) << "open() must have registered a transmit callback";

    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_SUCCESS));

    CECFrame frame = directedFrame();
    EXPECT_NO_THROW({ Driver::getInstance().writeAsync(frame); });
    EXPECT_NO_THROW({ mock->simulateTxResult(HDMI_CEC_IO_SENT_AND_ACKD); });
}

// printFrameDetails() runs on every transmit and must tolerate a frame too short to
// carry an opcode - it is expected to swallow the decode failure, not propagate it.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl DriverImpl::printFrameDetails.
// Boundary case: frame length exactly at the opcode-presence threshold.
TEST_F(DriverImplAsyncTest, PrintFrameDetailsToleratesFrameWithoutOpcode) {
    CECFrame frame;
    frame.append(0x40);

    EXPECT_NO_THROW({ Driver::getInstance().printFrameDetails(frame); });
}

// The logger's handler catches the CEC Exception hierarchy only, so the
// std::out_of_range raised when a header is read out of an empty frame is NOT
// swallowed - it propagates to the caller. This case pins that boundary down so a
// future change to the handler is a visible, deliberate decision rather than a
// silent one.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl DriverImpl::printFrameDetails.
// Pins the uncoverable-range (1) boundary documented in this file's header.
TEST_F(DriverImplAsyncTest, PrintFrameDetailsPropagatesOutOfRangeForEmptyFrame) {
    CECFrame emptyFrame;

    EXPECT_THROW({ Driver::getInstance().printFrameDetails(emptyFrame); }, std::out_of_range);
}

/* =================================================================================
 * Error, invalid-state and lifecycle surface.
 *
 * Everything above exercises the asynchronous transmit seam while the driver is
 * open. Everything below is the complementary half: the guard clauses that reject
 * calls made outside the opened state, the frame-cleanup handler that runs when the
 * inbound queue rejects a frame, and the destructor.
 *
 * Design note on isolation. Most of the cases below need a driver that is NOT open,
 * which is the exact opposite of the precondition this fixture establishes and of
 * the state every sibling suite in this binary expects to inherit. Rather than
 * cycling the shared library and driver for each one, they construct a LOCAL driver
 * instance: a freshly constructed DriverImpl begins life closed, so it reaches the
 * very same compiled guard clauses while touching no process-global state at all.
 * That makes these cases inherently order-independent.
 *
 * Only the frame-cleanup handler genuinely requires the shared instance, because the
 * receive callback is a static function that resolves the driver through
 * Driver::getInstance() and the incoming queue it consults is private. That single
 * case - and only that one - performs one terminate/initialise cycle, and it uses
 * non-fatal expectations throughout so the restoration is reached even if an
 * expectation fails part-way.
 * ================================================================================= */

// An empty frame cannot survive the frame-detail logging that writeAsync performs
// before it takes the lock, so writeAsync rejects it and - importantly - the HAL is
// never reached. This is the true minimum-length edge case for the asynchronous
// path: not a short frame, but an absent one.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl DriverImpl::writeAsync
// (buffer read + frame logging); rank 10 #gap-hal-txasync HdmiCecTxAsync must NOT
// be reached. Boundary case: zero-length frame.
TEST_F(DriverImplAsyncTest, WriteAsyncPropagatesOutOfRangeForEmptyFrame) {
    ASSERT_NE(mock, nullptr);

    // The frame never survives logging, so nothing may reach the HAL.
    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _)).Times(0);

    CECFrame emptyFrame;
    EXPECT_THROW({ Driver::getInstance().writeAsync(emptyFrame); }, std::out_of_range);
}

// Ordering proof. writeAsync logs the frame BEFORE it takes the lock and before it
// consults the driver state, so a closed driver handed an empty frame reports the
// decode failure - not the invalid state. If the two were ever reordered, this case
// would start reporting InvalidStateException and fail loudly.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl DriverImpl::writeAsync
// (ordering of the frame logging relative to the state guard).
TEST_F(DriverImplAsyncTest, FrameLoggingPrecedesStateGuardInWriteAsync) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _)).Times(0);

    // A local instance begins life closed, so both the logging and the state guard
    // are live for it - which is what makes the ordering observable.
    DriverImpl closedDriver;
    CECFrame emptyFrame;

    EXPECT_THROW({ closedDriver.writeAsync(emptyFrame); }, std::out_of_range);

    // Positive control: with a well-formed frame the same closed instance gets past
    // the logging and is then stopped by the state guard instead.
    CECFrame wellFormed = directedFrame();
    EXPECT_THROW({ closedDriver.writeAsync(wellFormed); }, InvalidStateException);
}

// The asynchronous transmit guard: a driver that was never opened must refuse to
// transmit, and must refuse before the HAL is touched.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl DriverImpl::writeAsync state
// guard; rank 10 #gap-hal-txasync HdmiCecTxAsync must NOT be reached.
TEST_F(DriverImplAsyncTest, WriteAsyncRejectedWhileDriverClosed) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _)).Times(0);

    DriverImpl closedDriver;
    CECFrame frame = directedFrame();

    EXPECT_THROW({ closedDriver.writeAsync(frame); }, InvalidStateException);
}

// The synchronous transmit guard, which shares the same shape as its asynchronous
// sibling and is likewise reached before any HAL call.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl DriverImpl::write state guard.
TEST_F(DriverImplAsyncTest, WriteRejectedWhileDriverClosed) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecTx(_, _, _, _)).Times(0);

    DriverImpl closedDriver;
    CECFrame frame = directedFrame();

    EXPECT_THROW({ closedDriver.write(frame); }, InvalidStateException);
}

// Address management is guarded too: claiming a logical address on a closed driver
// must fail before the HAL is asked to reserve anything, otherwise the driver's
// bookkeeping and the HAL's would diverge.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl
// DriverImpl::addLogicalAddress state guard.
TEST_F(DriverImplAsyncTest, AddLogicalAddressRejectedWhileDriverClosed) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecAddLogicalAddress(_, _)).Times(0);

    DriverImpl closedDriver;
    LogicalAddress address(LogicalAddress::PLAYBACK_DEVICE_1);

    EXPECT_THROW({ closedDriver.addLogicalAddress(address); }, InvalidStateException);
}

// The release half of address management, guarded identically.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl
// DriverImpl::removeLogicalAddress state guard.
TEST_F(DriverImplAsyncTest, RemoveLogicalAddressRejectedWhileDriverClosed) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecRemoveLogicalAddress(_, _)).Times(0);

    DriverImpl closedDriver;
    LogicalAddress address(LogicalAddress::PLAYBACK_DEVICE_1);

    EXPECT_THROW({ closedDriver.removeLogicalAddress(address); }, InvalidStateException);
}

// Boundary sweep over the guard: every logical address the type admits, from the TV
// at one end to the broadcast/unregistered value at the other, must be rejected
// identically. The guard is a state check, so no address may slip past it.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl DriverImpl::addLogicalAddress
// and DriverImpl::removeLogicalAddress state guards. Boundary case: minimum,
// maximum and interior logical addresses.
TEST_F(DriverImplAsyncTest, AddressGuardsRejectEveryLogicalAddressWhileClosed) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecAddLogicalAddress(_, _)).Times(0);
    EXPECT_CALL(*mock, HdmiCecRemoveLogicalAddress(_, _)).Times(0);

    DriverImpl closedDriver;

    const int addresses[] = {
        LogicalAddress::TV,                // minimum
        LogicalAddress::PLAYBACK_DEVICE_1, // interior
        LogicalAddress::BROADCAST          // maximum / unregistered
    };

    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i++) {
        LogicalAddress address(addresses[i]);
        EXPECT_THROW({ closedDriver.addLogicalAddress(address); }, InvalidStateException)
            << "addLogicalAddress accepted address " << addresses[i] << " while closed";
        EXPECT_THROW({ closedDriver.removeLogicalAddress(address); }, InvalidStateException)
            << "removeLogicalAddress accepted address " << addresses[i] << " while closed";
    }
}

// The frame-cleanup handler in the receive callback, and the incoming-queue guard
// that triggers it.
//
// The receive callback allocates a frame, then hands it to the incoming queue. When
// the driver is not open the queue refuses it, and the callback's cleanup handler
// releases the frame it had allocated - the guard against leaking one frame per
// rejected message. Neither the handler nor the queue's own guard had ever executed.
//
// This is the one case in this file that cannot use a local instance: the receive
// callback is a static function that resolves the driver through Driver::getInstance()
// and the queue it consults is private, so the shared instance is the only route.
// It therefore performs a single terminate/initialise cycle and uses non-fatal
// expectations throughout, so the restoration below is reached even if an
// expectation fails part-way.
//
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl
// DriverImpl::DriverReceiveCallback cleanup handler + DriverImpl::getIncomingQueue
// state guard; rank 1 #gap-flow-a, whose inbound chain begins at this callback.
TEST_F(DriverImplAsyncTest, ReceiveCallbackDiscardsFrameWhenIncomingQueueRejectsIt) {
    ASSERT_NE(mock, nullptr);
    // The mock forwards only to a callback it has captured; without one the
    // injections below would be silent no-ops and this case would prove nothing.
    ASSERT_NE(mock->rxCallback, nullptr) << "open() must have registered a receive callback";

    LibCCEC &lib = LibCCEC::getInstance();

    // Bring the shared library and driver to a coherent closed state. term() reports
    // an invalid state if the library was never initialised, in which case an
    // initialise/terminate pair reaches the same place.
    try {
        lib.term();
    } catch (const InvalidStateException &) {
        EXPECT_NO_THROW({ lib.init("CEC_TEST"); });
        EXPECT_NO_THROW({ lib.term(); });
    }

    // A real HAL invokes this callback from its own thread and has no way to handle
    // an exception, so the contract under test is that nothing escapes - the frame
    // is released and the rejection is swallowed. The callback is exercised across
    // the inbound length boundaries to show the cleanup is not length-dependent.
    const unsigned char directed[] = { 0x40, 0x36 };
    EXPECT_NO_THROW({ mock->injectReceivedMessage(directed, sizeof(directed)); });

    const unsigned char single[] = { 0x4F };
    EXPECT_NO_THROW({ mock->injectReceivedMessage(single, sizeof(single)); });   // minimum
    EXPECT_NO_THROW({ mock->injectReceivedMessage(single, 0); });                // empty

    unsigned char maximal[CECFrame::MAX_LENGTH];                                 // maximum
    for (size_t i = 0; i < sizeof(maximal); i++) {
        maximal[i] = static_cast<unsigned char>(i & 0xFF);
    }
    maximal[0] = 0x40;
    EXPECT_NO_THROW({
        mock->injectReceivedMessage(maximal, static_cast<int>(sizeof(maximal)));
    });

    // Restore the state the global test environment established, then prove the
    // driver genuinely recovered rather than merely stopped throwing. Recovery is
    // demonstrated with a transmit rather than another injection: an injected frame
    // would be delivered to whatever frame listeners sibling suites have registered
    // on the shared bus, and this case must not perturb them.
    EXPECT_NO_THROW({ lib.init("CEC_TEST"); });

    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_SUCCESS));

    CECFrame frame = directedFrame();
    EXPECT_NO_THROW({ Driver::getInstance().writeAsync(frame); });

    ::testing::Mock::VerifyAndClearExpectations(mock);
}

/* ---------------------------------------------------------------------------------
 * Destructor.
 *
 * Driver::getInstance() resolves to a function-local static, so the shared driver's
 * destructor runs only at static destruction and cannot be observed from a test.
 * A local instance is the only way to reach it. Each case below scopes its instance
 * explicitly so the destructor runs while the mock expectations are still live.
 *
 * A local instance is well behaved: opening it re-registers the very same static
 * callback functions the shared instance registered, so the captured callbacks are
 * unchanged, and closing it drains its own queue rather than the shared one. The
 * queue's own "elements left in queue" diagnostic on stdout during these cases is
 * that container reporting its sentinel, not a failure.
 * --------------------------------------------------------------------------------- */

// A driver that was never opened must not attempt to close on destruction - closing
// a handle that was never acquired would be an error against the HAL.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl DriverImpl::~DriverImpl
// (already-closed arm).
TEST_F(DriverImplAsyncTest, DestructorOfNeverOpenedInstanceSkipsClose) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecClose(_)).Times(0);

    {
        DriverImpl neverOpened;

        // Confirms the instance really is closed going into destruction, so the
        // expectation above is meaningful rather than vacuous.
        CECFrame frame = directedFrame();
        EXPECT_THROW({ neverOpened.writeAsync(frame); }, InvalidStateException);
    }

    ::testing::Mock::VerifyAndClearExpectations(mock);
}

// An open driver must release its HAL handle on destruction; leaking it would strand
// the handle for the lifetime of the process.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl DriverImpl::~DriverImpl
// (open arm, delegating to DriverImpl::close).
TEST_F(DriverImplAsyncTest, DestructorClosesOpenedInstance) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecClose(_))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_SUCCESS));
    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_SUCCESS));

    {
        DriverImpl opened;
        EXPECT_NO_THROW({ opened.open(); });

        // Confirms the instance really is open going into destruction.
        CECFrame frame = directedFrame();
        EXPECT_NO_THROW({ opened.writeAsync(frame); });
    }

    // The destructor has run by now, so the close expectation is satisfied here.
    ::testing::Mock::VerifyAndClearExpectations(mock);
}

// The destructor's own error handler. A destructor must not propagate an exception,
// so when the HAL refuses the close the driver has to absorb the resulting failure
// rather than let it escape during stack unwinding.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl DriverImpl::~DriverImpl
// exception handler, reached by making DriverImpl::close fail.
TEST_F(DriverImplAsyncTest, DestructorSwallowsCloseFailure) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecClose(_))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_GENERAL_ERROR));

    // The instance is constructed, opened and destroyed inside the guarded block, so
    // the assertion covers destruction as well as construction.
    EXPECT_NO_THROW({
        DriverImpl failingClose;
        failingClose.open();
    });

    ::testing::Mock::VerifyAndClearExpectations(mock);
}
