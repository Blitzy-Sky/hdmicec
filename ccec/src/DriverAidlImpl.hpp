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



/**
* @defgroup hdmicec
* @{
* @defgroup ccec
* @{
**/


/**
 * @file DriverAidlImpl.hpp
 *
 * @brief Declaration of the AIDL/binder back-end of the CCEC middleware Driver interface
 *
 * This is the second of the two CCEC::Driver implementations the middleware ships.
 * It adapts the out-of-process `com.rdk.hal.hdmicec` AIDL HAL onto exactly the same
 * CCEC::Driver contract that DriverImpl implements over the legacy in-process C ABI
 * (`libRCECHal.so`).@n
 * Exactly one of the two is selected once, during initialization, by
 * Driver::getInstance(), on the basis of a runtime query for the `"HdmiCec"` binder
 * service. The selection is then stable for the lifetime of the process. Nothing
 * above the Driver seam changes: the frame queue, the Bus reader and writer threads,
 * Connection, FrameListener and LibCCEC are untouched, and only the thread that
 * produces received frames into the incoming queue differs between the two
 * back-ends.
 *
 * This header is deliberately NOT an installed public header. It is absent from the
 * `nobase_include_HEADERS` list in `hdmicec/Makefile.am`, exactly as `DriverImpl.hpp`
 * is, which is what allows a second back-end to exist without altering the middleware
 * public API. Production code reaches it only from `ccec/src`; test translation units
 * reach it through the established relative route
 * `#include "../../../ccec/src/DriverAidlImpl.hpp"`.
 *
 * @note Include-order contract, and it is load bearing: every AIDL and binder header
 *       is included BEFORE CCEC_BEGIN_NAMESPACE. The generated stubs open
 *       `namespace com::rdk::hal::hdmicec` and transitively pull the `binder/` and
 *       `utils/` SDK headers that open `namespace android`. Including any of them
 *       after CCEC_BEGIN_NAMESPACE would nest those declarations as
 *       `CCEC::com::rdk::hal::hdmicec` and `CCEC::android`, which breaks the link and
 *       silently violates the one-definition rule.
 * @warning This header requires C++17. The generated AIDL stubs include `<optional>`
 *          and `IHdmiCec::getProperty()` takes a `std::optional<PropertyValue>*`, so a
 *          C++14 translation unit cannot compile this file.
 * @warning This is the C++ libbinder AIDL backend, not the NDK backend: the pointer
 *          type is `android::sp<>`, the status type is `android::binder::Status`, and
 *          the server bases are the generated `Bn*` classes.
 *
 * @see hdmicec/ccec/src/DriverImpl.hpp
 * @see hdmicec/ccec/include/ccec/Driver.hpp
 */

#ifndef HDMI_CCEC_DRIVER_AIDL_IMPL_HPP_
#define HDMI_CCEC_DRIVER_AIDL_IMPL_HPP_

#include <list>
#include <string>

/*
 * AIDL and binder headers. These MUST stay above CCEC_BEGIN_NAMESPACE - see the
 * include-order note in the file block above. Both interface headers are required at
 * header level rather than forward declared, because the android::sp<> session members
 * below are complete-type class members.
 *
 * <com/rdk/hal/hdmicec/BnHdmiCecEventListener.h> is deliberately NOT included here.
 * The nested EventListener is forward declared below and defined in DriverAidlImpl.cpp,
 * which keeps the listener's server-side base out of every translation unit that merely
 * includes this header.
 */
#include <com/rdk/hal/hdmicec/IHdmiCec.h>
#include <com/rdk/hal/hdmicec/IHdmiCecController.h>
#include <utils/StrongPointer.h>

/*
 * LOG_FATAL reconciliation, required because two independent definitions collide.
 *
 * The binder SDK's log/log_main.h:165 defines LOG_FATAL as a function-like macro,
 * guarded by #ifndef. ccec/Util.hpp:55 defines it, unguarded, as the integer log level
 * 0, and CCEC code consumes it as a value - CCEC_LOG(LOG_FATAL, ...). Left alone the
 * two orders behave differently: binder-then-ccec merely warns about a redefinition,
 * while ccec-then-binder leaves the function-like macro in force and turns any later
 * use of LOG_FATAL as a value into a hard compile error.
 *
 * Dropping the binder spelling here and restoring the CCEC value below makes this
 * header order independent: whichever way round a translation unit includes things,
 * LOG_FATAL ends up as the CCEC log level, with no diagnostic. Nothing in the binder
 * SDK headers consumes a bare LOG_FATAL - they use LOG_FATAL_IF and LOG_ALWAYS_FATAL,
 * which are separately defined and untouched here - so removing it costs nothing.
 */
#undef LOG_FATAL

#include "osal/Mutex.hpp"
#include "osal/EventQueue.hpp"

#include "ccec/Driver.hpp"
#include "ccec/Header.hpp"

/*
 * Restores the CCEC log level when ccec/Util.hpp was already included by this
 * translation unit and its include guard therefore skipped the block above. The value
 * is identical to ccec/Util.hpp:55 and this never redefines an existing definition.
 */
#ifndef LOG_FATAL
/** @brief CCEC fatal log level, identical to the definition at ccec/Util.hpp:55. */
#define LOG_FATAL 0
#endif

using CCEC_OSAL::EventQueue;
using CCEC_OSAL::Mutex;

CCEC_BEGIN_NAMESPACE

/**
 * @brief AIDL/binder implementation of the CCEC middleware Driver interface
 *
 * DriverAidlImpl is a drop-in sibling of DriverImpl. It implements every CCEC::Driver
 * virtual with the same signature, the same guards, the same statement order and the
 * same exceptions, substituting the `com.rdk.hal.hdmicec` AIDL calls for the legacy
 * HDMI CEC C API. Where the legacy implementation contains something a reviewer would
 * ordinarily correct - the `#if 0`'d throws in open() and close(), getLogicalAddress()
 * ignoring its `devType` argument, removeLogicalAddress() discarding the HAL return,
 * close() leaving the local address list populated, or writeAsync() doing frame work
 * before its state guard - this class reproduces the OBSERVABLE behaviour rather than
 * the improved one, because callers and the existing test suite depend on it.
 *
 * Three observable differences from the legacy back-end are authorized, each forced by
 * the AIDL contract and each documented on the method that carries it: the 16-byte
 * frame limit enforced by write(), the OperationNotSupportedException raised by
 * writeAsync(), and the coarser failure category reported by addLogicalAddress(). Any
 * other observable difference is a defect.
 *
 * The AIDL surface is split across two interfaces and only nine of its thirteen
 * methods are consumed. `IHdmiCec` supplies open(), close() and getLogicalAddresses();
 * `IHdmiCecController`, obtained from open(), supplies addLogicalAddresses(),
 * removeLogicalAddresses() and sendMessage(); the listener supplies
 * onMessageReceived(), onStateChanged() and onMessageSent(). `getState()`,
 * `getProperty()`, `registerEventListener()` and `unregisterEventListener()` are
 * deliberately not consumed: the middleware keeps its own state machine, the HAL
 * properties have no legacy counterpart, and this back-end is the controlling client
 * and so receives events through the listener it hands to open().
 *
 * Construction touches no binder at all, which is what makes the factory's
 * construct-then-query shape safe on a legacy-only SOC. Every binder interaction
 * begins either in isServiceAvailable() or in open().
 *
 * @warning Instances are not copyable. Exactly one is expected to exist, as the
 *          function-local static held by Driver::getInstance().
 * @warning The receive path delivers on a binder threadpool thread, so the incoming
 *          queue is written from a thread this class does not own. That queue is the
 *          existing cross-thread synchronization point and needs no change.
 *
 * @see DriverImpl - the legacy back-end this class mirrors
 * @see Driver::getInstance() - the single selection point between the two
 */
class DriverAidlImpl : public Driver
{
public:
	/**
	 * @brief Queue of received CEC frames, drained by the Bus reader thread
	 *
	 * Identical to DriverImpl::IncomingQueue. Frames are heap allocated by the
	 * listener, offered here, and taken and deleted by read(). A NULL entry is the
	 * sentinel close() uses to wake a blocked reader.
	 */
	typedef EventQueue<CECFrame *> IncomingQueue;

	/**
	 * @brief Lifecycle state of this back-end
	 *
	 * The same three-state machine DriverImpl runs, with the same values, so that the
	 * state guards on both back-ends are directly comparable. CLOSING exists so that a
	 * receive callback or a blocked reader arriving during teardown is rejected rather
	 * than served.
	 */
	enum {
		CLOSED = 0,
		CLOSING,
		OPENED,
	};

	/**
	 * @brief Default binder driver node inspected by the preflight predicate
	 *
	 * The node libbinder itself opens on this platform. It is exposed as a named
	 * constant, and as the default argument of isBinderPreflightOk(), so that the
	 * production call site names nothing and a test can pass a path of its own.
	 */
	static constexpr const char *DEFAULT_BINDER_DRIVER_PATH = "/dev/binder";

	/**
	 * @brief Default bound, in milliseconds, on resolving the binder context manager
	 *
	 * Obtaining an `IServiceManager` at all polls until binder handle 0 resolves, in
	 * one-second intervals and without an upper bound of its own. This value bounds
	 * the preflight's own probe instead, and is large enough to tolerate one such
	 * interval while keeping LibCCEC::init() bounded on a platform whose
	 * `servicemanager` never started.
	 */
	static constexpr unsigned int DEFAULT_CONTEXT_MANAGER_TIMEOUT_MS = 2000;

	/**
	 * @brief Ceiling, in milliseconds, that a caller-supplied context-manager timeout is
	 *        clamped to
	 *
	 * isBinderPreflightOk() takes its timeout as an `unsigned int`, so a caller - a test, or
	 * a future platform-integration knob - can name a value far larger than any plausible
	 * `servicemanager` start-up delay, and every millisecond of it is time LibCCEC::init()
	 * would spend blocked. The parameter is therefore clamped to this ceiling, and the
	 * clamp is logged so a mis-set value is visible rather than silently obeyed.@n
	 * It is an order of magnitude above DEFAULT_CONTEXT_MANAGER_TIMEOUT_MS, so the default
	 * is never clamped and no legitimate integration value is truncated, while an
	 * initialization stall stays bounded by a figure a human can reason about.
	 *
	 * @warning Must remain greater than or equal to DEFAULT_CONTEXT_MANAGER_TIMEOUT_MS, or
	 *          the default itself would be clamped and the default path would log on every
	 *          start-up.
	 *
	 * @see isBinderPreflightOk()
	 */
	static constexpr unsigned int MAX_CONTEXT_MANAGER_TIMEOUT_MS = 10000;

	/**
	 * @brief Number of received frames the incoming queue holds before it stops accepting
	 *
	 * The value CCEC_OSAL::EventQueue would have defaulted to, named here and passed to
	 * the queue explicitly by the constructor, so that the capacity this class REASONS
	 * ABOUT and the capacity the queue ENFORCES are the same number by construction rather
	 * than by coincidence. A future change to the OSAL default cannot silently desynchronize
	 * them.@n
	 * It matters because `EventQueue::offer()` returns void and DROPS SILENTLY when the
	 * queue is at capacity. The receive path therefore has to establish for itself that
	 * there is room before it parts with ownership of a frame - see offerReceivedFrame().@n
	 * The LAST of these slots is reserved for close()'s NULL sentinel: the receive path
	 * refuses at one below this number, so the wake-the-reader offer can never be the one
	 * the queue swallows. The effective depth available to received frames is therefore
	 * this value minus one.
	 *
	 * @see offerReceivedFrame()
	 * @see IncomingQueue
	 */
	static constexpr size_t INCOMING_QUEUE_CAPACITY = 32;

	/**
	 * @brief Constructs the AIDL back-end without touching binder
	 *
	 * Initializes the state to CLOSED, the legacy handle field to 0 and the local
	 * logical-address list to empty, exactly as DriverImpl::DriverImpl() does. No
	 * service lookup, no `ProcessState`, no threadpool and no driver-node access
	 * happen here.@n
	 * This is deliberate and it is what the selection order requires: both back-ends
	 * are always constructed, and only then is the AIDL one asked whether its service
	 * came up. A constructor that reached for binder would abort the process on a
	 * legacy-only SOC before the fallback could ever be taken.
	 *
	 * @post The instance is inert until isServiceAvailable() or open() is called.
	 * @warning Never throws, so that the enclosing function-local static in
	 *          Driver::getInstance() cannot fail to initialize.
	 *
	 * @see isServiceAvailable()
	 * @see DriverImpl::DriverImpl()
	 */
	DriverAidlImpl(void);

	/**
	 * @brief Destroys the back-end, closing an open session first
	 *
	 * Mirrors ~DriverImpl(): under the instance lock, a state other than CLOSED is
	 * closed through this class's own close(), and an exception escaping that close is
	 * caught and logged rather than propagated out of the destructor.
	 *
	 * @pre None. Safe on an instance that was never opened.
	 * @warning Does not propagate exceptions.
	 *
	 * @see close()
	 */
	virtual ~DriverAidlImpl();

	/**
	 * @brief Opens the AIDL HDMI CEC session and begins receiving CEC messages
	 *
	 * Reproduces DriverImpl::open() with `IHdmiCec::open()` substituted for
	 * `HdmiCecOpen()`. A call made while the state is not CLOSED returns SILENTLY: the
	 * legacy `throw InvalidStateException()` is `#if 0`'d out at DriverImpl.cpp:110-117,
	 * so a silent return is the observable legacy behaviour and is what this back-end
	 * must reproduce.@n
	 * On the way through, the process binder threadpool is started so that the `oneway`
	 * listener callbacks have a thread to arrive on, the nested EventListener is handed
	 * to `IHdmiCec::open()`, and the `IHdmiCecController` it returns is retained for the
	 * transmit and logical-address operations. The state then becomes OPENED.
	 *
	 * @throws IOException - The `IHdmiCec::open()` transaction reported a non-ok
	 *                       binder status, or it succeeded and returned a null
	 *                       controller. This mirrors the legacy mapping of an
	 *                       `HdmiCecOpen()` failure at DriverImpl.cpp:120-122.
	 *
	 * @pre isServiceAvailable() has returned true, so a compatible `IHdmiCec` proxy is
	 *      held. Calling open() without that is an IOException, not a crash.
	 * @post close() must be called to release the session.
	 * @warning Not idempotent HAL-side. `IHdmiCec.open()` admits a single controlling
	 *          client and fails with `EX_ILLEGAL_STATE` when a session is already open,
	 *          which is why this class runs the same CLOSED/CLOSING/OPENED machine the
	 *          legacy back-end runs.
	 *
	 * @see close()
	 * @see DriverImpl::open()
	 */
	virtual void  open(void) noexcept(false);

	/**
	 * @brief Closes the AIDL HDMI CEC session
	 *
	 * Reproduces DriverImpl::close() step for step. A call made while the state is not
	 * OPENED returns SILENTLY, for the same `#if 0` reason open() does. Otherwise the
	 * state becomes CLOSING, the NULL sentinel is offered onto the incoming queue so a
	 * blocked reader wakes and unwinds, the HAL session is released, the state becomes
	 * CLOSED, and only then is a failure reported - so a caller that swallows the
	 * exception still sees a consistently closed object.@n
	 * The local logical-address list is deliberately NOT cleared. DriverImpl::close()
	 * does not clear it either, so isValidLogicalAddress() can remain true across a
	 * close on the legacy path, and the HAL removes the addresses on its own side.
	 * Clearing here would be an unauthorized improvement.
	 *
	 * @throws IOException - The close transaction reported a non-ok binder status, or
	 *                       reported success with a false result. The state is already
	 *                       CLOSED when this is raised, matching DriverImpl.cpp:145-149.
	 *
	 * @pre None. Safe to call on a closed instance, which returns silently.
	 * @post The state is CLOSED and no controller is held.
	 * @warning BLOCKED ITEM B2. `IHdmiCec.close()` is a HIGH-CONFIDENCE CANDIDATE for
	 *          the legacy `HdmiCecClose()`, PENDING OWNER CONFIRMATION: the HAL mapping
	 *          table carries no entry for `HdmiCecClose()`, and the candidate is used
	 *          here only because a back-end that cannot close is not deliverable - every
	 *          `LibCCEC::term()` would leak an open session and the next open() would
	 *          fail `EX_ILLEGAL_STATE`. If the mapping owners reject the candidate, the
	 *          body of this one method changes and nothing else does.
	 *
	 * @see open()
	 * @see DriverImpl::close()
	 */
	virtual void  close(void) noexcept(false);

	/**
	 * @brief Takes the next received CEC frame, blocking until one arrives
	 *
	 * Byte-for-byte the legacy DriverImpl::read(): the same queue, the same
	 * `status != OPENED` guard, and the same behaviour on the NULL sentinel, which
	 * flushes whatever remains in the queue and then raises. There is no AIDL call in
	 * this method at all, and that is precisely what leaves the Bus reader thread
	 * unchanged by this migration - only the thread that PRODUCES into the queue
	 * differs between the two back-ends.
	 *
	 * @param [out] frame - Receives a copy of the frame taken from the incoming queue.
	 *                      Left untouched when the call raises.
	 *
	 * @throws InvalidStateException - The driver was not OPENED on entry, or it stopped
	 *                                 being OPENED while this call was blocked, in
	 *                                 which case the queue is flushed first.
	 *
	 * @pre open() has completed successfully.
	 * @warning Blocks the calling thread until a frame or the close sentinel arrives.
	 *          It is called from the Bus reader thread, never from a plugin thread.
	 *
	 * @see close()
	 * @see DriverImpl::read()
	 */
	virtual void  read(CECFrame &frame) noexcept(false);

	/**
	 * @brief Transmits a CEC frame synchronously and reports the bus outcome
	 *
	 * Reproduces DriverImpl::write() including its statement order: the frame buffer is
	 * taken and the frame is logged, then the lock is acquired and the state checked,
	 * then the transmit runs with the lock still held. `IHdmiCecController::sendMessage()`
	 * replaces `HdmiCecTx()`, and the returned `SendMessageStatus` is translated onto
	 * the legacy exception set.@n
	 * That translation has to respect an INVERTED sense: `ACK_STATE_0` means
	 * acknowledged for a directed message but rejected for a broadcast, and `ACK_STATE_1`
	 * is the mirror of that. The destination nibble is read from `frame.at(0) & 0x0F`
	 * exactly as the legacy implementation reads it, and the CEC CTS 9-3-3 arm - a
	 * broadcast REPORT_PHYSICAL_ADDRESS that was rejected - raises so the caller retries.
	 *
	 * @param [in] frame - The frame to transmit, header byte first. Must be no longer
	 *                     than 16 bytes.
	 *
	 * @throws IOException           - The `sendMessage()` transaction reported a non-ok
	 *                                 binder status; or the HAL reported `BUSY`, meaning
	 *                                 arbitration failed and nothing was sent; or the
	 *                                 frame exceeded the 16-byte limit.
	 * @throws CECNoAckException     - A directed message was not acknowledged, or the
	 *                                 CEC CTS 9-3-3 broadcast arm was hit.
	 * @throws InvalidStateException - The driver is not OPENED.
	 *
	 * @pre open() has completed successfully.
	 * @warning AUTHORIZED OBSERVABLE DIFFERENCE 1. `sendMessage()` states a 16-byte
	 *          maximum while CECFrame carries up to CECFrame::MAX_LENGTH (128) and the
	 *          legacy HAL specification allows 20, so a frame of 17 to 20 bytes is
	 *          sendable on the legacy back-end and raises IOException here. The frame is
	 *          POLICED, never truncated: truncating would put a corrupt CEC frame on the
	 *          bus. No frame the current plugin surface produces comes close to the limit.
	 * @warning Holds the instance lock across the whole IPC round trip, exactly as the
	 *          legacy implementation holds it across the whole in-process call. This
	 *          preserves the existing serialization rather than introducing new
	 *          contention.
	 *
	 * @see poll()
	 * @see DriverImpl::write()
	 */
	virtual void  write(const CECFrame &frame) noexcept(false);

	/**
	 * @brief Not supported on the AIDL back-end; raises after the legacy prelude
	 *
	 * Asynchronous transmit is NOT migrated. The AIDL HAL exposes no asynchronous
	 * transmit and none is emulated here - no threads, no work queue, no deferred
	 * callback and no wrapper. This is a decided point, not an unresolved mapping, and
	 * it is safe because no production call site reaches this method: every plugin
	 * transmit goes through Connection::sendToAsync() and Connection::sendAsync() onto
	 * the Bus writer thread, which then calls the SYNCHRONOUS write().@n
	 * The legacy statement order is nevertheless reproduced exactly, because it is
	 * observable. DriverImpl::writeAsync() takes the frame buffer and logs the frame
	 * BEFORE it locks and checks the state, so an empty frame on a closed driver raises
	 * `std::out_of_range` from the frame access rather than InvalidStateException. This
	 * override runs the same two prelude calls, applies the same state guard, and only
	 * then declines the transmit.
	 *
	 * @param [in] frame - The frame that would have been transmitted. Still accessed by
	 *                     the prelude, so an empty frame raises before the guard runs.
	 *
	 * @throws std::out_of_range               - Raised by the prelude for an empty
	 *                                           frame, on both back-ends alike.
	 * @throws InvalidStateException           - The driver is not OPENED.
	 * @throws OperationNotSupportedException  - Always, once the prelude and the guard
	 *                                           have passed.
	 *
	 * @pre None beyond the prelude's own requirement that the frame be non-empty.
	 * @warning AUTHORIZED OBSERVABLE DIFFERENCE 2. A valid frame on an open driver
	 *          succeeds on the legacy back-end and raises
	 *          OperationNotSupportedException here. The two closed-driver and
	 *          empty-frame cases behave identically on both, because the prelude
	 *          precedes the guard on both.
	 *
	 * @see write() - the synchronous path every production caller actually reaches
	 * @see DriverImpl::writeAsync()
	 */
	virtual void  writeAsync(const CECFrame &frame) noexcept(false);

	/**
	 * @brief Relinquishes one logical address
	 *
	 * Reproduces DriverImpl::removeLogicalAddress(), whose shape is the specification:
	 * the state guard first, then the removal from the LOCAL list, and only then the
	 * HAL call - whose result the legacy implementation IGNORES. That disposition is
	 * kept. `IHdmiCecController::removeLogicalAddresses()` is called with a one-element
	 * vector, and both a false result and a non-ok binder status are logged and
	 * otherwise ignored, because raising where the legacy back-end returns silently
	 * would be an unregistered behaviour change.
	 *
	 * @param [in] source - The logical address to relinquish. Marshalled as a
	 *                      one-element `std::vector<int32_t>`; no multi-address state is
	 *                      introduced anywhere.
	 *
	 * @throws InvalidStateException - The driver is not OPENED.
	 *
	 * @pre open() has completed successfully.
	 * @post The address is absent from the local list whether or not the HAL agreed.
	 * @warning Reports nothing about HAL-side failure, by design. A caller that needs to
	 *          know an address was released must re-query.
	 *
	 * @see addLogicalAddress()
	 * @see DriverImpl::removeLogicalAddress()
	 */
	virtual void  removeLogicalAddress(const LogicalAddress &source);

	/**
	 * @brief Acquires one logical address
	 *
	 * Calls `IHdmiCecController::addLogicalAddresses()` with a one-element vector built
	 * from LogicalAddress::toInt(), then records the address in the local list on
	 * success. The state guard and the local bookkeeping match
	 * DriverImpl::addLogicalAddress() exactly.
	 *
	 * @param [in] source - The logical address to acquire.
	 *
	 * @return bool - Acquisition result
	 * @retval true  - The address was acquired. This is the only value ever returned;
	 *                 every failure leaves by way of an exception, as on the legacy
	 *                 back-end.
	 *
	 * @throws AddressNotAvailableException - The HAL reported false, meaning the address
	 *                                        is outside 0x0..0xE or is already taken.
	 * @throws IOException                  - The transaction reported a non-ok binder
	 *                                        status.
	 * @throws InvalidStateException        - The driver is not OPENED.
	 *
	 * @pre open() has completed successfully.
	 * @warning AUTHORIZED OBSERVABLE DIFFERENCE 3. The legacy back-end distinguishes
	 *          three HAL outcomes - address unavailable, general error, and anything
	 *          else as success - whereas `addLogicalAddresses()` returns a single
	 *          boolean documented as false both when the address is out of range and
	 *          when it is already added. Carrying the legacy distinction would require a
	 *          new HAL method, which is out of bounds for this migration, so false maps
	 *          to the nearer legacy category, AddressNotAvailableException. Callers that
	 *          discriminate IOException from other failures therefore see a different
	 *          category for the same underlying condition.
	 *
	 * @see removeLogicalAddress()
	 * @see DriverImpl::addLogicalAddress()
	 */
	virtual bool  addLogicalAddress   (const LogicalAddress &source);

	/**
	 * @brief Reads back the logical address in use
	 *
	 * Calls `IHdmiCec::getLogicalAddresses()` - on the top-level interface, not on the
	 * controller - and returns the first entry. `devType` is IGNORED, exactly as
	 * DriverImpl::getLogicalAddress() ignores it, and the legacy per-device-type get and
	 * the AIDL get-all are treated as equivalent.@n
	 * When the HAL returns more than one address the count and the entry used are
	 * logged and the first entry is returned. No iteration, no multi-address state and
	 * no dispatch fan-out is introduced.
	 *
	 * @param [in] devType - Accepted for signature compatibility with the Driver
	 *                       interface and with the legacy back-end. Not used.
	 *
	 * @return int - The logical address in use
	 * @retval 0 - No address is in use. This single value covers three cases, which are
	 *             distinguished in the log rather than in the return value: a non-ok
	 *             binder status, a successful call that returned no addresses, and the
	 *             genuine address 0. The legacy implementation behaves the same way - it
	 *             zero-initializes its local and returns whatever the HAL leaves there -
	 *             and LibCCEC::getLogicalAddress() turns a zero into an
	 *             InvalidStateException, which is the existing signal for "no address".
	 *             Any other sentinel here would suppress that throw.
	 * @retval other - The first logical address the HAL reports.
	 *
	 * @pre None. Unlike most methods here this one carries no state guard, matching the
	 *      legacy implementation.
	 * @warning Never throws on HAL failure. The zero return is the failure signal.
	 *
	 * @see isValidLogicalAddress()
	 * @see DriverImpl::getLogicalAddress()
	 */
	virtual int   getLogicalAddress(int devType);

	/**
	 * @brief Physical-address retrieval - BLOCKED on B1 for this back-end
	 *
	 * BLOCKED ITEM B1. On the AIDL back-end this method does not retrieve a physical
	 * address. It logs that the read is unavailable pending the device-settings HAL
	 * contract, names B1, and returns leaving the caller's out parameter UNTOUCHED.@n
	 * The legacy back-end reads the address with `HdmiCecGetPhysicalAddress()`, a CEC
	 * HAL call that needs a native handle obtained from `HdmiCecOpen()` - a handle this
	 * back-end never has, because it opens the AIDL HAL instead. The mandated
	 * replacement is an EDID-byte read that belongs to the DEVICE SETTINGS HAL layer,
	 * not to the CEC HAL, and the public header declaring it was to be supplied as a
	 * separate input and was not. Reconstructing that declaration from usage, taking the
	 * AIDL HDMI-output EDID event, and substituting any CEC HAL call are all excluded,
	 * and acquiring a legacy CEC handle lazily is excluded on top of that because
	 * `HdmiCecOpen()` performs logical-address discovery for source devices, which puts
	 * CEC traffic on the wire and would leave two controllers contending.
	 *
	 * Returning without writing is the minimum-harm interim behaviour rather than a
	 * fabricated value: it is the same observable outcome the legacy path already
	 * produces when its own call fails, since the legacy implementation ignores the
	 * return value and writes nothing on failure. Both plugin call sites already wrap
	 * the call and tolerate not obtaining an address.
	 *
	 * @param [out] physicalAddress - Would receive the physical address. LEFT UNTOUCHED
	 *                                by this back-end, so the caller keeps whatever it
	 *                                initialized the variable to.
	 *
	 * @pre None.
	 * @post The out parameter is unmodified.
	 * @warning The physical address is UNAVAILABLE on the AIDL back-end until B1
	 *          resolves. This is a real functional gap, and it is reported rather than
	 *          papered over.
	 * @warning THE BODY OF THIS METHOD IS THE ONE PLACE THAT CHANGES WHEN THE B1
	 *          CONTRACT ARRIVES. At that point the mandated device-settings read is
	 *          implemented here against the supplied declaration, and
	 *          DriverImpl::getPhysicalAddress() is reviewed for whether the same read
	 *          should replace its own CEC HAL call so that both back-ends use the
	 *          mandated API.
	 *
	 * @see DriverImpl::getPhysicalAddress() - the legacy implementation, unchanged
	 */
	virtual void  getPhysicalAddress(unsigned int *physicalAddress);

	/**
	 * @brief Reports whether a logical address is one this device holds
	 *
	 * Byte-for-byte the legacy DriverImpl::isValidLogicalAddress(): a walk of the local
	 * list under the instance lock, with no HAL call on either back-end. Because close()
	 * does not clear that list on either back-end, this can still report true after a
	 * close.
	 *
	 * @param [in] source - The logical address to test.
	 *
	 * @return bool - Whether the address is held locally
	 * @retval true  - The address is in the local list.
	 * @retval false - It is not.
	 *
	 * @pre None. Valid in any state.
	 * @warning Never throws.
	 *
	 * @see addLogicalAddress()
	 * @see DriverImpl::isValidLogicalAddress()
	 */
	virtual bool isValidLogicalAddress(const LogicalAddress &source) const;

	/**
	 * @brief Pings a logical address by transmitting a header-only frame
	 *
	 * Byte-for-byte the legacy DriverImpl::poll(): the header byte is assembled as
	 * `((from & 0x0F) << 4) | (to & 0x0F)`, appended to a one-byte CECFrame, and handed
	 * to THIS back-end's own write(). The AIDL `getState()` is deliberately not used -
	 * a poll is a CEC ping performed by a one-byte transmit, not a state query - so the
	 * outcome reaches the caller through write()'s exceptions.
	 *
	 * @param [in] from - Initiator logical address, placed in the high nibble.
	 * @param [in] to   - Follower logical address to ping, placed in the low nibble.
	 *
	 * @throws CECNoAckException     - Nothing acknowledged the ping, which is the
	 *                                 normal way a caller learns the address is free.
	 * @throws IOException           - The transmit failed, or the HAL was busy.
	 * @throws InvalidStateException - The driver is not OPENED.
	 *
	 * @pre open() has completed successfully.
	 *
	 * @see write()
	 * @see DriverImpl::poll()
	 */
	virtual void poll(const LogicalAddress &from, const LogicalAddress &to) noexcept(false);

	/**
	 * @brief Logs a decoded, human-readable rendering of a frame
	 *
	 * Byte-for-byte the legacy DriverImpl::printFrameDetails(): pure formatting with no
	 * HAL call on either back-end. The header is decoded from the frame, the opcode name
	 * is resolved when the frame is long enough to carry one, and every CCEC exception
	 * raised while decoding a malformed frame is caught and logged so that diagnostics
	 * can never break a transmit.
	 *
	 * @param [in] frame - The frame to render.
	 *
	 * @pre None. A malformed or empty frame is tolerated.
	 * @warning Declared `noexcept(false)` to match the Driver interface, but does not in
	 *          practice propagate CCEC exceptions.
	 *
	 * @see write()
	 * @see DriverImpl::printFrameDetails()
	 */
	virtual void printFrameDetails(const CECFrame &frame) noexcept(false);

	/**
	 * @brief Reports whether a usable, compatible AIDL HDMI CEC service is present
	 *
	 * This is the question the selection helper in `ccec/src/Driver.cpp` asks, once,
	 * after both back-ends have been constructed. It answers in three ordered stages and
	 * stops at the first that fails:
	 * -# isBinderPreflightOk() on the default driver path, so that nothing in this
	 *    process touches libbinder unless doing so is known to be safe;
	 * -# the service lookup, which resolves `IHdmiCec::serviceName()` - the literal
	 *    `"HdmiCec"` - through the binder service manager and yields a typed proxy or
	 *    nullptr;
	 * -# the compatibility check, which rejects a null proxy, an empty or `"-1"`
	 *    interface hash, an unfrozen development server, and a server whose interface
	 *    version does not satisfy the compiled-against `IHdmiCec::VERSION` within the
	 *    same era and major.
	 *
	 * PRESENCE ALONE IS NOT SUFFICIENT - a service that answers but cannot be spoken to
	 * compatibly is treated exactly as an absent one, and the legacy back-end is
	 * selected. A resolved and compatible proxy is CACHED on this instance for open() to
	 * use, so the lookup happens once rather than once per operation.
	 *
	 * @return bool - Whether the AIDL back-end should be selected
	 * @retval true  - The preflight passed, the service resolved, and it is compatible.
	 *                 The proxy is cached and open() may be called.
	 * @retval false - Any stage failed. The reason is logged, and the caller must select
	 *                 the legacy back-end.
	 *
	 * @pre None. Safe to call on a platform with no binder driver, no `servicemanager`
	 *      and no AIDL HAL whatsoever - which is the entire point of the preflight.
	 * @post No state transition occurs. The instance remains CLOSED either way.
	 * @warning NEVER aborts, NEVER blocks indefinitely and NEVER propagates an
	 *          exception, because a legacy-only SOC must reach the legacy back-end
	 *          rather than fail to initialize.
	 * @warning Not thread safe, and not intended to be: it is called once from the
	 *          factory's one-time static initializer, which the language already
	 *          serializes.
	 *
	 * @see isBinderPreflightOk()
	 * @see unavailabilityReason()
	 * @see Driver::getInstance()
	 */
	bool isServiceAvailable(void);

	/**
	 * @brief Reports WHY the last isServiceAvailable() declined, without asking again
	 *
	 * THE DECISION IS PRESERVED RATHER THAN RECONSTRUCTED, and that is the whole point
	 * of this accessor. isServiceAvailable() already establishes which of its three
	 * ordered stages declined; the selection helper in `ccec/src/Driver.cpp` needs that
	 * same answer in order to name the platform condition in its fallback line. Asking
	 * a second question to rebuild the answer to the first would be wrong twice over,
	 * and both ways were observed before this accessor existed: re-running the bounded
	 * preflight pays its context-manager timeout AGAIN, doubling the worst-case delay
	 * inside LibCCEC::init() on exactly the platform least able to afford it; and the
	 * second run need not agree with the first, because a `servicemanager` that starts
	 * or dies between the two calls flips its verdict, so the reported reason could
	 * name a condition that did not cause the fallback.@n
	 * So the reason is recorded by the query that decided it and simply read back here.
	 *
	 * @return const char* - Why the AIDL back-end is unusable
	 * @retval NULL - Either the query has not been run yet, or it returned true. In
	 *                both cases there is no fallback reason to report, and a caller
	 *                must not print one.
	 * @retval non-NULL - A stable, human-readable phrase naming the platform condition
	 *                    that declined, suitable for substitution into a log line. It
	 *                    has static storage duration and outlives this object, so it is
	 *                    safe to hold and to format after this call returns.
	 *
	 * @pre None. Safe before isServiceAvailable() has ever run, which is what the NULL
	 *      retval covers.
	 * @post Nothing changed. This is a pure read of state the query already set.
	 * @warning Never throws and never touches binder.
	 * @warning The returned phrases are a TEST AND TOOLING CONTRACT: the coverage
	 *          runner and the L2 tier both transcribe the resulting log lines. Reword
	 *          one and those consumers stop matching.
	 *
	 * @see isServiceAvailable()
	 * @see Driver::getInstance()
	 */
	const char *unavailabilityReason(void) const;

	/**
	 * @brief The four kernel-facing operations the binder preflight performs
	 *
	 * A SEAM, and one the preflight cannot be verified without. Three of the predicate's
	 * five decision arms are unreachable through its path argument alone, on any host: a
	 * node that opens and reports a MISMATCHED protocol version, a node that reports a
	 * MATCHING version and then fails the context-manager check, and a fully positive
	 * verdict. A driverless host reaches only the first two arms, and a binder-capable
	 * host reaches only the positive one - so on neither can the whole predicate be
	 * exercised, and its most consequential arm (the protocol-equality check, which is
	 * what stands between a mismatched kernel and libbinder's own abort) would ship
	 * unexercised. Substituting these four operations makes every arm reachable
	 * deterministically, with no binder driver and without rendering a runner's real
	 * driver unusable.
	 *
	 * The struct is deliberately POD-with-function-pointers rather than an abstract
	 * interface: it adds no virtual dispatch, no allocation and no ownership question,
	 * and it keeps the default - the real syscalls - a compile-time constant. It also
	 * mentions NO BINDER KERNEL TYPE, which is required rather than tidy: this header
	 * must still compile where the binder kernel UAPI definitions are absent, so the
	 * protocol version crosses this boundary as a plain `unsigned int` and the driver
	 * node as a plain descriptor.
	 *
	 * @warning INTERNAL AND TEST-VISIBLE, NOT PUBLIC API, for the same reason
	 *          isBinderPreflightOk() is: `ccec/src/DriverAidlImpl.hpp` is not an
	 *          installed header, so this adds nothing to the middleware public API, and
	 *          nothing outside `ccec/src` and the test suites may use it.
	 * @warning Every member must be non-null. isBinderPreflightOk() calls them
	 *          unconditionally and does not defend against a partially filled probe,
	 *          exactly as it does not defend against a null `::open`.
	 *
	 * @see defaultBinderProbe()
	 * @see isBinderPreflightOk()
	 * @see expectedBinderProtocolVersion()
	 */
	struct BinderPreflightProbe {
		/**
		 * @brief Opens the binder driver node
		 *
		 * The real implementation is `::open`. Declared with a fixed second parameter
		 * rather than as the variadic `::open` itself, so the default probe supplies a
		 * thin wrapper.
		 *
		 * @param [in] path  - Node to open.
		 * @param [in] flags - Open flags, `O_RDWR | O_CLOEXEC` in production.
		 *
		 * @return int - Descriptor, or negative on failure with `errno` set.
		 */
		int  (*openNode)(const char *path, int flags);
		/**
		 * @brief Reads the protocol version the driver reports
		 *
		 * The real implementation is `ioctl(fd, BINDER_VERSION, ...)`. The version is
		 * carried out as an `unsigned int` so that no binder kernel type appears in this
		 * header.
		 *
		 * @param [in]  fd      - Descriptor returned by openNode().
		 * @param [out] version - Receives the reported protocol version. Written only on
		 *                        success.
		 *
		 * @return int - Outcome
		 * @retval 0  - The version was read into @p version.
		 * @retval -1 - It could not be read; `errno` carries the reason where the
		 *              underlying call set one.
		 */
		int  (*readProtocolVersion)(int fd, unsigned int *version);
		/**
		 * @brief Asks, under a bounded wait, whether binder handle 0 resolves
		 *
		 * The real implementation posts a PING_TRANSACTION to handle 0 over @p fd and
		 * drains the driver's command stream under the deadline.
		 *
		 * @param [in] fd        - Descriptor whose protocol version has been verified.
		 * @param [in] timeoutMs - Upper bound in milliseconds; zero means do not wait.
		 *
		 * @return bool - Whether a context manager answered
		 * @retval true  - It answered, so `servicemanager` is reachable.
		 * @retval false - It did not, within the bound.
		 */
		bool (*pingContextManager)(int fd, unsigned int timeoutMs);
		/**
		 * @brief Releases the descriptor openNode() returned
		 *
		 * The real implementation is `::close`.
		 *
		 * @param [in] fd - Descriptor to release.
		 *
		 * @return int - Zero on success, negative on failure. The preflight ignores it,
		 *               exactly as the production code it replaces ignored `::close`'s.
		 */
		int  (*closeNode)(int fd);
	};

	/**
	 * @brief The production probe: the real `::open`, `BINDER_VERSION`, ping and `::close`
	 *
	 * The default argument of isBinderPreflightOk(), so that every production call site
	 * names nothing and behaves precisely as it did before the seam existed. Returns a
	 * reference to a single immutable instance with static storage duration, which is why
	 * it is safe as a default argument and why no caller has anything to own.
	 *
	 * @return const BinderPreflightProbe& - The real kernel-facing operations.
	 *
	 * @pre None.
	 * @post Nothing is initialized in the process. The probe holds function pointers and
	 *       performs no work until it is called.
	 * @warning Never throws.
	 *
	 * @see BinderPreflightProbe
	 * @see isBinderPreflightOk()
	 */
	static const BinderPreflightProbe &defaultBinderProbe(void);

	/**
	 * @brief The binder protocol version this build must speak to be usable
	 *
	 * `BINDER_CURRENT_PROTOCOL_VERSION`, exposed as a function so that a test can
	 * synthesise BOTH a matching and a mismatching version without the binder kernel
	 * UAPI definitions leaking into this header or into the test translation unit. The
	 * constant follows `BINDER_IPC_32BIT` - protocol 7 for an all-32-bit platform, 8 for
	 * 32-bit middleware against a 64-bit vendor - which is exactly why it is read from
	 * the build rather than written down anywhere.
	 *
	 * @return unsigned int - Expected protocol version
	 * @retval 0     - This build carries no binder kernel ABI definitions, so no version
	 *                 can be named. The preflight cannot reach its comparison in that
	 *                 configuration, because the default probe's version read fails
	 *                 first.
	 * @retval other - The protocol version the linked libbinder was built for.
	 *
	 * @pre None.
	 * @warning Never throws.
	 *
	 * @see isBinderPreflightOk()
	 */
	static unsigned int expectedBinderProtocolVersion(void);

	/**
	 * @brief Decides whether a binder lookup may safely be attempted at all
	 *
	 * A preflight is REQUIRED rather than convenient. Reaching the service manager
	 * directly is unsafe in two independent ways on the pinned binder stack, and either
	 * one of them defeats the absolute requirement that a supported SOC without an AIDL
	 * HAL must reach the legacy back-end:
	 * - a missing or protocol-mismatched driver node is FATAL, not an error return. The
	 *   pin extends libbinder's failed-driver `LOG_ALWAYS_FATAL_IF` to plain Linux, so
	 *   what upstream guards for Android is live here and would ABORT the middleware
	 *   during initialization instead of falling back;
	 * - a missing context manager BLOCKS INDEFINITELY. Obtaining an `IServiceManager` at
	 *   all polls in one-second intervals until binder handle 0 resolves, with no upper
	 *   bound of its own, so a working driver with no running `servicemanager` would
	 *   stall LibCCEC::init() forever.
	 *
	 * This predicate therefore runs BEFORE ANYTHING IN THE PROCESS TOUCHES LIBBINDER and
	 * checks, in this order, stopping at the first failure:
	 * -# the driver node exists and can be opened;
	 * -# the protocol version it reports equals the version the linked libbinder was
	 *    built for, since libbinder enforces equality and a mismatch fails every open;
	 * -# binder handle 0, the context manager, resolves within a BOUNDED timeout rather
	 *    than being waited on without limit.
	 *
	 * Only when all three pass may the caller proceed to the service lookup and the
	 * compatibility check. Any failure means "AIDL absent" and the legacy back-end.
	 *
	 * @param [in]  binderDriverPath          - Binder driver node to inspect. Taken as a
	 *                                          parameter, not hard-coded, so that the
	 *                                          negative arms can be exercised - a
	 *                                          nonexistent path, or a path whose reported
	 *                                          protocol differs - WITHOUT rendering a
	 *                                          test runner's real driver unusable.
	 *                                          Defaults to DEFAULT_BINDER_DRIVER_PATH.
	 * @param [in]  contextManagerTimeoutMs   - Upper bound, in milliseconds, on the
	 *                                          handle-0 resolution check. Zero means do
	 *                                          not wait at all, which is how the timeout
	 *                                          arm is exercised. Defaults to
	 *                                          DEFAULT_CONTEXT_MANAGER_TIMEOUT_MS.
	 * @param [in]  probe                     - The four kernel-facing operations this
	 *                                          predicate performs, injected so that the
	 *                                          arms a path argument CANNOT reach are
	 *                                          reachable: a node that opens but reports a
	 *                                          MISMATCHED protocol version, a node that
	 *                                          reports a MATCHING version and then fails
	 *                                          the context-manager check, and a fully
	 *                                          positive verdict - none of which any real
	 *                                          path on a driverless host produces.
	 *                                          Defaults to defaultBinderProbe(), which is
	 *                                          the real syscalls, so production behaviour
	 *                                          and every production log line are exactly
	 *                                          what they would be without the seam.
	 *
	 * @return bool - Whether a binder lookup may safely be attempted
	 * @retval true  - All three checks passed.
	 * @retval false - The node is absent or cannot be opened, or the protocol version
	 *                 differs, or handle 0 did not resolve within the bound. Which one
	 *                 is distinguished in the log, not in the return value.
	 *
	 * @pre None whatsoever. This is the first thing that runs, on any platform.
	 * @post Nothing in the process has been left initialized. In particular no
	 *       `ProcessState` singleton is created and no threadpool is started, so a false
	 *       result leaves the process exactly as it was.
	 * @warning INTERNAL AND TEST-VISIBLE, NOT PUBLIC API. It is public only so that a
	 *          test translation unit can call it - a private static is unreachable from a
	 *          non-friend, and coupling production code to a test fixture name with a
	 *          `friend` declaration would be worse. `ccec/src/DriverAidlImpl.hpp` is not
	 *          an installed header, so this adds nothing to the middleware public API.
	 *          Nothing outside `ccec/src` and the test suites may call it.
	 * @warning Implementations must not propagate exceptions and must not block beyond
	 *          the stated bound. Both guarantees are what the caller relies on.
	 *
	 * @see isServiceAvailable()
	 * @see BinderPreflightProbe
	 * @see defaultBinderProbe()
	 * @see DEFAULT_BINDER_DRIVER_PATH
	 * @see DEFAULT_CONTEXT_MANAGER_TIMEOUT_MS
	 */
	static bool isBinderPreflightOk(const std::string &binderDriverPath = DEFAULT_BINDER_DRIVER_PATH,
	                                unsigned int contextManagerTimeoutMs = DEFAULT_CONTEXT_MANAGER_TIMEOUT_MS,
	                                const BinderPreflightProbe &probe = defaultBinderProbe());

	/**
	 * @brief Names the CATEGORY of an interface hash observed after a compatibility rejection
	 *
	 * Pure description of one string. It reports what the value IS, never what it caused:
	 * isServiceAvailable() cannot know which of halcompat's three rules rejected a server,
	 * because the metadata that predicate read is a local inside a read-only header and a
	 * fresh read is a different binder transaction. The distinction is the whole reason this
	 * function exists rather than a classification chain.
	 *
	 * @param [in] hash - The hash exactly as the server reported it, on a read taken AFTER
	 *                    the decision. May be empty and may contain any byte value.
	 *
	 * @return const char* - A static-storage-duration phrase describing the category. Never
	 *                       NULL, and never a statement about causation.
	 *
	 * @pre None.
	 * @post No state changed.
	 * @warning DIAGNOSTICS ONLY. No compatibility decision is taken on this value.
	 * @warning Public so a test translation unit may call it: the production arm that logs it
	 *          is unreachable without a binder transport, so this is the only way the wording
	 *          is covered on a host without one.
	 *
	 * @see DriverAidlImpl::observedMetadataWouldBeAccepted()
	 * @see DriverAidlImpl::isServiceAvailable()
	 */
	static const char *describeObservedInterfaceHash(const std::string &hash);

	/**
	 * @brief Whether a post-decision metadata SNAPSHOT would itself be accepted
	 *
	 * Answers one question about one snapshot: had these values been the ones halcompat read,
	 * would it have accepted them? A `true` answer does not overturn the rejection - the
	 * decision stands on the metadata that governed - it establishes that the observation
	 * DISAGREES with the decision, which means the server's metadata changed or recovered and
	 * the rejection must NOT be attributed to the version rule.
	 *
	 * The version half delegates to `halcompat::detail::isCompatible()`, the real rule, which
	 * is constexpr over two ints and therefore performs no transaction. The hash half mirrors
	 * halcompat's own gate order for the OBSERVED value only, including that an unfrozen
	 * server is not accepted, because production calls the predicate with its `allowUnfrozen`
	 * default of false.
	 *
	 * @param [in] hash            - The observed interface hash, unmodified.
	 * @param [in] clientVersion   - This client's compiled-in interface version.
	 * @param [in] observedVersion - The observed server interface version, from the SAME
	 *                               snapshot as @p hash. Passing values from two different
	 *                               reads is exactly the defect this signature exists to
	 *                               prevent.
	 *
	 * @return bool - True when this snapshot alone would have satisfied the rule.
	 * @retval true  - The observation disagrees with the decision: report changed or recovered
	 *                 metadata, never a version mismatch.
	 * @retval false - The observation is consistent with a rejection, which still does not
	 *                 establish WHICH rule applied.
	 *
	 * @pre @p hash and @p observedVersion come from one snapshot.
	 * @post No state changed.
	 * @warning DIAGNOSTICS ONLY. Never used to select a back-end.
	 * @warning Public for the reason describeObservedInterfaceHash() gives.
	 *
	 * @see DriverAidlImpl::describeObservedInterfaceHash()
	 * @see DriverAidlImpl::isServiceAvailable()
	 */
	static bool observedMetadataWouldBeAccepted(const std::string &hash,
	                                            int clientVersion,
	                                            int observedVersion);

	/**
	 * @brief Emits the compatibility-rejection diagnostic for a service halcompat rejected
	 *
	 * The whole of what `isServiceAvailable()` says about a present-but-incompatible
	 * service, in one place, so that the wording a test captures is *byte-for-byte* the
	 * wording production emits. It was inline in `isServiceAvailable()` until a review
	 * observed that no test could reach it on a host without a binder driver -- the
	 * preflight declines first -- so the diagnostic's wording was unverified precisely
	 * where it mattered most. Extracting it does not add a seam for its own sake: this is
	 * the ONLY caller in production, and a second copy in a test would be the drift the
	 * extraction exists to prevent.
	 *
	 * It takes ONE snapshot of the server's interface hash and version and derives every
	 * statement from that single pair. It makes NO causal claim about which of halcompat's
	 * three rules rejected the server, because nothing here can know: the metadata the
	 * decision read are locals inside a template in a read-only consumed header, and a
	 * fresh read is a different transaction.
	 *
	 * @param[in] service        The rejected service. Non-null; the caller has already
	 *                           established that halcompat rejected it.
	 * @param[in] halServiceName The binder name the service was resolved under, for the log.
	 *
	 * @return void
	 *
	 * @pre `halcompat::isCompatible<IHdmiCec>(service)` has already returned false, and the
	 *      caller has already recorded `REASON_NO_COMPATIBLE_SERVICE`.
	 * @post No member state changes. **This is structural rather than a promise:** the
	 *       function is `static`, so it has no `this` and therefore *cannot* touch
	 *       `availabilityReason`. That is why a failed observation can never relabel an
	 *       established compatibility rejection as `REASON_QUERY_FAILED` -- the outer
	 *       handler that would do so is unreachable from here, because this function
	 *       swallows its own failure.
	 * @warning Diagnostics only. Nothing it computes influences back-end selection.
	 * @warning Public for the reason describeObservedInterfaceHash() gives: on a host with
	 *          no binder driver the preflight declines before `isServiceAvailable()` ever
	 *          reaches the compatibility stage, so this is the only route by which a test
	 *          can exercise the real production wording at all.
	 *
	 * @see DriverAidlImpl::isServiceAvailable()
	 * @see DriverAidlImpl::observedMetadataWouldBeAccepted()
	 */
	static void emitCompatibilityRejectionDiagnostic(
		const ::android::sp< ::com::rdk::hal::hdmicec::IHdmiCec > &service,
		const std::string &halServiceName);
/*
 * INTERNAL, AND `protected` RATHER THAN `private` FOR TWO REASONS, BOTH OF THEM F02's: the
 * receive-queue handoff below has to be TESTABLE, and the lock that serializes the queue's
 * producers has to be HOLDABLE BY A TEST. Neither can be reached any other way.
 *
 * offerReceivedFrame() only accepts a frame while the state is OPENED, and the state only
 * becomes OPENED through open(), which requires a live compatible AIDL service - so on a host
 * with no binder driver the ownership contract that F02 turns on, and the reserved sentinel
 * slot that protects it, would have no coverage at all. `protected` lets a TEST-LOCAL SUBCLASS
 * establish that precondition and drive the handoff directly, on any host, with no service and
 * no driver, exactly as isBinderPreflightOk() is public so its negative arms can be exercised.
 *
 * queueProducerMutex IS REACHABLE FOR THE SAME REASON AND FOR A SHARPER ONE. F02's defect was a
 * MISSING LOCK ACQUISITION in close(), and no serial test can observe a lock that is not taken:
 * a case that fills the queue, offers once more and only then closes passes whether or not
 * close() holds this lock. What does observe it is a test that HOLDS the lock itself and then
 * drives the real close() from another thread - the sentinel offer cannot complete while the
 * lock is held, and completing anyway is precisely the regression. That probe needs the lock
 * object, and reaching it through a test-local subclass is the whole of what `protected` buys
 * here. The alternative, a case that re-states close()'s offer rather than calling close(),
 * would pass against production code that had stopped taking the lock - which is the one thing
 * such a case exists to catch.
 *
 * The alternatives were weighed and are worse. A `friend` declaration would name a test fixture
 * inside production code, which the note on isBinderPreflightOk() already rejects. A public
 * introspection or state-setting API would add real middleware surface, which the plan forbids.
 * And no member moved: the declaration order still matches DriverImpl's, so the two back-ends
 * continue to diff cleanly against one another.
 *
 * WHAT THIS DOES NOT DO. It adds nothing to the middleware public API - `ccec/src/DriverAidlImpl.hpp`
 * is not an installed header, nothing in production derives from this class, and no consumer can
 * reach a protected member. Nothing outside `ccec/src` and the test suites may use any of it.
 */
protected:
	/**
	 * @brief Receives the HAL's `oneway` CEC events on a binder threadpool thread
	 *
	 * Forward declaration only. The definition derives from the generated
	 * `BnHdmiCecEventListener` and lives in `DriverAidlImpl.cpp`, so that the server-side
	 * base header is not pulled into every translation unit that includes this one.
	 * Defining a private nested class out of line is well formed, and keeping it private
	 * states plainly that it is not part of any interface.@n
	 * It holds a NULLABLE back pointer to its owner, guarded by a lock of its own, and
	 * reaches the incoming queue through getIncomingQueue(), never through
	 * Driver::getInstance() - resolving through the factory would reintroduce the legacy
	 * static's `static_cast<DriverImpl &>`, which is ill-typed once the factory can
	 * return this class.@n
	 * The back pointer is nullable because the object's lifetime is NOT this class's to
	 * decide: the listener crossed the binder boundary in `IHdmiCec::open()`, so the HAL
	 * holds a strong reference of its own and releasing ours need not destroy it. Every
	 * path that ends a session therefore DETACHES the listener - both arms of close(),
	 * the failure arms of open(), and the destructor - and detachment is synchronous
	 * with respect to any callback already in flight. Without it, a HAL that keeps
	 * calling after a FAILED close would dereference a destroyed owner, which is
	 * CWE-416.
	 *
	 * @see getIncomingQueue()
	 * @see close()
	 */
	class EventListener;

	/**
	 * @brief Returns the incoming frame queue, but only while the driver is open
	 *
	 * The state-guarded accessor that mirrors DriverImpl::getIncomingQueue(). The guard
	 * is the load-bearing part: it is what rejects a receive callback that arrives during
	 * or after a close, and it is what drives the listener's release of the frame it was
	 * about to enqueue. The listener must reach the queue through here and never touch
	 * the member directly, or it would accept frames the legacy path rejects.@n
	 * The legacy signature carries a native handle that its own body never reads. It is
	 * dropped here, because the AIDL back-end has no handle to pass.
	 *
	 * @return IncomingQueue& - The queue received frames are offered onto and that read()
	 *                          drains.
	 *
	 * @throws InvalidStateException - The driver is not OPENED, i.e. it is closed or
	 *                                 closing.
	 *
	 * @pre None. The state check is the method's purpose.
	 * @warning Reads the state without holding the instance lock, reproducing the
	 *          pre-existing unlocked read in DriverImpl::getIncomingQueue(). This is a
	 *          known pre-existing condition, preserved deliberately rather than fixed,
	 *          because behaviour preservation outranks code improvement here.
	 *
	 * @see read()
	 * @see DriverImpl::getIncomingQueue()
	 */
	IncomingQueue & getIncomingQueue(void);

	/**
	 * @brief Hands one received frame to the incoming queue and REPORTS WHETHER IT TOOK IT
	 *
	 * The ownership-transferring counterpart of getIncomingQueue(), and the reason the
	 * receive path cannot lose a frame. `CCEC_OSAL::EventQueue::offer()` returns void and
	 * silently discards its argument when the queue is at INCOMING_QUEUE_CAPACITY, so a
	 * caller that offers and then forgets the pointer LEAKS one frame per event for as long
	 * as the queue stays full. This method closes that hole by establishing that there is
	 * room BEFORE it offers, and by telling the caller which of the two of them still owns
	 * the frame afterwards.@n
	 * It reaches the queue through getIncomingQueue(), never through the member, so the
	 * load-bearing opened-state guard still applies: a frame arriving during or after a
	 * close is rejected by an InvalidStateException propagating out of here, exactly as on
	 * the legacy path, and the caller's existing catch releases it.
	 *
	 * @param [in] frame - Frame to hand over. Ownership passes to the queue if and only if
	 *                     this returns true; on false, and on any exception, ownership
	 *                     stays with the caller, which must release it.
	 *
	 * @return bool - Whether ownership was transferred
	 * @retval true  - The frame is on the queue. The caller must NOT delete it and must
	 *                 drop its pointer immediately.
	 * @retval false - There was no slot this method may use, or the queue was observed at
	 *                 capacity after the offer. The frame was NOT queued and the caller
	 *                 still owns it.
	 *
	 * @throws InvalidStateException - The driver is not OPENED, raised by
	 *                                 getIncomingQueue(). Ownership stays with the caller.
	 *
	 * @pre @p frame is a heap-allocated frame the caller currently owns.
	 * @post Exactly one of: the frame is queued and this returned true; or the frame is
	 *       still the caller's and this returned false or raised.
	 * @warning Serializes producers on queueProducerMutex - a lock of its OWN, never the
	 *          instance lock. The instance lock is held across an entire synchronous IPC
	 *          round trip by write(), so reusing it here would couple frame delivery on a
	 *          binder thread to transmit latency and could deadlock the receive path behind
	 *          a stalled HAL.
	 * @warning WHAT MAKES THE CHECK-THEN-OFFER SOUND IS THAT EVERY PRODUCER TAKES THIS
	 *          LOCK, and nothing weaker. This method and close()'s NULL sentinel offer are
	 *          the only writers on the queue, and both hold queueProducerMutex for their
	 *          offer, so while it is held nothing can RAISE the occupancy and the observed
	 *          "there is room" cannot turn false. Consumers do not take this lock and keep
	 *          removing, which only LOWERS occupancy and is therefore harmless to the
	 *          check.
	 * @warning THE LAST SLOT IS RESERVED FOR close()'s SENTINEL: this method refuses at
	 *          INCOMING_QUEUE_CAPACITY - 1, so the wake-the-reader offer can never be the
	 *          one `EventQueue::offer()` swallows and a blocked Bus reader always wakes.
	 * @warning ACCEPTANCE IS READ BACK FROM THE QUEUE, not inferred from the check that
	 *          preceded the offer, because `EventQueue::offer()` returns void and reports
	 *          nothing. The read-back is trusted in ONE DIRECTION ONLY: an occupancy at or
	 *          above the capacity is inconsistent with acceptance and is reported as a
	 *          refusal, while an occupancy below it is reported as acceptance even if it did
	 *          not rise - a consumer can take the frame the instant the offer wakes it, so a
	 *          non-rise does not prove refusal, and reporting one would hand the caller a
	 *          pointer the queue already owns. The body states the full reasoning.
	 *
	 * @see getIncomingQueue()
	 * @see INCOMING_QUEUE_CAPACITY
	 */
	bool offerReceivedFrame(CECFrame *frame);

	/** @brief Lifecycle state: one of CLOSED, CLOSING or OPENED. */
	int status;
	/**
	 * @brief Legacy native handle field, retained for shape parity with DriverImpl
	 *
	 * Initialized to 0 by the constructor and never used to address the AIDL HAL, which
	 * is reached through the two interface proxies below. It is kept so that the two
	 * back-ends declare the same members in the same order and diff cleanly against one
	 * another.
	 */
	int nativeHandle;
	/**
	 * @brief Frames received from the HAL, awaiting the Bus reader thread
	 *
	 * Constructed with INCOMING_QUEUE_CAPACITY explicitly rather than left on the OSAL
	 * default, so the capacity offerReceivedFrame() checks against is the capacity the
	 * queue enforces. That equality is what lets the receive path prove, from its check
	 * alone, that its offer cannot be the one the queue silently discards.
	 */
	IncomingQueue rQueue;
        /** @brief Guards the state and the local address list. Mutable, so const methods may lock. */
        mutable Mutex mutex;
	/**
	 * @brief Serializes EVERY producer that offers onto the incoming queue
	 *
	 * Held by both writers on that queue, which is the whole of what makes the receive
	 * path's occupancy check meaningful: offerReceivedFrame(), for its check and its
	 * offer, and close(), for its single NULL sentinel offer. A producer that skipped
	 * this lock could raise the occupancy inside the window between that check and that
	 * offer, whereupon `EventQueue::offer()` would discard the received frame silently
	 * while the receive path reported it accepted.@n
	 * A SEPARATE lock from the instance mutex, and deliberately so. The instance mutex is
	 * held by write() across an entire synchronous IPC round trip - that is the legacy
	 * critical section, preserved on purpose - so taking it on the receive path would make
	 * frame delivery on a binder thread wait out every transmit, and would put the receive
	 * path behind a stalled HAL. This lock is held only for the occupancy reads and the
	 * offer between them, all of which are constant-time and none of which performs IPC,
	 * allocation or logging. close() takes it INSIDE the instance lock, and
	 * offerReceivedFrame() takes it alone and never takes the instance lock, so the
	 * nesting order is one-way and no inversion is possible.
	 *
	 * @see offerReceivedFrame()
	 * @see close()
	 */
	Mutex queueProducerMutex;
	/**
	 * @brief Logical addresses this device currently holds
	 *
	 * A list, exactly as in DriverImpl, because the acquire call may be made more than
	 * once. It is NOT widened for the AIDL back-end: the array shape the AIDL calls
	 * require is confined to the one-element `std::vector<int32_t>` temporaries built
	 * inside the implementation, and no middleware structure or signature grows a plural
	 * form.
	 */
	std::list<LogicalAddress> logicalAddresses;

	/**
	 * @brief The top-level AIDL service proxy, cached by isServiceAvailable()
	 *
	 * Supplies open(), close() and getLogicalAddresses(). Null until a successful
	 * isServiceAvailable(), and required to be non-null by open().
	 */
	android::sp< ::com::rdk::hal::hdmicec::IHdmiCec> hdmiCecService;
	/**
	 * @brief The controller session proxy returned by `IHdmiCec::open()`
	 *
	 * Supplies addLogicalAddresses(), removeLogicalAddresses() and sendMessage(). Null
	 * outside an open session, which is why those operations are state guarded. The
	 * split across two interfaces is the reason both proxies are held.
	 */
	android::sp< ::com::rdk::hal::hdmicec::IHdmiCecController> hdmiCecController;
	/**
	 * @brief The listener handed to `IHdmiCec::open()`, held for the session's lifetime
	 *
	 * Retained here so the HAL's strong reference has a local counterpart and the object
	 * cannot be destroyed while the HAL may still call into it. Constructed fresh by each
	 * open() and released by close(), by open()'s own failure arms and by the destructor,
	 * each of which DETACHES it first so that a HAL still holding its own reference can no
	 * longer reach this instance through it. Releasing this reference alone would not be
	 * enough: it need not destroy the object, and an undetached survivor is a
	 * use-after-free waiting on the next callback.
	 */
	android::sp<EventListener> eventListener;

	/**
	 * @brief Why the last isServiceAvailable() declined, or NULL
	 *
	 * Set by isServiceAvailable() on every one of its exit paths - to NULL when the
	 * AIDL back-end is usable, and otherwise to the phrase naming the stage that
	 * declined - and read back through unavailabilityReason() by the selection helper.
	 * Holding a `const char *` rather than a copied string is deliberate: every value
	 * ever stored here is a string literal with static storage duration, so there is
	 * nothing to own, nothing to allocate on a path that must not fail, and the pointer
	 * stays valid for the lifetime of the process.
	 *
	 * @warning Initialized to NULL by the constructor, which is what makes
	 *          unavailabilityReason() safe to call before any query has run.
	 */
	const char *availabilityReason;

private:
	/**
	 * @brief Copy construction is not allowed
	 *
	 * Declared and never defined, exactly as in DriverImpl. Copying would duplicate the
	 * session proxies, the frame queue and the lock, none of which has copy semantics.
	 *
	 * @param [in] other - Unused. The declaration exists only to suppress the implicit
	 *                     copy constructor.
	 */
	DriverAidlImpl(const DriverAidlImpl &other); /* Not allowed */

	/**
	 * @brief Copy assignment is not allowed
	 *
	 * Declared and never defined, for the same reason as the copy constructor.
	 *
	 * @param [in] other - Unused.
	 *
	 * @return DriverAidlImpl& - Never returns; the declaration is never defined.
	 */
	DriverAidlImpl & operator = (const DriverAidlImpl &other); /* Not allowed */

};

CCEC_END_NAMESPACE

#endif


/** @} */
/** @} */
