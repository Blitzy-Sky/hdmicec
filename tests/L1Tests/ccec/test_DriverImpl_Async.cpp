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
 * The asynchronous path and the transmit-completion callback are driven through the
 * existing HAL mock: failure-return injection reaches the error arms, and the mock's
 * transmit-result helper invokes the callback the driver registers during open().
 * Invoking that callback directly instead of waiting on hardware keeps every case
 * deterministic and free of wall-clock waits.
 *
 * ISOLATION: this suite shares one process-global driver with every other suite in
 * the binary, and the binary is order-sensitive. Cases needing an open driver
 * establish that precondition themselves, leave the driver open again - the state
 * the global environment sets up at start-up - and clear their own mock
 * expectations. Cases needing a driver that is NOT open construct a local instance
 * instead of disturbing the shared one; exactly one case cycles the shared library,
 * because it has no alternative.
 *
 * TWO PATHS ARE UNREACHABLE FROM A TEST-ONLY CHANGE and are deliberately not chased:
 *
 * (1) The body of printFrameDetails' catch(Exception &e). The only statement in the
 *     guarded try that can throw is the Header construction, which reads
 *     frame.at(0) and raises std::out_of_range. CCEC's Exception and
 *     std::out_of_range are siblings under std::exception, so catch(Exception &e)
 *     cannot catch it and the exception escapes instead. Reaching the handler needs
 *     a production change (a broader handler, or a length guard before the Header).
 *     A case below asserts that std::out_of_range escapes, so that boundary is
 *     pinned and any future change to the handler is a visible decision.
 *
 * (2) The flush arm inside read()'s "status is no longer OPENED" branch. read()
 *     checks the status before entering the loop and throws, so a read after close
 *     never reaches the arm; reaching it needs the status to leave OPENED while the
 *     queue is still non-empty, which is a genuine race between the Bus reader
 *     thread and a concurrent close.
 *     NOTHING IN THIS FILE MAY CALL read(): with an empty queue EventQueue::poll
 *     blocks by contract, so a read() here would either throw at once or hang the
 *     whole test binary.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "ccec/CECFrame.hpp"
#include "ccec/Driver.hpp"
#include "ccec/Exception.hpp"
#include "ccec/LibCCEC.hpp"

/*
 * DriverImpl.hpp lives under ccec/src, which AM_CPPFLAGS does not cover, so it is
 * reached by relative path rather than by adding an -I. It is needed for the two
 * things the abstract Driver interface cannot express: constructing a local driver
 * instance so its destructor actually runs (the process-wide instance is a
 * function-local static, whose destructor runs only at static destruction), and
 * reaching the invalid-state guards on a fresh instance without disturbing the
 * process-global driver every other suite in this binary shares.
 */
#include "../../../ccec/src/DriverImpl.hpp"

#include <stdexcept>
#include "hdmi_cec_driver_mock.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;

/*
 * ---------------------------------------------------------------------------------
 * HAL RESULT-CODE CONTRACT CHECK
 *
 * The authoritative HAL (rdk-halif-hdmi_cec/include/hdmi_cec_driver.h) numbers the
 * transmit result codes:
 *
 *     HDMI_CEC_IO_SUCCESS           = 0
 *     HDMI_CEC_IO_SENT_AND_ACKD     = 1     <- sent, and the peer acknowledged
 *     HDMI_CEC_IO_SENT_BUT_NOT_ACKD = 2
 *     HDMI_CEC_IO_SENT_FAILED       = 3
 *
 * The mock HAL header this suite compiles against (hdmicec/mocks/hdmicec/
 * hdmi_cec_driver.h) instead ALIASES SENT_AND_ACKD onto SUCCESS - both 0 - and
 * shifts everything below it down by one, so the mock's SENT_BUT_NOT_ACKD is 1,
 * exactly the value the real HAL uses for an acknowledged send.
 *
 * That drift conceals a production defect rather than a test defect. DriverImpl
 * compares transmit results against SUCCESS alone, in two places:
 *
 *     DriverImpl.cpp:82   DriverTransmitCallback:  if (HDMI_CEC_IO_SUCCESS != result)
 *     DriverImpl.cpp:265  write():                 if (sendResult != HDMI_CEC_IO_SUCCESS)
 *
 * Against the real HAL, an acknowledged transmit therefore does not compare equal to
 * SUCCESS and is handled on the failure path; worse, because the code was compiled
 * against a numbering in which 1 means SENT_BUT_NOT_ACKD, write() reports a
 * successfully acknowledged directed frame to its caller as CECNoAckException.
 *
 * BLOCKED PRODUCTION CHANGE - reported here, deliberately NOT made, because
 * Directive 6 puts production source entirely out of scope:
 *
 *     DriverImpl must accept an acknowledged completion as success in both places,
 *     e.g.  if (result != HDMI_CEC_IO_SUCCESS && result != HDMI_CEC_IO_SENT_AND_ACKD)
 *     and the equivalent guard in write(), with the mock HAL header then renumbered
 *     to match the authoritative one.
 *
 * The mock header is NOT renumbered here, on purpose: 26 cases in test_Connection.cpp
 * and 22 in test_Bus.cpp hand HDMI_CEC_IO_SENT_AND_ACKD back as write()'s result
 * out-parameter and pass only because it aliases to SUCCESS. Renumbering it would
 * turn 48 currently passing cases red and force those passing tests to be rewritten
 * to expect the defective behaviour - which Directive 5 forbids. The static assertions
 * below pin the drift instead, so it cannot change silently: align the mock header and
 * the build stops here, pointing at this analysis and at the characterization test
 * named below, which must be inverted at the same time.
 * ---------------------------------------------------------------------------------
 */
namespace {

// The authoritative HAL's values, named so the tests can speak about real-HAL
// semantics without depending on the drifted fixture spelling.
constexpr int kAuthoritativeSuccess = 0;
constexpr int kAuthoritativeSentAndAckd = 1;

static_assert(HDMI_CEC_IO_SUCCESS == kAuthoritativeSuccess,
    "Fixture HAL drift: SUCCESS must remain 0, which is what the authoritative HAL defines "
    "and what DriverImpl compares every transmit result against.");

static_assert(HDMI_CEC_IO_SENT_AND_ACKD == HDMI_CEC_IO_SUCCESS,
    "Fixture HAL drift changed: the mock HAL no longer aliases SENT_AND_ACKD onto SUCCESS. "
    "If the mock was aligned with the authoritative HAL, the BLOCKED production change described "
    "above must land with it, and AckedTransmitIsReportedAsUnacknowledged must be inverted to "
    "assert that an acknowledged transmit no longer raises CECNoAckException.");

static_assert(HDMI_CEC_IO_SENT_BUT_NOT_ACKD == kAuthoritativeSentAndAckd,
    "Fixture HAL drift changed: the mock's SENT_BUT_NOT_ACKD no longer collides with the "
    "authoritative SENT_AND_ACKD value (1). AckedTransmitIsReportedAsUnacknowledged depends on "
    "that collision to demonstrate the misclassification - revisit it together with the mock.");

} // namespace

class DriverImplAsyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock = HdmiCecDriverMock::getInstance();
        if (mock != nullptr) {
            ::testing::Mock::VerifyAndClearExpectations(mock);
        }

        // Establish this suite's own precondition instead of inheriting whatever
        // state the previously executed suite happened to leave behind.
        //
        // open() is deliberately NOT wrapped in catch(...). It returns silently when the
        // driver is already opened (DriverImpl::open, whose InvalidStateException throw is
        // compiled out), so the only exception it can raise is the IOException that means
        // the HAL - here the mock - refused to open. Swallowing that would leave the
        // process-global driver CLOSED while this suite carried on asserting against it,
        // and would hand every sibling suite in this binary a closed driver too. So it is
        // a fatal setup failure instead: the test body does not run, and TearDown still
        // gets its chance to put the shared state back.
        //
        // "open() returned normally" is itself the proof the driver is open: the function
        // either finds the status already OPENED or sets it, and throws in every other
        // case. No separate state query is needed, and none exists on the interface.
        ASSERT_NO_THROW({ Driver::getInstance().open(); })
            << "the shared driver could not be opened, so this suite's precondition does "
               "not hold; continuing would assert against a closed driver";
    }

    void TearDown() override {
        if (mock != nullptr) {
            ::testing::Mock::VerifyAndClearExpectations(mock);
        }

        // Restore the baseline the global environment set up, so no sibling suite
        // observes a closed driver because of anything done here. TearDown runs even when
        // the test body aborted on a fatal assertion or was skipped, which is what makes
        // this the reliable restoration point; a non-fatal expectation is used so that a
        // failure here is reported rather than cutting the rest of the cleanup short.
        EXPECT_NO_THROW({ Driver::getInstance().open(); })
            << "failed to restore the shared driver to its opened baseline";
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

// A single-byte frame is what a poll looks like on the wire.
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

// The frame-detail logging that runs before the HAL call is also on this path, so the
// maximum length exercises its formatting buffer as well as the transmit itself.
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

// Addressing lives in the header nibble, not in the transmit routine, so broadcast
// takes the identical path.
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

TEST_F(DriverImplAsyncTest, WriteAsyncThrowsIOExceptionOnHalGeneralError) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_GENERAL_ERROR));

    CECFrame frame = directedFrame();
    EXPECT_THROW({ Driver::getInstance().writeAsync(frame); }, IOException);
}

TEST_F(DriverImplAsyncTest, WriteAsyncThrowsIOExceptionOnHalInvalidArgument) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_INVALID_ARGUMENT));

    CECFrame frame = directedFrame();
    EXPECT_THROW({ Driver::getInstance().writeAsync(frame); }, IOException);
}

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
//
// HDMI_CEC_IO_SUCCESS is named explicitly rather than HDMI_CEC_IO_SENT_AND_ACKD. In this
// fixture the two constants are the same value, so passing the latter would exercise the
// success arm while appearing to test acknowledged-transmit handling - which is the
// contract drift described at the top of this file. Acknowledged-transmit handling is
// covered separately, and honestly, by AckedTransmitIsReportedAsUnacknowledged.
// Traceability: section 6.2 rank 9 #gap-hal-settxcallback HdmiCecSetTxCallback;
// rank 8 #gap-mw-driverimpl DriverImpl::DriverTransmitCallback. Positive result arm.
TEST_F(DriverImplAsyncTest, TransmitCompletionCallbackHandlesSuccessResult) {
    ASSERT_NE(mock, nullptr);
    ASSERT_NE(mock->txCallback, nullptr) << "open() must have registered a transmit callback";

    EXPECT_NO_THROW({ mock->simulateTxResult(HDMI_CEC_IO_SUCCESS); });
}

TEST_F(DriverImplAsyncTest, TransmitCompletionCallbackHandlesFailureResult) {
    ASSERT_NE(mock, nullptr);
    ASSERT_NE(mock->txCallback, nullptr) << "open() must have registered a transmit callback";

    EXPECT_NO_THROW({ mock->simulateTxResult(HDMI_CEC_IO_SENT_BUT_NOT_ACKD); });
    EXPECT_NO_THROW({ mock->simulateTxResult(HDMI_CEC_IO_SENT_FAILED); });
    EXPECT_NO_THROW({ mock->simulateTxResult(HDMI_CEC_IO_GENERAL_ERROR); });
}

// The completion callback fires asynchronously with respect to the transmit that
// triggered it, so it must also be safe immediately after a transmit.
TEST_F(DriverImplAsyncTest, TransmitCompletionCallbackAfterAsyncWrite) {
    ASSERT_NE(mock, nullptr);
    ASSERT_NE(mock->txCallback, nullptr) << "open() must have registered a transmit callback";

    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_SUCCESS));

    CECFrame frame = directedFrame();
    EXPECT_NO_THROW({ Driver::getInstance().writeAsync(frame); });
    EXPECT_NO_THROW({ mock->simulateTxResult(HDMI_CEC_IO_SUCCESS); });
}

// What the driver actually does with an ACKNOWLEDGED transmit, expressed in the
// authoritative HAL's numbering rather than in the fixture's drifted spelling.
//
// This is a characterization test of the BLOCKED production defect documented at the top
// of this file: it records the behaviour as it is, not as it should be. The real HAL
// reports an acknowledged send as 1; DriverImpl::write() compares the result against
// SUCCESS (0) alone and, because it was compiled against a numbering in which 1 means
// SENT_BUT_NOT_ACKD, raises CECNoAckException for a directed frame. In other words a
// successfully acknowledged message is reported to the caller as unacknowledged.
//
// When the BLOCKED production change lands - accepting SENT_AND_ACKD as success - this
// expectation must be inverted to EXPECT_NO_THROW. The static assertions above fail the
// build if the mock HAL is realigned without doing so, which is what keeps this pair
// honest instead of quietly asserting a bug forever.
// Traceability: section 6.2 rank 8 #gap-mw-driverimpl DriverImpl::write result handling.
TEST_F(DriverImplAsyncTest, AckedTransmitIsReportedAsUnacknowledged) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecTx(_, _, _, _))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<3>(kAuthoritativeSentAndAckd), Return(HDMI_CEC_IO_SUCCESS)));

    CECFrame frame = directedFrame();
    EXPECT_THROW({ Driver::getInstance().write(frame); }, CECNoAckException);
}

// The completion-callback side of the same defect. The callback has no return value and
// no observable side effect beyond its log line, so the only contract a test can hold it
// to is that it must not throw back into the HAL's calling thread - which it must not do
// for ANY result value, acknowledged or otherwise. The misclassification itself is
// therefore only visible in the log here; the caller-visible consequence is pinned by
// AckedTransmitIsReportedAsUnacknowledged above.
// Traceability: section 6.2 rank 9 #gap-hal-settxcallback HdmiCecSetTxCallback;
// rank 8 #gap-mw-driverimpl DriverImpl::DriverTransmitCallback, authoritative ACKD value.
TEST_F(DriverImplAsyncTest, TransmitCompletionCallbackToleratesAuthoritativeAckdResult) {
    ASSERT_NE(mock, nullptr);
    ASSERT_NE(mock->txCallback, nullptr) << "open() must have registered a transmit callback";

    EXPECT_NO_THROW({ mock->simulateTxResult(kAuthoritativeSentAndAckd); });
}

// printFrameDetails() runs on every transmit. It guards the opcode lookup on
// frame.length() > OPCODE_OFFSET, so a frame carrying only a header simply has no
// opcode decoded and the byte dump still completes - no failure arises to be handled.
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

// The guard must reject before the HAL is touched, hence the Times(0) expectation.
TEST_F(DriverImplAsyncTest, WriteAsyncRejectedWhileDriverClosed) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _)).Times(0);

    DriverImpl closedDriver;
    CECFrame frame = directedFrame();

    EXPECT_THROW({ closedDriver.writeAsync(frame); }, InvalidStateException);
}

TEST_F(DriverImplAsyncTest, WriteRejectedWhileDriverClosed) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecTx(_, _, _, _)).Times(0);

    DriverImpl closedDriver;
    CECFrame frame = directedFrame();

    EXPECT_THROW({ closedDriver.write(frame); }, InvalidStateException);
}

// The claim must fail before the HAL reserves anything, or the driver's bookkeeping
// and the HAL's would diverge.
TEST_F(DriverImplAsyncTest, AddLogicalAddressRejectedWhileDriverClosed) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecAddLogicalAddress(_, _)).Times(0);

    DriverImpl closedDriver;
    LogicalAddress address(LogicalAddress::PLAYBACK_DEVICE_1);

    EXPECT_THROW({ closedDriver.addLogicalAddress(address); }, InvalidStateException);
}

TEST_F(DriverImplAsyncTest, RemoveLogicalAddressRejectedWhileDriverClosed) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecRemoveLogicalAddress(_, _)).Times(0);

    DriverImpl closedDriver;
    LogicalAddress address(LogicalAddress::PLAYBACK_DEVICE_1);

    EXPECT_THROW({ closedDriver.removeLogicalAddress(address); }, InvalidStateException);
}

// The guard is a state check, not an address check, so no address may slip past it -
// swept from the TV at one end to the broadcast/unregistered value at the other.
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

// The receive callback allocates a frame and hands it to the incoming queue; a closed
// driver's queue refuses it and the callback's cleanup handler releases the allocation,
// which is what stops one frame leaking per rejected message.
//
// This is the one case here that cannot use a local instance: the receive callback is a
// static function resolving the driver through Driver::getInstance(), and the queue it
// consults is private, so the shared instance is the only route. It therefore performs
// a single terminate/initialise cycle and uses non-fatal expectations throughout, so
// the restoration below is reached even if an expectation fails part-way.
//
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
