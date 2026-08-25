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
 * @file DriverAidlImpl.cpp
 *
 * @brief Implementation of the AIDL/binder back-end of the CCEC middleware Driver
 *
 * This translation unit holds the whole of the AIDL back-end: the twelve CCEC::Driver
 * overrides, the runtime service-availability query the selection point in
 * `ccec/src/Driver.cpp` asks, the binder preflight predicate that makes that query safe
 * to attempt at all, and the nested event listener that receives CEC messages from the
 * HAL.@n
 * Every method here mirrors its DriverImpl counterpart's structure, guards, log lines,
 * exceptions AND STATEMENT ORDER, with the `com.rdk.hal.hdmicec` AIDL call substituted
 * for the legacy HDMI CEC C API. Four of them - read(), isValidLogicalAddress(), poll()
 * and printFrameDetails() - make no HAL call on either back-end and are therefore
 * byte-for-byte copies of the legacy bodies, class name aside. That is deliberate: it
 * is what makes the receive and formatting paths behaviourally indistinguishable
 * between the two back-ends, so the Bus reader thread and everything above it are
 * untouched by this migration.
 *
 * Three observable differences from the legacy back-end are authorized, each forced by
 * the AIDL contract and each documented on the method that carries it - the 16-byte
 * frame limit in write(), the OperationNotSupportedException in writeAsync(), and the
 * coarser failure category in addLogicalAddress(). Any other observable difference is a
 * defect. Two items are BLOCKED on inputs that were not supplied and are marked as such
 * in the code: B1 on getPhysicalAddress() and B2 on close().
 *
 * @note Include-order contract, and it is load bearing: EVERY AIDL, binder and
 *       halcompat header is included BEFORE CCEC_BEGIN_NAMESPACE. The legacy
 *       DriverImpl.cpp includes its plain C HAL header from INSIDE the namespace, which
 *       is tolerable there and must NOT be imitated here - the generated stubs open
 *       `namespace com::rdk::hal::hdmicec` and transitively pull the SDK headers that
 *       open `namespace android`, and nesting either inside CCEC would break the link
 *       and violate the one-definition rule.
 * @note No legacy HAL header is included and no legacy HAL symbol is referenced
 *       anywhere in this file. `ccec/src/DriverImpl.cpp` remains the sole production
 *       include of `ccec/drivers/hdmi_cec_driver.h`, which is possible only because
 *       getPhysicalAddress() reports its B1 block rather than working around it.
 * @note Global functions whose names collide with this class's own members - open,
 *       close, read, write and poll - are called through the global scope operator, so
 *       that `::open()` reaches the system call and `open()` reaches the member.
 *
 * @warning Requires C++17. The generated AIDL stubs include `<optional>`.
 * @warning This is the C++ libbinder AIDL backend, NOT the NDK backend: the pointer
 *          type is `android::sp<>`, the status type is `android::binder::Status`, and
 *          the server base is the generated `BnHdmiCecEventListener`.
 *
 * @see hdmicec/ccec/src/DriverAidlImpl.hpp
 * @see hdmicec/ccec/src/DriverImpl.cpp
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <cstdint>
#include <string>
#include <vector>

/*
 * AIDL and binder headers. These MUST stay above CCEC_BEGIN_NAMESPACE - see the
 * include-order note in the file block above.
 */
#include <binder/IBinder.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <binder/Status.h>
#include <utils/Errors.h>
#include <utils/String8.h>
#include <utils/StrongPointer.h>
#include <com/rdk/hal/hdmicec/BnHdmiCecEventListener.h>
#include <com/rdk/hal/hdmicec/IHdmiCec.h>
#include <com/rdk/hal/hdmicec/IHdmiCecController.h>
#include <com/rdk/hal/hdmicec/SendMessageStatus.h>
#include <com/rdk/hal/hdmicec/State.h>

/*
 * The shared HAL interface compatibility helpers. This header lives under
 * `rdk-halif-aidl/common/current/` rather than inside either frozen snapshot include
 * root, so a build that reaches the generated stubs but not this file is missing an
 * include root rather than missing a dependency.
 */
#include "halcompat.h"

/*
 * Binder kernel ABI, used by the preflight predicate to inspect the driver node
 * directly - deliberately WITHOUT going through libbinder, because the whole point of
 * the preflight is to decide whether touching libbinder is safe at all.
 *
 * The header is part of the kernel UAPI and is present wherever libbinder itself can be
 * built. It is nevertheless probed rather than required, because a middleware sysroot
 * carrying a prebuilt libbinder.so without the kernel headers is a configuration that
 * can exist. When it is absent the preflight reports "AIDL absent" and the legacy
 * back-end is selected, which is the safe direction to fail in: an unverifiable
 * platform must not be allowed to reach libbinder, where a missing or
 * protocol-mismatched driver node aborts the process outright.
 */
#if defined(__has_include)
#  if __has_include(<linux/android/binder.h>)
#    include <linux/android/binder.h>
/** @brief Set to 1 when the binder kernel UAPI definitions are available. */
#    define CCEC_HAVE_BINDER_UAPI 1
#  endif
#endif
#ifndef CCEC_HAVE_BINDER_UAPI
/** @brief Set to 0 when the binder kernel UAPI definitions are unavailable. */
#define CCEC_HAVE_BINDER_UAPI 0
#endif

#include "DriverAidlImpl.hpp"

#include "osal/EventQueue.hpp"
#include "osal/Exception.hpp"
#include "ccec/Util.hpp"
#include "ccec/Exception.hpp"
#include "ccec/OpCode.hpp"

using CCEC_OSAL::AutoLock;

/**
 * @brief Short alias for the generated `com.rdk.hal.hdmicec` C++ namespace.
 *
 * Introduced instead of `using`-declarations for the individual types: the CCEC
 * middleware compiles with CCEC_NAMESPACE undefined, so its own names sit at global
 * scope, and pulling generic names such as `State` in beside them invites a collision
 * that a later header could create silently.
 */
namespace cechal = ::com::rdk::hal::hdmicec;

/** @brief Short alias for the shared HAL compatibility helpers in halcompat.h. */
namespace halcompat = ::com::rdk::hal::halcompat;

/*
 * Frame block offsets. DriverImpl.hpp defines these for the legacy back-end;
 * DriverAidlImpl.hpp deliberately does not, so they are defined here with the same
 * values, guarded so that a translation unit which has already seen the legacy header
 * keeps one definition.
 */
#ifndef HEADER_OFFSET
/** @brief Index of the CEC header block within a frame. */
#define HEADER_OFFSET 0
#endif
#ifndef OPCODE_OFFSET
/** @brief Index of the CEC opcode block within a frame. */
#define OPCODE_OFFSET 1
#endif

CCEC_BEGIN_NAMESPACE

namespace {

/**
 * @brief Largest CEC message the AIDL transmit contract accepts, in bytes
 *
 * `IHdmiCecController.sendMessage()` states a maximum message size of 16 bytes for the
 * header block plus the opcode block plus the operand blocks. CECFrame carries up to
 * CECFrame::MAX_LENGTH (128) and the legacy HAL specification documents 20, so this
 * limit is the source of authorized observable difference 1. It exists as a named
 * constant so that write() polices the frame against the contract rather than against a
 * literal, and so that the value has exactly one definition.
 *
 * @see DriverAidlImpl::write()
 */
const size_t AIDL_MAX_MESSAGE_LENGTH = 16;

#if CCEC_HAVE_BINDER_UAPI

/**
 * @brief Size of the transient binder mapping the context-manager probe installs
 *
 * The binder driver allocates the buffer for an incoming REPLY out of the receiving
 * process's own mapping, so a probe that expects a reply must map something. The value
 * is far smaller than libbinder's own mapping because the probe exchanges a ping and
 * nothing else, and the mapping is unmapped before the predicate returns either way.
 */
const size_t BINDER_PROBE_MAP_SIZE = 64 * 1024;

/**
 * @brief Upper bound on the iterations the context-manager probe will perform
 *
 * A second, structural bound alongside the caller's timeout. The driver interleaves
 * bookkeeping commands - transaction-complete, no-op, spawn-looper - with the reply, so
 * the probe must drain rather than read once; this caps that drain so the predicate
 * terminates even if the clock misbehaves or the driver returns an unexpected stream.
 */
const unsigned int BINDER_PROBE_MAX_ITERATIONS = 64;

/**
 * @brief Milliseconds elapsed on the monotonic clock since a starting instant
 *
 * Used to convert the caller's timeout into the residual budget for each poll() in the
 * probe's drain loop. The monotonic clock is used deliberately: a wall-clock step
 * during initialization must not extend or collapse a bound whose whole purpose is to
 * keep LibCCEC::init() from stalling.
 *
 * @param [in] start - Instant the measurement began, as captured by clock_gettime()
 *                     against CLOCK_MONOTONIC.
 *
 * @return long - Milliseconds elapsed since @p start
 * @retval 0        - The clock could not be read, in which case reporting no elapsed
 *                    time lets the caller's own iteration bound terminate the loop
 *                    rather than a bogus negative budget doing it.
 * @retval positive - Elapsed milliseconds.
 *
 * @pre @p start was populated by a successful clock_gettime() call.
 * @warning Never throws and never blocks.
 */
long monotonicElapsedMs(const struct timespec &start)
{
	struct timespec now;

	if (0 != clock_gettime(CLOCK_MONOTONIC, &now)) {
		return 0;
	}

	return (((long)(now.tv_sec - start.tv_sec)) * 1000L) +
	       ((now.tv_nsec - start.tv_nsec) / 1000000L);
}

/**
 * @brief Asks the binder driver, under a bounded wait, whether handle 0 resolves
 *
 * Binder handle 0 is the context manager - the `servicemanager` daemon every name
 * lookup goes through. libbinder obtains it by pinging handle 0 and, in the pinned
 * stack, retries that in one-second intervals with no upper bound, so a platform whose
 * driver works but whose `servicemanager` never started would stall middleware
 * initialization indefinitely. This function asks the same question with a deadline,
 * and it asks it over a file descriptor of its own so that no libbinder singleton is
 * created and the process is left exactly as it was.
 *
 * The exchange is the minimum the kernel ABI allows: map a small region so the driver
 * has somewhere to place a reply, post one synchronous BC_TRANSACTION carrying
 * PING_TRANSACTION at handle 0, then drain the driver's command stream under the
 * deadline until the reply or a rejection appears. A missing context manager is
 * rejected promptly with BR_FAILED_REPLY rather than being waited on, so the common
 * negative case costs nothing.
 *
 * @param [in] driverFd  - Open binder driver descriptor whose protocol version has
 *                         already been verified. Left open; the caller closes it.
 * @param [in] timeoutMs - Upper bound, in milliseconds, on waiting for an answer. Zero
 *                         means poll once without waiting.
 *
 * @return bool - Whether handle 0 answered the ping
 * @retval true  - A reply arrived, so a context manager is registered and reachable.
 * @retval false - The mapping failed, the transaction could not be posted, the driver
 *                 rejected the ping, or the deadline expired first.
 *
 * @pre @p driverFd is a binder driver descriptor opened O_RDWR.
 * @post The probe's mapping is released. The reply buffer the driver charged to this
 *       descriptor is reclaimed when the caller closes it, which is why the probe does
 *       not issue BC_FREE_BUFFER of its own.
 * @warning Never throws, never blocks past @p timeoutMs, and treats every unexpected
 *          condition as "unreachable" - the direction that yields the legacy back-end.
 *
 * @see DriverAidlImpl::isBinderPreflightOk()
 */
bool pingBinderContextManager(int driverFd, unsigned int timeoutMs)
{
	void *mapped = ::mmap(0, BINDER_PROBE_MAP_SIZE, PROT_READ,
	                      MAP_PRIVATE | MAP_NORESERVE, driverFd, 0);
	if (MAP_FAILED == mapped) {
		CCEC_LOG( LOG_INFO, "DriverAidlImpl preflight: binder mmap failed with errno %d, treating the context manager as unreachable\r\n", errno);
		return false;
	}

	/*
	 * The write buffer is a command word immediately followed by the transaction
	 * payload, with no alignment padding between them - that is the layout the driver
	 * parses, and it is why the two are memcpy'd into a byte array rather than declared
	 * as a struct the compiler would pad.
	 */
	unsigned char writeBuffer[sizeof(uint32_t) + sizeof(struct binder_transaction_data)];
	const uint32_t writeCommand = BC_TRANSACTION;
	struct binder_transaction_data transaction;

	memset(&transaction, 0, sizeof(transaction));
	transaction.target.handle = 0;                              /* the context manager */
	transaction.code = ::android::IBinder::PING_TRANSACTION;
	transaction.flags = TF_ACCEPT_FDS;                          /* synchronous: TF_ONE_WAY deliberately unset */

	memcpy(writeBuffer, &writeCommand, sizeof(writeCommand));
	memcpy(writeBuffer + sizeof(writeCommand), &transaction, sizeof(transaction));

	struct binder_write_read exchange;

	memset(&exchange, 0, sizeof(exchange));
	exchange.write_size = sizeof(writeBuffer);
	exchange.write_buffer = (binder_uintptr_t)(uintptr_t)writeBuffer;

	bool answered = false;
	bool finished = false;

	if (::ioctl(driverFd, BINDER_WRITE_READ, &exchange) < 0) {
		CCEC_LOG( LOG_INFO, "DriverAidlImpl preflight: could not post the context manager ping, errno %d\r\n", errno);
		finished = true;
	}

	struct timespec started;

	if (0 != clock_gettime(CLOCK_MONOTONIC, &started)) {
		memset(&started, 0, sizeof(started));
	}

	for (unsigned int iteration = 0; !finished && (iteration < BINDER_PROBE_MAX_ITERATIONS); iteration++) {
		long remainingMs = ((long)timeoutMs) - monotonicElapsedMs(started);

		if (remainingMs < 0) {
			remainingMs = 0;
		}

		struct pollfd waiter;

		waiter.fd = driverFd;
		waiter.events = POLLIN;
		waiter.revents = 0;

		const int ready = ::poll(&waiter, 1, (int)remainingMs);

		if (ready < 0) {
			if (EINTR == errno) {
				continue;   /* the deadline above still bounds the retry */
			}
			CCEC_LOG( LOG_INFO, "DriverAidlImpl preflight: waiting on the binder driver failed, errno %d\r\n", errno);
			break;
		}
		if (0 == ready) {
			CCEC_LOG( LOG_INFO, "DriverAidlImpl preflight: the binder context manager did not answer within %u ms, treating the AIDL HAL as absent\r\n", timeoutMs);
			break;
		}

		unsigned char readBuffer[256];

		memset(&exchange, 0, sizeof(exchange));
		exchange.read_size = sizeof(readBuffer);
		exchange.read_buffer = (binder_uintptr_t)(uintptr_t)readBuffer;

		if (::ioctl(driverFd, BINDER_WRITE_READ, &exchange) < 0) {
			if (EINTR == errno) {
				continue;
			}
			CCEC_LOG( LOG_INFO, "DriverAidlImpl preflight: reading the binder reply failed, errno %d\r\n", errno);
			break;
		}
		if (0 == exchange.read_consumed) {
			break;   /* readable but nothing delivered: do not spin */
		}

		size_t consumed = 0;

		while ((consumed + sizeof(uint32_t)) <= (size_t)exchange.read_consumed) {
			uint32_t command = 0;

			memcpy(&command, readBuffer + consumed, sizeof(command));
			consumed += sizeof(command);
			consumed += (size_t)_IOC_SIZE(command);   /* the ABI encodes each command's payload size */

			if (BR_REPLY == command) {
				answered = true;
				finished = true;
				break;
			}
			if ((BR_FAILED_REPLY == command) || (BR_DEAD_REPLY == command) || (BR_ERROR == command)) {
				CCEC_LOG( LOG_INFO, "DriverAidlImpl preflight: the binder driver rejected the context manager ping with command 0x%08x, treating the AIDL HAL as absent\r\n", command);
				finished = true;
				break;
			}
			/* Anything else - transaction complete, no-op, spawn looper - is skipped. */
		}
	}

	::munmap(mapped, BINDER_PROBE_MAP_SIZE);

	return answered;
}

#endif /* CCEC_HAVE_BINDER_UAPI */

} /* anonymous namespace */

/**
 * @brief Receives the HAL's `oneway` CEC events on a binder threadpool thread
 *
 * The AIDL counterpart of the legacy DriverImpl::DriverReceiveCallback() and
 * DriverImpl::DriverTransmitCallback() function pointers, defined here in the
 * implementation file so that the generated server-side base is not pulled into every
 * translation unit that includes DriverAidlImpl.hpp. Only three callbacks are
 * implemented, because BnHdmiCecEventListener already supplies onTransact() and
 * concrete getInterfaceVersion()/getInterfaceHash().@n
 * Of the three, only onMessageReceived() carries behaviour. The other two are
 * diagnostics: acting on them would be new behaviour with no legacy counterpart, since
 * an in-process HAL can neither change state behind the middleware's back nor vanish.
 *
 * Every callback returns `binder::Status::ok()`, including when it has caught a
 * failure. A `oneway` callback has no caller to receive a fault, and letting an
 * exception escape into onTransact() would be worse than dropping one frame - which is
 * exactly the disposition the legacy receive callback takes when it deletes the frame
 * on throw.
 *
 * @warning Reaches its owner through the back pointer below and NEVER through
 *          Driver::getInstance(). The legacy receive callback resolves its target with
 *          `static_cast<DriverImpl &>(Driver::getInstance())`, which is well defined
 *          only because it is registered from DriverImpl::open() and so never runs on
 *          the AIDL path. Reusing that route here would be an ill-typed downcast and
 *          undefined behaviour rather than a failed assertion.
 * @warning Not copyable: RefBase, reached through BnHdmiCecEventListener, keeps its
 *          copy constructor and copy assignment private.
 * @warning Instances are reference counted and must only ever be held in an
 *          `android::sp<>`; the HAL holds a strong reference of its own for the
 *          lifetime of the session.
 *
 * @see DriverAidlImpl::getIncomingQueue()
 * @see DriverImpl::DriverReceiveCallback()
 */
class DriverAidlImpl::EventListener : public cechal::BnHdmiCecEventListener
{
public:
	/**
	 * @brief Binds a listener to the back-end instance that will consume its frames
	 *
	 * @param [in] ownerDriver - Back-end that owns this listener and whose incoming
	 *                           queue received frames are offered onto. Stored by
	 *                           reference, which is safe because the owner creates the
	 *                           listener and releases it no earlier than its own
	 *                           destruction.
	 *
	 * @pre @p ownerDriver outlives this listener.
	 * @warning Never throws.
	 */
	explicit EventListener(DriverAidlImpl &ownerDriver) : owner(ownerDriver)
	{
		CCEC_LOG( LOG_DEBUG, "Creating DriverAidlImpl::EventListener done\r\n");
	}

	/**
	 * @brief Delivers one received CEC message onto the owner's incoming queue
	 *
	 * The AIDL replacement for the legacy `HdmiCecRxCallback_t`, and a step-for-step
	 * equivalent of DriverImpl::DriverReceiveCallback(): the payload is copied into a
	 * freshly allocated CECFrame, logged, and offered onto the incoming queue THROUGH
	 * THE OWNER'S STATE-GUARDED ACCESSOR.@n
	 * Going through that accessor rather than touching the queue member is the
	 * load-bearing part. The accessor raises when the driver is not OPENED, and that is
	 * what rejects a callback which arrives during or after a close and drives the
	 * release of the frame that was about to be enqueued. Offering to the member
	 * directly would accept frames the legacy path rejects.
	 *
	 * The whole body is copy-and-enqueue with no blocking call in it, so a binder
	 * threadpool thread is never held up here, and the frame is handed to the existing
	 * cross-thread queue that the Bus reader thread already drains. The only thing this
	 * migration changes about the receive path is which thread produces into that
	 * queue.
	 *
	 * @param [in] message - The raw CEC message as received by the HAL, header byte
	 *                       first. An empty message is tolerated and yields an empty
	 *                       frame, matching the legacy callback's behaviour for a zero
	 *                       length buffer.
	 *
	 * @return ::android::binder::Status - Always ok
	 * @retval ok - Returned unconditionally, including after a caught failure. See the
	 *              class documentation for why a `oneway` callback must not report a
	 *              fault.
	 *
	 * @pre The owner is OPENED, otherwise the frame is discarded rather than queued.
	 * @post Either the frame is on the incoming queue or it has been released. It is
	 *       never leaked and never double freed.
	 * @warning Runs on a binder threadpool thread, not on any middleware thread.
	 *
	 * @see DriverAidlImpl::read()
	 */
	::android::binder::Status onMessageReceived(const ::std::vector<uint8_t> &message) override
	{
		CECFrame *frame = 0;

		try {
			frame = new CECFrame();
			frame->append(message.data(), message.size());

			CCEC_LOG( LOG_DEBUG, ">>>>>>> >>>>> >>>> >> >> >\r\n");

			dump_buffer(const_cast<unsigned char *>(message.data()), (int)message.size());

			CCEC_LOG(LOG_DEBUG, "==========================\r\n");

			owner.getIncomingQueue().offer(frame);

			CCEC_LOG( LOG_DEBUG, "frame offered\r\n");
		}
		catch(...) {
			CCEC_LOG( LOG_EXP, "Exception during frame offer...discarding\r\n");
			delete frame;
		}

		return ::android::binder::Status::ok();
	}

	/**
	 * @brief Records a HAL state transition in the log and does nothing else
	 *
	 * There is deliberately no action here. Considered and rejected: offering the NULL
	 * sentinel onto the incoming queue on a transition to `State::CLOSED`, so that a
	 * blocked Bus reader unwinds. It has no legacy counterpart - an in-process HAL
	 * cannot close itself underneath the middleware - so it would be new behaviour, and
	 * new behaviour is out of bounds for a transport migration. Death recipients and
	 * `linkToDeath()` are omitted for the same reason.
	 *
	 * @param [in] oldState - State the HAL is leaving.
	 * @param [in] newState - State the HAL has entered.
	 *
	 * @return ::android::binder::Status - Always ok
	 * @retval ok - Returned unconditionally.
	 *
	 * @pre None.
	 * @post No middleware state has changed.
	 * @warning Runs on a binder threadpool thread.
	 */
	::android::binder::Status onStateChanged(cechal::State oldState, cechal::State newState) override
	{
		CCEC_LOG( LOG_INFO, "DriverAidlImpl::EventListener: HAL state changed from %s to %s\r\n", cechal::toString(oldState).c_str(), cechal::toString(newState).c_str());

		return ::android::binder::Status::ok();
	}

	/**
	 * @brief Records the outcome of a HAL-side transmit in the log and nothing else
	 *
	 * A diagnostic only, mirroring the shape of DriverImpl::DriverTransmitCallback(),
	 * which the legacy back-end also uses for nothing but logging. Synchronous transmit
	 * results reach callers as the return value of
	 * `IHdmiCecController::sendMessage()`, so nothing here feeds back into the
	 * middleware.@n
	 * The legacy callback logs only on failure. This one logs unconditionally, because
	 * `SendMessageStatus` has NO back-end-independent success value: `ACK_STATE_0`
	 * means acknowledged for a directed message and rejected for a broadcast, so
	 * deciding "failure" would require re-deriving the destination nibble from the
	 * message. Doing that in a log-only callback would duplicate write()'s translation
	 * in a second place where it could drift, so the status is reported verbatim and
	 * left to the reader.
	 *
	 * @param [in] message - The message the HAL transmitted, echoed back.
	 * @param [in] status  - Bus outcome the HAL observed.
	 *
	 * @return ::android::binder::Status - Always ok
	 * @retval ok - Returned unconditionally.
	 *
	 * @pre None.
	 * @post No middleware state has changed.
	 * @warning Runs on a binder threadpool thread.
	 *
	 * @see DriverAidlImpl::write() - where a transmit outcome is actually acted on
	 */
	::android::binder::Status onMessageSent(const ::std::vector<uint8_t> &message, cechal::SendMessageStatus status) override
	{
		CCEC_LOG( LOG_DEBUG, "======== onMessageSent received. Result: %s, message length: %zu\r\n", cechal::toString(status).c_str(), message.size());

		return ::android::binder::Status::ok();
	}

private:
	/** @brief The back-end that owns this listener and consumes its frames. */
	DriverAidlImpl &owner;
};

/**
 * @brief Constructs the AIDL back-end without touching binder
 *
 * Initializes exactly the same three pieces of state DriverImpl::DriverImpl()
 * initializes, in the same order: the lifecycle state to CLOSED, the retained legacy
 * handle field to 0, and - by their default constructors - the empty frame queue, the
 * lock, the empty local address list and the three null session pointers.
 *
 * Nothing here looks up a service, creates a `ProcessState`, starts a threadpool or
 * opens a driver node, and that is the point rather than an omission. The selection
 * point constructs BOTH back-ends and only then asks this one whether its service came
 * up, so a constructor that reached for binder would abort the process on a legacy-only
 * SOC before the fallback could ever be taken.
 *
 * @post The instance is inert and CLOSED. isServiceAvailable() or open() is the first
 *       thing that touches binder.
 * @warning Does not throw for any reason this class controls.
 *
 * @see DriverAidlImpl::isServiceAvailable()
 * @see DriverImpl::DriverImpl()
 */
DriverAidlImpl::DriverAidlImpl() : status(CLOSED), nativeHandle(0)
{
	CCEC_LOG( LOG_DEBUG, "Creating DriverAidlImpl done\r\n");
}

/**
 * @brief Destroys the back-end, closing an open session first
 *
 * Byte-for-byte the shape of ~DriverImpl(): under the instance lock, a state other than
 * CLOSED is closed through this class's own close(), and a CCEC exception escaping that
 * close is caught and logged rather than allowed out of a destructor.
 *
 * @pre None. Safe on an instance that was never opened, and safe on one whose
 *      isServiceAvailable() returned false.
 * @post No AIDL session is held.
 * @warning Takes the instance lock and then calls close(), which takes it again. That
 *          is sound because CCEC_OSAL::Mutex is documented as recursive, and it is the
 *          legacy destructor's own shape, preserved rather than tidied.
 *
 * @see DriverAidlImpl::close()
 */
DriverAidlImpl::~DriverAidlImpl()
{
    {AutoLock lock_(mutex);
		if (status != CLOSED) {
			try{
                this->close();
	        }
	        catch(Exception &e)
	        {
                CCEC_LOG( LOG_EXP, "DriverAidlImpl: Caught Exception while calling ~DriverAidlImpl::close()\r\n");

            }
		}
    }
}

/**
 * @brief Opens the AIDL HDMI CEC session and begins receiving CEC messages
 *
 * DriverImpl::open() with `IHdmiCec::open()` substituted for `HdmiCecOpen()`, in the
 * same order and under the same lock:
 * -# a state other than CLOSED RETURNS SILENTLY. The legacy `throw
 *    InvalidStateException()` is `#if 0`'d out, so a silent return is the observable
 *    legacy behaviour and the `#if 0` is carried here verbatim so the two files read
 *    alike;
 * -# the process binder threadpool is started, without which no `oneway` listener
 *    callback ever arrives;
 * -# the nested listener is created once and handed to the HAL;
 * -# a non-ok status OR a null controller is an IOException, mirroring the legacy
 *    mapping of an `HdmiCecOpen()` failure;
 * -# the state becomes OPENED.
 *
 * The null-controller arm is contract, not defence: `IHdmiCec.open()` is declared
 * `@nullable` and documented as returning null on error, so an ok status carrying no
 * controller is a genuinely reachable outcome that must not be mistaken for success.
 *
 * @throws IOException - No compatible service proxy is held, or `IHdmiCec::open()`
 *                       reported a non-ok binder status, or it succeeded and returned
 *                       no controller.
 *
 * @pre isServiceAvailable() has returned true. Without it there is no proxy and this
 *      raises IOException rather than crashing.
 * @post The state is OPENED and a controller session is held.
 * @warning `IHdmiCec.open()` admits a single controlling client and fails with
 *          `EX_ILLEGAL_STATE` if one is already open, which is why the CLOSED /
 *          CLOSING / OPENED machine is reproduced rather than simplified.
 *
 * @see DriverAidlImpl::close()
 * @see DriverImpl::open()
 */
void DriverAidlImpl::open(void) noexcept(false)
{
    {AutoLock lock_(mutex);
		if (status != CLOSED) {
			#if 0
				throw InvalidStateException();
			#else
				return;
			#endif
		}

		if (hdmiCecService == 0) {
			CCEC_LOG( LOG_EXP, "DriverAidlImpl::open : no compatible AIDL service proxy is held\r\n");
			throw IOException();
		}

		/*
		 * IHdmiCecEventListener is declared oneway, so its callbacks arrive on a binder
		 * thread inside THIS process, and binder starts no threadpool by default -
		 * without this call no CEC message is ever delivered. startThreadPool() is
		 * idempotent and safe whether or not another component in this process has
		 * already started one, which is precisely why the maximum thread count is left
		 * alone: no back-end-local flag can detect a pool started elsewhere, and
		 * lowering an already-established maximum can abort the process. The library
		 * default governs the thread count and joinThreadPool() is never called, since
		 * it would not return.
		 */
		::android::ProcessState::self()->startThreadPool();

		if (eventListener == 0) {
			eventListener = new EventListener(*this);
		}

		::android::sp<cechal::IHdmiCecController> controller;
		::android::binder::Status txn = hdmiCecService->open(eventListener, &controller);

		CCEC_LOG( LOG_DEBUG, "DriverAidlImpl:: call IHdmiCec::open DONE %s, controller %s\r\n", txn.toString8().string(), (controller == 0) ? "null" : "present");

		if (!txn.isOk() || (controller == 0)) {
			throw IOException();
		}

		hdmiCecController = controller;
		status = OPENED;
    }
}

/**
 * @brief Closes the AIDL HDMI CEC session
 *
 * DriverImpl::close() step for step, with the same ordering decisions preserved because
 * each of them is observable:
 * -# a state other than OPENED RETURNS SILENTLY, for the same `#if 0` reason open()
 *    does;
 * -# the state becomes CLOSING, so a receive callback or a blocked reader arriving from
 *    here on is rejected;
 * -# the NULL sentinel is offered onto the incoming queue so a blocked reader wakes and
 *    unwinds;
 * -# the HAL session is released;
 * -# the state becomes CLOSED BEFORE any failure is raised, so a caller that swallows
 *    the exception still sees a consistently closed object.
 *
 * The local logical-address list is DELIBERATELY NOT CLEARED. DriverImpl::close() does
 * not clear it either, so isValidLogicalAddress() can keep reporting true across a
 * close on the legacy path, and the HAL drops the addresses on its own side in any
 * case. Clearing here would be an unauthorized improvement and a fourth observable
 * difference.
 *
 * @throws IOException - The close transaction reported a non-ok binder status, or it
 *                       succeeded and reported a false result. The state is already
 *                       CLOSED by then.
 *
 * @pre None. A closed instance returns silently.
 * @post The state is CLOSED and no controller session is held. The local address list
 *       is untouched, by design.
 * @warning BLOCKED ITEM B2. `IHdmiCec::close()` is a HIGH-CONFIDENCE CANDIDATE for the
 *          legacy `HdmiCecClose()`, PENDING OWNER CONFIRMATION - the HAL mapping table
 *          carries no entry for `HdmiCecClose()` and no divergence settles it. The
 *          candidate is used because a back-end that cannot close is not deliverable:
 *          every `LibCCEC::term()` would leak an open session and the next open() would
 *          fail `EX_ILLEGAL_STATE`, leaving a back-end that works once per process. If
 *          the mapping owners reject the candidate, the body of THIS ONE METHOD changes
 *          and nothing else does.
 *
 * @see DriverAidlImpl::open()
 * @see DriverImpl::close()
 */
void  DriverAidlImpl::close(void) noexcept(false)
{

    {AutoLock lock_(mutex);
		if (status != OPENED) {
			#if 0
				throw InvalidStateException();
			#else
				return;
			#endif
		}
		status = CLOSING;

		/* Use NULL as sentinel */
		rQueue.offer(0);

		bool closed = false;
		::android::binder::Status txn = ::android::binder::Status::fromStatusT(::android::DEAD_OBJECT);

		if (hdmiCecService != 0) {
			/* B2: candidate mapping for the legacy HdmiCecClose(), pending confirmation. */
			txn = hdmiCecService->close(hdmiCecController, &closed);
		}
		else {
			CCEC_LOG( LOG_EXP, "DriverAidlImpl::close : no AIDL service proxy is held\r\n");
		}

		hdmiCecController.clear();

		CCEC_LOG( LOG_DEBUG, "DriverAidlImpl:: call IHdmiCec::close DONE %s, result %s\r\n", txn.toString8().string(), closed ? "true" : "false");

		if (!txn.isOk() || !closed) {
            status = CLOSED;
			throw IOException();
		}

		status = CLOSED;
    }
}

/**
 * @brief Takes the next received CEC frame, blocking until one arrives
 *
 * BYTE-FOR-BYTE the legacy DriverImpl::read(), class name aside: the same queue, the
 * same `status != OPENED` guard on entry, the same re-check under the lock when the
 * queue yields nothing, and the same flush-then-raise on the NULL close sentinel. There
 * is NO AIDL call in this method at all.@n
 * Copying rather than re-deriving is the specification here, and it is what leaves the
 * Bus reader thread untouched by this migration: the only difference between the two
 * back-ends on the receive path is which thread PRODUCES into the queue - the vendor
 * HAL's own thread on the legacy path, a binder threadpool thread here - and the queue
 * itself was already the cross-thread synchronization point.
 *
 * @param [out] frame - Receives a copy of the frame taken from the incoming queue. Also
 *                      written by the flush that precedes the raise on close, exactly
 *                      as on the legacy path.
 *
 * @throws InvalidStateException - The driver was not OPENED on entry, or it stopped
 *                                 being OPENED while this call was blocked.
 *
 * @pre open() has completed successfully.
 * @warning Blocks the calling thread until a frame or the close sentinel arrives. It is
 *          called from the Bus reader thread, never from a plugin thread.
 *
 * @see DriverAidlImpl::close()
 * @see DriverImpl::read()
 */
void  DriverAidlImpl::read(CECFrame &frame)  noexcept(false)
{
    {AutoLock lock_(mutex);
		if (status != OPENED) {
			throw InvalidStateException();
		}
    }

    CCEC_LOG( LOG_DEBUG, "DriverAidlImpl::Read()\r\n");

    bool backToPoll = false;
	do {
		backToPoll = false;

		CECFrame * inFrame = rQueue.poll();

		if (inFrame != 0) {
			frame = *inFrame;
			delete inFrame;
		}
		else {AutoLock lock_(mutex);

			if (status != OPENED) {
				/* Flush and return */
				while (rQueue.size() > 0) {
					inFrame = rQueue.poll();
					frame = *inFrame;
					delete inFrame;
				}
				throw InvalidStateException();
			}
			else {
				backToPoll = true;
			}
		}
    } while(backToPoll);
}

/**
 * @brief Not supported on the AIDL back-end; raises after the legacy prelude
 *
 * Asynchronous transmit is NOT MIGRATED. The AIDL HAL exposes no asynchronous transmit
 * and none is emulated here: no threads, no work queue, no deferred callback and no
 * wrapper. That is a decided point rather than an unresolved mapping, and it is safe
 * because NO production call site reaches this method - every plugin transmit goes
 * through Connection::sendToAsync() and Connection::sendAsync() onto the Bus writer
 * thread, which then calls the SYNCHRONOUS write().
 *
 * The legacy statement order is nevertheless reproduced exactly, because it is
 * observable. DriverImpl::writeAsync() takes the frame buffer and logs the frame BEFORE
 * it locks and checks the state, and printFrameDetails() catches only CCEC exceptions -
 * so an EMPTY frame raises `std::out_of_range` out of the header decode regardless of
 * the driver state, rather than InvalidStateException. Existing tests assert exactly
 * that. Reordering the prelude for tidiness would be an unregistered behaviour change.
 *
 * @param [in] frame - The frame that would have been transmitted. Still read by the
 *                     prelude, which is why an empty frame raises before the state
 *                     guard runs.
 *
 * @throws std::out_of_range              - Raised out of the prelude for an empty
 *                                          frame. Identical on both back-ends.
 * @throws InvalidStateException          - The driver is not OPENED. Identical on both
 *                                          back-ends.
 * @throws OperationNotSupportedException - Always, once the prelude and the guard have
 *                                          passed.
 *
 * @pre None beyond the prelude's own requirement that the frame be non-empty.
 * @post Nothing was transmitted and no middleware state changed.
 * @warning AUTHORIZED OBSERVABLE DIFFERENCE 2. A valid frame on an open driver succeeds
 *          on the legacy back-end and raises OperationNotSupportedException here.
 *
 * @see DriverAidlImpl::write() - the synchronous path every production caller reaches
 * @see DriverImpl::writeAsync()
 */
void  DriverAidlImpl::writeAsync(const CECFrame &frame)  noexcept(false)
{

	const uint8_t *buf = NULL;
	size_t length = 0;

	frame.getBuffer(&buf, &length);
	printFrameDetails(frame);

    {AutoLock lock_(mutex);
    	if (status != OPENED) {
    		throw InvalidStateException();
    	}
		CCEC_LOG( LOG_EXP, "DriverAidlImpl::writeAsync is not supported on the AIDL back-end; asynchronous transmit is not migrated and is deliberately not emulated. Declining a frame of %zu bytes.\r\n", length);

		throw OperationNotSupportedException();
    }
}


/*
 * Only 1 write is allowed at a time. Queue the write request and wait for response.
 */
/**
 * @brief Transmits a CEC frame synchronously and reports the bus outcome
 *
 * DriverImpl::write() with `IHdmiCecController::sendMessage()` substituted for
 * `HdmiCecTx()`, in the same order: the frame buffer is taken and the frame is logged,
 * then the lock is acquired and the state checked, then the frame length is policed,
 * then the transmit runs WITH THE LOCK STILL HELD, and finally the returned status is
 * translated onto the legacy exception set.@n
 * Holding the lock across the whole IPC round trip is deliberate. The legacy
 * implementation holds it across the whole in-process call, so keeping it preserves the
 * existing serialization exactly; narrowing the critical section would change behaviour
 * and is out of scope.
 *
 * The translation has to respect an INVERTED sense, which is the crux of the whole
 * adaptation. `ACK_STATE_0` means acknowledged for a directed message but REJECTED for
 * a broadcast, and `ACK_STATE_1` is the mirror of that. The destination nibble is read
 * from `frame.at(0) & 0x0F` exactly as the legacy implementation reads it, and each arm
 * below reproduces one arm of the legacy mapping:
 * - a non-ok binder status - transport failure, `EX_ILLEGAL_STATE`, dead binder - is an
 *   IOException, as `err != HDMI_CEC_IO_SUCCESS` is;
 * - `BUSY`, meaning arbitration failed after two attempts and nothing was sent, is an
 *   IOException, as the legacy send-failed family is;
 * - a directed message with `ACK_STATE_1` was not acknowledged and raises
 *   CECNoAckException;
 * - a broadcast with `ACK_STATE_0` raises CECNoAckException ONLY on the CEC CTS 9-3-3
 *   arm - a rejected REPORT_PHYSICAL_ADDRESS - so that the caller retries, and returns
 *   normally for every other opcode, which is what the legacy implementation does;
 * - a directed `ACK_STATE_0` and a broadcast `ACK_STATE_1` both mean success and return
 *   normally.
 *
 * @param [in] frame - The frame to transmit, header byte first. Must be no longer than
 *                     AIDL_MAX_MESSAGE_LENGTH.
 *
 * @throws InvalidStateException - The driver is not OPENED.
 * @throws IOException           - The frame exceeds the AIDL length contract; or no
 *                                 controller session is held; or the transaction
 *                                 reported a non-ok binder status; or the HAL reported
 *                                 `BUSY`.
 * @throws CECNoAckException     - A directed message was not acknowledged, or the CEC
 *                                 CTS 9-3-3 broadcast arm was hit.
 * @throws std::out_of_range     - Raised out of `frame.at()` for a frame with no header
 *                                 byte, as on the legacy path.
 *
 * @pre open() has completed successfully.
 * @post On a normal return the frame reached the CEC bus and was not rejected.
 * @warning AUTHORIZED OBSERVABLE DIFFERENCE 1. `sendMessage()` states a 16-byte
 *          maximum, while CECFrame carries up to CECFrame::MAX_LENGTH (128) and the
 *          legacy HAL specification allows 20 - so a frame of 17 to 20 bytes is
 *          sendable on the legacy back-end and raises IOException here. The frame is
 *          POLICED, NEVER TRUNCATED: truncating would put a corrupt CEC frame on the
 *          bus, which is worse than refusing to send it.
 *
 * @see DriverAidlImpl::poll()
 * @see DriverImpl::write()
 */
void  DriverAidlImpl::write(const CECFrame &frame)  noexcept(false)
{

	const uint8_t *buf = NULL;
	size_t length = 0;

	frame.getBuffer(&buf, &length);
	printFrameDetails(frame);

    {AutoLock lock_(mutex);
    	if (status != OPENED) {
    		throw InvalidStateException();
    	}
		if (length > AIDL_MAX_MESSAGE_LENGTH) {
			CCEC_LOG( LOG_EXP, "DriverAidlImpl::write frame of %zu bytes exceeds the AIDL sendMessage limit of %zu bytes; refusing to truncate\r\n", length, AIDL_MAX_MESSAGE_LENGTH);
			throw IOException();
		}
		if (hdmiCecController == 0) {
			CCEC_LOG( LOG_EXP, "DriverAidlImpl::write : no AIDL controller session is held\r\n");
			throw IOException();
		}
		cechal::SendMessageStatus sendResult = cechal::SendMessageStatus::BUSY;
		CCEC_LOG( LOG_DEBUG, "DriverAidlImpl::write to call IHdmiCecController::sendMessage\r\n");

		::android::binder::Status txn = hdmiCecController->sendMessage(std::vector<uint8_t>(buf, buf + length), &sendResult);

		CCEC_LOG( LOG_DEBUG, ">>>>>>> >>>>> >>>> >> >> >\r\n");

		dump_buffer((unsigned char*)buf,length);

		CCEC_LOG(LOG_DEBUG, "==========================\r\n");

		CCEC_LOG( LOG_DEBUG, "DriverAidlImpl:: call IHdmiCecController::sendMessage DONE %s, result %s\r\n", txn.toString8().string(), cechal::toString(sendResult).c_str());

		if (!txn.isOk()) {
			throw IOException();
		}

        if (sendResult == cechal::SendMessageStatus::BUSY) {
            /* Arbitration failed after two attempts and the message was not sent, which
               is the legacy send-failed family and therefore an IOException. */
            throw IOException();
        }

		if (((frame.at(0) & 0x0F) != 0x0F) && sendResult == cechal::SendMessageStatus::ACK_STATE_1) {
			throw CECNoAckException();
		}
		   /* CEC CTS 9-3-3 -Ensure that the DUT will accept a negatively for broadcat report physical address msg and retry atleast once */
		else if (((frame.at(0) & 0x0F) == 0x0F) && (length > 1) && ((frame.at(1) & 0xFF) == REPORT_PHYSICAL_ADDRESS ) && (sendResult == cechal::SendMessageStatus::ACK_STATE_0))
		{

                   throw CECNoAckException();
		}
    }

    CCEC_LOG( LOG_DEBUG, "Send Completed\r\n");
}

/**
 * @brief Reads back the logical address in use
 *
 * Calls `IHdmiCec::getLogicalAddresses()` - on the top-level interface, NOT on the
 * controller, which carries only the three transmit and address-mutation methods - and
 * returns the first entry. `devType` is IGNORED, exactly as DriverImpl::getLogicalAddress()
 * ignores it: the legacy per-device-type get and the AIDL get-all are treated as
 * equivalent, and no per-type filtering is invented here.@n
 * When the HAL reports more than one address the count and the entry used are logged and
 * the first entry is returned. There is no iteration, no multi-address state and no
 * dispatch fan-out, and no middleware structure or signature grows a plural form: the
 * array shape is confined to the local vector below.
 *
 * @param [in] devType - Accepted for signature compatibility with the Driver interface
 *                       and with the legacy back-end. NOT USED, beyond being echoed into
 *                       the log so the two back-ends' traces line up.
 *
 * @return int - The logical address in use
 * @retval 0     - No address is in use. THIS SINGLE VALUE COVERS FOUR CASES, which are
 *                 distinguished in the log rather than in the return value: no service
 *                 proxy, a non-ok binder status, a successful call that reported no
 *                 addresses, and the genuine address 0. That is not a shortcut - the
 *                 legacy implementation zero-initializes its local and returns whatever
 *                 the HAL leaves there, and LibCCEC::getLogicalAddress() turns a zero
 *                 into an InvalidStateException, which is the EXISTING signal for "no
 *                 address". Any other sentinel here would suppress that throw.
 * @retval other - The first logical address the HAL reports.
 *
 * @pre None. Unlike most methods here this one carries no state guard, matching the
 *      legacy implementation.
 * @post No state changed.
 * @warning Never throws on HAL failure. The zero return is the failure signal.
 * @warning An empty result is a DOCUMENTED NORMAL OUTCOME rather than an error: the
 *          AIDL contract states the array is empty when no additional addresses have
 *          been set.
 *
 * @see DriverAidlImpl::isValidLogicalAddress()
 * @see DriverImpl::getLogicalAddress()
 */
int DriverAidlImpl::getLogicalAddress(int devType)
{
    {AutoLock lock_(mutex);
	int logicalAddress = 0;
	CCEC_LOG( LOG_DEBUG, "DriverAidlImpl::getLogicalAddress called for devType : %d \r\n", devType);

	std::vector<int32_t> halAddresses;

	if (hdmiCecService == 0) {
		CCEC_LOG( LOG_EXP, "DriverAidlImpl::getLogicalAddress : no AIDL service proxy is held; reporting no address\r\n");
	}
	else {
		::android::binder::Status txn = hdmiCecService->getLogicalAddresses(&halAddresses);

		if (!txn.isOk()) {
			CCEC_LOG( LOG_EXP, "DriverAidlImpl::getLogicalAddress : IHdmiCec::getLogicalAddresses failed [%s]; reporting no address\r\n", txn.toString8().string());
		}
		else if (halAddresses.empty()) {
			CCEC_LOG( LOG_INFO, "DriverAidlImpl::getLogicalAddress : the HAL holds no logical addresses; reporting no address\r\n");
		}
		else {
			if (halAddresses.size() > 1) {
				CCEC_LOG( LOG_INFO, "DriverAidlImpl::getLogicalAddress : the HAL reports %zu logical addresses; operating on entry 0 [%d]\r\n", halAddresses.size(), (int)halAddresses[0]);
			}
			logicalAddress = (int)halAddresses[0];
		}
	}

	CCEC_LOG( LOG_DEBUG, "DriverAidlImpl::getLogicalAddress got logical Address : %d \r\n", logicalAddress);
	return logicalAddress;
    }
}

/**
 * @brief Physical-address retrieval - BLOCKED on B1 for this back-end
 *
 * BLOCKED ITEM B1. On the AIDL back-end this method DOES NOT retrieve a physical
 * address. It logs that the read is unavailable pending the device settings HAL
 * contract, names B1, and returns leaving the caller's out parameter UNTOUCHED.
 *
 * The legacy back-end reads the address with `HdmiCecGetPhysicalAddress()`, a CEC HAL
 * call that needs a native handle obtained from `HdmiCecOpen()` - a handle this back-end
 * never has, because it opens the AIDL HAL instead. The mandated replacement is an
 * EDID-byte read belonging to the DEVICE SETTINGS HAL layer, not to the CEC HAL, and the
 * public header declaring it was to be supplied as a separate input to this migration
 * and WAS NOT. Every alternative route is closed, and each for its own reason:
 * - reconstructing the declaration from the plugin test mocks is forbidden, and a mock
 *   is usage;
 * - the AIDL HDMI-output controller EDID event is forbidden outright;
 * - substituting any CEC HAL call is forbidden;
 * - acquiring a legacy CEC handle lazily is forbidden as such a substitution AND is
 *   independently unsafe, because `HdmiCecOpen()` performs logical-address discovery for
 *   source devices - that is CEC polling on the wire, so it would put a SECOND CEC
 *   controller on the bus while the AIDL HAL is the active controller, and the legacy
 *   header additionally warns that the API is not thread safe and requires a matching
 *   close.
 *
 * Returning without writing is the minimum-harm interim behaviour rather than a
 * fabricated value: it is the SAME observable outcome the legacy path already produces
 * when its own call fails, since the legacy implementation ignores the return value and
 * writes nothing on failure. Both plugin call sites already wrap the call and tolerate
 * not obtaining an address.
 *
 * @param [out] physicalAddress - Would receive the physical address. LEFT UNTOUCHED by
 *                                this back-end, so the caller keeps whatever value it
 *                                initialized the variable to.
 *
 * @pre None.
 * @post The out parameter is unmodified and no state changed.
 * @warning The physical address is UNAVAILABLE on the AIDL back-end until B1 resolves.
 *          This is a real functional gap and it is reported rather than papered over.
 * @warning THE BODY OF THIS METHOD IS THE ONE PLACE THAT CHANGES WHEN THE B1 CONTRACT
 *          ARRIVES. At that point the mandated device settings read is implemented here
 *          against the supplied declaration, and DriverImpl::getPhysicalAddress() is
 *          reviewed for whether the same read should replace its own CEC HAL call so
 *          that both back-ends use the mandated API.
 *
 * @see DriverImpl::getPhysicalAddress() - the legacy implementation, unchanged
 */
void DriverAidlImpl::getPhysicalAddress(unsigned int *physicalAddress)
{
    {AutoLock lock_(mutex);
        CCEC_LOG( LOG_EXP, "DriverAidlImpl::getPhysicalAddress : BLOCKED ITEM B1 - the device settings HAL contract for the EDID byte read was not supplied, so the physical address is unavailable on the AIDL back-end. The caller's value is left untouched.\r\n");

        (void)physicalAddress;   /* deliberately NOT written - see B1 above */

        return ;
    }
}


/**
 * @brief Relinquishes one logical address
 *
 * DriverImpl::removeLogicalAddress(), whose shape IS the specification and is therefore
 * reproduced rather than improved: the state guard first, then the removal from the
 * LOCAL list, and only then the HAL call - whose result the legacy implementation
 * DISCARDS.@n
 * That disposition is kept exactly. `IHdmiCecController::removeLogicalAddresses()` is
 * called with a ONE-ELEMENT vector, and both a false result and a non-ok binder status
 * are logged and otherwise ignored, because raising where the legacy back-end returns
 * silently would be an unregistered behaviour change. A caller that needs to know an
 * address was really released must re-query, on either back-end.
 *
 * @param [in] source - The logical address to relinquish. Marshalled as a one-element
 *                      `std::vector<int32_t>` built from LogicalAddress::toInt(); no
 *                      multi-address state is introduced anywhere.
 *
 * @throws InvalidStateException - The driver is not OPENED.
 *
 * @pre open() has completed successfully.
 * @post The address is absent from the local list whether or not the HAL agreed, because
 *       the local removal precedes the HAL call and is not rolled back - which is what
 *       the legacy back-end does.
 * @warning Reports nothing about HAL-side failure, by design and not by omission.
 *
 * @see DriverAidlImpl::addLogicalAddress()
 * @see DriverImpl::removeLogicalAddress()
 */
void DriverAidlImpl::removeLogicalAddress(const LogicalAddress &source)
{
    {AutoLock lock_(mutex);
		if (status != OPENED) {
			throw InvalidStateException();
		}

		logicalAddresses.remove(source);

		if (hdmiCecController == 0) {
			CCEC_LOG( LOG_EXP, "DriverAidlImpl::removeLogicalAddress : no AIDL controller session is held; ignored, matching the legacy back-end\r\n");
		}
		else {
			bool removed = false;
			::android::binder::Status txn = hdmiCecController->removeLogicalAddresses(std::vector<int32_t>{ source.toInt() }, &removed);

			if (!txn.isOk()) {
				CCEC_LOG( LOG_EXP, "DriverAidlImpl::removeLogicalAddress : IHdmiCecController::removeLogicalAddresses failed [%s]; ignored, matching the legacy back-end\r\n", txn.toString8().string());
			}
			else if (!removed) {
				CCEC_LOG( LOG_EXP, "DriverAidlImpl::removeLogicalAddress : the HAL declined to remove logical address %d; ignored, matching the legacy back-end\r\n", source.toInt());
			}
		}
    }
}

/**
 * @brief Acquires one logical address
 *
 * Calls `IHdmiCecController::addLogicalAddresses()` with a ONE-ELEMENT vector built from
 * LogicalAddress::toInt(), then records the address in the local list on success. The
 * state guard, the local bookkeeping and the return-outside-the-lock shape all match
 * DriverImpl::addLogicalAddress().
 *
 * @param [in] source - The logical address to acquire.
 *
 * @return bool - Acquisition result
 * @retval true - The address was acquired. This is the ONLY value ever returned; every
 *                failure leaves by way of an exception, exactly as on the legacy
 *                back-end.
 *
 * @throws InvalidStateException        - The driver is not OPENED. This guard is present
 *                                        on the legacy back-end and is reproduced here.
 * @throws IOException                  - No controller session is held, or the
 *                                        transaction reported a non-ok binder status.
 * @throws AddressNotAvailableException - The HAL reported false.
 *
 * @pre open() has completed successfully.
 * @post On success the address is in the local list, so isValidLogicalAddress() reports
 *       it.
 * @warning AUTHORIZED OBSERVABLE DIFFERENCE 3. The legacy back-end distinguishes three
 *          HAL outcomes - address unavailable, general error, and anything else as
 *          success - whereas `addLogicalAddresses()` returns a SINGLE BOOLEAN documented
 *          as false both when the address is outside 0x0..0xE and when it is already
 *          added. Carrying the legacy distinction would require a new HAL method, which
 *          is out of bounds for this migration, so false maps to the NEARER legacy
 *          category, AddressNotAvailableException. A caller that discriminates
 *          IOException from other failures therefore sees a different category for the
 *          same underlying condition.
 *
 * @see DriverAidlImpl::removeLogicalAddress()
 * @see DriverImpl::addLogicalAddress()
 */
bool DriverAidlImpl::addLogicalAddress(const LogicalAddress &source)
{
    {AutoLock lock_(mutex);

		if (status != OPENED) {
			throw InvalidStateException();
		}

		if (hdmiCecController == 0) {
			CCEC_LOG( LOG_EXP, "DriverAidlImpl::addLogicalAddress : no AIDL controller session is held\r\n");
			throw IOException();
		}

		bool added = false;
		::android::binder::Status txn = hdmiCecController->addLogicalAddresses(std::vector<int32_t>{ source.toInt() }, &added);

		if (!txn.isOk()) {
			CCEC_LOG( LOG_EXP, "DriverAidlImpl::addLogicalAddress : IHdmiCecController::addLogicalAddresses failed [%s]\r\n", txn.toString8().string());
			throw IOException();
		}
		else if (!added) {
			CCEC_LOG( LOG_EXP, "DriverAidlImpl::addLogicalAddress : the HAL declined logical address %d\r\n", source.toInt());
			throw AddressNotAvailableException();
		}
		else {
			logicalAddresses.push_back(source);
		}
    }

    return true;
}

/**
 * @brief Reports whether a logical address is one this device holds
 *
 * BYTE-FOR-BYTE the legacy DriverImpl::isValidLogicalAddress(), class name aside: a walk
 * of the local list under the instance lock, with NO HAL CALL on either back-end. It is
 * copied rather than re-derived because there is nothing here for the transport to
 * change, and because keeping it identical is what makes the two back-ends
 * indistinguishable to Connection, its only production caller.@n
 * Note that close() does not clear the local list on either back-end, so this can still
 * report true after a close. That is pre-existing behaviour, preserved.
 *
 * @param [in] source - The logical address to test.
 *
 * @return bool - Whether the address is held locally
 * @retval true  - The address is in the local list.
 * @retval false - It is not.
 *
 * @pre None. Valid in any state.
 * @post No state changed.
 * @warning Never throws.
 *
 * @see DriverAidlImpl::addLogicalAddress()
 * @see DriverImpl::isValidLogicalAddress()
 */
bool DriverAidlImpl::isValidLogicalAddress(const LogicalAddress & source) const
{
	AutoLock lock_(mutex);
		bool found = false;
		std::list<LogicalAddress>::const_iterator it;
		for (it = logicalAddresses.begin(); it != logicalAddresses.end(); it++) {
			if(*it == source) {
				found = true;
				break;
			}
		}
	return found;
}

/**
 * @brief Pings a logical address by transmitting a header-only frame
 *
 * BYTE-FOR-BYTE the legacy DriverImpl::poll(), class name aside: the header byte is
 * assembled as `((from & 0x0F) << 4) | (to & 0x0F)`, appended to a one-byte CECFrame,
 * and handed to THIS back-end's own write() - so the poll travels over whichever
 * transport the enclosing back-end uses, with no transport-specific code of its own.@n
 * The AIDL `getState()` is DELIBERATELY NOT USED. A poll is a CEC ping performed by a
 * one-byte transmit, not a state query, and consulting the HAL's state would create a
 * second source of truth beside the middleware's own machine. The outcome therefore
 * reaches the caller through write()'s exceptions.
 *
 * The trailing `#if 0` block is inert legacy code, carried verbatim so that this method
 * remains a character-for-character copy of its counterpart and the two files diff
 * cleanly. It is not new dead code and it is not a placeholder.
 *
 * @param [in] from - Initiator logical address, placed in the high nibble.
 * @param [in] to   - Follower logical address to ping, placed in the low nibble.
 *
 * @throws CECNoAckException     - Nothing acknowledged the ping, which is the normal way
 *                                 a caller learns the address is free.
 * @throws IOException           - The transmit failed, or the HAL was busy.
 * @throws InvalidStateException - The driver is not OPENED.
 *
 * @pre open() has completed successfully.
 * @post Nothing was queued locally; the ping went out on the bus.
 *
 * @see DriverAidlImpl::write()
 * @see DriverImpl::poll()
 */
void DriverAidlImpl::poll(const LogicalAddress &from, const LogicalAddress &to)
     	 	 	 	  noexcept(false)
{
	uint8_t firstByte = (((from.toInt() & 0x0F) << 4) | (to.toInt() & 0x0F));
	CCEC_LOG( LOG_DEBUG, "$$$$$$$$$$$$$$$$$$$$ POST POLL [%s] [%s]$$$$$$$$$$$$$$$$$$$$$\r\n", from.toString().c_str(), to.toString().c_str());

	{
		CECFrame frame;
		frame.append(firstByte);
		write(frame);
	}
	
#if 0
	{
		/* Send a Poll so indicate there is a device present */
		CECFrame *frame = new CECFrame();
		frame->append(firstByte);
		rQueue.offer(frame);
	}
#endif
}

/**
 * @brief Returns the incoming frame queue, but only while the driver is open
 *
 * The state-guarded accessor that mirrors DriverImpl::getIncomingQueue(). The GUARD is
 * the load-bearing part, not the return: it is what rejects a receive callback that
 * arrives during or after a close, and it is what drives the listener's release of the
 * frame it was about to enqueue. The listener must reach the queue through here and
 * never touch the member directly, or it would accept frames the legacy path rejects.@n
 * The legacy signature carries a native handle its own body never reads; it is dropped
 * here because the AIDL back-end has no handle to pass.
 *
 * @return IncomingQueue& - The queue received frames are offered onto and that read()
 *                          drains.
 *
 * @throws InvalidStateException - The driver is not OPENED, i.e. it is closed or
 *                                 closing.
 *
 * @pre None. The state check is this method's entire purpose.
 * @post No state changed.
 * @warning Reads the state WITHOUT holding the instance lock, reproducing the
 *          pre-existing unlocked read in DriverImpl::getIncomingQueue(). That race
 *          exists today between the vendor HAL's thread and a closing thread; here it is
 *          the same shape with a binder thread in place of the HAL thread. It is a known
 *          pre-existing condition, PRESERVED DELIBERATELY rather than fixed, because
 *          behaviour preservation outranks code improvement in this migration. Adding a
 *          lock here would be an unregistered change.
 *
 * @see DriverAidlImpl::read()
 * @see DriverImpl::getIncomingQueue()
 */
DriverAidlImpl::IncomingQueue & DriverAidlImpl::getIncomingQueue(void)
{
	if (status != OPENED) {
		throw InvalidStateException();
	}

	return rQueue;
}

/**
 * @brief Logs a decoded, human-readable rendering of a frame
 *
 * BYTE-FOR-BYTE the legacy DriverImpl::printFrameDetails(), class name aside: pure
 * formatting with no HAL call on either back-end. The header is decoded from the frame,
 * the opcode name is resolved when the frame is long enough to carry one, and every CCEC
 * exception raised while decoding a malformed frame is caught and logged so that
 * diagnostics can never break a transmit.@n
 * Two details are preserved precisely because they are load bearing rather than
 * incidental. The catch names `Exception`, the CCEC base, so a `std::out_of_range` from
 * an EMPTY frame's header decode ESCAPES - which is the mechanism by which
 * writeAsync()'s prelude raises before its state guard on both back-ends. And the single
 * log line here terminates with a bare `\n` where every other log line in this file uses
 * `\r\n`; that is how the legacy line reads, and normalizing it would break the
 * character-for-character copy this method is required to be.
 *
 * @param [in] frame - The frame to render.
 *
 * @throws std::out_of_range - Propagated out of the header decode for a frame with no
 *                             header byte. Not caught here, on either back-end.
 *
 * @pre None. A malformed frame is tolerated; an empty one raises.
 * @post No state changed.
 * @warning Declared `noexcept(false)` to match the Driver interface, and does not in
 *          practice propagate CCEC exceptions - only the non-CCEC one noted above.
 *
 * @see DriverAidlImpl::write()
 * @see DriverImpl::printFrameDetails()
 */
void  DriverAidlImpl::printFrameDetails(const CECFrame &frame)  noexcept(false) {
	const uint8_t *buf = NULL;
	char strBuffer[50] = {0};
	size_t len = 0;
	const char *opname = "none";

	try{
		frame.getBuffer(&buf, &len);
		Header header(frame,HEADER_OFFSET);
		for (size_t i = 0; i < len; i++) {
			snprintf(strBuffer + strlen(strBuffer) , (sizeof(strBuffer) - strlen(strBuffer)) ,"%02X ",(uint8_t) *(buf + i));
		}
		if (frame.length() > OPCODE_OFFSET) {
			opname = GetOpName(OpCode(frame,OPCODE_OFFSET).opCode());
			CCEC_LOG( LOG_INFO, "%s to %s : opcode: %s :%s\n",header.from.toString().c_str(), header.to.toString().c_str(), opname, strBuffer);
		}
	}
	catch(Exception &e)
	{
		CCEC_LOG(LOG_EXP, "printFrameDetails caught %s \r\n",e.what());
	}
}

/**
 * @brief Decides whether a binder lookup may safely be attempted at all
 *
 * A preflight is REQUIRED rather than convenient, and this is the method that discharges
 * the absolute requirement that a supported SOC WITHOUT an AIDL HAL must reach the
 * legacy back-end - never abort, never block. Reaching the service manager directly is
 * unsafe in two independent ways on the pinned binder stack, and either one alone
 * defeats that requirement:
 * - a missing or protocol-mismatched driver node is FATAL, not an error return. The pin
 *   extends libbinder's failed-driver `LOG_ALWAYS_FATAL_IF` to plain Linux, so the abort
 *   that upstream guards for Android is live here and would kill the middleware during
 *   initialization instead of falling back. This was confirmed by observation on a
 *   driverless host, where merely reaching `ProcessState::self()` raises SIGABRT;
 * - a missing context manager BLOCKS. Obtaining an `IServiceManager` at all polls in
 *   one-second intervals until binder handle 0 resolves, with no upper bound of its own.
 *
 * The three checks therefore run BEFORE ANYTHING IN THE PROCESS TOUCHES LIBBINDER, in
 * this order, stopping at the first failure: the node exists and can be opened; the
 * protocol version it reports EQUALS the version this build was compiled for, because
 * libbinder enforces equality and a mismatch fails every open; and handle 0 resolves
 * within a BOUNDED timeout. Only when all three pass may the caller proceed to the
 * service lookup and the compatibility check.
 *
 * @param [in]  binderDriverPath        - Binder driver node to inspect. Taken as a
 *                                        parameter, not hard-coded, so that the negative
 *                                        arms - a nonexistent path, or a path whose
 *                                        reported protocol differs - can be exercised
 *                                        WITHOUT rendering a test runner's real driver
 *                                        unusable.
 * @param [in]  contextManagerTimeoutMs - Upper bound, in milliseconds, on the handle-0
 *                                        resolution check. Zero means do not wait at
 *                                        all, which is how the timeout arm is
 *                                        exercised.
 *
 * @return bool - Whether a binder lookup may safely be attempted
 * @retval true  - All three checks passed.
 * @retval false - The path was empty, or the node is absent or cannot be opened, or its
 *                 protocol version could not be read or differs, or handle 0 did not
 *                 answer within the bound, or this build has no binder kernel ABI
 *                 definitions with which to check any of it. WHICH ONE is distinguished
 *                 in the log, not in the return value.
 *
 * @pre None whatsoever. This is the first thing that runs, on any platform.
 * @post Nothing in the process has been left initialized: no `ProcessState` singleton
 *       exists, no threadpool was started, and the descriptor and mapping this check
 *       used are released before it returns. A false result leaves the process exactly
 *       as it was.
 * @warning The protocol constant this compares against follows `BINDER_IPC_32BIT`, so
 *          the middleware MUST be compiled with the same setting as the libbinder it
 *          links - an all-32-bit platform speaks protocol 7 and a 32-bit-middleware
 *          against 64-bit-vendor platform speaks 8. Disagreement makes every open fail,
 *          and this check reports that as "AIDL absent" rather than letting libbinder
 *          abort on it.
 * @warning Never throws and never blocks beyond the stated bound. Both guarantees are
 *          what the caller relies on.
 *
 * @see DriverAidlImpl::isServiceAvailable()
 * @see DriverAidlImpl::DEFAULT_BINDER_DRIVER_PATH
 * @see DriverAidlImpl::DEFAULT_CONTEXT_MANAGER_TIMEOUT_MS
 */
bool DriverAidlImpl::isBinderPreflightOk(const std::string &binderDriverPath,
                                         unsigned int contextManagerTimeoutMs)
{
#if CCEC_HAVE_BINDER_UAPI
	if (binderDriverPath.empty()) {
		CCEC_LOG( LOG_INFO, "DriverAidlImpl preflight: no binder driver path was given; treating the AIDL HAL as absent\r\n");
		return false;
	}

	/*
	 * Qualified as ::open and ::close throughout: this class has members of both names,
	 * and inside a member function an unqualified call would find those instead.
	 */
	const int driverFd = ::open(binderDriverPath.c_str(), O_RDWR | O_CLOEXEC);

	if (driverFd < 0) {
		CCEC_LOG( LOG_INFO, "DriverAidlImpl preflight: binder driver [%s] could not be opened, errno %d; treating the AIDL HAL as absent\r\n", binderDriverPath.c_str(), errno);
		return false;
	}

	struct binder_version driverVersion;

	memset(&driverVersion, 0, sizeof(driverVersion));

	if (::ioctl(driverFd, BINDER_VERSION, &driverVersion) < 0) {
		CCEC_LOG( LOG_INFO, "DriverAidlImpl preflight: binder driver [%s] would not report its protocol version, errno %d; treating the AIDL HAL as absent\r\n", binderDriverPath.c_str(), errno);
		::close(driverFd);
		return false;
	}

	if (driverVersion.protocol_version != BINDER_CURRENT_PROTOCOL_VERSION) {
		CCEC_LOG( LOG_WARN, "DriverAidlImpl preflight: binder driver [%s] speaks protocol %d but this build expects %d; treating the AIDL HAL as absent\r\n", binderDriverPath.c_str(), (int)driverVersion.protocol_version, (int)BINDER_CURRENT_PROTOCOL_VERSION);
		::close(driverFd);
		return false;
	}

	const bool contextManagerReachable = pingBinderContextManager(driverFd, contextManagerTimeoutMs);

	::close(driverFd);

	if (contextManagerReachable) {
		CCEC_LOG( LOG_INFO, "DriverAidlImpl preflight: binder driver [%s] and its context manager are usable\r\n", binderDriverPath.c_str());
	}

	return contextManagerReachable;
#else
	CCEC_LOG( LOG_WARN, "DriverAidlImpl preflight: this build carries no binder kernel ABI definitions, so binder driver [%s] cannot be verified (timeout %u ms unused); treating the AIDL HAL as absent\r\n", binderDriverPath.c_str(), contextManagerTimeoutMs);
	return false;
#endif
}

/**
 * @brief Reports whether a usable, compatible AIDL HDMI CEC service is present
 *
 * This is the question the selection helper in `ccec/src/Driver.cpp` asks, ONCE, after
 * both back-ends have been constructed. It answers in three ordered stages and stops at
 * the first that fails: the binder preflight, so that nothing in this process touches
 * libbinder unless doing so is known to be safe; the service lookup, which resolves
 * `IHdmiCec::serviceName()` - the literal `"HdmiCec"` - through the binder service
 * manager; and the compatibility check.
 *
 * PRESENCE ALONE IS NOT SUFFICIENT. A service that answers but cannot be spoken to
 * compatibly is treated exactly as an absent one and the legacy back-end is selected.
 * The compatibility rule is NOT reimplemented here - `halcompat::isCompatible()` is
 * reused verbatim, which rejects a null proxy, an empty or `"-1"` interface hash and an
 * unfrozen development server, and otherwise compares this client's compiled-in
 * `IHdmiCec::VERSION` against the server's within one era and major. That comparison
 * ACCEPTS A NEWER SERVER within the same era and major, which is why nothing here treats
 * "not exactly our version" as incompatible.
 *
 * A resolved and compatible proxy is CACHED on this instance for open() to use, so the
 * lookup happens once for the process rather than once per operation.
 *
 * @return bool - Whether the AIDL back-end should be selected
 * @retval true  - The preflight passed, the service resolved, and it is compatible. The
 *                 proxy is cached and open() may be called.
 * @retval false - Any stage failed. The reason is logged, and the caller must select the
 *                 legacy back-end.
 *
 * @pre None. Safe to call on a platform with no binder driver, no `servicemanager` and
 *      no AIDL HAL whatsoever - which is the entire point of the preflight.
 * @post No lifecycle transition occurs; the instance remains CLOSED either way. On true,
 *       and only on true, the service proxy is held.
 * @warning NEVER aborts, NEVER blocks indefinitely and NEVER propagates an exception -
 *          a legacy-only SOC must reach the legacy back-end rather than fail to
 *          initialize. The catch-all below is what makes the last of those three
 *          unconditional rather than merely expected.
 * @warning Not thread safe, and not intended to be: it is called once from the factory's
 *          one-time static initializer, which the language already serializes.
 *
 * @see DriverAidlImpl::isBinderPreflightOk()
 * @see DriverAidlImpl::open()
 */
bool DriverAidlImpl::isServiceAvailable(void)
{
	try {
		if (!isBinderPreflightOk()) {
			CCEC_LOG( LOG_INFO, "DriverAidlImpl::isServiceAvailable : the binder preflight declined; the AIDL HDMI CEC HAL is treated as absent\r\n");
			return false;
		}

		const std::string &halServiceName = cechal::IHdmiCec::serviceName();
		::android::sp<cechal::IHdmiCec> service = halcompat::getService<cechal::IHdmiCec>();

		if (service == 0) {
			CCEC_LOG( LOG_INFO, "DriverAidlImpl::isServiceAvailable : binder service [%s] is not registered; the AIDL HDMI CEC HAL is treated as absent\r\n", halServiceName.c_str());
			return false;
		}

		if (!halcompat::isCompatible<cechal::IHdmiCec>(service)) {
			const int clientVersion = cechal::IHdmiCec::VERSION;

			CCEC_LOG( LOG_WARN, "DriverAidlImpl::isServiceAvailable : binder service [%s] is present but not compatible with this client's interface version %d; the AIDL HDMI CEC HAL is treated as absent\r\n", halServiceName.c_str(), clientVersion);
			return false;
		}

		hdmiCecService = service;

		CCEC_LOG( LOG_INFO, "DriverAidlImpl::isServiceAvailable : binder service [%s] is present and compatible\r\n", halServiceName.c_str());

		return true;
	}
	catch(...) {
		CCEC_LOG( LOG_EXP, "DriverAidlImpl::isServiceAvailable : unexpected failure while querying the AIDL HDMI CEC service; it is treated as absent\r\n");
		return false;
	}
}




CCEC_END_NAMESPACE


/** @} */
/** @} */
