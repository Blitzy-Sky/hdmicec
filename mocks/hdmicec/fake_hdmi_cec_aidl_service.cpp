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
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unistd.h>
#include <utility>
#include <utils/Errors.h>
#include <utils/String16.h>

/**
 * @defgroup HDMI_CEC_FAKE_AIDL_SERVICE_IMPL HDMI CEC Fake AIDL Service Implementation
 * @ingroup HDMI_CEC_FAKE_AIDL_SERVICE
 * @{
 * @par Fake Service Implementation Specification
 * Every method below follows one shape and nothing more: take the lock, count the call and capture
 * what the caller handed over, trace the call, then answer with the canned response the test
 * installed.@n
 * There is deliberately no CEC reasoning anywhere in this file.  It does not read a frame's
 * destination nibble, does not classify a message as directed or broadcast, does not inspect an
 * opcode, does not police a frame length and does not derive a send status from message content.
 * All of that belongs to the middleware adapter under test; a second copy of it here would make the
 * suite assert this fake's opinion instead of the adapter's behaviour.  The fake's whole purpose is
 * to make each adapter branch reachable.
 *
 * There is likewise no GoogleMock and no GoogleTest here, even though the legacy driver double in
 * this same directory is built on both.  This translation unit is also compiled into the separate
 * fake-service host binary, which links the AIDL stub and binder libraries alone, so a single
 * reference to either framework would break that link.
 *
 * The binder threadpool is deliberately absent too.  The service-side threadpool belongs to the host
 * binary and the client-side threadpool belongs to the middleware adapter; starting one here would
 * blur the in-process and out-of-process cases, and telling those two apart is the entire reason both
 * exist.
 *
 */

/**
 * @file fake_hdmi_cec_aidl_service.cpp
 *
 * @brief Implementation of the test-scope fake com.rdk.hal.hdmicec AIDL HdmiCec service.
 *
 * Defines FakeHdmiCecController, FakeHdmiCecService and the registration entry point declared in
 * fake_hdmi_cec_aidl_service.h.  Both classes derive from the generated server bases, so the
 * interface they present is exactly the frozen 0.1.0.0 snapshot's: no .aidl is authored here, no
 * interface is added and no method is added to an interface.
 *
 * @warning Test scope only.  This file is built for test targets exclusively - the fake-service host
 *          binary and the test runners - and must never appear in a production source list, so no
 *          symbol defined here can reach the shipped middleware library.
 *
 * @see fake_hdmi_cec_aidl_service.h
 */

/**
 * @brief Binder driver node consulted before the service manager is reached.
 *
 * Registration checks this node before it touches libbinder, because the linked libbinder aborts the
 * whole process when it cannot open its driver: on a host without kernel binder support the check is
 * what turns "the process died during test set-up" into "registration reported false", which is the
 * behaviour registerFakeHdmiCecService() documents.  It is the default node name the SDK's own
 * process state uses, and it is the node a test runner and its service manager share.
 */
static const char FAKE_HDMI_CEC_BINDER_DRIVER[] = "/dev/binder";

/**
 * @brief The word a diagnostic reports in place of an object it was not given.
 *
 * Spelled once so that absence reads the same way at every trace site, and so that no site can drift
 * into printing an empty field - where a reader could not tell a missing object from a missing value.
 */
static const char FAKE_HDMI_CEC_TRACE_ABSENT[] = "absent";

// Static instance pointer
FakeHdmiCecService* FakeHdmiCecService::instance = nullptr;

/**
 * @brief Reports one traced object as a stable, address-free label.
 *
 * A registry and the sequence it draws from, both function-local statics behind their own short
 * critical section.  Function-local rather than file-scope so that construction is ordered by first
 * use rather than by link order: this translation unit is compiled into two binaries - the L1 test
 * runner and the separate fake-service host - and the first caller differs in each.@n
 * The lock is this registry's own and is taken nowhere else in the file, so a call made from inside
 * one of the fake's critical sections - which is where most of the trace sites sit - cannot deadlock
 * against it, and a call arriving on a binder thread cannot race one made from a test thread.@n
 * Nothing is ever pruned.  Reusing an ordinal would let two different objects appear under one label
 * in a single capture, which is the exact confusion the ordinal exists to remove, and the registry is
 * bounded by the handful of objects one run traces.
 *
 * @param [in] object                     - Object to label, or nullptr.  Used as an identity key
 *                                          only; its value is never printed and never returned
 *
 * @return ::std::string                          - The label
 * @retval "absent"                               - The object was nullptr
 * @retval "#N"                                   - N is this object's ordinal, minted on first sight
 *
 * @post A non-null object has an ordinal for the rest of the process, and a later call for the same
 *       object returns the same label.
 *
 * @warning Diagnostics only.  See the declaration for why nothing may parse a label, and why an
 *          address reused after a destruction is reported under the first object's ordinal.
 *
 * @see fake_hdmi_cec_aidl_service.h
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
        lastOrdinal = minted.first->second;                                                   // First sight of this object
    }

    return "#" + ::std::to_string(minted.first->second);
}

/**
 * @brief Adds logical addresses on the fake HAL.
 *
 * Captures the vector verbatim, counts the call, then answers.  The width of the captured vector is
 * the assertion target for single-element marshalling, so the vector is stored exactly as it
 * arrived - neither normalised, sorted, deduplicated nor trimmed.
 *
 * @param [in]  logicalAddresses          - Addresses the client marshalled, captured verbatim
 * @param [out] _aidl_return              - Receives the canned boolean result.  Left untouched when
 *                                          the canned binder status is non-ok, and when the pointer
 *                                          is null
 *
 * @return ::android::binder::Status              - Status
 * @retval ok                                     - The canned boolean was reported
 * @retval the installed non-ok status            - Returned before the out-parameter is written
 *
 * @post getAddLogicalAddressesCallCount() has advanced by one and
 *       getLastAddedLogicalAddresses() reports this call's vector, whatever the outcome.
 *
 * @see setAddLogicalAddressesResult(), setAddLogicalAddressesBinderStatus(),
 *      getLastAddedLogicalAddresses()
 */
::android::binder::Status FakeHdmiCecController::addLogicalAddresses(const ::std::vector<int32_t>& logicalAddresses,
                                                                    bool* _aidl_return)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    ++addLogicalAddressesCallCount;
    lastAddedLogicalAddresses = logicalAddresses;

    std::cout << "[FakeHdmiCecController::addLogicalAddresses] Received " << logicalAddresses.size()
              << " address(es), reporting " << (addLogicalAddressesResult ? "true" : "false")
              << ", status: " << addLogicalAddressesBinderStatus.toString8().c_str() << std::endl;

    if (!addLogicalAddressesBinderStatus.isOk()) {
        return addLogicalAddressesBinderStatus;
    }

    if (_aidl_return) {
        *_aidl_return = addLogicalAddressesResult;
    } else {
        std::cout << "[FakeHdmiCecController::addLogicalAddresses] Null out-parameter, result not written" << std::endl;
    }

    return ::android::binder::Status::ok();
}

/**
 * @brief Removes logical addresses on the fake HAL.
 *
 * Captures the vector verbatim, counts the call, then answers.  Both a false result and a non-ok
 * status are ordinary outcomes here rather than errors: the legacy back-end discards the HAL's
 * removal return value, so the adapter under test has to log them and let nothing escape, and these
 * are the inputs that prove it does.
 *
 * @param [in]  logicalAddresses          - Addresses the client marshalled, captured verbatim
 * @param [out] _aidl_return              - Receives the canned boolean result.  Left untouched when
 *                                          the canned binder status is non-ok, and when the pointer
 *                                          is null
 *
 * @return ::android::binder::Status              - Status
 * @retval ok                                     - The canned boolean was reported
 * @retval the installed non-ok status            - Returned before the out-parameter is written
 *
 * @post getRemoveLogicalAddressesCallCount() has advanced by one and
 *       getLastRemovedLogicalAddresses() reports this call's vector.
 *
 * @see setRemoveLogicalAddressesResult(), setRemoveLogicalAddressesBinderStatus(),
 *      getLastRemovedLogicalAddresses()
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

    if (_aidl_return) {
        *_aidl_return = removeLogicalAddressesResult;
    } else {
        std::cout << "[FakeHdmiCecController::removeLogicalAddresses] Null out-parameter, result not written" << std::endl;
    }

    return ::android::binder::Status::ok();
}

/**
 * @brief Transmits a CEC message through the fake HAL.
 *
 * Captures the whole frame, counts the call and reports the canned send status.  The bytes are never
 * examined: the fake neither knows nor cares whether the frame is directed or broadcast, which is
 * what keeps the meaning of ACK_STATE_0 and ACK_STATE_1 - inverted between those two cases - a
 * property of the adapter under test rather than of this file.  No length limit is applied either, so
 * a test can prove an over-length frame was rejected by the adapter and never reached the HAL by
 * reading getSendMessageCallCount().
 *
 * @param [in]  message                   - Raw CEC frame the client marshalled, captured verbatim and
 *                                          untruncated whatever its length
 * @param [out] _aidl_return              - Receives the canned send status.  Left untouched when the
 *                                          canned binder status is non-ok, and when the pointer is
 *                                          null
 *
 * @return ::android::binder::Status              - Status
 * @retval ok                                     - The canned send status was reported
 * @retval the installed non-ok status            - Returned before the out-parameter is written
 *
 * @post getSendMessageCallCount() has advanced by one and getLastSentMessage() reports this frame.
 *
 * @see setSendMessageResult(), setSendMessageBinderStatus(), getLastSentMessage(),
 *      getSendMessageCallCount()
 */
::android::binder::Status FakeHdmiCecController::sendMessage(const ::std::vector<uint8_t>& message,
                                                            ::com::rdk::hal::hdmicec::SendMessageStatus* _aidl_return)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

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
 * @brief Reports the interface version this fake controller claims.
 *
 * @return int32_t                                - The compiled-in
 *                                                  ::com::rdk::hal::hdmicec::IHdmiCecController::VERSION
 *
 * @warning Effective under local (in-process) dispatch only.  Across a binder transaction the
 *          generated onTransact() answers this from the compiled-in constant, so a remote fake cannot
 *          report a divergent version at all.
 *
 * @note There is no setter for this value, because the middleware's compatibility check reads the
 *       SERVICE interface's metadata alone and a divergent version reported here could change no
 *       outcome under test.  The divergence trace below is therefore defensive: with no setter it can
 *       only fire if this fake is later given one, and it says so at the moment it would matter rather
 *       than leaving a silent metadata change to be inferred from a failing selection.
 *
 * @see reset()
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
 * @brief Reports the interface hash this fake controller claims.
 *
 * @return std::string                            - The compiled-in
 *                                                  ::com::rdk::hal::hdmicec::IHdmiCecController::HASHVALUE
 *
 * @warning Effective under local (in-process) dispatch only, exactly as for getInterfaceVersion().
 *
 * @note There is no setter for this value either, for the reason getInterfaceVersion() records.
 *
 * @see reset()
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
 * @brief Selects the boolean addLogicalAddresses() reports.
 *
 * Reaches the address-unavailable arm: the AIDL HAL collapses the legacy HAL's distinction between
 * "logical address unavailable" and "general error" into one boolean, and false is the value the
 * adapter under test must translate into AddressNotAvailableException.
 *
 * @param [in] result                     - Value addLogicalAddresses() reports.  Default: true
 *
 * @post The next addLogicalAddresses() call reports this value.
 *
 * @see addLogicalAddresses()
 */
void FakeHdmiCecController::setAddLogicalAddressesResult(bool result)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    addLogicalAddressesResult = result;
}

/**
 * @brief Selects the boolean removeLogicalAddresses() reports.
 *
 * Reaches the ignored-failure arm: a false removal result must be logged by the adapter under test
 * and otherwise ignored, because raising where the legacy back-end returns silently would be an
 * unregistered behaviour difference.  Held independently of the add result, so one arm can be
 * exercised without disturbing the other.
 *
 * @param [in] result                     - Value removeLogicalAddresses() reports.  Default: true
 *
 * @post The next removeLogicalAddresses() call reports this value.
 *
 * @see removeLogicalAddresses()
 */
void FakeHdmiCecController::setRemoveLogicalAddressesResult(bool result)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    removeLogicalAddressesResult = result;
}

/**
 * @brief Selects the send status sendMessage() reports.
 *
 * Reaches every transmit-outcome arm of the adapter under test.  The value is stored and returned
 * without interpretation: whether ACK_STATE_0 means acknowledged or rejected depends on the frame's
 * destination, and that reading belongs to the adapter.
 *
 * @param [in] status                     - Value sendMessage() reports.  Default:
 *                                          ::com::rdk::hal::hdmicec::SendMessageStatus::ACK_STATE_0
 *
 * @post The next sendMessage() call reports this value.
 *
 * @see sendMessage()
 */
void FakeHdmiCecController::setSendMessageResult(::com::rdk::hal::hdmicec::SendMessageStatus status)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    sendMessageResult = status;
}

/**
 * @brief Installs the binder status addLogicalAddresses() returns.
 *
 * Reaches the add transport-failure arm, which the adapter under test must translate into
 * IOException.  Independent of the statuses installed for the other two controller methods.
 *
 * @param [in] status                     - Status to return, ok or non-ok.  Default: ok
 *
 * @post The next addLogicalAddresses() call returns this status, and writes its out-parameter only
 *       when the status is ok.
 *
 * @see addLogicalAddresses()
 */
void FakeHdmiCecController::setAddLogicalAddressesBinderStatus(const ::android::binder::Status& status)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    addLogicalAddressesBinderStatus = status;
}

/**
 * @brief Installs the binder status removeLogicalAddresses() returns.
 *
 * Reaches the removal transport-failure arm, which - unlike the add case - the adapter under test
 * must log and swallow rather than raise.
 *
 * @param [in] status                     - Status to return, ok or non-ok.  Default: ok
 *
 * @post The next removeLogicalAddresses() call returns this status, and writes its out-parameter only
 *       when the status is ok.
 *
 * @see removeLogicalAddresses()
 */
void FakeHdmiCecController::setRemoveLogicalAddressesBinderStatus(const ::android::binder::Status& status)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    removeLogicalAddressesBinderStatus = status;
}

/**
 * @brief Installs the binder status sendMessage() returns.
 *
 * Reaches the transmit transport-failure arm, which the adapter under test must translate into
 * IOException regardless of any send status value.
 *
 * @param [in] status                     - Status to return, ok or non-ok.  Default: ok
 *
 * @post The next sendMessage() call returns this status, and writes its out-parameter only when the
 *       status is ok.
 *
 * @see sendMessage()
 */
void FakeHdmiCecController::setSendMessageBinderStatus(const ::android::binder::Status& status)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    sendMessageBinderStatus = status;
}

/**
 * @brief Returns the address vector the last addLogicalAddresses() call carried.
 *
 * The assertion target for single-element array marshalling: a correct adapter marshals exactly one
 * entry carrying exactly the requested address.
 *
 * @return ::std::vector<int32_t>                 - Copy of the captured vector, empty when
 *                                                  addLogicalAddresses() has not been called
 *
 * @see addLogicalAddresses()
 */
::std::vector<int32_t> FakeHdmiCecController::getLastAddedLogicalAddresses() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return lastAddedLogicalAddresses;
}

/**
 * @brief Returns the address vector the last removeLogicalAddresses() call carried.
 *
 * The removal-side counterpart of getLastAddedLogicalAddresses(), asserting the same single-element
 * marshalling contract.
 *
 * @return ::std::vector<int32_t>                 - Copy of the captured vector, empty when
 *                                                  removeLogicalAddresses() has not been called
 *
 * @see removeLogicalAddresses()
 */
::std::vector<int32_t> FakeHdmiCecController::getLastRemovedLogicalAddresses() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return lastRemovedLogicalAddresses;
}

/**
 * @brief Returns the message bytes the last sendMessage() call carried.
 *
 * The assertion target for frame marshalling and for the frame-length boundary cases: a frame at the
 * contract limit must arrive byte for byte, and an over-length frame must never arrive at all, so
 * reading this capture is how a test tells "rejected" apart from "trimmed".
 *
 * @return ::std::vector<uint8_t>                 - Copy of the captured frame, empty when
 *                                                  sendMessage() has not been called
 *
 * @see sendMessage(), getSendMessageCallCount()
 */
::std::vector<uint8_t> FakeHdmiCecController::getLastSentMessage() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return lastSentMessage;
}

/**
 * @brief Returns how many times addLogicalAddresses() has been called.
 *
 * @return int32_t                                - Call count since construction or the last reset()
 *
 * @see addLogicalAddresses(), reset()
 */
int32_t FakeHdmiCecController::getAddLogicalAddressesCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return addLogicalAddressesCallCount;
}

/**
 * @brief Returns how many times removeLogicalAddresses() has been called.
 *
 * @return int32_t                                - Call count since construction or the last reset()
 *
 * @see removeLogicalAddresses(), reset()
 */
int32_t FakeHdmiCecController::getRemoveLogicalAddressesCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return removeLogicalAddressesCallCount;
}

/**
 * @brief Returns how many times sendMessage() has been called.
 *
 * The assertion target for "the adapter rejected this frame without transmitting": a guard that fires
 * before the HAL call leaves this counter unchanged, which is the only way to tell a rejection apart
 * from a failed transmit.
 *
 * @return int32_t                                - Call count since construction or the last reset()
 *
 * @see sendMessage(), reset()
 */
int32_t FakeHdmiCecController::getSendMessageCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return sendMessageCallCount;
}

/**
 * @brief Restores every canned response to its default and clears every capture and counter.
 *
 * Called from a fixture's set-up, because the middleware resolves its back-end once per process and
 * the fake therefore cannot be re-registered per case: without this, one case's configuration and
 * observations would leak into the next.
 *
 * @post Canned responses hold their documented defaults - true, true,
 *       SendMessageStatus::ACK_STATE_0, three ok statuses, the compiled-in version and the
 *       compiled-in hash; all three captures are empty; all three counters are zero.
 *
 * @see FakeHdmiCecService::reset()
 */
void FakeHdmiCecController::reset()
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    addLogicalAddressesResult = true;                                                       // Default: address acquired
    removeLogicalAddressesResult = true;                                                    // Default: address removed
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

    interfaceVersionResult = ::com::rdk::hal::hdmicec::IHdmiCecController::VERSION;          // Default: the frozen version
    interfaceHashResult = ::com::rdk::hal::hdmicec::IHdmiCecController::HASHVALUE;           // Default: the frozen hash

    std::cout << "[FakeHdmiCecController::reset] Canned responses, captures and counters restored to defaults"
              << std::endl;
}


/**
 * @brief Releases the fake service.
 *
 * Clears the static instance pointer when it still refers to this object, so a destroyed fake can
 * never be handed out by getInstance().  The binder registration is not withdrawn, because the pinned
 * C++ service manager exposes no service-removal API; a test therefore keeps its registered fake
 * alive for the life of the process and reuses it through reset().
 *
 * @post getInstance() no longer returns this object.
 *
 * @see setInstance(), getInstance()
 */
FakeHdmiCecService::~FakeHdmiCecService()
{
    if (instance == this) {
        instance = nullptr;
    }
}

/**
 * @brief Reports the HAL state of the fake service.
 *
 * Answers DEFAULT_STATE with an ok status, always.  There is deliberately no canned state and no
 * canned status to install: the middleware never calls this method, so a control that varied its
 * answer could not change any behaviour under test and would only imply coverage that does not
 * exist.  What the method does carry is its counter, and that is the whole of its value here.
 *
 * @param [out] _aidl_return              - Receives DEFAULT_STATE.  Left untouched when the pointer is
 *                                          null
 *
 * @return ::android::binder::Status              - Status
 * @retval ok                                     - Always
 *
 * @post getGetStateCallCount() has advanced by one, which is what makes the expectation below
 *       assertable.
 *
 * @warning The middleware deliberately does not consume this method: it tracks its own closed,
 *          closing and opened states and consulting the HAL's would create a second source of truth.
 *          The implementation exists to satisfy the pure-virtual interface, and its counter exists so
 *          a test can assert it stays at zero across a full session.
 *
 * @see FakeHdmiCecService::DEFAULT_STATE, getGetStateCallCount()
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
 * @brief Reads a property from the fake service.
 *
 * Always reports an empty optional, which is a valid "property not available" answer.  No metric
 * value is invented here: none of the properties this interface publishes has a legacy counterpart,
 * so a fabricated value could only ever be asserted against itself.
 *
 * @param [in]  property                  - Property requested.  Recorded only as a call count
 * @param [out] _aidl_return              - Receives an empty optional.  Left untouched when the
 *                                          pointer is null
 *
 * @return ::android::binder::Status              - Status
 * @retval ok                                     - Always; there is no canned status to install, for
 *                                                  the same reason getState() has none
 *
 * @post getGetPropertyCallCount() has advanced by one.
 *
 * @warning The middleware deliberately does not consume this method: reading the HAL's CEC version or
 *          its transmit metrics would be new behaviour with no legacy counterpart.  The
 *          implementation exists to satisfy the pure-virtual interface, and its counter exists so a
 *          test can assert it stays at zero.
 *
 * @see getGetPropertyCallCount()
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
 * @brief Reports the logical addresses the fake HAL holds.
 *
 * Copies the canned vector out verbatim, whatever its width: empty, exactly one entry, or more than
 * one.  It is neither normalised, sorted, deduplicated nor truncated, because each of those widths
 * reaches a different arm of the adapter under test - no address available, the ordinary case, and
 * "log the count and operate on the first entry only".
 *
 * @param [out] _aidl_return              - Receives a copy of the canned address vector.  Left
 *                                          untouched when the canned binder status is non-ok, and when
 *                                          the pointer is null
 *
 * @return ::android::binder::Status              - Status
 * @retval ok                                     - The canned vector was reported
 * @retval the installed non-ok status            - Returned before the out-parameter is written
 *
 * @post getGetLogicalAddressesCallCount() has advanced by one.
 *
 * @see setLogicalAddressesResult(), setGetLogicalAddressesBinderStatus(),
 *      getGetLogicalAddressesCallCount()
 */
::android::binder::Status FakeHdmiCecService::getLogicalAddresses(::std::vector<int32_t>* _aidl_return)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    ++getLogicalAddressesCallCount;

    std::cout << "[FakeHdmiCecService::getLogicalAddresses] Reporting " << logicalAddressesResult.size()
              << " address(es), status: " << getLogicalAddressesBinderStatus.toString8().c_str() << std::endl;

    if (!getLogicalAddressesBinderStatus.isOk()) {
        return getLogicalAddressesBinderStatus;
    }

    if (_aidl_return) {
        *_aidl_return = logicalAddressesResult;
    } else {
        std::cout << "[FakeHdmiCecService::getLogicalAddresses] Null out-parameter, addresses not written" << std::endl;
    }

    return ::android::binder::Status::ok();
}

/**
 * @brief Opens a controller session on the fake HAL and captures the event listener.
 *
 * Captures the listener - which is what stops the three triggers being no-ops - and reports the owned
 * controller, or a null controller when the null-controller flag is set.  That second combination,
 * an ok status carrying nothing usable, is only producible by asking for it, and it is what the
 * adapter under test must map to IOException rather than storing a null session and failing later.
 *
 * @param [in]  cecControllerListener     - Event listener the client supplies.  Captured, and
 *                                          retrievable through getListener().  The parameter name is
 *                                          the generated one; the type is IHdmiCecEventListener
 * @param [out] _aidl_return              - Receives the owned controller, or nullptr when the
 *                                          null-controller flag is set.  Left untouched when the
 *                                          canned binder status is non-ok, and when the pointer is
 *                                          null
 *
 * @return ::android::binder::Status              - Status
 * @retval ok                                     - A controller, or a deliberate nullptr, was reported
 * @retval the installed non-ok status            - Returned before the out-parameter is written,
 *                                                  including the EX_ILLEGAL_STATE the interface
 *                                                  documents for an already-open service
 *
 * @post getListener() reports the supplied listener and getOpenCallCount() has advanced by one.  The
 *       listener is captured even when a null controller is reported, so the receive path can be
 *       exercised against a session the adapter rejected.
 *
 * @see setOpenReturnsNullController(), setOpenBinderStatus(), getListener(), getController(),
 *      getOpenCallCount()
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
 * @brief Closes a controller session on the fake HAL.
 *
 * Captures the controller it was handed, so a test can prove the adapter closed the very session
 * open() handed out, and reports the canned boolean.@n
 * The captured listener is deliberately left in place.  A trigger fired after a close still has to
 * reach the adapter's listener, because "a callback arriving during or after a close is rejected by
 * the adapter's own state guard" is a required behaviour and clearing the listener here would make it
 * untestable.
 *
 * @param [in]  hdmiCecController         - Controller the client is closing.  Captured, and retrievable
 *                                          through getLastClosedController()
 * @param [out] _aidl_return              - Receives the canned boolean result.  Left untouched when the
 *                                          canned binder status is non-ok, and when the pointer is
 *                                          null
 *
 * @return ::android::binder::Status              - Status
 * @retval ok                                     - The canned boolean was reported
 * @retval the installed non-ok status            - Returned before the out-parameter is written
 *
 * @post getLastClosedController() reports the supplied controller, getCloseCallCount() has advanced by
 *       one, and getListener() still reports the listener captured by open().
 *
 * @see setCloseResult(), setCloseBinderStatus(), getLastClosedController(), getCloseCallCount()
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

    if (_aidl_return) {
        *_aidl_return = closeResult;
    } else {
        std::cout << "[FakeHdmiCecService::close] Null out-parameter, result not written" << std::endl;
    }

    return ::android::binder::Status::ok();
}

/**
 * @brief Registers an additional event listener on the fake HAL.
 *
 * Counts the call and reports true.  The offered listener is deliberately not retained: this fake
 * delivers events only to the listener captured by open(), so retaining a second one would invent a
 * delivery route the middleware never uses.
 *
 * @param [in]  cecEventListener          - Listener offered by a non-controlling client.  Counted only
 * @param [out] _aidl_return              - Receives true.  Left untouched when the pointer is null
 *
 * @return ::android::binder::Status              - Status
 * @retval ok                                     - Always; there is no canned status to install, for
 *                                                  the same reason getState() has none
 *
 * @post getRegisterEventListenerCallCount() has advanced by one.
 *
 * @warning The middleware deliberately does not consume this method: it is the controlling client and
 *          receives events through the listener it passes to open().  The implementation exists to
 *          satisfy the pure-virtual interface, and its counter exists so a test can assert it stays at
 *          zero.
 *
 * @see getRegisterEventListenerCallCount()
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
 * @brief Unregisters an additional event listener on the fake HAL.
 *
 * Counts the call and reports true.  Nothing is withdrawn, because registerEventListener() retains
 * nothing.
 *
 * @param [in]  cecEventListener          - Listener being withdrawn.  Counted only
 * @param [out] _aidl_return              - Receives true.  Left untouched when the pointer is null
 *
 * @return ::android::binder::Status              - Status
 * @retval ok                                     - Always; there is no canned status to install, for
 *                                                  the same reason getState() has none
 *
 * @post getUnregisterEventListenerCallCount() has advanced by one.
 *
 * @warning The middleware deliberately does not consume this method, for the same reason as
 *          registerEventListener().  Its counter exists so a test can assert it stays at zero.
 *
 * @see getUnregisterEventListenerCallCount()
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
 * @brief Reports the interface version this fake service claims.
 *
 * @return int32_t                                - The compiled-in
 *                                                  ::com::rdk::hal::hdmicec::IHdmiCec::VERSION
 *
 * @warning Effective under local (in-process) dispatch only.  Across a binder transaction the
 *          generated onTransact() answers this from the compiled-in constant, so a remote fake cannot
 *          report a divergent version at all.
 *
 * @note There is no setter for this value.  The version arms of the middleware's compatibility check
 *       are reached at unit level, by locally constructed doubles in
 *       tests/L1Tests/ccec/test_DriverAidl.cpp, rather than through the registered fake; a setter here
 *       would suggest a route the suite does not take.
 *
 * @see reset(), setInterfaceHash()
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
 * @brief Reports the interface hash this fake service claims.
 *
 * @return std::string                            - The hash setInterfaceHash() installed.  Default:
 *                                                  ::com::rdk::hal::hdmicec::IHdmiCec::HASHVALUE
 *
 * @warning Effective under local (in-process) dispatch only, exactly as for getInterfaceVersion().
 *
 * @see setInterfaceHash()
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
 * @brief Selects the address vector getLogicalAddresses() reports.
 *
 * Reaches three distinct adapter arms, which is why the parameter is a whole vector rather than a
 * single address: empty, where the adapter must report no address at all; exactly one entry, the
 * ordinary case; and more than one entry, where the adapter must log the count and operate on the
 * first entry only, adding no multi-address state, no iteration and no dispatch fan-out.
 *
 * @param [in] logicalAddresses           - Addresses to report.  Default: a one-entry vector holding
 *                                          DEFAULT_LOGICAL_ADDRESS
 *
 * @post The next getLogicalAddresses() call reports a copy of this vector.
 *
 * @see getLogicalAddresses(), DEFAULT_LOGICAL_ADDRESS
 */
void FakeHdmiCecService::setLogicalAddressesResult(const ::std::vector<int32_t>& logicalAddresses)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    logicalAddressesResult = logicalAddresses;
}

/**
 * @brief Selects the boolean close() reports.
 *
 * Reaches the failed-close arm, where the adapter under test must still complete its own transition to
 * closed before raising, so that a subsequent open is not blocked by a half-closed session.
 *
 * @param [in] result                     - Value close() reports.  Default: true
 *
 * @post The next close() call reports this value.
 *
 * @see close()
 */
void FakeHdmiCecService::setCloseResult(bool result)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    closeResult = result;
}

/**
 * @brief Selects whether open() reports a null controller alongside an ok status.
 *
 * Reaches the null-controller arm: a HAL that answered successfully and handed back nothing usable
 * must be turned into IOException by the adapter rather than stored as a null session that fails on
 * first use.  The combination is not otherwise producible, since a successful open always yields the
 * owned controller.
 *
 * @param [in] returnsNull                - true to report a null controller, false to report the owned
 *                                          controller.  Default: false
 *
 * @post The next open() call reports nullptr or the owned controller accordingly, and captures the
 *       listener either way.
 *
 * @see open(), getController()
 */
void FakeHdmiCecService::setOpenReturnsNullController(bool returnsNull)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    openReturnsNullController = returnsNull;
}

/**
 * @brief Installs the binder status open() returns.
 *
 * Reaches the open transport-failure arm, which the adapter under test must translate into IOException.
 * This is also where the EX_ILLEGAL_STATE the interface documents for an already-open service is
 * expressed, since the fake runs no state machine that could raise it by itself.
 *
 * @param [in] status                     - Status to return, ok or non-ok.  Default: ok
 *
 * @post The next open() call returns this status, and writes its out-parameter only when the status is
 *       ok.  The listener is captured regardless.
 *
 * @see open()
 */
void FakeHdmiCecService::setOpenBinderStatus(const ::android::binder::Status& status)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    openBinderStatus = status;
}

/**
 * @brief Installs the binder status close() returns.
 *
 * Reaches the close transport-failure arm, where the adapter must still reach its own closed state
 * before raising.
 *
 * @param [in] status                     - Status to return, ok or non-ok.  Default: ok
 *
 * @post The next close() call returns this status, and writes its out-parameter only when the status is
 *       ok.
 *
 * @see close()
 */
void FakeHdmiCecService::setCloseBinderStatus(const ::android::binder::Status& status)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    closeBinderStatus = status;
}

/**
 * @brief Installs the binder status getLogicalAddresses() returns.
 *
 * Reaches the failed-query arm, which the adapter must report as no address available - the same
 * observable outcome as a successful but empty query, distinguished in the log rather than in the
 * return value.
 *
 * @param [in] status                     - Status to return, ok or non-ok.  Default: ok
 *
 * @post The next getLogicalAddresses() call returns this status, and writes its out-parameter only when
 *       the status is ok.
 *
 * @see getLogicalAddresses(), setLogicalAddressesResult()
 */
void FakeHdmiCecService::setGetLogicalAddressesBinderStatus(const ::android::binder::Status& status)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    getLogicalAddressesBinderStatus = status;
}

/**
 * @brief Overrides the interface hash this fake service claims.
 *
 * The one metadata control on either fake class.  It publishes a service the middleware finds and then
 * refuses, which is what makes the FACTORY-level fallback from a present but incompatible service
 * observable: the selection is the thing under test there, not the compatibility predicate itself.  The
 * parameter is an arbitrary string, because the value that produces that outcome belongs to the harness.
 *
 * @param [in] hash                       - Hash string to report.  Default:
 *                                          ::com::rdk::hal::hdmicec::IHdmiCec::HASHVALUE
 *
 * @post getInterfaceHash() reports this value and traces it when it differs from the default.
 *
 * @warning Effective under local (in-process) dispatch only.  A remotely served fake answers the
 *          metadata transactions from its compiled-in constants, so this has no effect out of process.
 *
 * @note The one consumer is the L1 harness in tests/L1Tests/test_main.cpp, whose `incompatible` mode
 *       installs the broken hash "-1" here before the middleware's selection resolves.  The empty-hash,
 *       "notfrozen" and version rejection arms are covered by locally constructed doubles in
 *       tests/L1Tests/ccec/test_DriverAidl.cpp instead, because a served Bn* object cannot report bad
 *       metadata at all; none of them reaches this setter.
 *
 * @see getInterfaceHash()
 */
void FakeHdmiCecService::setInterfaceHash(std::string hash)
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    std::cout << "[FakeHdmiCecService::setInterfaceHash] Hash set from \"" << interfaceHashResult
              << "\" to \"" << hash << "\"" << std::endl;
    interfaceHashResult = ::std::move(hash);
}

/**
 * @brief Returns the controller this service owns and hands out from open().
 *
 * The route by which a test configures the controller's canned responses and reads its captures,
 * whether or not a session has been opened yet: the controller is created with the service and never
 * replaced, so a reference taken before open() stays valid afterwards.
 *
 * @return ::android::sp<FakeHdmiCecController>   - The owned controller, never null
 *
 * @see open(), FakeHdmiCecController
 */
::android::sp<FakeHdmiCecController> FakeHdmiCecService::getController() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return controller;
}

/**
 * @brief Returns the event listener captured by the last open() call.
 *
 * Lets a test confirm the adapter actually supplied a listener before it asserts anything about the
 * receive path, so a silent no-op trigger is not mistaken for a delivery failure.
 *
 * @return ::android::sp<::com::rdk::hal::hdmicec::IHdmiCecEventListener> - The captured listener, null
 *                                                                          when open() has not been
 *                                                                          called or reset() has
 *                                                                          cleared it
 *
 * @see open(), fireOnMessageReceived()
 */
::android::sp<::com::rdk::hal::hdmicec::IHdmiCecEventListener> FakeHdmiCecService::getListener() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return listener;
}

/**
 * @brief Returns the controller captured by the last close() call.
 *
 * The assertion target for "the session was closed with the controller that open() handed out", which
 * is what proves the adapter kept the two paired rather than closing something else.  A driver that
 * closed a null or a stale controller would still receive this fake's canned result, so without this
 * capture that mistake leaves no trace.
 *
 * @return ::android::sp<::com::rdk::hal::hdmicec::IHdmiCecController> - The captured controller, null
 *                                                                       when close() has not been
 *                                                                       called
 *
 * @note Consumed by the close-contract cases of the AIDL back-end's L1 suite, which assert this equals
 *       the exact controller open() reported, on the successful close, the false-result close and the
 *       non-ok-status close alike.
 *
 * @see close(), getController(), setCloseResult(), setCloseBinderStatus()
 */
::android::sp<::com::rdk::hal::hdmicec::IHdmiCecController> FakeHdmiCecService::getLastClosedController() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return lastClosedController;
}

/**
 * @brief Returns how many times open() has been called.
 *
 * @return int32_t                                - Call count since construction or the last reset()
 *
 * @see open(), reset()
 */
int32_t FakeHdmiCecService::getOpenCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return openCallCount;
}

/**
 * @brief Returns how many times close() has been called.
 *
 * The assertion target for "a second close on an already-closed session never reaches the HAL", which
 * the adapter's own guard is required to prevent.
 *
 * @return int32_t                                - Call count since construction or the last reset()
 *
 * @see close(), reset()
 */
int32_t FakeHdmiCecService::getCloseCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return closeCallCount;
}

/**
 * @brief Returns how many times getLogicalAddresses() has been called.
 *
 * @return int32_t                                - Call count since construction or the last reset()
 *
 * @see getLogicalAddresses(), reset()
 */
int32_t FakeHdmiCecService::getGetLogicalAddressesCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return getLogicalAddressesCallCount;
}

/**
 * @brief Returns how many times getState() has been called.
 *
 * The assertion target for "the adapter never consults the HAL's state machine": this counter must stay
 * zero across a full open, transmit, receive and close cycle.
 *
 * @return int32_t                                - Call count since construction or the last reset()
 *
 * @see getState(), reset()
 */
int32_t FakeHdmiCecService::getGetStateCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return getStateCallCount;
}

/**
 * @brief Returns how many times getProperty() has been called.
 *
 * The assertion target for "the adapter never reads HAL properties": this counter must stay zero across
 * a full session, since no property has a legacy counterpart.
 *
 * @return int32_t                                - Call count since construction or the last reset()
 *
 * @see getProperty(), reset()
 */
int32_t FakeHdmiCecService::getGetPropertyCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return getPropertyCallCount;
}

/**
 * @brief Returns how many times registerEventListener() has been called.
 *
 * The assertion target for "the adapter receives events through the listener it passed to open()": this
 * counter must stay zero, because the middleware is the controlling client and never registers a
 * diagnostic listener.
 *
 * @return int32_t                                - Call count since construction or the last reset()
 *
 * @see registerEventListener(), reset()
 */
int32_t FakeHdmiCecService::getRegisterEventListenerCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return registerEventListenerCallCount;
}

/**
 * @brief Returns how many times unregisterEventListener() has been called.
 *
 * The assertion target for the same expectation as getRegisterEventListenerCallCount(): this counter
 * must stay zero.
 *
 * @return int32_t                                - Call count since construction or the last reset()
 *
 * @see unregisterEventListener(), reset()
 */
int32_t FakeHdmiCecService::getUnregisterEventListenerCallCount() const
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    return unregisterEventListenerCallCount;
}

/**
 * @brief Restores every canned response to its default and clears every capture and counter.
 *
 * Called from a fixture's set-up, because the middleware resolves its back-end once per process and the
 * fake therefore cannot be re-registered per case: without this, one case's configuration and
 * observations would leak into the next.  The captured listener is cleared too, which returns the
 * triggers to their pre-open no-op behaviour.
 *
 * @post Canned responses hold their documented defaults - a one-entry address vector, true, false,
 *       three ok statuses, the compiled-in version and the compiled-in hash; the captured listener and
 *       closed controller are null; all seven counters are zero.
 * @post The owned controller is unchanged and still handed out by open().
 *
 * @warning This resets the service only.  Reset the controller through getController()->reset() when a
 *          case also configured it.
 *
 * @see FakeHdmiCecController::reset()
 */
void FakeHdmiCecService::reset()
{
    ::std::lock_guard<::std::mutex> guard(mutex);

    listener = nullptr;
    lastClosedController = nullptr;

    logicalAddressesResult = ::std::vector<int32_t> { DEFAULT_LOGICAL_ADDRESS };              // Default: Playback device
    closeResult = true;                                                                       // Default: session closed
    openReturnsNullController = false;                                                        // Default: a valid controller

    /*
     * Three statuses, not seven. getState(), getProperty(), registerEventListener() and
     * unregisterEventListener() answer a fixed ok, because the middleware never calls them and a
     * settable failure arm on a method nothing under test reaches would imply coverage that does not
     * exist. Their counters are still cleared below: those are what "the adapter never called this"
     * is asserted against.
     */
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
 * @brief Delivers a received CEC message to the captured event listener.
 *
 * The entry point for the receive path.  The caller supplies the exact bytes, so the fake forms no
 * frame of its own; out of process the call crosses the binder driver and the listener runs on the
 * client's binder threadpool, which is the only arrangement in which that threadpool is genuinely
 * exercised.
 *
 * @param [in] message                    - Raw CEC frame bytes to deliver
 *
 * @return bool                                   - Whether the callback was invoked
 * @retval true                                   - A listener was captured and the callback ran, whatever
 *                                                  status the listener itself reported
 * @retval false                                  - No listener captured, so nothing was delivered
 *
 * @pre open() must have captured a listener, otherwise this is a traced no-op.
 *
 * @post The status the listener returned - the callback is declared oneway yet still returns one - has
 *       been traced rather than discarded.
 *
 * @warning Fired before open(), or after reset() cleared the captured listener, this delivers nothing
 *          and reports false.  A test that ignores the return value cannot tell a genuine delivery from
 *          a no-op.
 * @warning The lock is released before the listener is invoked, deliberately: a callback that re-enters
 *          this fake would otherwise deadlock against a lock held across the call.
 *
 * @see open(), getListener()
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
 * @brief Delivers a state-change notification to the captured event listener.
 *
 * Covers the diagnostic callback the adapter under test is required to log and act on in no other way:
 * an in-process HAL cannot vanish, so a state transition has no legacy counterpart and reacting to one
 * would be new behaviour.
 *
 * @param [in] oldState                   - State being left
 * @param [in] newState                   - State being entered
 *
 * @return bool                                   - Whether the callback was invoked
 * @retval true                                   - A listener was captured and the callback ran
 * @retval false                                  - No listener captured, so nothing was delivered
 *
 * @pre open() must have captured a listener, otherwise this is a traced no-op.
 *
 * @warning Fired before open(), or after reset(), this delivers nothing and reports false.
 * @warning The lock is released before the listener is invoked, for the same reason as
 *          fireOnMessageReceived().
 *
 * @note Called in process by
 *       DriverAidlSessionTest.DiagnosticCallbacksAreReportedWithoutDisturbingTheSession, which reads
 *       what the adapter logged.  The separate-process host exposes no command for this trigger,
 *       because that assertion is already made in process.
 *
 * @see open(), fireOnMessageReceived()
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
 * @brief Delivers a transmit-completion notification to the captured event listener.
 *
 * Covers the second diagnostic callback the adapter under test is required to log and act on in no
 * other way: synchronous transmit already returns its own status, so nothing about the outcome depends
 * on this notification arriving.
 *
 * @param [in] message                    - Frame the notification refers to
 * @param [in] status                     - Send status reported for it
 *
 * @return bool                                   - Whether the callback was invoked
 * @retval true                                   - A listener was captured and the callback ran
 * @retval false                                  - No listener captured, so nothing was delivered
 *
 * @pre open() must have captured a listener, otherwise this is a traced no-op.
 *
 * @warning Fired before open(), or after reset(), this delivers nothing and reports false.
 * @warning The lock is released before the listener is invoked, for the same reason as
 *          fireOnMessageReceived().
 *
 * @note Called in process by the same case as fireOnStateChanged(), and exposed by no host command
 *       for the same reason.
 *
 * @see open(), fireOnMessageReceived(), fireOnStateChanged()
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
 * @brief Returns the fake a test harness published for this process.
 *
 * The route by which a test reaches the registered fake in order to configure it, given that the
 * harness registers the fake before the middleware is initialised while the test bodies run later.
 *
 * @return FakeHdmiCecService*                    - The published fake, or nullptr when none was set
 *
 * @note This accessor traces nothing, and that is deliberate - recorded here so that nobody restores a
 *       trace on the assumption it was overlooked.  It is reached from the harness, from every fixture
 *       and from the fake's own paths, so a line per call floods every captured log and dilutes the
 *       lines a case actually asserts on, while reporting only what setInstance() already reported once
 *       at the moment the pointer changed.  Nor is it hidden behind a level: a quiet level is the same
 *       flood one configuration change away, and an opt-in nothing consumes is not worth the branch.
 *
 * @see setInstance(), registerFakeHdmiCecService()
 */
FakeHdmiCecService* FakeHdmiCecService::getInstance()
{
    return instance;
}

/**
 * @brief Records the fake a test harness published for this process.
 *
 * Called by the harness right after it constructs and registers the fake, and called with nullptr when
 * it tears it down.  This is a single pointer at test scope, not a service registry and not a factory,
 * following the same two-function idiom the legacy driver double in this directory already uses.
 *
 * @param [in] newFake                    - Fake to publish, or nullptr to clear
 *
 * @post getInstance() returns the supplied pointer.
 *
 * @see getInstance()
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

// Service-manager registration that publishes the fake under the production service name

/**
 * @brief Publishes a fake HdmiCec service under the production service name.
 *
 * Registration is an explicit callable step rather than something a constructor does, because the two
 * callers need different things around it: an in-process harness registers and stops there, since a
 * locally registered name resolves to this very object and no transaction crosses the driver, while the
 * separate host binary registers and then starts a binder threadpool so it can serve real transactions.
 * Folding a threadpool into registration would force one on the in-process caller.@n
 * The name comes from ::com::rdk::hal::hdmicec::IHdmiCec::serviceName(), never from a literal, so there
 * is exactly one spelling of it in the build and this fake cannot drift from the name the middleware
 * looks up.  Only the service is published: a client obtains its controller from the out-parameter of
 * open(), so the controller is never registered separately.
 *
 * The binder driver node is checked before libbinder is touched at all.  The linked libbinder treats a
 * driver it cannot open as fatal and terminates the process, so without that check a host with no kernel
 * binder support would lose the whole test run - every legacy case included - instead of seeing this
 * function report false.  A driver present but speaking a different protocol version remains fatal
 * inside libbinder; policing that is the middleware's own preflight, and duplicating it here would put
 * a second copy of a production decision into a test fake.
 *
 * @param [in] service                    - Fake to publish.  Must not be null
 *
 * @return bool                                   - Whether the service was published
 * @retval true                                   - The service manager accepted the registration
 * @retval false                                  - The service was null, the binder driver node was
 *                                                  absent or unopenable, the service manager was
 *                                                  unreachable, or it refused the registration
 *
 * @pre A binder driver node must be present and a service manager reachable.  Where neither is, this
 *      reports false rather than aborting.
 * @pre No service may already be registered under the production name, since the pinned C++ service
 *      manager exposes no removal API and a stale registration would decide the outcome of the run.
 * @post On success the middleware's own service lookup resolves to the supplied fake, which is what
 *       makes its runtime back-end selection choose the AIDL path.
 *
 * @warning Test scope only.  Nothing in a production source list may call this.
 *
 * @see FakeHdmiCecService, FakeHdmiCecService::setInstance()
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
