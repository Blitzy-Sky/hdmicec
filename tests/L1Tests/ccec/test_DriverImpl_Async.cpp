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
 * the binary, and the suite is known to be order-sensitive. Every case therefore
 * establishes its own precondition (driver opened) rather than assuming one, leaves
 * the driver in that same opened state - the state the global test environment
 * establishes at start-up - and clears its own mock expectations.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "ccec/CECFrame.hpp"
#include "ccec/Driver.hpp"
#include "ccec/Exception.hpp"

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

// A failed asynchronous transmit must leave the driver usable: the next transmit
// still reaches the HAL.
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
TEST_F(DriverImplAsyncTest, TransmitCompletionCallbackHandlesSuccessResult) {
    ASSERT_NE(mock, nullptr);
    ASSERT_NE(mock->txCallback, nullptr) << "open() must have registered a transmit callback";

    EXPECT_NO_THROW({ mock->simulateTxResult(HDMI_CEC_IO_SENT_AND_ACKD); });
}

// The failure arm of the completion callback - the branch that logs the result code.
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
    EXPECT_NO_THROW({ mock->simulateTxResult(HDMI_CEC_IO_SENT_AND_ACKD); });
}

// printFrameDetails() runs on every transmit and must tolerate a frame too short to
// carry an opcode - it is expected to swallow the decode failure, not propagate it.
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
