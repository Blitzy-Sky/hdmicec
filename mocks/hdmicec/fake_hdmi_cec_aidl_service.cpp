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

#include "fake_hdmi_cec_aidl_service.h"
#include <binder/IServiceManager.h>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>
#include <utils/Errors.h>
#include <utils/String16.h>

/**
 * @defgroup HDMI_CEC_FAKE_AIDL_SERVICE_IMPL HDMI CEC Fake AIDL Service Implementation
 * @ingroup HDMI_CEC_FAKE_AIDL_SERVICE
 * @{
 * @par Fake Service Implementation Specification
 * Each interface method takes the lock, counts the call, captures its input, traces it and
 * answers, and the only CEC reasoning is recognising an allocation poll (a one-byte frame whose
 * initiator equals its destination); setters and accessors take the same lock, and accessors
 * return copies. Member contracts are stated on the declarations in fake_hdmi_cec_aidl_service.h
 * and copied here with @c \@copydoc, followed only by what is true of the body. No GoogleTest,
 * GoogleMock or binder threadpool is used: the fake-service host binary links this file too, and
 * the host and the adapter own the threadpools.
 */

/**
 * @file fake_hdmi_cec_aidl_service.cpp
 *
 * @brief Implementation of the test-scope fake com.rdk.hal.hdmicec AIDL HdmiCec service.
 *
 * Defines FakeHdmiCecController, FakeHdmiCecService and registerFakeHdmiCecService(). Both classes
 * derive from the generated server bases, so they present exactly the frozen 0.1.0.0 interface.
 *
 * @warning Test scope only: built for test targets and never listed in a production source list.
 * @see fake_hdmi_cec_aidl_service.h
 */

/**
 * @brief Binder driver node consulted before the service manager is reached.
 *
 * Checking it first turns a libbinder abort on a host without kernel binder support into a false
 * return from registerFakeHdmiCecService().
 */
static const char FAKE_HDMI_CEC_BINDER_DRIVER[] = "/dev/binder";

/**
 * @brief The word a diagnostic reports in place of an object it was not given.
 *
 * Spelled once, so absence reads the same at every trace site and never prints as an empty field.
 */
static const char FAKE_HDMI_CEC_TRACE_ABSENT[] = "absent";

// Static instance pointer
FakeHdmiCecService* FakeHdmiCecService::instance = nullptr;

/**
 * @copydoc fakeHdmiCecTraceLabel
 *
 * The registry and its sequence are function-local statics behind a lock taken nowhere else, so
 * construction follows first use in both binaries and a call from inside another critical section
 * cannot deadlock. Entries are never pruned, so no ordinal is reused within a run.
 */
::std::string fakeHdmiCecTraceLabel(const void* object)
{
    if (object == nullptr) {
        return ::std::string(FAKE_HDMI_CEC_TRACE_ABSENT);
    }

    static ::std::mutex ordinalMutex;
    static ::std::map<const void*, int32_t> ordinals;
    static int32_t lastOrdinal = 0;

    ::std::lock_guard<::std::mutex> guard(ordinalMutex);

    const ::std::pair<::std::map<const void*, int32_t>::iterator, bool> minted =
        ordinals.emplace(object, lastOrdinal + 1);

    if (minted.second) {
        lastOrdinal = minted.first->second;
    }

    return "#" + ::std::to_string(minted.first->second);
}

/**
 * @copydoc FakeHdmiCecController::addLogicalAddresses
 *
 * The optional delay is taken first with no lock held; the counter and the capture then advance
 * before the status is examined, so failing arms are recorded too. The vector is captured exactly
 * as it arrived, and a null out-parameter is traced rather than dereferenced.
 */
::android::binder::Status FakeHdmiCecController::addLogicalAddresses(const ::std::vector<int32_t>& logicalAddresses,
                                                                    bool* _aidl_return)
{
    // Optional delay, read under the lock and slept with it dropped, so capture reads are never
    // blocked behind it; see setAddLogicalAddressesDelayMs().
    int32_t delayMs = 0;
    {
        ::std::lock_guard<::std::mutex> delayGuard(mutex);

        delayMs = addLogicalAddressesDelayMs;
    }

    if (delayMs > 0) {
        std::cout << "[FakeHdmiCecController::addLogicalAddresses] Delaying " << delayMs
                  << " ms before answering, as configured" << std::endl;

        ::std::this_thread::sleep_for(::std::chrono::milliseconds(delayMs));
    }

    ::std::lock_guard<::std::mutex> guard(mutex);

    ++addLogicalAddressesCallCount;
    lastAddedLogicalAddresses = logicalAddresses;

    std::cout << "[FakeHdmiCecController::addLogicalAddresses] Received " << logicalAddresses.size()
              << " address(es), reporting " << (addLogicalAddressesResult ? "true" : "false")
              << ", status: " << addLogicalAddressesBinderStatus.toString8().c_str() << std::endl;

    if (!addLogicalAddressesBinderStatus.isOk()) {
        return addLogicalAddressesBinderStatus;
    }

    if (addLogicalAddressesResult) {
        for (const int32_t address : logicalAddresses) {
            if (::std::find(registeredLogicalAddresses.begin(), registeredLogicalAddresses.end(), address) ==
                registeredLogicalAddresses.end()) {
                registeredLogicalAddresses.push_back(address);
            }
        }
    }

    if (_aidl_return) {
        *_aidl_return = addLogicalAddressesResult;
    } else {
        std::cout << "[FakeHdmiCecController::addLogicalAddresses] Null out-parameter, result not written" << std::endl;
    }

    return ::android::binder::Status::ok();
}

/**
 * @copydoc FakeHdmiCecController::removeLogicalAddresses
 *
 * Identical in shape to addLogicalAddresses() apart from the delay: a false result and a non-ok
 * status are ordinary outcomes that leave the registrations as they were.
 */
::android::binder::Status FakeHdmiCecController::removeLogicalAddresses(const ::std::vector<int32_t>& logicalAddresses,
                                                                       bool* _aidl_return)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    ++removeLogicalAddressesCallCount;
    lastRemovedLogicalAddresses = logicalAddresses;

    std::cout << "[FakeHdmiCecController::removeLogicalAddresses] Received " << logicalAddresses.size()
              << " address(es), reporting " << (removeLogicalAddressesResult ? "true" : "false")
              << ", status: " << removeLogicalAddressesBinderStatus.toString8().c_str() << std::endl;

    if (!removeLogicalAddressesBinderStatus.isOk()) {
        return removeLogicalAddressesBinderStatus;
    }

    if (removeLogicalAddressesResult) {
        for (const int32_t address : logicalAddresses) {
            registeredLogicalAddresses.erase(
                ::std::remove(registeredLogicalAddresses.begin(), registeredLogicalAddresses.end(), address),
                registeredLogicalAddresses.end());
        }
    }

    if (_aidl_return) {
        *_aidl_return = removeLogicalAddressesResult;
    } else {
        std::cout << "[FakeHdmiCecController::removeLogicalAddresses] Null out-parameter, result not written" << std::endl;
    }

    return ::android::binder::Status::ok();
}

/**
 * @copydoc FakeHdmiCecController::sendMessage
 *
 * An allocation poll is answered with the status installed for its address, ACK_STATE_1 (free) by
 * default, and recorded only in getAllocationPolls(). Any other frame is counted, captured whole
 * and answered with the canned statuses; reading a send status as directed or broadcast is left to
 * the adapter under test.
 */
::android::binder::Status FakeHdmiCecController::sendMessage(const ::std::vector<uint8_t>& message,
                                                            ::com::rdk::hal::hdmicec::SendMessageStatus* _aidl_return)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    if ((message.size() == 1) && (((message[0] >> 4) & 0x0F) == (message[0] & 0x0F))) {
        const int32_t polled = static_cast<int32_t>(message[0] & 0x0F);
        const ::std::map<int32_t, ::com::rdk::hal::hdmicec::SendMessageStatus>::const_iterator answer =
            allocationPollResults.find(polled);
        const ::com::rdk::hal::hdmicec::SendMessageStatus pollStatus =
            (answer != allocationPollResults.end()) ? answer->second
                                                    : ::com::rdk::hal::hdmicec::SendMessageStatus::ACK_STATE_1;

        allocationPolls.push_back(polled);

        std::cout << "[FakeHdmiCecController::sendMessage] Allocation poll of logical address " << polled
                  << ", reporting " << ::com::rdk::hal::hdmicec::toString(pollStatus) << std::endl;

        if (_aidl_return) {
            *_aidl_return = pollStatus;
        } else {
            std::cout << "[FakeHdmiCecController::sendMessage] Null out-parameter, status not written" << std::endl;
        }

        return ::android::binder::Status::ok();
    }

    ++sendMessageCallCount;
    lastSentMessage = message;

    std::cout << "[FakeHdmiCecController::sendMessage] Received " << message.size()
              << " byte(s), reporting " << ::com::rdk::hal::hdmicec::toString(sendMessageResult)
              << ", status: " << sendMessageBinderStatus.toString8().c_str() << std::endl;

    if (!sendMessageBinderStatus.isOk()) {
        return sendMessageBinderStatus;
    }

    if (_aidl_return) {
        *_aidl_return = sendMessageResult;
    } else {
        std::cout << "[FakeHdmiCecController::sendMessage] Null out-parameter, status not written" << std::endl;
    }

    return ::android::binder::Status::ok();
}

/**
 * @copydoc FakeHdmiCecController::getInterfaceVersion
 *
 * Traces only when the reported version differs from the compiled-in one, as installed by
 * setInterfaceVersion(), so an ordinary run prints nothing here.
 */
int32_t FakeHdmiCecController::getInterfaceVersion()
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    if (interfaceVersionResult != ::com::rdk::hal::hdmicec::IHdmiCecController::VERSION) {
        std::cout << "[FakeHdmiCecController::getInterfaceVersion] Reporting overridden version: "
                  << interfaceVersionResult << std::endl;
    }

    return interfaceVersionResult;
}

/**
 * @copydoc FakeHdmiCecController::getInterfaceHash
 *
 * Carries the same divergence trace as getInterfaceVersion(), on the same terms and driven by
 * setInterfaceHash().
 */
std::string FakeHdmiCecController::getInterfaceHash()
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    if (interfaceHashResult != ::com::rdk::hal::hdmicec::IHdmiCecController::HASHVALUE) {
        std::cout << "[FakeHdmiCecController::getInterfaceHash] Reporting overridden hash: \""
                  << interfaceHashResult << "\"" << std::endl;
    }

    return interfaceHashResult;
}

/**
 * @copydoc FakeHdmiCecController::setAddLogicalAddressesResult
 */
void FakeHdmiCecController::setAddLogicalAddressesResult(bool result)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    addLogicalAddressesResult = result;
}

/**
 * @copydoc FakeHdmiCecController::setRemoveLogicalAddressesResult
 *
 * Held in a member of its own, so installing it disturbs no other canned response.
 */
void FakeHdmiCecController::setRemoveLogicalAddressesResult(bool result)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    removeLogicalAddressesResult = result;
}

/**
 * @copydoc FakeHdmiCecController::setAddLogicalAddressesDelayMs
 *
 * Stores the value only; addLogicalAddresses() sleeps with the lock dropped and treats a negative
 * value as no delay.
 */
void FakeHdmiCecController::setAddLogicalAddressesDelayMs(int32_t delayMs)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    addLogicalAddressesDelayMs = delayMs;
}

/**
 * @copydoc FakeHdmiCecController::setSendMessageResult
 *
 * The value is stored without interpretation, so no reading of ACK_STATE_0 or ACK_STATE_1 is fixed
 * here.
 */
void FakeHdmiCecController::setSendMessageResult(::com::rdk::hal::hdmicec::SendMessageStatus status)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    sendMessageResult = status;
}

/**
 * @copydoc FakeHdmiCecController::setAddLogicalAddressesBinderStatus
 *
 * Each of the three controller methods holds its own canned status, so failing one leaves the other
 * two alone.
 */
void FakeHdmiCecController::setAddLogicalAddressesBinderStatus(const ::android::binder::Status& status)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    addLogicalAddressesBinderStatus = status;
}

/**
 * @copydoc FakeHdmiCecController::setRemoveLogicalAddressesBinderStatus
 */
void FakeHdmiCecController::setRemoveLogicalAddressesBinderStatus(const ::android::binder::Status& status)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    removeLogicalAddressesBinderStatus = status;
}

/**
 * @copydoc FakeHdmiCecController::setSendMessageBinderStatus
 *
 * Held independently of the canned send status, so the two can be installed in any combination.
 */
void FakeHdmiCecController::setSendMessageBinderStatus(const ::android::binder::Status& status)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    sendMessageBinderStatus = status;
}

/**
 * @copydoc FakeHdmiCecController::setLogicalAddressOccupied
 */
void FakeHdmiCecController::setLogicalAddressOccupied(int32_t address, bool occupied)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    if (occupied) {
        allocationPollResults[address] = ::com::rdk::hal::hdmicec::SendMessageStatus::ACK_STATE_0;
    } else {
        allocationPollResults.erase(address);
    }
}

/**
 * @copydoc FakeHdmiCecController::setAllocationPollResult
 */
void FakeHdmiCecController::setAllocationPollResult(int32_t address,
                                                    ::com::rdk::hal::hdmicec::SendMessageStatus status)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    allocationPollResults[address] = status;
}

/**
 * @copydoc FakeHdmiCecController::setInterfaceHash
 *
 * Traces the replaced and the installed value on one line and stores the string unvalidated,
 * because the caller owns which hash is reported.
 */
void FakeHdmiCecController::setInterfaceHash(std::string hash)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    std::cout << "[FakeHdmiCecController::setInterfaceHash] Hash set from \"" << interfaceHashResult
              << "\" to \"" << hash << "\"" << std::endl;
    interfaceHashResult = ::std::move(hash);
}

/**
 * @copydoc FakeHdmiCecController::setInterfaceVersion
 *
 * Traces the replaced and the installed version, as the hash setter does, and stores any int32_t
 * unvalidated.
 */
void FakeHdmiCecController::setInterfaceVersion(int32_t version)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    std::cout << "[FakeHdmiCecController::setInterfaceVersion] Version set from "
              << interfaceVersionResult << " to " << version << std::endl;
    interfaceVersionResult = version;
}

/**
 * @copydoc FakeHdmiCecController::getLastAddedLogicalAddresses
 */
::std::vector<int32_t> FakeHdmiCecController::getLastAddedLogicalAddresses() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return lastAddedLogicalAddresses;
}

/**
 * @copydoc FakeHdmiCecController::getLastRemovedLogicalAddresses
 */
::std::vector<int32_t> FakeHdmiCecController::getLastRemovedLogicalAddresses() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return lastRemovedLogicalAddresses;
}

/**
 * @copydoc FakeHdmiCecController::getLastSentMessage
 */
::std::vector<uint8_t> FakeHdmiCecController::getLastSentMessage() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return lastSentMessage;
}

/**
 * @copydoc FakeHdmiCecController::getAddLogicalAddressesCallCount
 */
int32_t FakeHdmiCecController::getAddLogicalAddressesCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return addLogicalAddressesCallCount;
}

/**
 * @copydoc FakeHdmiCecController::getRemoveLogicalAddressesCallCount
 */
int32_t FakeHdmiCecController::getRemoveLogicalAddressesCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return removeLogicalAddressesCallCount;
}

/**
 * @copydoc FakeHdmiCecController::getSendMessageCallCount
 */
int32_t FakeHdmiCecController::getSendMessageCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return sendMessageCallCount;
}

/**
 * @copydoc FakeHdmiCecController::getAllocationPolls
 */
::std::vector<int32_t> FakeHdmiCecController::getAllocationPolls() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return allocationPolls;
}

/**
 * @copydoc FakeHdmiCecController::getRegisteredLogicalAddresses
 */
::std::vector<int32_t> FakeHdmiCecController::getRegisteredLogicalAddresses() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return registeredLogicalAddresses;
}

/**
 * @copydoc FakeHdmiCecController::clearRegisteredLogicalAddresses
 */
void FakeHdmiCecController::clearRegisteredLogicalAddresses()
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    registeredLogicalAddresses.clear();
}

/**
 * @copydoc FakeHdmiCecController::reset
 *
 * One critical section restores every default and clears every capture, counter, allocation-poll
 * record and registration, so no case observes a half-restored controller or inherits a divergent
 * hash or version. Each default is spelled beside the line that restores it.
 */
void FakeHdmiCecController::reset()
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    addLogicalAddressesResult = true;                                                       // Default: address acquired
    removeLogicalAddressesResult = true;                                                    // Default: address removed
    addLogicalAddressesDelayMs = 0;                                                         // Default: answer immediately
    sendMessageResult = ::com::rdk::hal::hdmicec::SendMessageStatus::ACK_STATE_0;            // Default: ACKed directed frame

    addLogicalAddressesBinderStatus = ::android::binder::Status::ok();
    removeLogicalAddressesBinderStatus = ::android::binder::Status::ok();
    sendMessageBinderStatus = ::android::binder::Status::ok();

    lastAddedLogicalAddresses.clear();
    lastRemovedLogicalAddresses.clear();
    lastSentMessage.clear();

    addLogicalAddressesCallCount = 0;
    removeLogicalAddressesCallCount = 0;
    sendMessageCallCount = 0;

    allocationPollResults.clear();                                                          // Default: every poll answers free
    allocationPolls.clear();
    registeredLogicalAddresses.clear();

    interfaceVersionResult = ::com::rdk::hal::hdmicec::IHdmiCecController::VERSION;          // Default: the frozen version
    interfaceHashResult = ::com::rdk::hal::hdmicec::IHdmiCecController::HASHVALUE;           // Default: the frozen hash

    std::cout << "[FakeHdmiCecController::reset] Canned responses, captures and counters restored to defaults"
              << std::endl;
}


/**
 * @copydoc FakeHdmiCecService::~FakeHdmiCecService
 *
 * The instance pointer is compared before it is cleared, so a fake destroyed after a second one was
 * published leaves that second one reachable.
 */
FakeHdmiCecService::~FakeHdmiCecService()
{
    if (instance == this) {
        instance = nullptr;
    }
}

/**
 * @copydoc FakeHdmiCecService::getState
 *
 * DEFAULT_STATE is written straight out, with no member or canned status behind it.
 */
::android::binder::Status FakeHdmiCecService::getState(::com::rdk::hal::hdmicec::State* _aidl_return)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    ++getStateCallCount;

    std::cout << "[FakeHdmiCecService::getState] Reporting "
              << ::com::rdk::hal::hdmicec::toString(DEFAULT_STATE) << ", status: ok" << std::endl;

    if (_aidl_return) {
        *_aidl_return = DEFAULT_STATE;
    } else {
        std::cout << "[FakeHdmiCecService::getState] Null out-parameter, state not written" << std::endl;
    }

    return ::android::binder::Status::ok();
}

/**
 * @copydoc FakeHdmiCecService::getProperty
 *
 * ::std::nullopt is written straight out; no PropertyValue is fabricated for a test to assert on.
 */
::android::binder::Status FakeHdmiCecService::getProperty(::com::rdk::hal::hdmicec::Property property,
                                                          ::std::optional<::com::rdk::hal::PropertyValue>* _aidl_return)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    ++getPropertyCallCount;

    std::cout << "[FakeHdmiCecService::getProperty] Property " << ::com::rdk::hal::hdmicec::toString(property)
              << " requested, reporting an empty optional, status: ok" << std::endl;

    if (_aidl_return) {
        *_aidl_return = ::std::nullopt;                                                      // Default: property not available
    } else {
        std::cout << "[FakeHdmiCecService::getProperty] Null out-parameter, optional not written" << std::endl;
    }

    return ::android::binder::Status::ok();
}

/**
 * @copydoc FakeHdmiCecService::getLogicalAddresses
 *
 * The installed vector, or the controller's registrations when none is installed, is copied out at
 * full width, never normalised, sorted, deduplicated or truncated, because each width reaches a
 * different arm of the adapter under test.
 */
::android::binder::Status FakeHdmiCecService::getLogicalAddresses(::std::vector<int32_t>* _aidl_return)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    ++getLogicalAddressesCallCount;

    const ::std::vector<int32_t> reported =
        logicalAddressesResult.has_value() ? *logicalAddressesResult : controller->getRegisteredLogicalAddresses();

    std::cout << "[FakeHdmiCecService::getLogicalAddresses] Reporting " << reported.size()
              << (logicalAddressesResult.has_value() ? " installed" : " registered")
              << " address(es), status: " << getLogicalAddressesBinderStatus.toString8().c_str() << std::endl;

    if (!getLogicalAddressesBinderStatus.isOk()) {
        return getLogicalAddressesBinderStatus;
    }

    if (_aidl_return) {
        *_aidl_return = reported;
    } else {
        std::cout << "[FakeHdmiCecService::getLogicalAddresses] Null out-parameter, addresses not written" << std::endl;
    }

    return ::android::binder::Status::ok();
}

/**
 * @copydoc FakeHdmiCecService::open
 *
 * The listener is captured before the canned status is examined, so the receive path stays
 * exercisable against a rejected session; the null-controller flag is read only on the ok arm.
 */
::android::binder::Status FakeHdmiCecService::open(const ::android::sp<::com::rdk::hal::hdmicec::IHdmiCecEventListener>& cecControllerListener,
                                                  ::android::sp<::com::rdk::hal::hdmicec::IHdmiCecController>* _aidl_return)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    ++openCallCount;
    listener = cecControllerListener;

    std::cout << "[FakeHdmiCecService::open] Listener captured: "
              << fakeHdmiCecTraceLabel(cecControllerListener.get())
              << " on open call " << openCallCount
              << ", reporting controller: "
              << (openReturnsNullController ? "nullptr (requested)" : "the owned controller")
              << ", status: " << openBinderStatus.toString8().c_str() << std::endl;

    if (!openBinderStatus.isOk()) {
        return openBinderStatus;
    }

    if (_aidl_return) {
        if (openReturnsNullController) {
            *_aidl_return = nullptr;
        } else {
            *_aidl_return = controller;
        }
    } else {
        std::cout << "[FakeHdmiCecService::open] Null out-parameter, controller not written" << std::endl;
    }

    return ::android::binder::Status::ok();
}

/**
 * @copydoc FakeHdmiCecService::close
 *
 * The captured listener is deliberately left untouched, so a trigger fired after a close still
 * reaches the adapter, whose own state guard must reject it.
 */
::android::binder::Status FakeHdmiCecService::close(const ::android::sp<::com::rdk::hal::hdmicec::IHdmiCecController>& hdmiCecController,
                                                   bool* _aidl_return)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    ++closeCallCount;
    lastClosedController = hdmiCecController;

    std::cout << "[FakeHdmiCecService::close] Controller captured: "
              << fakeHdmiCecTraceLabel(hdmiCecController.get())
              << " on close call " << closeCallCount
              << ", reporting " << (closeResult ? "true" : "false")
              << ", status: " << closeBinderStatus.toString8().c_str() << std::endl;

    if (!closeBinderStatus.isOk()) {
        return closeBinderStatus;
    }

    if (closeResult) {
        controller->clearRegisteredLogicalAddresses();
    }

    if (_aidl_return) {
        *_aidl_return = closeResult;
    } else {
        std::cout << "[FakeHdmiCecService::close] Null out-parameter, result not written" << std::endl;
    }

    return ::android::binder::Status::ok();
}

/**
 * @copydoc FakeHdmiCecService::registerEventListener
 *
 * The offered listener is stored nowhere in this body, so no second delivery route exists for a test
 * to reach by accident.
 */
::android::binder::Status FakeHdmiCecService::registerEventListener(const ::android::sp<::com::rdk::hal::hdmicec::IHdmiCecEventListener>& cecEventListener,
                                                                   bool* _aidl_return)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    ++registerEventListenerCallCount;

    std::cout << "[FakeHdmiCecService::registerEventListener] Listener offered: "
              << fakeHdmiCecTraceLabel(cecEventListener.get())
              << " on registerEventListener call " << registerEventListenerCallCount
              << ", not retained, reporting true, status: ok" << std::endl;

    if (_aidl_return) {
        *_aidl_return = true;                                                                // Default: registration accepted
    } else {
        std::cout << "[FakeHdmiCecService::registerEventListener] Null out-parameter, result not written" << std::endl;
    }

    return ::android::binder::Status::ok();
}

/**
 * @copydoc FakeHdmiCecService::unregisterEventListener
 *
 * Nothing is withdrawn here, because registerEventListener() retains nothing to withdraw.
 */
::android::binder::Status FakeHdmiCecService::unregisterEventListener(const ::android::sp<::com::rdk::hal::hdmicec::IHdmiCecEventListener>& cecEventListener,
                                                                     bool* _aidl_return)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    ++unregisterEventListenerCallCount;

    std::cout << "[FakeHdmiCecService::unregisterEventListener] Listener withdrawn: "
              << fakeHdmiCecTraceLabel(cecEventListener.get())
              << " on unregisterEventListener call " << unregisterEventListenerCallCount
              << ", reporting true, status: ok" << std::endl;

    if (_aidl_return) {
        *_aidl_return = true;                                                                // Default: withdrawal accepted
    } else {
        std::cout << "[FakeHdmiCecService::unregisterEventListener] Null out-parameter, result not written" << std::endl;
    }

    return ::android::binder::Status::ok();
}

/**
 * @copydoc FakeHdmiCecService::getInterfaceVersion
 *
 * Carries the same divergence trace as the controller's version getter, driven by
 * setInterfaceVersion().
 */
int32_t FakeHdmiCecService::getInterfaceVersion()
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    if (interfaceVersionResult != ::com::rdk::hal::hdmicec::IHdmiCec::VERSION) {
        std::cout << "[FakeHdmiCecService::getInterfaceVersion] Reporting overridden version: "
                  << interfaceVersionResult << std::endl;
    }

    return interfaceVersionResult;
}

/**
 * @copydoc FakeHdmiCecService::getInterfaceHash
 *
 * Traces only when the reported hash differs from the compiled-in one, which marks the
 * deliberately incompatible run.
 */
std::string FakeHdmiCecService::getInterfaceHash()
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    if (interfaceHashResult != ::com::rdk::hal::hdmicec::IHdmiCec::HASHVALUE) {
        std::cout << "[FakeHdmiCecService::getInterfaceHash] Reporting overridden hash: \""
                  << interfaceHashResult << "\"" << std::endl;
    }

    return interfaceHashResult;
}


/**
 * @copydoc FakeHdmiCecService::setLogicalAddressesResult
 *
 * The vector is stored whole and unvalidated, because every shape is a case a test may install.
 */
void FakeHdmiCecService::setLogicalAddressesResult(const ::std::vector<int32_t>& logicalAddresses)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    logicalAddressesResult = logicalAddresses;
}

/**
 * @copydoc FakeHdmiCecService::setCloseResult
 *
 * Held separately from the canned close status, so a test can drive a transport failure and a
 * HAL-reported refusal independently of one another.
 */
void FakeHdmiCecService::setCloseResult(bool result)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    closeResult = result;
}

/**
 * @copydoc FakeHdmiCecService::setOpenReturnsNullController
 *
 * Only a flag is stored; the owned controller is neither released nor replaced, so clearing the flag
 * restores the ordinary successful open with the same controller object as before.
 */
void FakeHdmiCecService::setOpenReturnsNullController(bool returnsNull)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    openReturnsNullController = returnsNull;
}

/**
 * @copydoc FakeHdmiCecService::setOpenBinderStatus
 *
 * Stored uninterpreted, so any exception code, EX_ILLEGAL_STATE included, reaches the adapter as
 * constructed.
 */
void FakeHdmiCecService::setOpenBinderStatus(const ::android::binder::Status& status)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    openBinderStatus = status;
}

/**
 * @copydoc FakeHdmiCecService::setCloseBinderStatus
 *
 * Held in a member of its own, independent of the canned close result, for the reason
 * setCloseResult() records.
 */
void FakeHdmiCecService::setCloseBinderStatus(const ::android::binder::Status& status)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    closeBinderStatus = status;
}

/**
 * @copydoc FakeHdmiCecService::setGetLogicalAddressesBinderStatus
 *
 * Independent of the answered addresses, so a failed query and an empty one can be installed
 * separately.
 */
void FakeHdmiCecService::setGetLogicalAddressesBinderStatus(const ::android::binder::Status& status)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    getLogicalAddressesBinderStatus = status;
}

/**
 * @copydoc FakeHdmiCecService::setInterfaceHash
 *
 * Traces the replaced and the installed value, marking where the fake was made incompatible, and
 * stores the string unvalidated.
 */
void FakeHdmiCecService::setInterfaceHash(std::string hash)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    std::cout << "[FakeHdmiCecService::setInterfaceHash] Hash set from \"" << interfaceHashResult
              << "\" to \"" << hash << "\"" << std::endl;
    interfaceHashResult = ::std::move(hash);
}

/**
 * @copydoc FakeHdmiCecService::setInterfaceVersion
 *
 * Traces the replaced and the installed version, as the hash setter does; held in its own member.
 */
void FakeHdmiCecService::setInterfaceVersion(int32_t version)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    std::cout << "[FakeHdmiCecService::setInterfaceVersion] Version set from "
              << interfaceVersionResult << " to " << version << std::endl;
    interfaceVersionResult = version;
}

/**
 * @copydoc FakeHdmiCecService::getController
 *
 * Copied out under the lock; the member is never reassigned, which keeps it non-null for the life
 * of the service.
 */
::android::sp<FakeHdmiCecController> FakeHdmiCecService::getController() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return controller;
}

/**
 * @copydoc FakeHdmiCecService::getListener
 *
 * Copied out under the lock, so the listener a caller obtained cannot be released underneath it by a
 * concurrent reset().
 */
::android::sp<::com::rdk::hal::hdmicec::IHdmiCecEventListener> FakeHdmiCecService::getListener() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return listener;
}

/**
 * @copydoc FakeHdmiCecService::getLastClosedController
 *
 * Reports whatever close() was handed, including a null, because "the adapter closed nothing usable" is
 * itself a result a case has to be able to observe.
 */
::android::sp<::com::rdk::hal::hdmicec::IHdmiCecController> FakeHdmiCecService::getLastClosedController() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return lastClosedController;
}

/**
 * @copydoc FakeHdmiCecService::getOpenCallCount
 */
int32_t FakeHdmiCecService::getOpenCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return openCallCount;
}

/**
 * @copydoc FakeHdmiCecService::getCloseCallCount
 */
int32_t FakeHdmiCecService::getCloseCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return closeCallCount;
}

/**
 * @copydoc FakeHdmiCecService::getGetLogicalAddressesCallCount
 */
int32_t FakeHdmiCecService::getGetLogicalAddressesCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return getLogicalAddressesCallCount;
}

/**
 * @copydoc FakeHdmiCecService::getGetStateCallCount
 */
int32_t FakeHdmiCecService::getGetStateCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return getStateCallCount;
}

/**
 * @copydoc FakeHdmiCecService::getGetPropertyCallCount
 */
int32_t FakeHdmiCecService::getGetPropertyCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return getPropertyCallCount;
}

/**
 * @copydoc FakeHdmiCecService::getRegisterEventListenerCallCount
 */
int32_t FakeHdmiCecService::getRegisterEventListenerCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return registerEventListenerCallCount;
}

/**
 * @copydoc FakeHdmiCecService::getUnregisterEventListenerCallCount
 */
int32_t FakeHdmiCecService::getUnregisterEventListenerCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return unregisterEventListenerCallCount;
}

/**
 * @copydoc FakeHdmiCecService::reset
 *
 * One critical section restores every default and clears every capture and counter, so no case
 * observes a half-reset fake or inherits a divergent hash or version. The owned controller is not
 * touched, as the declaration's warning explains.
 */
void FakeHdmiCecService::reset()
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    listener = nullptr;
    lastClosedController = nullptr;

    logicalAddressesResult.reset();                                                           // Default: report registrations
    closeResult = true;                                                                       // Default: session closed
    openReturnsNullController = false;                                                        // Default: a valid controller

    // Three statuses, not seven: the four methods the middleware never calls answer a fixed ok, but
    // their counters are still cleared below.
    openBinderStatus = ::android::binder::Status::ok();
    closeBinderStatus = ::android::binder::Status::ok();
    getLogicalAddressesBinderStatus = ::android::binder::Status::ok();

    openCallCount = 0;
    closeCallCount = 0;
    getLogicalAddressesCallCount = 0;
    getStateCallCount = 0;
    getPropertyCallCount = 0;
    registerEventListenerCallCount = 0;
    unregisterEventListenerCallCount = 0;

    interfaceVersionResult = ::com::rdk::hal::hdmicec::IHdmiCec::VERSION;                     // Default: the frozen version
    interfaceHashResult = ::com::rdk::hal::hdmicec::IHdmiCec::HASHVALUE;                      // Default: the frozen hash

    std::cout << "[FakeHdmiCecService::reset] Canned responses, captures and counters restored to defaults"
              << std::endl;
}


/**
 * @copydoc FakeHdmiCecService::fireOnMessageReceived
 *
 * The listener is copied out under the lock and invoked outside it, so re-entrancy is safe. The
 * invocation's status is traced but conditions nothing, since from a oneway remote proxy it only
 * reports that the driver accepted the transaction.
 *
 * @see getListener()
 */
bool FakeHdmiCecService::fireOnMessageReceived(const ::std::vector<uint8_t>& message)
{
    ::android::sp<::com::rdk::hal::hdmicec::IHdmiCecEventListener> target;
    {
        ::std::lock_guard<::std::mutex> guard(mutex);
        target = listener;
    }

    if (target == nullptr) {
        std::cout << "[FakeHdmiCecService::fireOnMessageReceived] No listener captured, delivery is a no-op"
                  << std::endl;
        return false;
    }

    const ::android::binder::Status status = target->onMessageReceived(message);

    std::cout << "[FakeHdmiCecService::fireOnMessageReceived] Delivered " << message.size()
              << " byte(s) to listener " << fakeHdmiCecTraceLabel(target.get())
              << ", listener returned: " << status.toString8().c_str() << std::endl;

    return true;
}

/**
 * @copydoc FakeHdmiCecService::fireOnStateChanged
 *
 * Identical in shape to fireOnMessageReceived(); only the callback and its traced values differ.
 */
bool FakeHdmiCecService::fireOnStateChanged(::com::rdk::hal::hdmicec::State oldState,
                                           ::com::rdk::hal::hdmicec::State newState)
{
    ::android::sp<::com::rdk::hal::hdmicec::IHdmiCecEventListener> target;
    {
        ::std::lock_guard<::std::mutex> guard(mutex);
        target = listener;
    }

    if (target == nullptr) {
        std::cout << "[FakeHdmiCecService::fireOnStateChanged] No listener captured, delivery is a no-op"
                  << std::endl;
        return false;
    }

    const ::android::binder::Status status = target->onStateChanged(oldState, newState);

    std::cout << "[FakeHdmiCecService::fireOnStateChanged] Delivered "
              << ::com::rdk::hal::hdmicec::toString(oldState) << " -> "
              << ::com::rdk::hal::hdmicec::toString(newState) << " to listener "
              << fakeHdmiCecTraceLabel(target.get())
              << ", listener returned: " << status.toString8().c_str() << std::endl;

    return true;
}

/**
 * @copydoc FakeHdmiCecService::fireOnMessageSent
 *
 * Identical in shape to fireOnMessageReceived(); the invocation status is named listenerStatus
 * because status already names the notified send status.
 */
bool FakeHdmiCecService::fireOnMessageSent(const ::std::vector<uint8_t>& message,
                                           ::com::rdk::hal::hdmicec::SendMessageStatus status)
{
    ::android::sp<::com::rdk::hal::hdmicec::IHdmiCecEventListener> target;
    {
        ::std::lock_guard<::std::mutex> guard(mutex);
        target = listener;
    }

    if (target == nullptr) {
        std::cout << "[FakeHdmiCecService::fireOnMessageSent] No listener captured, delivery is a no-op"
                  << std::endl;
        return false;
    }

    const ::android::binder::Status listenerStatus = target->onMessageSent(message, status);

    std::cout << "[FakeHdmiCecService::fireOnMessageSent] Delivered " << message.size()
              << " byte(s) with " << ::com::rdk::hal::hdmicec::toString(status)
              << " to listener " << fakeHdmiCecTraceLabel(target.get())
              << ", listener returned: " << listenerStatus.toString8().c_str() << std::endl;

    return true;
}

/**
 * @copydoc FakeHdmiCecService::getInstance
 *
 * Lock-free: the harness publishes the fake before initialising the middleware and clears it in
 * teardown, when no test or binder thread can be running.
 */
FakeHdmiCecService* FakeHdmiCecService::getInstance()
{
    return instance;
}

/**
 * @copydoc FakeHdmiCecService::setInstance
 *
 * Traces the replaced and the installed label, the record that lets getInstance() stay silent.
 */
void FakeHdmiCecService::setInstance(FakeHdmiCecService* newFake)
{
    std::cout << "[FakeHdmiCecService::setInstance] Setting instance from "
              << fakeHdmiCecTraceLabel(instance) << " to " << fakeHdmiCecTraceLabel(newFake)
              << std::endl;
    instance = newFake;
    std::cout << "[FakeHdmiCecService::setInstance] Instance is now: "
              << fakeHdmiCecTraceLabel(instance) << std::endl;
}

/**
 * @copydoc registerFakeHdmiCecService
 *
 * The null service and the binder driver node are checked before libbinder is touched, so a host
 * without kernel binder support gets false rather than a process abort; the service manager and
 * the registration status are checked after. Every arm traces before it returns.
 */
bool registerFakeHdmiCecService(const ::android::sp<FakeHdmiCecService>& service)
{
    if (service == nullptr) {
        std::cout << "[FakeRegistration] Refusing to publish a null service" << std::endl;
        return false;
    }

    if (::access(FAKE_HDMI_CEC_BINDER_DRIVER, R_OK | W_OK) != 0) {
        std::cout << "[FakeRegistration] Binder driver " << FAKE_HDMI_CEC_BINDER_DRIVER
                  << " is absent or unopenable, service not published" << std::endl;
        return false;
    }

    const ::android::sp<::android::IServiceManager> serviceManager = ::android::defaultServiceManager();
    if (serviceManager == nullptr) {
        std::cout << "[FakeRegistration] Service manager is unreachable, service not published" << std::endl;
        return false;
    }

    const ::std::string& name = ::com::rdk::hal::hdmicec::IHdmiCec::serviceName();
    const ::android::status_t added = serviceManager->addService(::android::String16(name.c_str()), service);

    if (added != ::android::OK) {
        std::cout << "[FakeRegistration] Service manager refused \"" << name
                  << "\", status " << added << std::endl;
        return false;
    }

    std::cout << "[FakeRegistration] Published fake " << fakeHdmiCecTraceLabel(service.get())
              << " as \"" << name << "\"" << std::endl;
    return true;
}

/** @} */ // End of HDMI_CEC_FAKE_AIDL_SERVICE_IMPL
