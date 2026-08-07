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
#include <cstring>
#include <string>
#include "ccec/CCEC.hpp"
#include "ccec/LibCCEC.hpp"
#include "ccec/Exception.hpp"
#include "ccec/Operands.hpp"
#include "ccec/Driver.hpp"
#include "ccec/CECFrame.hpp"
#include "hdmi_cec_driver_mock.h"

// LibCCEC::init() is the only writer of the CEC log prefix, and the prefix is the only
// observable effect its name argument has - so the null-name branch under test is observable
// only here, and reading the prefix back is also how that branch is undone without a
// term()/init() cycle. The buffer is defined with external linkage in ccec/src/Util.cpp and
// LibCCEC.cpp declares it exactly this way, so this reads production state directly instead of
// inferring it.
CCEC_BEGIN_NAMESPACE
extern char _CEC_LOG_PREFIX[64];
CCEC_END_NAMESPACE

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

namespace {

// Guarantees the shared library is left initialized under the prefix the global test
// environment established, whatever happens to the test body - including a fatal
// assertion or an exception escaping mid-cycle.
class ScopedLibCcecRestore {
public:
    ScopedLibCcecRestore() {}

    ScopedLibCcecRestore(const ScopedLibCcecRestore &) = delete;
    ScopedLibCcecRestore &operator=(const ScopedLibCcecRestore &) = delete;

    ~ScopedLibCcecRestore()
    {
        // On the success path the body has already restored the library, and this guard
        // must then cost NOTHING: every extra term()/init() pair is another chance to
        // lose the Bus reader race described in the test below, so the guard probes
        // instead of unconditionally cycling.
        //
        // init() is a side-effect-free probe for "is the library initialized": it checks
        // that flag before touching any other state and throws InvalidStateException when
        // it is set. If the flag is clear the probe performs exactly the restore that was
        // needed anyway.
        bool wasInitialized = false;
        try {
            LibCCEC::getInstance().init("CEC_TEST");
        } catch (const InvalidStateException &) {
            wasInitialized = true;
        }

        // The only case still needing a full cycle is a body that aborted while a
        // different prefix was installed - which can only follow a reported failure.
        if (wasInitialized && (std::string(_CEC_LOG_PREFIX) != std::string("CEC_TEST"))) {
            try {
                LibCCEC::getInstance().term();
            } catch (const InvalidStateException &) {
                // Raced to terminated; the following init() still restores the prefix.
            }
            try {
                LibCCEC::getInstance().init("CEC_TEST");
            } catch (const InvalidStateException &) {
                // Raced to initialized; the prefix is correct either way.
            }
        }
    }
};
} // namespace


class LibCCECTest : public ::testing::Test {
protected:
    void SetUp() override {
        // LibCCEC may already be initialized by other test suites (DriverTest, etc.)
        // Try to initialize, but ignore InvalidStateException if already initialized
        try {
            LibCCEC::getInstance().init("TestCEC");
        } catch (const InvalidStateException&) {
            // Already initialized - this is fine
        }
        // No case in THIS fixture terminates the library, so an init() performed here is never
        // followed by a term() that could race the Bus reader it just started. The cases that do
        // need the uninitialised state live in LibCCECUninitializedTest below, which owns that
        // state at suite level for exactly that reason.
    }

    void TearDown() override {
        // Don't call term() here to avoid thread cleanup issues
        // LibCCEC will be cleaned up by the global test environment
    }
};

TEST_F(LibCCECTest, GetInstanceReturnsSingleton) {
    LibCCEC& instance1 = LibCCEC::getInstance();
    LibCCEC& instance2 = LibCCEC::getInstance();
    EXPECT_EQ(&instance1, &instance2);
}

TEST_F(LibCCECTest, InitWithValidName) {
    // LibCCEC is already initialized in SetUp; verify getInstance() is reachable.
    EXPECT_NO_THROW({
        LibCCEC& instance = LibCCEC::getInstance();
        (void)instance;
    });
}

TEST_F(LibCCECTest, InitThrowsWhenAlreadyInitialized) {
    LibCCEC& lib = LibCCEC::getInstance();
    
    // Already initialized in SetUp
    // Try to initialize again - should throw InvalidStateException
    EXPECT_THROW(lib.init("TestCEC2"), InvalidStateException);
}

TEST_F(LibCCECTest, AddLogicalAddressAfterInit) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    LibCCEC& lib = LibCCEC::getInstance();

    // Verify HdmiCecAddLogicalAddress is called with PLAYBACK_DEVICE_1 (4)
    EXPECT_CALL(*mock, HdmiCecAddLogicalAddress(_, LogicalAddress::PLAYBACK_DEVICE_1))
        .Times(1)
        .WillOnce(Return(HDMI_CEC_IO_SUCCESS));

    LogicalAddress addr(LogicalAddress::PLAYBACK_DEVICE_1);
    int result = 0;
    EXPECT_NO_THROW(result = lib.addLogicalAddress(addr));
    EXPECT_TRUE(result);

    ::testing::Mock::VerifyAndClearExpectations(mock);
}

TEST_F(LibCCECTest, GetLogicalAddressAfterInit) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    LibCCEC& lib = LibCCEC::getInstance();

    // Configure the mock to return a known logical address value (5 = AUDIO_SYSTEM)
    EXPECT_CALL(*mock, HdmiCecGetLogicalAddress(_, _))
        .Times(1)
        .WillOnce(Invoke([](int, int* addr) -> int {
            if (addr) *addr = LogicalAddress::AUDIO_SYSTEM; // 5
            return HDMI_CEC_IO_SUCCESS;
        }));

    int logicalAddr = 0;
    EXPECT_NO_THROW(logicalAddr = lib.getLogicalAddress(DeviceType::PLAYBACK_DEVICE));
    EXPECT_EQ(logicalAddr, LogicalAddress::AUDIO_SYSTEM);

    ::testing::Mock::VerifyAndClearExpectations(mock);
}


TEST_F(LibCCECTest, GetPhysicalAddressAfterInit) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    LibCCEC& lib = LibCCEC::getInstance();

    // Configure the mock to return a specific, known physical address
    const unsigned int expectedPhysAddr = 0x2100;
    EXPECT_CALL(*mock, HdmiCecGetPhysicalAddress(_, _))
        .Times(1)
        .WillOnce(Invoke([expectedPhysAddr](int, unsigned int* addr) -> int {
            if (addr) *addr = expectedPhysAddr;
            return HDMI_CEC_IO_SUCCESS;
        }));

    unsigned int physAddr = 0;
    EXPECT_NO_THROW(lib.getPhysicalAddress(&physAddr));
    EXPECT_EQ(physAddr, expectedPhysAddr);

    ::testing::Mock::VerifyAndClearExpectations(mock);
}


TEST_F(LibCCECTest, AddMultipleLogicalAddresses) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    LibCCEC& lib = LibCCEC::getInstance();

    // Each addLogicalAddress call must reach the driver with the correct address value
    EXPECT_CALL(*mock, HdmiCecAddLogicalAddress(_, LogicalAddress::PLAYBACK_DEVICE_1))
        .Times(1).WillOnce(Return(HDMI_CEC_IO_SUCCESS));
    EXPECT_CALL(*mock, HdmiCecAddLogicalAddress(_, LogicalAddress::PLAYBACK_DEVICE_2))
        .Times(1).WillOnce(Return(HDMI_CEC_IO_SUCCESS));
    EXPECT_CALL(*mock, HdmiCecAddLogicalAddress(_, LogicalAddress::AUDIO_SYSTEM))
        .Times(1).WillOnce(Return(HDMI_CEC_IO_SUCCESS));

    LogicalAddress addr1(LogicalAddress::PLAYBACK_DEVICE_1);
    LogicalAddress addr2(LogicalAddress::PLAYBACK_DEVICE_2);
    LogicalAddress addr3(LogicalAddress::AUDIO_SYSTEM);

    EXPECT_TRUE(lib.addLogicalAddress(addr1));
    EXPECT_TRUE(lib.addLogicalAddress(addr2));
    EXPECT_TRUE(lib.addLogicalAddress(addr3));

    ::testing::Mock::VerifyAndClearExpectations(mock);
}

TEST_F(LibCCECTest, GetLogicalAddressForDifferentDeviceTypes) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    LibCCEC& lib = LibCCEC::getInstance();

    // Note: DriverImpl::getLogicalAddress ignores devType and always calls HdmiCecGetLogicalAddress.
    // The three calls return distinct pre-configured values so we verify the pass-through.
    EXPECT_CALL(*mock, HdmiCecGetLogicalAddress(_, _))
        .Times(3)
        .WillOnce(Invoke([](int, int* a) -> int { if (a) *a = LogicalAddress::TV;             return HDMI_CEC_IO_SUCCESS; }))
        .WillOnce(Invoke([](int, int* a) -> int { if (a) *a = LogicalAddress::PLAYBACK_DEVICE_1; return HDMI_CEC_IO_SUCCESS; }))
        .WillOnce(Invoke([](int, int* a) -> int { if (a) *a = LogicalAddress::AUDIO_SYSTEM;   return HDMI_CEC_IO_SUCCESS; }));

    // getLogicalAddress throws InvalidStateException when the driver returns 0 (TV=0),
    // so we test the two non-zero values and handle TV separately.
    EXPECT_THROW(lib.getLogicalAddress(DeviceType::TV), InvalidStateException); // TV=0 triggers the guard

    int addr2 = 0;
    EXPECT_NO_THROW(addr2 = lib.getLogicalAddress(DeviceType::PLAYBACK_DEVICE));
    EXPECT_EQ(addr2, LogicalAddress::PLAYBACK_DEVICE_1); // 4

    int addr3 = 0;
    EXPECT_NO_THROW(addr3 = lib.getLogicalAddress(DeviceType::AUDIO_SYSTEM));
    EXPECT_EQ(addr3, LogicalAddress::AUDIO_SYSTEM); // 5

    ::testing::Mock::VerifyAndClearExpectations(mock);
}

/*
 * Fixture for the "called before init()" guard family - LibCCEC::term,
 * LibCCEC::addLogicalAddress, LibCCEC::getLogicalAddress and LibCCEC::getPhysicalAddress each
 * throw InvalidStateException when the library has not been initialised, and those four throw
 * arms are the invalid-state family this suite is required to cover.
 *
 * WHY THE STATE IS OWNED BY THE FIXTURE AND NOT BY THE BODIES. LibCCEC is a process singleton
 * and term() is its only route to the uninitialised state, so these cases have to terminate the
 * shared library.  A term() issued shortly after an init() can lose the Bus reader's
 * RUNNING/STOPPING transition: Bus::start() creates the reader thread, Bus::Reader::run() marks
 * itself RUNNING from inside that new thread, and Bus::Reader::stop() promotes only a RUNNING
 * reader to STOPPING - so a stop request issued before the reader was scheduled is dropped, the
 * reader then re-arms itself against a closed driver, and the suite hangs or crashes.  That
 * window is a defect in CCEC_OSAL::Stoppable and CCEC::Bus, both production source and both out
 * of scope for this pass, so these cases must not open it rather than time their way around it.
 *
 * The fixture therefore performs ONE term() for the whole suite and NO init() until the suite is
 * over.  SetUp() only ensures "terminated", which is already true for every case after the first,
 * so no case here ever pairs an init() with a following term(), and no case waits on anything.
 * TearDownTestSuite() restores the initialised state the rest of the binary expects; it is the
 * only init() in this fixture, it is the last thing the fixture does, and nothing in this
 * translation unit terminates after it.
 *
 * BLOCKED, REPORTED NOT FIXED: making a term() safe immediately after an init() needs a
 * production change - CCEC_OSAL::Stoppable must not let Bus::Reader::run() promote a reader that
 * has already been asked to stop (an explicit STARTING state, or a start that publishes RUNNING
 * from the calling thread).  Until that lands, no test can cycle the shared library rapidly, and
 * DriverTest.WriteAsync / WriteAsyncWithFailure remain the only cases that do so at all.
 */
class LibCCECUninitializedTest : public ::testing::Test {
protected:
    void SetUp() override {
        try {
            LibCCEC::getInstance().term();
            drainClosedDriverQueue();
        } catch (const InvalidStateException &) {
            // Already terminated by an earlier case in this fixture, which is the state wanted.
        }
    }

    /*
     * Remove the sentinel that the close() inside term() leaves on the driver's receive queue.
     *
     * DriverImpl::close() offers a NULL sentinel to that queue and DriverImpl::read() is what
     * consumes it - but Bus::stop() only waits for the Bus reader to leave its loop, and the
     * reader can leave without having come back for the sentinel.  A later close() then queues a
     * SECOND one, and read()'s flush arm - `while (rQueue.size() > 0) { inFrame = rQueue.poll();
     * frame = *inFrame; ... }` - dereferences it: EventQueue guards its element count and its
     * condition variable with DIFFERENT mutexes, so poll() can return the default-constructed
     * null for a queue that size() has just reported as non-empty.  Measured on this tree: that
     * is a segmentation fault in the reader thread in roughly 1 full-suite run in 60 once any
     * case re-initialises the shared library mid-run.
     *
     * BLOCKED - REQUIRED PRODUCTION CHANGE, REPORTED NOT MADE (Directive 6): DriverImpl::read()
     * must null-check inFrame in its flush arm, or CCEC_OSAL::EventQueue must publish its element
     * count and its condition under one lock.  Until one of those lands, a test that terminates
     * the shared library has to take the sentinel off the queue itself, which is what this does -
     * read() is public, the driver is closed, so the call drains the queue and then reports the
     * invalid state.  It is safe from this thread because term() has already waited for the reader
     * to leave its loop, so there is no second consumer.
     */
    static void drainClosedDriverQueue() {
        CECFrame frame;
        try {
            Driver::getInstance().read(frame);
        } catch (const InvalidStateException &) {
            // The expected outcome: the driver is closed, and the queue is now empty.
        }
    }

    static void TearDownTestSuite() {
        try {
            LibCCEC::getInstance().init("CEC_TEST");
        } catch (const InvalidStateException &) {
            // The last case left it initialised, which is the state wanted.
        }
        // The prefix is part of the state the global environment established, and one case here
        // deliberately empties it, so it is put back in place rather than through another cycle.
        strncpy(_CEC_LOG_PREFIX, "CEC_TEST", sizeof(_CEC_LOG_PREFIX) - 1);
        _CEC_LOG_PREFIX[sizeof(_CEC_LOG_PREFIX) - 1] = '\0';
    }
};

// #gap-mw-libccec; LibCCEC::term uninitialised guard.
TEST_F(LibCCECUninitializedTest, TermThrowsWhenNotInitialized) {
    LibCCEC& lib = LibCCEC::getInstance();

    EXPECT_THROW(lib.term(), InvalidStateException);
}

// #gap-mw-libccec; LibCCEC::addLogicalAddress uninitialised guard. The driver must not be
// reached at all, which is what the zero-cardinality expectation pins.
TEST_F(LibCCECUninitializedTest, AddLogicalAddressThrowsWhenNotInitialized) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    LibCCEC& lib = LibCCEC::getInstance();

    EXPECT_CALL(*mock, HdmiCecAddLogicalAddress(_, _)).Times(0);
    LogicalAddress address(LogicalAddress::PLAYBACK_DEVICE_1);
    EXPECT_THROW(lib.addLogicalAddress(address), InvalidStateException);
    ::testing::Mock::VerifyAndClearExpectations(mock);
}

// #gap-mw-libccec; LibCCEC::getLogicalAddress uninitialised guard. This reaches the
// !initialized guard, not the zero-logical-address guard that follows the driver call and is
// already covered by GetLogicalAddressForDifferentDeviceTypes.
TEST_F(LibCCECUninitializedTest, GetLogicalAddressThrowsWhenNotInitialized) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    LibCCEC& lib = LibCCEC::getInstance();

    EXPECT_CALL(*mock, HdmiCecGetLogicalAddress(_, _)).Times(0);
    EXPECT_THROW(lib.getLogicalAddress(DeviceType::PLAYBACK_DEVICE), InvalidStateException);
    ::testing::Mock::VerifyAndClearExpectations(mock);
}

// #gap-mw-libccec; LibCCEC::getPhysicalAddress uninitialised guard.
TEST_F(LibCCECUninitializedTest, GetPhysicalAddressThrowsWhenNotInitialized) {
    HdmiCecDriverMock* mock = HdmiCecDriverMock::getInstance();
    LibCCEC& lib = LibCCEC::getInstance();

    EXPECT_CALL(*mock, HdmiCecGetPhysicalAddress(_, _)).Times(0);
    unsigned int physicalAddress = 0;
    EXPECT_THROW(lib.getPhysicalAddress(&physicalAddress), InvalidStateException);
    ::testing::Mock::VerifyAndClearExpectations(mock);
}

// #gap-mw-libccec; §6.2 rank 23, P1; LibCCEC::init null-name arm.
//
// init(NULL) takes the else arm of the name guard, whose entire observable effect is that it
// empties the shared log prefix. Asserting only that the call does not throw would pass even if
// that arm stopped clearing the prefix, so the prefix is seeded to a known non-empty value
// first and read back afterwards.
//
// DECLARED LAST IN THIS FIXTURE ON PURPOSE: it is the one case here that leaves the library
// INITIALISED, and a following SetUp() would terminate it - the init()/term() pairing this
// fixture exists to avoid. TearDownTestSuite() runs next instead and finds the state it wants.
TEST_F(LibCCECUninitializedTest, InitWithNullNameClearsLogPrefix) {
    LibCCEC& lib = LibCCEC::getInstance();
    ScopedLibCcecRestore restoreLibCcec;

    // Seeded before the call under test: if init(NULL) stopped clearing the prefix, this value
    // would still be there afterwards.
    strncpy(_CEC_LOG_PREFIX, "CEC_SEED", sizeof(_CEC_LOG_PREFIX) - 1);
    _CEC_LOG_PREFIX[sizeof(_CEC_LOG_PREFIX) - 1] = '\0';
    const std::string seededPrefix(_CEC_LOG_PREFIX);
    ASSERT_FALSE(seededPrefix.empty());

    // The null-name arm must empty the prefix rather than leave the seed in place.
    EXPECT_NO_THROW({ lib.init(NULL); });

    EXPECT_STREQ("", _CEC_LOG_PREFIX);
    EXPECT_NE(seededPrefix, std::string(_CEC_LOG_PREFIX));

    // The library is otherwise fully up, which the rejected second init confirms.
    EXPECT_THROW(lib.init(NULL), InvalidStateException);

    // The prefix is restored in place rather than through a term()/init() pair, and the library
    // is deliberately left initialised - TearDownTestSuite() below expects exactly that.
    strncpy(_CEC_LOG_PREFIX, "CEC_TEST", sizeof(_CEC_LOG_PREFIX) - 1);
    _CEC_LOG_PREFIX[sizeof(_CEC_LOG_PREFIX) - 1] = '\0';
    EXPECT_STREQ("CEC_TEST", _CEC_LOG_PREFIX);
    EXPECT_EQ('\0', _CEC_LOG_PREFIX[sizeof(_CEC_LOG_PREFIX) - 1]);
}
