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

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "ccec/Driver.hpp"
#include "ccec/Exception.hpp"
#include "ccec/Connection.hpp"
#include "ccec/LibCCEC.hpp"
#include "hdmi_cec_driver_mock.h"

using ::testing::_;
using ::testing::Return;
using ::testing::DoAll;
using ::testing::SetArgPointee;

namespace {

/*
 * Remove the sentinel that the close() inside LibCCEC::term() leaves on the driver's receive
 * queue, before the matching init() puts a fresh Bus reader behind it.
 *
 * DriverImpl::close() offers a NULL sentinel to that queue and DriverImpl::read() is what
 * consumes it - but Bus::stop() only waits for the reader to leave its loop, and the reader can
 * leave without having come back for the sentinel.  A later close() then queues a SECOND one, and
 * read()'s flush arm - `while (rQueue.size() > 0) { inFrame = rQueue.poll(); frame = *inFrame; }` -
 * dereferences it: CCEC_OSAL::EventQueue guards its element count and its condition variable with
 * DIFFERENT mutexes, so poll() can return the default-constructed null for a queue that size() has
 * just reported as non-empty.  Measured on this tree: a segmentation fault in the reader thread in
 * roughly 1 full-suite run in 60 once any case re-initialises the shared library mid-run.
 *
 * BLOCKED - REQUIRED PRODUCTION CHANGE, REPORTED NOT MADE (Directive 6): DriverImpl::read() must
 * null-check inFrame in its flush arm, or EventQueue must publish its element count and its
 * condition under one lock.  Until one of those lands, a case that cycles the shared library takes
 * the sentinel off the queue itself.  read() is public, the driver is closed, so the call drains
 * the queue and then reports the invalid state; it is safe from this thread because term() has
 * already waited for the reader to leave its loop, so there is no second consumer.  The same
 * helper and the same reasoning appear in ccec/test_LibCCEC.cpp - each translation unit keeps its
 * own copy because this suite has no shared test-utility target and adding one would be harness
 * architecture no gap asks for.
 */
void drainClosedDriverQueue()
{
    CECFrame frame;
    try {
        Driver::getInstance().read(frame);
    } catch (const InvalidStateException &) {
        // The expected outcome: the driver is closed, and the queue is now empty.
    }
}

} // namespace

class DriverTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clear any lingering mock expectations
        HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
        if (mock != nullptr) {
            ::testing::Mock::VerifyAndClearExpectations(mock);
        }
        
        // Driver should already be open from global environment
        // Don't force open here as it causes conflicts
    }

    void TearDown() override {
        // Clear mock expectations after each test
        HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
        if (mock != nullptr) {
            ::testing::Mock::VerifyAndClearExpectations(mock);
        }
        
        // Leave driver state as-is for next test
        // Global environment will clean up at the end
    }
};

// Test driver singleton access - runs first alphabetically
TEST_F(DriverTest, AAA_DriverSingletonAccess) {
    EXPECT_NO_THROW({
        Driver &driver = Driver::getInstance();
        (void)driver; // Suppress unused variable warning
    });
}

// Test driver open (already opened by global environment)
TEST_F(DriverTest, DriverAlreadyOpen) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    if (mock == nullptr) {
        GTEST_SKIP() << "Mock is nullptr - test environment not initialized";
    }
    Driver &driver = Driver::getInstance();
    
    // Opening again should not throw (handled gracefully)
    EXPECT_NO_THROW({
        driver.open();
        driver.open(); // Second open
    });

    // Close to restore state - set up mock expectation
    EXPECT_CALL(*mock, HdmiCecClose(::testing::_))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_SUCCESS));
    
    EXPECT_NO_THROW({
        driver.close();
    });

    ::testing::Mock::VerifyAndClearExpectations(mock);
    
    // Reopen for subsequent tests
    driver.open();
}

TEST_F(DriverTest, CloseAndReopen) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    if (mock == nullptr) {
        GTEST_SKIP() << "Mock is nullptr - test environment not initialized";
    }
    
    Driver &driver = Driver::getInstance();
    
    // Ensure we start in a good state
    try {
        driver.open();
    } catch (...) {
        // Already open, that's fine
    }
    
    // Set up mock for close
    EXPECT_CALL(*mock, HdmiCecClose(::testing::_))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_SUCCESS));
    
    // Close the driver
    EXPECT_NO_THROW({
        driver.close();
    });
    
    ::testing::Mock::VerifyAndClearExpectations(mock);
    
    // Reopen it
    EXPECT_NO_THROW({
        driver.open();
    });
    
    // Verify it works by doing a simple operation
    EXPECT_NO_THROW({
        driver.open(); // Should handle gracefully
    });
    
    // Leave driver in OPENED state for next test
}

// Test multiple close calls
TEST_F(DriverTest, MultipleClose) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    if (mock == nullptr) {
        GTEST_SKIP() << "Mock is nullptr - test environment not initialized";
    }
    
    Driver &driver = Driver::getInstance();

    // Ensure we start in a good state
    try {
        driver.open();
    } catch (...) {
        // Already open, that's fine
    }
    
    // Set up mock for first close
    EXPECT_CALL(*mock, HdmiCecClose(::testing::_))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_SUCCESS));
    
    // First close
    EXPECT_NO_THROW({
        driver.close();
    });
    
    ::testing::Mock::VerifyAndClearExpectations(mock);
    
    // Second close should not throw (handled gracefully - driver already closed)
    EXPECT_NO_THROW({
        driver.close();
    });
    
    // Restore state - reopen the driver for next test
    EXPECT_NO_THROW({
        driver.open();
    });
}

// Test getLogicalAddress
TEST_F(DriverTest, GetLogicalAddress) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    if (mock == nullptr) {
        GTEST_SKIP() << "Mock is nullptr - test environment not initialized";
    }
    Driver &driver = Driver::getInstance();
    
    try {
        driver.open();
    } catch (...) {
        // If this fails, skip the test
        GTEST_SKIP() << "Driver not in valid state for this test";
    }
    
    // Set up mock to return a logical address
    EXPECT_CALL(*mock, HdmiCecGetLogicalAddress(_, _))
        .Times(1)
        .WillOnce(DoAll(
            SetArgPointee<1>(4), // Return logical address 4
            Return(HDMI_CEC_IO_SUCCESS)
        ));
    
    int logicalAddr = -1;
    EXPECT_NO_THROW({
        logicalAddr = driver.getLogicalAddress(0);
    });
    EXPECT_EQ(logicalAddr, 4);
    
    // Clear mock expectations
    ::testing::Mock::VerifyAndClearExpectations(mock);
}

// Test getPhysicalAddress
TEST_F(DriverTest, GetPhysicalAddress) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    if (mock == nullptr) {
        GTEST_SKIP() << "Mock is nullptr - test environment not initialized";
    }
    Driver &driver = Driver::getInstance();
    
    // Set up mock to return a physical address
    EXPECT_CALL(*mock, HdmiCecGetPhysicalAddress(_, _))
        .Times(1)
        .WillOnce(DoAll(
            SetArgPointee<1>(0x1000), // Return physical address 1.0.0.0
            Return(HDMI_CEC_IO_SUCCESS)
        ));
    
    unsigned int physicalAddr = 0;
    EXPECT_NO_THROW({
        driver.getPhysicalAddress(&physicalAddr);
    });
    EXPECT_EQ(physicalAddr, 0x1000u);
    
    // Clear mock expectations
    ::testing::Mock::VerifyAndClearExpectations(mock);
}

// Test addLogicalAddress success
TEST_F(DriverTest, AddLogicalAddressSuccess) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    if (mock == nullptr) {
        GTEST_SKIP() << "Mock is nullptr - test environment not initialized";
    }
    Driver &driver = Driver::getInstance();
    
    // Set up mock to succeed
    EXPECT_CALL(*mock, HdmiCecAddLogicalAddress(_, _))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_SUCCESS));
    
    LogicalAddress addr(LogicalAddress::PLAYBACK_DEVICE_2);
    bool result = false;
    
    EXPECT_NO_THROW({
        result = driver.addLogicalAddress(addr);
    });
    EXPECT_TRUE(result);
    
    ::testing::Mock::VerifyAndClearExpectations(mock);
    
    // Clean up - remove the address we added
    EXPECT_CALL(*mock, HdmiCecRemoveLogicalAddress(_, _))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_SUCCESS));
    
    driver.removeLogicalAddress(addr);
    
    // Clear mock expectations
    ::testing::Mock::VerifyAndClearExpectations(mock);
}

// Test addLogicalAddress - address unavailable
TEST_F(DriverTest, AddLogicalAddressUnavailable) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    if (mock == nullptr) {
        GTEST_SKIP() << "Mock is nullptr - test environment not initialized";
    }
    Driver &driver = Driver::getInstance();
    
    // Set up mock to return unavailable
    EXPECT_CALL(*mock, HdmiCecAddLogicalAddress(_, _))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_LOGICALADDRESS_UNAVAILABLE));
    
    LogicalAddress addr(LogicalAddress::RECORDING_DEVICE_1);
    
    EXPECT_THROW({
        driver.addLogicalAddress(addr);
    }, AddressNotAvailableException);
    
    // Clear mock expectations
    ::testing::Mock::VerifyAndClearExpectations(mock);
}

// Test addLogicalAddress - general error
TEST_F(DriverTest, AddLogicalAddressGeneralError) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    if (mock == nullptr) {
        GTEST_SKIP() << "Mock is nullptr - test environment not initialized";
    }
    Driver &driver = Driver::getInstance();
    
    // Set up mock to return general error
    EXPECT_CALL(*mock, HdmiCecAddLogicalAddress(_, _))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_GENERAL_ERROR));
    
    LogicalAddress addr(LogicalAddress::TUNER_1);
    
    EXPECT_THROW({
        driver.addLogicalAddress(addr);
    }, IOException);
    
    // Clear mock expectations
    ::testing::Mock::VerifyAndClearExpectations(mock);
}

// Test removeLogicalAddress
TEST_F(DriverTest, RemoveLogicalAddress) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    if (mock == nullptr) {
        GTEST_SKIP() << "Mock is nullptr - test environment not initialized";
    }
    Driver &driver = Driver::getInstance();
    
    // First add a logical address
    EXPECT_CALL(*mock, HdmiCecAddLogicalAddress(_, _))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_SUCCESS));
    
    LogicalAddress addr(LogicalAddress::PLAYBACK_DEVICE_3);
    driver.addLogicalAddress(addr);
    
    ::testing::Mock::VerifyAndClearExpectations(mock);
    
    // Now remove it
    EXPECT_CALL(*mock, HdmiCecRemoveLogicalAddress(_, _))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_SUCCESS));
    
    EXPECT_NO_THROW({
        driver.removeLogicalAddress(addr);
    });
    
    // Clear mock expectations
    ::testing::Mock::VerifyAndClearExpectations(mock);
}

// Test isValidLogicalAddress - address is valid
TEST_F(DriverTest, IsValidLogicalAddressTrue) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    if (mock == nullptr) {
        GTEST_SKIP() << "Mock is nullptr - test environment not initialized";
    }
    Driver &driver = Driver::getInstance();
    
    // Add a logical address
    EXPECT_CALL(*mock, HdmiCecAddLogicalAddress(_, _))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_SUCCESS));
    
    LogicalAddress addr(LogicalAddress::AUDIO_SYSTEM);
    driver.addLogicalAddress(addr);
    
    ::testing::Mock::VerifyAndClearExpectations(mock);
    
    // Check if it's valid
    EXPECT_TRUE(driver.isValidLogicalAddress(addr));
    
    // Clean up
    EXPECT_CALL(*mock, HdmiCecRemoveLogicalAddress(_, _))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_SUCCESS));
    
    driver.removeLogicalAddress(addr);
    
    ::testing::Mock::VerifyAndClearExpectations(mock);
}

// Test isValidLogicalAddress - address is not valid
TEST_F(DriverTest, IsValidLogicalAddressFalse) {
    Driver &driver = Driver::getInstance();
    
    LogicalAddress addr(LogicalAddress::TUNER_4);
    
    // This address was never added, so it should not be valid
    EXPECT_FALSE(driver.isValidLogicalAddress(addr));
}

// Test write with NACK for non-broadcast
TEST_F(DriverTest, WriteWithNackNonBroadcast) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    Driver &driver = Driver::getInstance();
    
    // Set up mock to return NACK
    EXPECT_CALL(*mock, HdmiCecTx(_, _, _, _))
        .Times(1)
        .WillOnce(DoAll(
            SetArgPointee<3>(HDMI_CEC_IO_SENT_BUT_NOT_ACKD),
            Return(HDMI_CEC_IO_SUCCESS)
        ));
    
    CECFrame frame;
    frame.append(0x40); // Non-broadcast (to = 0, not F)
    frame.append(0x36);
    
    // Should throw NACK exception
    EXPECT_THROW({
        driver.write(frame);
    }, CECNoAckException);
    
    // Clear mock expectations
    ::testing::Mock::VerifyAndClearExpectations(mock);
}

// Test write with NACK for broadcast REPORT_PHYSICAL_ADDRESS
TEST_F(DriverTest, WriteWithNackBroadcastReportPhysicalAddress) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    Driver &driver = Driver::getInstance();
    
    // Set up mock to return NACK
    EXPECT_CALL(*mock, HdmiCecTx(_, _, _, _))
        .Times(1)
        .WillOnce(DoAll(
            SetArgPointee<3>(HDMI_CEC_IO_SENT_BUT_NOT_ACKD),
            Return(HDMI_CEC_IO_SUCCESS)
        ));
    
    CECFrame frame;
    frame.append(0x4F); // Broadcast (to = F)
    frame.append(0x84); // REPORT_PHYSICAL_ADDRESS opcode
    frame.append(0x10);
    frame.append(0x00);
    
    // Should throw NACK exception for broadcast report physical address
    EXPECT_THROW({
        driver.write(frame);
    }, CECNoAckException);
    
    // Clear mock expectations
    ::testing::Mock::VerifyAndClearExpectations(mock);
}

// Test write with sendResult errors
TEST_F(DriverTest, WriteWithSendResultErrors) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    Driver &driver = Driver::getInstance();
    
    int errorCodes[] = {
        HDMI_CEC_IO_INVALID_HANDLE,
        HDMI_CEC_IO_INVALID_ARGUMENT,
        HDMI_CEC_IO_LOGICALADDRESS_UNAVAILABLE,
        HDMI_CEC_IO_SENT_FAILED,
        HDMI_CEC_IO_GENERAL_ERROR
    };
    
    for (int errorCode : errorCodes) {
        EXPECT_CALL(*mock, HdmiCecTx(_, _, _, _))
            .Times(1)
            .WillOnce(DoAll(
                SetArgPointee<3>(errorCode),
                Return(HDMI_CEC_IO_SUCCESS)
            ));
        
        CECFrame frame;
        frame.append(0x40);
        frame.append(0x36);
        
        // Should throw IOException
        EXPECT_THROW({
            driver.write(frame);
        }, IOException);
        
        ::testing::Mock::VerifyAndClearExpectations(mock);
    }
}

// Test write with HdmiCecTx failure
TEST_F(DriverTest, WriteWithHdmiCecTxFailure) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    Driver &driver = Driver::getInstance();
    
    // Set up mock to fail
    EXPECT_CALL(*mock, HdmiCecTx(_, _, _, _))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_GENERAL_ERROR));
    
    CECFrame frame;
    frame.append(0x40);
    frame.append(0x36);
    
    // Should throw IOException
    EXPECT_THROW({
        driver.write(frame);
    }, IOException);
    
    // Clear mock expectations
    ::testing::Mock::VerifyAndClearExpectations(mock);
}

// Test open with HdmiCecOpen failure - runs late to avoid breaking other tests
TEST_F(DriverTest, ZZZ_OpenWithFailure) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    Driver &driver = Driver::getInstance();
    
    // Close first
    driver.close();
    
    // Set up mock to fail on open
    EXPECT_CALL(*mock, HdmiCecOpen(_))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_GENERAL_ERROR));
    
    // Should throw IOException
    EXPECT_THROW({
        driver.open();
    }, IOException);
    
    ::testing::Mock::VerifyAndClearExpectations(mock);
    
    // Restore state - successfully reopen the driver
    EXPECT_NO_THROW({
        driver.open();
    });
}

// Test close with HdmiCecClose failure - runs late to avoid breaking other tests
TEST_F(DriverTest, ZZZ_CloseWithFailure) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    if (mock == nullptr) {
        GTEST_SKIP() << "Mock is nullptr - test environment not initialized";
    }
    
    Driver &driver = Driver::getInstance();
    
    // Set up mock to fail on close
    EXPECT_CALL(*mock, HdmiCecClose(::testing::_))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_GENERAL_ERROR));
    
    // Should throw IOException
    EXPECT_THROW({
        driver.close();
    }, IOException);
    
    ::testing::Mock::VerifyAndClearExpectations(mock);
    
    // Restore state - after failed close, driver is in undefined state
    // Try to close again (should be no-op or succeed)
    try {
        driver.close();
    } catch (...) {
        // Ignore - driver might already be closed or in bad state
    }
    

}

// Test printFrameDetails with various frames
TEST_F(DriverTest, PrintFrameDetails) {
    
    Driver &driver = Driver::getInstance();

    EXPECT_NO_THROW({
        driver.open();
    });
    
    // Test with normal frame
    CECFrame frame1;
    frame1.append(0x40);
    frame1.append(0x36);
    
    EXPECT_NO_THROW({
        driver.printFrameDetails(frame1);
    });
    
    // Test with longer frame
    CECFrame frame2;
    frame2.append(0x4F);
    frame2.append(0x82);
    frame2.append(0x10);
    frame2.append(0x00);
    frame2.append(0x04);
    
    EXPECT_NO_THROW({
        driver.printFrameDetails(frame2);
    });
    
    // Test with single byte frame (poll)
    CECFrame frame3;
    frame3.append(0x44);
    
    EXPECT_NO_THROW({
        driver.printFrameDetails(frame3);
    });

    EXPECT_NO_THROW({
        driver.close();
    });


}

// Test poll through driver
TEST_F(DriverTest, PollAddress) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    
    Driver &driver = Driver::getInstance();
    
    // Ensure driver is open - set up expectations if needed
    EXPECT_NO_THROW({
        driver.open();
    });
    
    // Set up mock for HdmiCecTx which poll()->write() will call
    EXPECT_CALL(*mock, HdmiCecTx(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(DoAll(
            SetArgPointee<3>(HDMI_CEC_IO_SENT_AND_ACKD),
            Return(HDMI_CEC_IO_SUCCESS)
        ));
    
    LogicalAddress from(LogicalAddress::PLAYBACK_DEVICE_1);
    LogicalAddress to(LogicalAddress::PLAYBACK_DEVICE_1);
    
    EXPECT_NO_THROW({
        driver.poll(from, to);
    });
    
    ::testing::Mock::VerifyAndClearExpectations(mock);
    EXPECT_NO_THROW({
        driver.close();
    });
}

TEST_F(DriverTest, WriteAsync) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    if (mock == nullptr) {
        GTEST_SKIP() << "Mock is nullptr - test environment not initialized";
    }

    Driver &driver = Driver::getInstance();

    // Own the HdmiCecOpen default action rather than inheriting one, BEFORE opening.
    //
    // HdmiCecDriverMockTest::TearDown() (ccec/test_Driver_Mock.cpp:53) installs an ON_CALL default
    // action on the PROCESS-GLOBAL driver mock whose lambda captures that fixture's `this` and
    // reads `mock->currentHandle` through it.  The action is stored on the mock, which outlives the
    // fixture, so once that fixture has been destroyed any later HdmiCecOpen call performs a lambda
    // that dereferences released storage.  Measured: with the default action inherited, the full
    // suite under `--gtest_shuffle --gtest_random_seed=12345` SEGFAULTs here, in
    // DriverImpl::open -> HdmiCecOpen, with the faulting frame being that TearDown lambda.
    // test_Driver_Mock.cpp is a pre-existing passing file this pass must not modify, so the fix
    // belongs here: gmock performs the LAST matching ON_CALL, so re-stating the default in this
    // test displaces the dangling one and makes the test self-sufficient rather than dependent on
    // whichever fixture happened to run before it.
    ON_CALL(*mock, HdmiCecOpen(_))
        .WillByDefault(DoAll(SetArgPointee<0>(mock->currentHandle), Return(HDMI_CEC_IO_SUCCESS)));

    // Guard against preceding tests that call driver.close() directly
    // while leaving LibCCEC::initialized == true (e.g. PollAddress, PrintFrameDetails)
    try { driver.open(); } catch (...) { /* already open, fine */ }

    // Set up mock for async write
    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_SUCCESS));
    
    CECFrame frame;
    frame.append(0x40);
    frame.append(0x36);
    
    EXPECT_NO_THROW({
        driver.writeAsync(frame);
    });

    // Tear down and restart via LibCCEC so Bus threads are properly stopped
    // before the driver is closed, avoiding a race condition/segfault.
    EXPECT_NO_THROW({ LibCCEC::getInstance().term(); });
    drainClosedDriverQueue();
    EXPECT_NO_THROW({ LibCCEC::getInstance().init("CEC_TEST"); });

    // Clear mock expectations
    ::testing::Mock::VerifyAndClearExpectations(mock);
}

TEST_F(DriverTest, WriteAsyncWithFailure) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    if (mock == nullptr) {
        GTEST_SKIP() << "Mock is nullptr - test environment not initialized";
    }

    Driver &driver = Driver::getInstance();

    // Same reason as DriverTest.WriteAsync above: displace the dangling HdmiCecOpen default action
    // that ccec/test_Driver_Mock.cpp's TearDown leaves on the process-global mock, so this test
    // opens the driver through an action it owns.
    ON_CALL(*mock, HdmiCecOpen(_))
        .WillByDefault(DoAll(SetArgPointee<0>(mock->currentHandle), Return(HDMI_CEC_IO_SUCCESS)));

    // Guard against preceding tests that call driver.close() directly
    // while leaving LibCCEC::initialized == true (e.g. PollAddress, PrintFrameDetails)
    try { driver.open(); } catch (...) { /* already open, fine */ }

    // Set up mock to fail
    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_GENERAL_ERROR));
    
    CECFrame frame;
    frame.append(0x40);
    frame.append(0x36);
    
    EXPECT_THROW({
        Driver::getInstance().writeAsync(frame);
    }, IOException);

    // Tear down and restart via LibCCEC so Bus threads are properly stopped
    // before the driver is closed, avoiding a race condition/segfault.
    EXPECT_NO_THROW({ LibCCEC::getInstance().term(); });
    drainClosedDriverQueue();
    EXPECT_NO_THROW({ LibCCEC::getInstance().init("CEC_TEST"); });

    // Clear mock expectations
    ::testing::Mock::VerifyAndClearExpectations(mock);
}

// DriverTest deliberately carries no "writeAsync before open" case.
//
// Reaching DriverImpl::writeAsync's not-open guard through Driver::getInstance() would mean
// terminating the shared library and restarting it, and a term() issued shortly after an init()
// can lose the Bus reader's RUNNING/STOPPING transition - a production defect in
// CCEC_OSAL::Stoppable and CCEC::Bus that a test cannot repair and must not provoke (measured on
// this tree: a case that cycled the shared library here produced 2 aborted runs in 60, one hang
// and one crash inside the reader thread).  The same guard is covered instead by
// ccec/test_DriverImpl_Async.cpp::DriverImplAsyncTest.WriteAsyncRejectedWhileDriverClosed, which
// drives a LOCAL DriverImpl instance and so needs no shared-library cycle at all; the address
// guards next to it are covered the same way.  Same production lines, no shared state touched.
