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

/**
 * @brief Shortest received CEC message this back-end will accept onto the incoming queue
 *
 * ONE BYTE, and the exact value matters in both directions.
 *
 * It cannot be zero. An out-of-process HAL can deliver an empty `std::vector<uint8_t>` -
 * nothing in the AIDL contract prevents it and no in-process HAL could produce it, so
 * nothing downstream was ever written to survive it. An empty frame queued here is taken by
 * the Bus reader thread, which hands it to printFrameDetails(); that decodes
 * `Header(frame, HEADER_OFFSET)`, which reaches `frame.at(0)` and raises
 * `std::out_of_range`. printFrameDetails() catches `Exception &`, the CCEC base, which does
 * NOT match it; Bus::Reader::run() catches `InvalidStateException &`, which does not match
 * it either. The exception therefore escapes the thread function and TERMINATES THE
 * PROCESS. One malformed message from the HAL is enough. Rejecting the message here, before
 * anything is allocated, is the only place inside this back-end that can prevent it -
 * `ccec/src/Bus.cpp` and the legacy back-end are out of bounds for this migration.
 *
 * It must not be two. A ONE-BYTE frame is legitimate CEC traffic: a poll, the header-only
 * ping this class's own poll() transmits, carries exactly one byte and no opcode. The
 * legacy receive path delivers such a frame, and printFrameDetails() handles it safely -
 * `Header(frame, 0)` succeeds and the `frame.length() > OPCODE_OFFSET` test simply skips
 * the opcode decode. Requiring a header plus an opcode would therefore drop valid frames
 * and be a behaviour regression, which is why the bound stops at the smallest length that
 * is decodable rather than at the smallest length that is useful.
 *
 * @see DriverAidlImpl::EventListener::onMessageReceived()
 */
const size_t MIN_RECEIVED_MESSAGE_LENGTH = 1;

/**
 * @brief Most message bytes the receive diagnostic renders before it truncates
 *
 * The receive callback runs on a binder threadpool thread and must return promptly, so its
 * diagnostic has to have a CEILING rather than a length that follows the message. A CEC
 * message cannot legitimately exceed the 16 bytes the AIDL transmit contract states, and
 * CECFrame can carry up to 128, so this ceiling shows every byte of any well-formed message
 * with margin, and truncates a malformed one instead of tracking it.
 *
 * @see renderReceivedMessageHex()
 */
const size_t RECEIVE_LOG_MAX_BYTES = 24;

/** @brief Appended to the rendering when the message was longer than RECEIVE_LOG_MAX_BYTES. */
const char RECEIVE_LOG_TRUNCATION_MARKER[] = "...";

/**
 * @brief Size of the stack buffer the receive diagnostic renders into
 *
 * Three characters per rendered byte - two hex digits and the separating space, matching the
 * `"%02X "` rendering the legacy dump produced - plus room for the truncation marker and its
 * terminator, which `sizeof` already includes. Derived rather than written out, so the
 * buffer cannot fall out of step with the ceiling above.
 */
const size_t RECEIVE_LOG_TEXT_SIZE = (RECEIVE_LOG_MAX_BYTES * 3) + sizeof(RECEIVE_LOG_TRUNCATION_MARKER);

/**
 * @brief Renders a received CEC message as bounded `"%02X "` hex text, without calling out
 *
 * The receive path's whole diagnostic, replacing three separate log lines and a
 * per-byte-`printf` dump. It matters that this is cheap and finite: the callback it serves
 * arrives on a binder threadpool thread for every CEC message the HAL delivers, and the
 * work it does is work the HAL's own thread pool waits on.@n
 * THE RENDERING IS UNCONDITIONAL, and that is a constraint rather than a choice.
 * `dump_buffer()` could skip its work by testing `cec_log_level`, but that variable is
 * file-static in `ccec/src/Util.cpp` and there is no accessor, so no code outside that
 * translation unit can ask whether DEBUG logging is even enabled. The rendering therefore
 * happens on every message regardless of log level, which is exactly why it must be
 * call-free and bounded: two direct nibble-to-character writes plus one separator per byte,
 * into a fixed stack buffer, with no function call, no allocation and no stdio in the loop.
 * CCEC_LOG() then discards the finished line cheaply when the level is below DEBUG.
 *
 * @param [in]  message - First byte of the message to render. Not read at all when
 *                        @p length is zero.
 * @param [in]  length  - Number of bytes available at @p message. Only the first
 *                        RECEIVE_LOG_MAX_BYTES are rendered; the rest are represented by
 *                        RECEIVE_LOG_TRUNCATION_MARKER.
 * @param [out] text    - Fixed-size buffer receiving the NUL-terminated rendering. Taken as
 *                        a REFERENCE TO AN ARRAY of exactly RECEIVE_LOG_TEXT_SIZE
 *                        characters, so a buffer of the wrong size is a compile error
 *                        rather than an overflow.
 *
 * @pre @p message addresses at least @p length readable bytes.
 * @post @p text holds a NUL-terminated string of at most RECEIVE_LOG_TEXT_SIZE - 1
 *       characters. The worst case is bounded by construction: 3 characters per rendered
 *       byte, at most RECEIVE_LOG_MAX_BYTES of them, plus the marker.
 * @warning Never throws, never blocks, performs no allocation and calls nothing.
 *
 * @see DriverAidlImpl::EventListener::onMessageReceived()
 */
void renderReceivedMessageHex(const uint8_t *message, size_t length, char (&text)[RECEIVE_LOG_TEXT_SIZE])
{
	static const char hexDigits[] = "0123456789ABCDEF";

	const size_t rendered = (length > RECEIVE_LOG_MAX_BYTES) ? RECEIVE_LOG_MAX_BYTES : length;
	size_t at = 0;

	for (size_t i = 0; i < rendered; i++) {
		text[at++] = hexDigits[(message[i] >> 4) & 0x0F];
		text[at++] = hexDigits[message[i] & 0x0F];
		text[at++] = ' ';
	}

	if (rendered < length) {
		for (const char *marker = RECEIVE_LOG_TRUNCATION_MARKER; *marker != '\0'; marker++) {
			text[at++] = *marker;
		}
	}

	text[at] = '\0';
}

/**
 * @brief Elapsed time, in milliseconds, above which one synchronous HAL call is reported
 *
 * A DIAGNOSTIC THRESHOLD, NOT A TIMEOUT AND NOT A GATE. Crossing it changes nothing about
 * the call: no transaction is abandoned, no exception is raised, no state moves and no
 * retry happens. The only effect is a single LOG_WARN line naming the operation and the
 * elapsed time, so that a stall which would otherwise be silent leaves evidence in the
 * log a field engineer can find.@n
 * The value is taken from THE ONLY DOCUMENTED TIMING FIGURE THIS PROJECT HAS - the HDMI
 * CEC HAL specification's bound of one second on CEC transmit completion, 200 ms desired.
 * It is deliberately not derived from a benchmark, because no performance measurement has
 * been taken: that measurement is deferred human work on target hardware. Every operation
 * instrumented with it is either a transmit or an operation that must complete well inside
 * a transmit's budget, so one second is a threshold no healthy HAL crosses and every
 * wedged one does.
 *
 * @warning THIS BOUNDS NOTHING - see the F10 and F20 notes on the methods that use it. The
 *          pinned libbinder C++ backend exposes no client-side transaction deadline, so an
 *          unbounded synchronous call remains unbounded and is merely made observable.
 *          Reading this constant as a timeout would be a serious mistake.
 *
 * @see warnIfHalCallSlow()
 */
const int64_t SLOW_HAL_CALL_WARN_MS = 1000;

/**
 * @brief Start instant standing for "the monotonic clock could not be read"
 *
 * Negative, so it cannot collide with any real CLOCK_MONOTONIC reading. When a start
 * instant carries this value the elapsed measurement is skipped altogether rather than
 * computed from a fabricated origin: a diagnostic that cannot be trusted is worth less
 * than no diagnostic, and inventing an origin is exactly how a bound becomes no bound at
 * all.
 *
 * @see halCallStarted()
 * @see warnIfHalCallSlow()
 */
const int64_t HAL_CALL_CLOCK_UNREADABLE = -1;

/**
 * @brief Reads CLOCK_MONOTONIC as whole milliseconds
 *
 * The single clock utility in this translation unit, used both by the synchronous-call
 * diagnostics and by the binder context-manager probe's deadline. It sits OUTSIDE the
 * CCEC_HAVE_BINDER_UAPI guard, unlike the probe that also uses it, because the
 * diagnostics are compiled into every build while the probe additionally needs the binder
 * kernel ABI definitions. That is the whole of the restructuring: one helper serves both,
 * so there is no second clock utility to drift.@n
 * The monotonic clock is used deliberately rather than the wall clock - a wall-clock step
 * during initialization must not extend or collapse either a diagnostic measurement or a
 * bound whose purpose is to keep LibCCEC::init() from stalling. Milliseconds in `int64_t`
 * cannot overflow on any reachable uptime, so no narrowing and no wraparound is possible
 * here.
 *
 * @param [out] nowMs - Receives the reading, in milliseconds. Left untouched on failure.
 *
 * @return bool - Whether the clock could be read
 * @retval true  - @p nowMs holds the current monotonic instant.
 * @retval false - clock_gettime() failed, @p nowMs is unmodified, and the caller must
 *                 treat the measurement as unavailable rather than assume a value.
 *
 * @pre None.
 * @warning Never throws and never blocks.
 *
 * @see halCallStarted()
 */
bool monotonicNowMs(int64_t &nowMs)
{
	struct timespec now;

	if (0 != clock_gettime(CLOCK_MONOTONIC, &now)) {
		return false;
	}

	nowMs = (((int64_t)now.tv_sec) * 1000LL) + (((int64_t)now.tv_nsec) / 1000000LL);

	return true;
}

/**
 * @brief Captures the instant immediately before a synchronous HAL call is issued
 *
 * Paired with warnIfHalCallSlow() around each synchronous AIDL operation. Two clock reads
 * per HAL operation is negligible against an IPC round trip, which is why the
 * instrumentation can be unconditional and needs no sampling, no counter and no state of
 * its own.
 *
 * @return int64_t - Monotonic start instant in milliseconds, or HAL_CALL_CLOCK_UNREADABLE
 *                   when the clock could not be read.
 *
 * @pre None.
 * @warning Never throws and never blocks.
 *
 * @see warnIfHalCallSlow()
 */
int64_t halCallStarted(void)
{
	int64_t nowMs = 0;

	return monotonicNowMs(nowMs) ? nowMs : HAL_CALL_CLOCK_UNREADABLE;
}

/**
 * @brief Reports, once, that a synchronous HAL call took longer than the threshold
 *
 * The observable half of the F10 and F20 mitigation. It measures how long a synchronous
 * AIDL call actually took and emits ONE LOG_WARN line when that exceeded
 * SLOW_HAL_CALL_WARN_MS. Nothing else happens: the call has already returned by the time
 * this runs, so there is nothing left to abandon, and no fast path gains a second log
 * line because below the threshold this function is silent.@n
 * It exists because the alternatives do not exist or are forbidden. The pinned libbinder
 * C++ backend offers no client-side transaction deadline;
 * `IPCThreadState::talkWithDriver()` retries on EINTR, so neither a signal nor an interval
 * timer can break a blocked transaction out; a bounded-join worker thread could not cancel
 * one either and is forbidden by this migration's threading constraint; and narrowing the
 * driver lock is forbidden because the legacy serialization must be preserved exactly.
 * What remains in scope is making a stall visible, which is what this does.
 *
 * @param [in] operation - Stable name of the AIDL operation that just returned, used
 *                         verbatim in the log so a reader can tell which call stalled.
 * @param [in] startedMs - Value halCallStarted() returned before the call. When it is
 *                         HAL_CALL_CLOCK_UNREADABLE the measurement is skipped.
 *
 * @pre @p startedMs came from halCallStarted() around the same call.
 * @post Either nothing was logged, or exactly one LOG_WARN line was.
 * @warning Never throws, never blocks and never alters control flow: the caller's status
 *          translation, exception mapping and return value are entirely unaffected.
 *
 * @see SLOW_HAL_CALL_WARN_MS
 * @see halCallStarted()
 */
void warnIfHalCallSlow(const char *operation, int64_t startedMs)
{
	int64_t nowMs = 0;

	if ((HAL_CALL_CLOCK_UNREADABLE == startedMs) || !monotonicNowMs(nowMs)) {
		return;
	}

	const int64_t elapsedMs = nowMs - startedMs;

	if (elapsedMs > SLOW_HAL_CALL_WARN_MS) {
		CCEC_LOG( LOG_WARN, "DriverAidlImpl: synchronous AIDL operation [%s] took %lld ms, past the %lld ms diagnostic threshold; the call was NOT abandoned and no deadline was enforced - see findings F10 and F20\r\n", operation, (long long)elapsedMs, (long long)SLOW_HAL_CALL_WARN_MS);
	}
}

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
 * @brief Longest single poll() wait the context-manager probe will ask for, in milliseconds
 *
 * The probe waits in SLICES rather than in one call, and this is the length of a slice.
 * That is what makes the `int` that poll() takes safe: the caller's timeout is an
 * `unsigned int` and a large value converted directly to `int` can land NEGATIVE, and
 * `poll(..., -1)` waits FOREVER - the exact opposite of the bound this probe exists to
 * provide. A value clamped into [0, POLL_SLICE_MAX_MS] cannot do that, whatever the caller
 * passed and whatever the clock reports.@n
 * Slices also make the wait re-derive itself from the absolute deadline on every iteration,
 * so no single accounting error can extend the total.
 *
 * @see pingBinderContextManager()
 */
const unsigned int POLL_SLICE_MAX_MS = 250;

/*
 * The two bounds must not fight each other: if the iteration cap could expire before the
 * deadline it would cut a legitimately waiting probe short and report a usable context
 * manager as unreachable. Whole slices needed to consume the largest timeout the class
 * accepts must therefore fit inside the cap. Checked at compile time so that changing
 * either constant cannot quietly break the relationship. Note that a handful of iterations
 * are also consumed by EINTR retries and by drain passes that deliver no reply, which is
 * why the cap is left comfortably above the required minimum rather than trimmed to it.
 */
static_assert((DriverAidlImpl::MAX_CONTEXT_MANAGER_TIMEOUT_MS / POLL_SLICE_MAX_MS) <= BINDER_PROBE_MAX_ITERATIONS,
              "BINDER_PROBE_MAX_ITERATIONS must cover MAX_CONTEXT_MANAGER_TIMEOUT_MS in POLL_SLICE_MAX_MS slices");

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
 * HOW THE WAIT IS BOUNDED, and why it is built this way rather than the obvious way:
 * - ONE ABSOLUTE DEADLINE is computed before the loop, in 64-bit milliseconds, and every
 *   iteration re-derives its budget from that deadline. Computing a residual from a fresh
 *   elapsed measurement each time - the obvious way - lets any single accounting error
 *   re-grant the full timeout on every pass, so the loop's real bound becomes the
 *   iteration count multiplied by the timeout rather than the timeout;
 * - each wait is SLICED and clamped into `[0, POLL_SLICE_MAX_MS]` before the conversion to
 *   the `int` poll() takes. An unclamped `unsigned int` timeout can convert to a negative
 *   `int`, and `poll(..., -1)` blocks FOREVER. The clamp makes that unreachable by
 *   construction rather than by the caller behaving reasonably;
 * - a slice expiring is NOT the deadline expiring: the loop continues while the deadline
 *   still leaves budget, and reports the timeout only once it does not;
 * - a CLOCK READ FAILURE fails the probe as "unreachable" instead of being treated as zero
 *   elapsed time. An unbounded wait is a worse outcome than a false negative, and a false
 *   negative here simply selects the legacy back-end;
 * - BINDER_PROBE_MAX_ITERATIONS still caps the loop, as belt and braces alongside the
 *   deadline. The static assertion beside it keeps that cap from firing before the deadline
 *   does.
 *
 * @param [in] driverFd  - Open binder driver descriptor whose protocol version has
 *                         already been verified. Left open; the caller closes it.
 * @param [in] timeoutMs - Upper bound, in milliseconds, on waiting for an answer. ZERO
 *                         MEANS POLL ONCE WITHOUT WAITING, which is how the negative arm
 *                         is exercised, and that meaning is load bearing. The caller
 *                         clamps this to DriverAidlImpl::MAX_CONTEXT_MANAGER_TIMEOUT_MS
 *                         before calling, and the slicing above makes the wait safe
 *                         regardless of the value that arrives here.
 *
 * @return bool - Whether handle 0 answered the ping
 * @retval true  - A reply arrived, so a context manager is registered and reachable.
 * @retval false - The mapping failed, the transaction could not be posted, the monotonic
 *                 clock could not be read, the driver rejected the ping, or the deadline
 *                 expired first.
 *
 * @pre @p driverFd is a binder driver descriptor opened O_RDWR.
 * @post The probe's mapping is released on every path, including the early ones. The reply
 *       buffer the driver charged to this descriptor is reclaimed when the caller closes
 *       it, which is why the probe does not issue BC_FREE_BUFFER of its own.
 * @warning Never throws, and never blocks longer than @p timeoutMs plus at most one poll
 *          slice of scheduling latency. Every unexpected condition is treated as
 *          "unreachable" - the direction that yields the legacy back-end.
 *
 * @see DriverAidlImpl::isBinderPreflightOk()
 * @see POLL_SLICE_MAX_MS
 * @see monotonicNowMs()
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

	/*
	 * ONE absolute deadline, in 64-bit milliseconds, established before the loop. Every
	 * iteration measures against this instant rather than recomputing an elapsed time, so
	 * no iteration can re-grant the caller's full timeout to itself.
	 */
	int64_t deadlineMs = 0;

	if (!finished) {
		int64_t startedMs = 0;

		if (!monotonicNowMs(startedMs)) {
			CCEC_LOG( LOG_INFO, "DriverAidlImpl preflight: the monotonic clock could not be read, so the context manager wait cannot be bounded; treating the AIDL HAL as absent\r\n");
			finished = true;
		}
		else {
			deadlineMs = startedMs + (int64_t)timeoutMs;
		}
	}

	for (unsigned int iteration = 0; !finished && (iteration < BINDER_PROBE_MAX_ITERATIONS); iteration++) {
		int64_t nowMs = 0;

		if (!monotonicNowMs(nowMs)) {
			CCEC_LOG( LOG_INFO, "DriverAidlImpl preflight: the monotonic clock stopped being readable mid-probe, so the remaining wait cannot be bounded; treating the AIDL HAL as absent\r\n");
			break;
		}

		const int64_t remainingMs = deadlineMs - nowMs;
		int64_t sliceMs = (remainingMs < 0) ? 0 : remainingMs;

		if (sliceMs > (int64_t)POLL_SLICE_MAX_MS) {
			sliceMs = (int64_t)POLL_SLICE_MAX_MS;
		}

		struct pollfd waiter;

		waiter.fd = driverFd;
		waiter.events = POLLIN;
		waiter.revents = 0;

		/*
		 * sliceMs is now known to lie in [0, POLL_SLICE_MAX_MS], so this conversion cannot
		 * produce a negative int and this poll() cannot become an unbounded wait.
		 */
		const int ready = ::poll(&waiter, 1, (int)sliceMs);

		if (ready < 0) {
			if (EINTR == errno) {
				continue;   /* the absolute deadline above still bounds the retry */
			}
			CCEC_LOG( LOG_INFO, "DriverAidlImpl preflight: waiting on the binder driver failed, errno %d\r\n", errno);
			break;
		}
		if (0 == ready) {
			if (remainingMs > (int64_t)sliceMs) {
				continue;   /* the slice expired, not the deadline */
			}
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

/*
 * The four default probe operations, i.e. the real kernel-facing calls the preflight
 * performs in production. They exist UNCONDITIONALLY, whether or not this build has the
 * binder kernel UAPI definitions, and that is deliberate: the CCEC_HAVE_BINDER_UAPI
 * guard is hoisted DOWN INTO them so that isBinderPreflightOk() itself has exactly five
 * decision arms in every configuration. A predicate whose branch structure changed with
 * a build macro could not be gated per branch, and each of these five arms carries one
 * record in the coverage branch manifest.
 *
 * On a build without the UAPI definitions the verdict is unchanged - false - because the
 * version read below fails, and the reason is logged rather than inferred.
 *
 * `::open` and `::close` are qualified for consistency with the rest of this file, where
 * the qualification is required because DriverAidlImpl has members of both names.
 */

/**
 * @brief Default probe: opens the binder driver node with the real `::open`
 *
 * A thin wrapper rather than `&::open` itself, because `::open` is variadic and its
 * address therefore has a type no fixed-arity function pointer can hold.
 *
 * @param [in] path  - Node to open.
 * @param [in] flags - Open flags; the preflight passes `O_RDWR | O_CLOEXEC`.
 *
 * @return int - Descriptor, or negative with `errno` set.
 *
 * @pre None.
 * @warning Never throws.
 *
 * @see DriverAidlImpl::defaultBinderProbe()
 */
int defaultOpenBinderNode(const char *path, int flags)
{
	return ::open(path, flags);
}

/**
 * @brief Default probe: reads the driver's protocol version with `BINDER_VERSION`
 *
 * Carries the version out as an `unsigned int` so that no binder kernel type appears in
 * DriverAidlImpl.hpp, and reports failure the way the preflight expects rather than
 * letting a kernel type or an ioctl return code cross the seam.
 *
 * @param [in]  driverFd        - Descriptor returned by defaultOpenBinderNode().
 * @param [out] protocolVersion - Receives the reported version. Written only on success.
 *
 * @return int - Outcome
 * @retval 0  - The version was read.
 * @retval -1 - The ioctl failed, or this build carries no binder kernel ABI definitions
 *              with which to ask. `errno` carries the ioctl's reason in the first case
 *              and ENOSYS in the second, so the caller's log line is meaningful either
 *              way.
 *
 * @pre @p driverFd is an open descriptor and @p protocolVersion is non-null.
 * @warning Never throws.
 *
 * @see DriverAidlImpl::expectedBinderProtocolVersion()
 */
int defaultReadBinderProtocolVersion(int driverFd, unsigned int *protocolVersion)
{
#if CCEC_HAVE_BINDER_UAPI
	struct binder_version driverVersion;

	memset(&driverVersion, 0, sizeof(driverVersion));

	if (::ioctl(driverFd, BINDER_VERSION, &driverVersion) < 0) {
		return -1;
	}

	*protocolVersion = (unsigned int)driverVersion.protocol_version;

	return 0;
#else
	(void)driverFd;
	(void)protocolVersion;

	CCEC_LOG( LOG_WARN, "DriverAidlImpl preflight: this build carries no binder kernel ABI definitions, so the driver's protocol version cannot be read; treating the AIDL HAL as absent\r\n");

	errno = ENOSYS;

	return -1;
#endif
}

/**
 * @brief Default probe: pings binder handle 0 under the caller's bound
 *
 * Delegates to pingBinderContextManager() where the binder kernel ABI definitions exist.
 * Where they do not, this is unreachable in practice - the version read above has
 * already failed the predicate - and reports "unreachable", which is the direction that
 * yields the legacy back-end.
 *
 * @param [in] driverFd  - Descriptor whose protocol version has been verified.
 * @param [in] timeoutMs - Upper bound in milliseconds; zero means do not wait.
 *
 * @return bool - Whether a context manager answered
 * @retval true  - It answered.
 * @retval false - It did not within the bound, or this build cannot ask.
 *
 * @pre @p driverFd is an open binder driver descriptor.
 * @warning Never throws and never blocks past @p timeoutMs.
 *
 * @see pingBinderContextManager()
 */
bool defaultPingBinderContextManager(int driverFd, unsigned int timeoutMs)
{
#if CCEC_HAVE_BINDER_UAPI
	return pingBinderContextManager(driverFd, timeoutMs);
#else
	(void)driverFd;
	(void)timeoutMs;

	return false;
#endif
}

/**
 * @brief Default probe: releases the driver descriptor with the real `::close`
 *
 * @param [in] driverFd - Descriptor to release.
 *
 * @return int - Zero on success, negative with `errno` set on failure. The preflight
 *               ignores it, exactly as the code this replaced ignored `::close`'s.
 *
 * @pre @p driverFd was returned by defaultOpenBinderNode().
 * @warning Never throws.
 *
 * @see DriverAidlImpl::defaultBinderProbe()
 */
int defaultCloseBinderNode(int driverFd)
{
	return ::close(driverFd);
}

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
 * LISTENER LIFETIME, AND WHY DETACHMENT RATHER THAN DESTRUCTION IS THE MECHANISM. The
 * owner holds one `android::sp<EventListener>` and the HAL holds a strong reference of
 * its own, taken when the listener crossed the binder boundary in `IHdmiCec::open()`.
 * Releasing the owner's reference therefore does NOT necessarily destroy the object:
 * the HAL's reference can keep it alive and, with it, keep alive a back pointer to a
 * DriverAidlImpl that is being or has been destroyed. That is reachable rather than
 * theoretical, because the AIDL "no further callbacks" guarantee holds only after a
 * SUCCESSFUL `IHdmiCec::close()`, while a FAILED close still drives this back-end to
 * CLOSED - and ~DriverAidlImpl() deliberately swallows that failure, exactly as
 * ~DriverImpl() swallows its own.@n
 * detach() closes that window. Every callback that touches the owner does so under the
 * listener's own lock, and detach() nulls the back pointer under that same lock, so a
 * detached listener the HAL still calls logs and drops rather than dereferencing freed
 * state. The owner detaches on every path that ends a session: both arms of close(),
 * the failure arms of open(), and unconditionally in its destructor.
 *
 * LOCK ORDER, stated so a later reader can re-check it rather than re-derive it. The
 * listener lock is a LEAF with one documented exception: a receive callback holds it
 * while calling EventQueue::offer(), which takes and releases the queue's own lock
 * inside. The teardown side never runs in the opposite order - close() calls
 * `rQueue.offer(0)` first, which takes and RELEASES the queue lock, and only then calls
 * detach() - so the two acquisitions never nest in both directions and there is no
 * inversion to deadlock on. Nor can detach() wait unboundedly on an in-flight callback:
 * that callback's only blocking-capable call is offer(), and offer() never blocks
 * (EventQueue::poll() waits on its condition variable OUTSIDE the queue lock). The
 * listener lock is also never taken while the owner's instance lock is required in the
 * other direction: a callback reaches the owner only through getIncomingQueue(), which
 * takes no instance lock at all.
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
 *          lifetime of the session, which is why the object may OUTLIVE the owner's
 *          reference to it and why detach() exists.
 *
 * @see DriverAidlImpl::EventListener::detach()
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
	 *                           queue received frames are offered onto. Its ADDRESS is
	 *                           stored, not a reference, so that detach() can null it
	 *                           when the owner ends the session; a reference member
	 *                           could not be cleared and would leave the only way to
	 *                           stop the callbacks be destroying an object the HAL may
	 *                           still hold a strong reference to.
	 *
	 * @pre @p ownerDriver outlives this listener, OR the owner detaches it before it
	 *      ceases to. The second alternative is what the owner actually guarantees.
	 * @post The listener is attached. Callbacks deliver to @p ownerDriver until
	 *       detach() is called.
	 * @warning Never throws.
	 *
	 * @see detach()
	 */
	explicit EventListener(DriverAidlImpl &ownerDriver) : owner(&ownerDriver)
	{
		CCEC_LOG( LOG_DEBUG, "Creating DriverAidlImpl::EventListener done\r\n");
	}

	/**
	 * @brief Severs the link to the owner, after any in-flight callback has finished
	 *
	 * The whole of the CWE-416 guarantee, and it rests on one property: every callback
	 * that touches the owner holds this listener's lock FOR ITS ENTIRE BODY, so taking
	 * that same lock here means detach() cannot return while a callback is part-way
	 * through dereferencing the owner. On return, therefore, no callback is inside the
	 * owner and no later callback can enter it - the back pointer is null and
	 * onMessageReceived() drops instead of delivering. THAT BLOCKING PROPERTY IS THE
	 * POINT of taking the lock rather than merely writing a null.@n
	 * It is called on every path that ends a session, including the ones that FAILED,
	 * because a failed `IHdmiCec::close()` leaves the HAL entitled to keep calling: the
	 * AIDL guarantee of no further callbacks holds only after a close that succeeded.
	 *
	 * Idempotent, and deliberately so: close() and ~DriverAidlImpl() may both reach it
	 * for the same listener, and open()'s failure arm may reach it for a listener that
	 * never received anything.
	 *
	 * @pre None. Safe on an attached or an already detached listener, and safe to call
	 *      from any thread.
	 * @post The owner back pointer is null, no callback is executing inside the owner,
	 *       and every subsequent callback is a logged drop.
	 * @warning Blocks until an in-flight callback completes. That wait is bounded: a
	 *          callback's only blocking-capable call is EventQueue::offer(), which takes
	 *          the queue lock briefly and never waits on it.
	 * @warning Does NOT destroy this object and must not be confused with doing so. The
	 *          HAL holds its own strong reference, so destruction is not the owner's to
	 *          schedule - which is precisely why detachment is the mechanism.
	 *
	 * @see onMessageReceived()
	 * @see DriverAidlImpl::close()
	 */
	void detach(void)
	{
		{AutoLock lock_(ownerMutex);
			owner = 0;
		}
	}

	/**
	 * @brief Delivers one received CEC message onto the owner's incoming queue
	 *
	 * The AIDL replacement for the legacy `HdmiCecRxCallback_t`, and a step-for-step
	 * equivalent of DriverImpl::DriverReceiveCallback(): the payload is copied into a
	 * freshly allocated CECFrame, logged, and handed to the incoming queue THROUGH THE
	 * OWNER'S STATE-GUARDED, ACCEPTANCE-REPORTING HANDOFF.@n
	 * Going through the owner rather than touching the queue member is the load-bearing
	 * part. The handoff reaches the queue through getIncomingQueue(), which raises when
	 * the driver is not OPENED, and that is what rejects a callback which arrives during
	 * or after a close and drives the release of the frame that was about to be
	 * enqueued. Offering to the member directly would accept frames the legacy path
	 * rejects.
	 *
	 * OWNERSHIP IS EXPLICIT HERE, because the queue can refuse. `EventQueue::offer()`
	 * returns void and silently discards its argument once the queue is at capacity, so a
	 * callback that offered and then dropped its pointer would leak one heap frame per
	 * event, without bound, whenever the Bus reader falls behind. Instead
	 * DriverAidlImpl::offerReceivedFrame() reports whether it took the frame: on true the
	 * local pointer is cleared IMMEDIATELY, so neither the code below nor any catch arm
	 * can touch a frame the queue now owns; on false this callback still owns the frame,
	 * releases it and says so at LOG_EXP. Nothing between the successful handoff and the
	 * clearing of the pointer can throw, so there is no window in which both would free
	 * it.
	 *
	 * DIAGNOSTIC OUTPUT IS ONE BOUNDED LINE, not a loop. The success path emits exactly
	 * one CCEC_LOG() call, carrying the byte count and a hex rendering of at most
	 * RECEIVE_LOG_MAX_BYTES bytes built by renderReceivedMessageHex() into a fixed-size
	 * stack buffer. It replaces two marker lines, a "frame offered" line and
	 * dump_buffer(), which together cost four log calls plus one stdio call PER BYTE on a
	 * binder thread for every message received. The rendering keeps the `%02X ` spelling
	 * dump_buffer() produced, so the bytes read the same in a log; what is gone is the
	 * per-byte call and the unbounded length. A refusal, a state-guard rejection or a
	 * length rejection is reported at LOG_EXP, so a line here with no LOG_EXP line after
	 * it means the frame was queued - which is the whole of what "frame offered" used to
	 * say.
	 *
	 * The whole body is copy-and-enqueue with no blocking call in it, so a binder
	 * threadpool thread is never held up here, and the frame is handed to the existing
	 * cross-thread queue that the Bus reader thread already drains. The only thing this
	 * migration changes about the receive path is which thread produces into that
	 * queue.
	 *
	 * THE MESSAGE LENGTH IS CHECKED FIRST AND THE DETACH SECOND, and nothing is
	 * allocated before either. The length check needs neither the owner nor the lock, so
	 * it runs ahead of both and a malformed message never even contends for the listener
	 * lock; MIN_RECEIVED_MESSAGE_LENGTH records why an under-length message would
	 * otherwise terminate the process. The owner back pointer is then read under the
	 * listener lock, and a null pointer - the state detach() leaves behind - means the
	 * session this listener belonged to has ended. A message arriving then is logged and dropped: not enqueued, not
	 * allocated, and above all not delivered through a pointer to storage that may
	 * already be gone. The HAL is entitled to arrive here after a FAILED close, so this
	 * arm is reachable rather than defensive. The lock is held across the whole
	 * delivery, which is what lets detach() guarantee that no callback is inside the
	 * owner once it returns.
	 *
	 * @param [in] message - The raw CEC message as received by the HAL, header byte
	 *                       first. A message shorter than MIN_RECEIVED_MESSAGE_LENGTH is
	 *                       REJECTED before the lock is taken and before anything is
	 *                       allocated: an out-of-process HAL can deliver an empty vector,
	 *                       and an empty frame on the queue kills the Bus reader thread
	 *                       and with it the process. A one-byte frame is a legitimate CEC
	 *                       poll and is accepted.
	 *
	 * @return ::android::binder::Status - Always ok
	 * @retval ok - Returned unconditionally, including after a caught failure and after
	 *              a post-detach drop. See the class documentation for why a `oneway`
	 *              callback must not report a fault.
	 *
	 * @pre The owner is attached and OPENED, otherwise the frame is dropped before
	 *      allocation or discarded after it, respectively.
	 * @post Either the frame is on the incoming queue or it has been released, or no
	 *       frame was ever allocated. It is never leaked and never double freed, in every
	 *       one of the six exits: queued; rejected for length before allocation; dropped
	 *       after a detach before allocation; refused at the queue's receive limit;
	 *       rejected by the OPENED-state guard; or lost to an allocation or append
	 *       failure.
	 * @post Nothing undecodable reaches the incoming queue, so the Bus reader thread
	 *       cannot be killed by a malformed message from the HAL.
	 * @warning Runs on a binder threadpool thread, not on any middleware thread.
	 * @warning A refusal means the Bus reader is not draining. Dropping the frame is then
	 *          the only choice left - the alternative is unbounded growth on a path a
	 *          remote HAL drives - and the drop is logged rather than silent.
	 *
	 * @see detach()
	 * @see DriverAidlImpl::offerReceivedFrame()
	 * @see MIN_RECEIVED_MESSAGE_LENGTH
	 * @see DriverAidlImpl::read()
	 */
	::android::binder::Status onMessageReceived(const ::std::vector<uint8_t> &message) override
	{
		/*
		 * LENGTH CHECK FIRST, before the lock and before anything is allocated. An empty
		 * message from an out-of-process HAL would otherwise become an empty frame on the
		 * incoming queue, and the Bus reader's decode of that frame raises
		 * std::out_of_range - which neither printFrameDetails() nor Bus::Reader::run()
		 * catches, so it escapes the thread function and TAKES THE PROCESS WITH IT. See
		 * MIN_RECEIVED_MESSAGE_LENGTH for the full chain and for why the bound is one byte
		 * rather than two. It runs ahead of the detach check because it needs neither the
		 * owner nor the lock, and running it first keeps a malformed message from even
		 * contending for the listener lock.
		 */
		if (message.size() < MIN_RECEIVED_MESSAGE_LENGTH) {
			CCEC_LOG( LOG_EXP, "DriverAidlImpl::EventListener::onMessageReceived : the HAL delivered a %zu byte message, shorter than the %zu byte minimum a CEC frame can carry; discarding it rather than queueing an undecodable frame\r\n", message.size(), MIN_RECEIVED_MESSAGE_LENGTH);

			return ::android::binder::Status::ok();
		}

		{AutoLock lock_(ownerMutex);
			if (owner == 0) {
				CCEC_LOG( LOG_EXP, "DriverAidlImpl::EventListener: message received after detach, dropping it\r\n");

				return ::android::binder::Status::ok();
			}

			CECFrame *frame = 0;

			try {
				frame = new CECFrame();
				frame->append(message.data(), message.size());

				/*
				 * ONE bounded diagnostic for the whole callback. What stood here was three
				 * CCEC_LOG() calls - two markers and a "frame offered" line, each a
				 * vsnprintf plus a timestamp plus a printf - and a dump_buffer() that
				 * issues one printf() PER BYTE, up to 128 of them, on a binder thread, for
				 * every message received. The rendering below is call-free and capped
				 * instead; see renderReceivedMessageHex() for why it cannot be skipped on
				 * log level and therefore has to be cheap.
				 */
				char messageText[RECEIVE_LOG_TEXT_SIZE];

				renderReceivedMessageHex(message.data(), message.size(), messageText);

				CCEC_LOG( LOG_DEBUG, ">>> DriverAidlImpl::EventListener::onMessageReceived : %zu bytes : %s\r\n", message.size(), messageText);

				/*
				 * OWNERSHIP IS EXPLICIT, because the queue can refuse.
				 * `EventQueue::offer()` returns void and silently discards its argument
				 * once the queue is at capacity, so a callback that offered and then
				 * dropped its pointer would leak one heap frame per event, without bound,
				 * whenever the Bus reader falls behind. offerReceivedFrame() reports
				 * whether it took the frame; it still reaches the queue through
				 * getIncomingQueue(), so the OPENED-state guard that rejects a callback
				 * arriving during or after a close still runs and still raises.
				 */
				if (owner->offerReceivedFrame(frame)) {
					/*
					 * The queue owns the frame from this point. Clear the local pointer
					 * before anything else runs, so that neither the code below nor any
					 * catch arm can release a frame that is no longer this callback's.
					 * Nothing between the handoff returning true and this assignment can
					 * throw.
					 */
					frame = 0;
				}
				else {
					/*
					 * The queue refused the frame, so it did NOT take it and this callback
					 * still owns it. Releasing it here is what keeps a stalled reader from
					 * turning every further event into a leaked frame. The refusal point
					 * is one entry BELOW the capacity, because the last slot is reserved
					 * for close()'s wake-the-reader sentinel; the line below names both
					 * numbers so a log reader is not left thinking the queue reported
					 * itself full one entry early.
					 */
					CCEC_LOG( LOG_EXP, "DriverAidlImpl::EventListener::onMessageReceived : the incoming frame queue refused the frame at its %zu entry receive limit, one below its %zu entry capacity so close()'s sentinel always fits; releasing a %zu byte frame rather than leaking it\r\n", (size_t)(DriverAidlImpl::INCOMING_QUEUE_CAPACITY - 1), (size_t)DriverAidlImpl::INCOMING_QUEUE_CAPACITY, message.size());

					delete frame;
					frame = 0;
				}
			}
			catch(InvalidStateException &e) {
				/*
				 * THE CLOSED-STATE REJECTION, GIVEN ITS OWN ARM so it is distinguishable
				 * in a log from an allocation or append failure. This is the expected and
				 * REACHABLE outcome of a frame arriving while the driver is not OPENED:
				 * getIncomingQueue(), reached through offerReceivedFrame(), raises before
				 * anything is offered, so the frame is still this callback's to release.
				 * It is the AIDL counterpart of the legacy delete-on-throw cleanup, and
				 * separating it from the general arm is what stops a routine
				 * close-race drop from reading like a fault.
				 */
				CCEC_LOG( LOG_EXP, "DriverAidlImpl::EventListener::onMessageReceived : the driver is not OPENED (%s), so the incoming queue rejected a %zu byte frame; releasing it\r\n", e.what(), message.size());

				delete frame;
			}
			catch(...) {
				CCEC_LOG( LOG_EXP, "Exception during frame offer...discarding\r\n");
				delete frame;
			}
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
	 * The line carries the MESSAGE BYTES as well as the status and the length. A status on
	 * its own cannot be tied to a transmit when several are in flight, and a length cannot
	 * distinguish two frames of the same size, so a report meant to be read after the fact
	 * has to name which message it is about. The bytes are rendered as lowercase hex into a
	 * bounded stack buffer sized for `CECFrame::MAX_LENGTH`, and a longer message - which
	 * the HAL cannot legitimately echo, since write() refuses to send one - is truncated in
	 * the RENDERING only, with an ellipsis, because a diagnostic must not be able to
	 * overrun and must not allocate on a binder thread.
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
		/*
		 * Two hex digits per byte plus the terminator, with the capacity fixed at compile
		 * time from the frame maximum rather than from the argument, so the rendering
		 * cannot be made to grow by anything the HAL sends.
		 */
		char rendered[(CECFrame::MAX_LENGTH * 2) + 4];
		size_t written = 0;

		for (size_t index = 0; (index < message.size()) && ((written + 2) < sizeof(rendered)); index++) {
			written += (size_t)snprintf(rendered + written, sizeof(rendered) - written, "%02x", message[index]);
		}

		rendered[written] = '\0';

		if (message.size() > (size_t)CECFrame::MAX_LENGTH) {
			snprintf(rendered + written, sizeof(rendered) - written, "...");
		}

		CCEC_LOG( LOG_DEBUG, "======== onMessageSent received. Result: %s, message length: %zu, message bytes: %s\r\n", cechal::toString(status).c_str(), message.size(), rendered);

		return ::android::binder::Status::ok();
	}

private:
	/**
	 * @brief Guards @ref owner against a callback racing the owner's teardown
	 *
	 * Held for the whole of every callback that dereferences @ref owner, and by
	 * detach() while it nulls it. Those two facts together are what make detach()
	 * synchronous with respect to in-flight delivery rather than merely eventual.@n
	 * It is a lock of the LISTENER's, deliberately distinct from the owner's instance
	 * lock: a callback must be able to complete while the owner's teardown holds that
	 * instance lock, which is exactly the situation close() and ~DriverAidlImpl() create.
	 * See the lock-order paragraph in the class documentation.
	 */
	Mutex ownerMutex;
	/**
	 * @brief The back-end that owns this listener and consumes its frames, or null
	 *
	 * A POINTER rather than a reference precisely so that it can be nulled. Null means
	 * the owner has detached and this listener - which the HAL may still hold a strong
	 * reference to - must not touch it again. Read and written only under
	 * @ref ownerMutex.
	 */
	DriverAidlImpl *owner;
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
DriverAidlImpl::DriverAidlImpl() : status(CLOSED), nativeHandle(0), rQueue(INCOMING_QUEUE_CAPACITY), availabilityReason(NULL)
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
 * One thing is added that the legacy destructor has no need of, because the legacy
 * back-end has no listener object: the event listener is DETACHED AND RELEASED
 * UNCONDITIONALLY, after the close attempt. Swallowing the close exception is required
 * of a destructor and is what the legacy destructor does, but on this back-end that
 * swallowed failure is exactly the case in which the HAL may still hold - and still
 * call - the listener, so the detach cannot be left to the close that just failed.
 *
 * @pre None. Safe on an instance that was never opened, and safe on one whose
 *      isServiceAvailable() returned false.
 * @post No AIDL session is held and no listener holds a pointer to this instance, on
 *       every path through this destructor.
 * @warning Takes the instance lock and then calls close(), which takes it again. That
 *          is sound because CCEC_OSAL::Mutex is documented as recursive, and it is the
 *          legacy destructor's own shape, preserved rather than tidied.
 * @warning The detach takes the LISTENER's lock while this destructor holds the
 *          INSTANCE lock. That nesting is one-directional: a callback never acquires
 *          the instance lock, because it reaches this object only through
 *          getIncomingQueue(), which takes no lock at all. See the lock-order paragraph
 *          on EventListener.
 *
 * @see DriverAidlImpl::EventListener::detach()
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

		/*
		 * Unconditional, and the last thing this object does. It must run on all three
		 * paths that reach here, which is why it sits outside the state test above:
		 * close() threw and its exception was swallowed just now; close() returned
		 * silently or succeeded and already detached, making this the idempotent second
		 * call; or the instance was never opened but open() had already constructed a
		 * listener before failing. Leaving any of those with an attached listener is
		 * CWE-416 - the storage this back pointer names is about to cease to exist,
		 * while the HAL's own strong reference can keep the listener callable.
		 */
		if (eventListener != 0) {
			eventListener->detach();
			eventListener.clear();
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
 * -# the nested listener is created, if this session does not have one yet, and handed
 *    to the HAL;
 * -# a non-ok status OR a null controller is an IOException, mirroring the legacy
 *    mapping of an `HdmiCecOpen()` failure;
 * -# the state becomes OPENED.
 *
 * The null-controller arm is contract, not defence: `IHdmiCec.open()` is declared
 * `@nullable` and documented as returning null on error, so an ok status carrying no
 * controller is a genuinely reachable outcome that must not be mistaken for success.
 *
 * A FRESH LISTENER IS CONSTRUCTED PER SESSION, and that is intended rather than
 * incidental. The `eventListener == 0` guard is kept, but every path that ends a
 * session - both arms of close(), both failure arms here, and the destructor - detaches
 * and releases the listener, so the guard finds a null pointer on the next open() and
 * builds a new one. A listener reused across sessions would be one the HAL of the
 * PREVIOUS session may still hold a strong reference to, which is the CWE-416 window
 * detach() exists to close; a per-session object keeps each session's callbacks
 * attributable to that session alone.
 *
 * @throws IOException - No compatible service proxy is held, or `IHdmiCec::open()`
 *                       reported a non-ok binder status, or it succeeded and returned
 *                       no controller. In the latter two cases the listener is detached
 *                       and released before the exception leaves, because a failed open
 *                       may still have left the HAL holding it.
 *
 * @pre isServiceAvailable() has returned true. Without it there is no proxy and this
 *      raises IOException rather than crashing.
 * @post The state is OPENED and a controller session is held, or nothing is held at all
 *       and IOException was raised.
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
		/* F20: synchronous, no client-side deadline available - measured, not bounded. */
		const int64_t openStartedMs = halCallStarted();
		::android::binder::Status txn = hdmiCecService->open(eventListener, &controller);

		warnIfHalCallSlow("IHdmiCec::open", openStartedMs);

		CCEC_LOG( LOG_DEBUG, "DriverAidlImpl:: call IHdmiCec::open DONE %s, controller %s\r\n", txn.toString8().string(), (controller == 0) ? "null" : "present");

		if (!txn.isOk() || (controller == 0)) {
			/*
			 * A failed open still handed the listener across the binder boundary, so
			 * the HAL may already hold a strong reference to it and is under no
			 * obligation to drop it - the "no further callbacks" guarantee attaches to
			 * a successful close, and there was no successful open to close. Detach
			 * before unwinding, so that a callback arriving on a session that never
			 * opened cannot reach this instance.
			 */
			if (eventListener != 0) {
				eventListener->detach();
				eventListener.clear();
			}

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
 *    unwinds. It is offered BEFORE the HAL transaction, which is the observable order
 *    the legacy back-end has and is relied upon;
 * -# the HAL session is released;
 * -# the listener is DETACHED AND RELEASED, on both arms - see the detach note below;
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
 * @post The state is CLOSED, no controller session is held, and the listener is
 *       detached and released WHETHER OR NOT the HAL reported a successful close. The
 *       local address list is untouched, by design.
 * @warning The detach is placed before the failure check on purpose and must stay
 *          there. A failed `IHdmiCec::close()` leaves the HAL entitled to keep calling
 *          the listener, since the AIDL no-further-callbacks guarantee attaches only to
 *          a close that succeeded; detaching only on the success arm would leave a
 *          listener holding a back pointer into an owner that is about to be destroyed,
 *          which is CWE-416 by construction.
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

		/*
		 * The sentinel is offered UNDER queueProducerMutex, because close() is a
		 * PRODUCER on this queue and not merely its terminator. The receive path
		 * establishes that there is room before it parts with ownership of a frame, and
		 * an occupancy observation is only stable while every producer that can raise
		 * occupancy is serialized against it - offering here without the producer lock
		 * would let this sentinel land in the gap between that check and its offer, and
		 * `EventQueue::offer()` would then DISCARD the frame silently while the receive
		 * path reported it accepted, leaking it. The lock is this class's own, held for
		 * one constant-time offer with no IPC under it, and it is taken INSIDE the
		 * instance lock, which is the only nesting order that exists: offerReceivedFrame()
		 * takes queueProducerMutex alone and never the instance lock, so no inversion is
		 * possible. The offer statement itself is unchanged from the legacy line.
		 *
		 * @see DriverAidlImpl::offerReceivedFrame()
		 */
		/* Use NULL as sentinel */
		{AutoLock lock_(queueProducerMutex);
			rQueue.offer(0);
		}

		bool closed = false;
		::android::binder::Status txn = ::android::binder::Status::fromStatusT(::android::DEAD_OBJECT);

		if (hdmiCecService != 0) {
			/* B2: candidate mapping for the legacy HdmiCecClose(), pending confirmation. */
			/* F20: synchronous, no client-side deadline available - measured, not bounded. */
			const int64_t closeStartedMs = halCallStarted();

			txn = hdmiCecService->close(hdmiCecController, &closed);

			warnIfHalCallSlow("IHdmiCec::close", closeStartedMs);
		}
		else {
			CCEC_LOG( LOG_EXP, "DriverAidlImpl::close : no AIDL service proxy is held\r\n");
		}

		hdmiCecController.clear();

		/*
		 * Detach BEFORE the failure check, so that BOTH arms detach. This is the
		 * CWE-416 fix and the placement is the whole of it: a close that reported
		 * failure leaves the HAL entitled to keep calling the listener, because the
		 * AIDL guarantee of no further callbacks attaches to a SUCCESSFUL close, and
		 * the failure arm below leaves by way of an exception that ~DriverAidlImpl()
		 * swallows. Detaching only on success would leave exactly that path holding a
		 * listener with a live back pointer into an object about to be destroyed.
		 *
		 * detach() returns only once any in-flight callback has finished, and it is
		 * called AFTER the sentinel offer above rather than before it, so the queue
		 * lock is never held while the listener lock is being waited on. See the
		 * lock-order paragraph on EventListener.
		 */
		if (eventListener != 0) {
			eventListener->detach();
			eventListener.clear();
		}

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

		/* F20: synchronous, no client-side deadline available - measured, not bounded. */
		const int64_t sendStartedMs = halCallStarted();

		::android::binder::Status txn = hdmiCecController->sendMessage(std::vector<uint8_t>(buf, buf + length), &sendResult);

		warnIfHalCallSlow("IHdmiCecController::sendMessage", sendStartedMs);

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
		/* F20: synchronous, no client-side deadline available - measured, not bounded. */
		const int64_t getStartedMs = halCallStarted();

		::android::binder::Status txn = hdmiCecService->getLogicalAddresses(&halAddresses);

		warnIfHalCallSlow("IHdmiCec::getLogicalAddresses", getStartedMs);

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
			/* F20: synchronous, no client-side deadline available - measured, not bounded. */
			const int64_t removeStartedMs = halCallStarted();
			::android::binder::Status txn = hdmiCecController->removeLogicalAddresses(std::vector<int32_t>{ source.toInt() }, &removed);

			warnIfHalCallSlow("IHdmiCecController::removeLogicalAddresses", removeStartedMs);

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
		/* F20: synchronous, no client-side deadline available - measured, not bounded. */
		const int64_t addStartedMs = halCallStarted();
		::android::binder::Status txn = hdmiCecController->addLogicalAddresses(std::vector<int32_t>{ source.toInt() }, &added);

		warnIfHalCallSlow("IHdmiCecController::addLogicalAddresses", addStartedMs);

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
 * @brief Hands one received frame to the incoming queue and REPORTS WHETHER IT TOOK IT
 *
 * The ownership-transferring counterpart of getIncomingQueue(). It exists because
 * `CCEC_OSAL::EventQueue::offer()` RETURNS VOID and, when the queue already holds
 * INCOMING_QUEUE_CAPACITY entries, silently discards its argument - the OSAL source
 * carries a long-standing unresolved marker where the rejection should be reported, and
 * `osal/include/osal/EventQueue.hpp` is out of bounds for this migration in any case. A
 * caller that offers and then drops its pointer therefore leaks one heap frame per event,
 * for as long as the queue stays full, and on a remotely driven receive path that is
 * unbounded. Establishing that there is room before parting with ownership, and reporting
 * the outcome, is what makes the loss impossible.
 *
 * The two-lock arrangement is deliberate and is the part to get right:
 * - the queue is reached through getIncomingQueue(), so the OPENED-state guard still runs.
 *   That guard is load bearing rather than decorative - it is what rejects a callback
 *   arriving during or after a close, and its exception is what drives the caller's
 *   release of the frame;
 * - the occupancy reads and the offer between them run under queueProducerMutex, a lock of
 *   THIS METHOD'S OWN that close() takes for its sentinel offer too. The instance mutex is
 *   emphatically not reused: write() holds it across a whole synchronous IPC round trip, so
 *   borrowing it here would make every received frame wait out the current transmit and
 *   would park a binder thread behind a stalled HAL.
 *
 * WHY THE OCCUPANCY OBSERVATION HOLDS, stated in terms of who can raise it. Every producer
 * on this queue is serialized on queueProducerMutex - this method and close()'s sentinel
 * offer, which takes the same lock for exactly that statement - so while this lock is held
 * nothing can ADD an entry. Consumers are unaffected by the lock and keep removing, which
 * can only LOWER the occupancy; "there is room" therefore cannot turn false between the
 * check and the offer. An earlier revision of this method argued the same conclusion from
 * consumers being removal-only alone, which was WRONG: close() is a producer, and before it
 * took this lock it could land its sentinel in precisely that gap, whereupon
 * `EventQueue::offer()` discarded the received frame silently while this method still
 * reported it accepted - one leaked frame per event.
 *
 * THE LAST SLOT IS RESERVED FOR close()'s SENTINEL, which is what the refusal point being
 * one below the capacity buys. A sentinel that a full queue swallowed would leave the Bus
 * reader blocked in EventQueue::poll() with nothing coming to wake it, so the receive path
 * stops one entry early and the wake-the-reader offer always has somewhere to go. That is
 * the property the previous revision wanted and did not achieve by exempting close() from
 * the lock.
 *
 * @param [in] frame - Frame to hand over. Ownership passes to the queue if and only if
 *                     this returns true.
 *
 * @return bool - Whether ownership was transferred
 * @retval true  - The frame is on the queue. The caller must not delete it and must drop
 *                 its pointer at once.
 * @retval false - The queue had no slot this method may use, the frame was NOT queued, and
 *                 the caller still owns it and must release it.
 *
 * @throws InvalidStateException - The driver is not OPENED. Raised by getIncomingQueue()
 *                                 before anything is offered, so ownership stays with the
 *                                 caller.
 *
 * @pre @p frame is a heap-allocated frame the caller owns. A null pointer would be a
 *      caller defect: NULL is the close sentinel on this queue and only close() may post
 *      it, which is why nothing here manufactures one.
 * @post Exactly one of: the frame is queued and true was returned; or the frame is still
 *       the caller's, and false was returned or an exception left.
 * @warning Holds queueProducerMutex only for two constant-time size reads and a
 *          constant-time offer. No IPC, no allocation and no logging happens under it, so
 *          it cannot become a latency source on the binder thread.
 * @warning A refusal means the Bus reader is not draining, so DROPPING the frame is the
 *          only remaining choice - the alternative is growing without bound on a path a
 *          remote HAL drives. The drop is logged by the caller at LOG_EXP so it is never
 *          silent.
 * @warning The read-back is trusted in ONE DIRECTION ONLY, and the body says why at
 *          length: a consumer may take the frame the instant the offer wakes it, so a
 *          non-rise does NOT prove refusal, and reporting refusal there would hand the
 *          caller a pointer the queue already owns - a double free, which is strictly
 *          worse than the leak this method exists to prevent.
 *
 * @see DriverAidlImpl::getIncomingQueue()
 * @see DriverAidlImpl::close() - the other producer, serialized on the same lock
 * @see DriverAidlImpl::EventListener::onMessageReceived()
 */
bool DriverAidlImpl::offerReceivedFrame(CECFrame *frame)
{
	/*
	 * The reserved-slot rule below subtracts one from the capacity, so a queue of fewer
	 * than two entries would leave the receive path with no usable slot at all. Asserted
	 * at compile time, at the one place the arithmetic is relied on, so that a future
	 * change to the constant cannot quietly produce a receive path that accepts nothing.
	 */
	static_assert(INCOMING_QUEUE_CAPACITY >= 2,
	              "INCOMING_QUEUE_CAPACITY must leave one slot for a received frame and one for close()'s sentinel");

	IncomingQueue &queue = getIncomingQueue();

	bool accepted = false;

    {AutoLock lock_(queueProducerMutex);
		/*
		 * ONE SLOT IS RESERVED FOR close()'s SENTINEL, which is why the refusal point is
		 * one BELOW the capacity rather than at it. close() offers NULL to wake a blocked
		 * Bus reader, under this same lock; if the receive path were allowed to fill the
		 * last slot, that offer would be silently discarded by EventQueue::offer() and the
		 * reader would stay asleep with nothing left to wake it. Stopping one entry early
		 * costs one frame of depth and makes the sentinel undroppable.
		 */
		const size_t occupancyBefore = queue.size();

		if (occupancyBefore >= (INCOMING_QUEUE_CAPACITY - 1)) {
			return false;
		}

		queue.offer(frame);

		/*
		 * THE OUTCOME IS READ BACK FROM THE QUEUE rather than assumed from the check
		 * above, because EventQueue::offer() returns void and reports nothing at all. The
		 * read-back is trusted in one direction only, and the asymmetry is deliberate:
		 *
		 * - a read-back BELOW the capacity is consistent with the offer having gone in, and
		 *   is the ordinary outcome. It may sit below occupancyBefore, because a consumer
		 *   can take entries - including this very frame - between the offer and this read;
		 *   the Bus reader is normally blocked in EventQueue::poll() on an empty queue and
		 *   is woken by the offer itself, so "offered, taken, and back to the previous
		 *   occupancy" is the COMMON interleaving, not a corner. Reporting a refusal there
		 *   would hand the caller a pointer the queue already owns and may already have
		 *   destroyed, turning a bounded leak into a double free;
		 * - a read-back AT OR ABOVE the capacity is inconsistent with acceptance and is
		 *   reported as a refusal. Producers are serialized here, so an accepted offer can
		 *   leave at most occupancyBefore + 1 entries, which the check above kept below the
		 *   capacity. Seeing the queue at capacity therefore means something outside this
		 *   lock filled it and this offer was discarded - the frame is still the caller's,
		 *   and saying so keeps the return value honest if a later change ever breaks that
		 *   assumption.
		 */
		const size_t occupancyAfter = queue.size();

		accepted = (occupancyAfter < INCOMING_QUEUE_CAPACITY);
    }

	return accepted;
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
 * @brief The production probe: the real `::open`, `BINDER_VERSION`, ping and `::close`
 *
 * One immutable instance with static storage duration, initialized from the four
 * file-local wrappers above. It is the default argument of isBinderPreflightOk(), which
 * is what keeps every production call site free of any mention of the seam and keeps
 * production behaviour - including every log line the predicate emits - exactly what it
 * would be if the seam did not exist.
 *
 * @return const BinderPreflightProbe& - The real kernel-facing operations.
 *
 * @pre None.
 * @post Nothing in the process is initialized. The probe is function pointers and
 *       nothing else.
 * @warning Never throws. The instance is const, so no caller can rebind production's
 *          probe; a test supplies its own by passing it.
 *
 * @see DriverAidlImpl::isBinderPreflightOk()
 * @see DriverAidlImpl::BinderPreflightProbe
 */
const DriverAidlImpl::BinderPreflightProbe &DriverAidlImpl::defaultBinderProbe(void)
{
	static const BinderPreflightProbe probe = {
		defaultOpenBinderNode,
		defaultReadBinderProtocolVersion,
		defaultPingBinderContextManager,
		defaultCloseBinderNode,
	};

	return probe;
}

/**
 * @brief The binder protocol version this build must speak to be usable
 *
 * `BINDER_CURRENT_PROTOCOL_VERSION`, read from the build rather than written down,
 * because the constant follows `BINDER_IPC_32BIT`: an all-32-bit platform speaks
 * protocol 7 and 32-bit middleware against a 64-bit vendor speaks 8. Exposed as a
 * function so that a test can synthesise both a matching and a mismatching version
 * through a probe of its own without the binder kernel UAPI definitions appearing in
 * DriverAidlImpl.hpp or in the test translation unit.
 *
 * @return unsigned int - Expected protocol version
 * @retval 0     - This build carries no binder kernel ABI definitions. The comparison
 *                 that consumes this value is unreachable in that configuration, because
 *                 the default probe's version read fails first.
 * @retval other - The protocol version the linked libbinder was built for.
 *
 * @pre None.
 * @warning Never throws.
 *
 * @see DriverAidlImpl::isBinderPreflightOk()
 */
unsigned int DriverAidlImpl::expectedBinderProtocolVersion(void)
{
#if CCEC_HAVE_BINDER_UAPI
	return (unsigned int)BINDER_CURRENT_PROTOCOL_VERSION;
#else
	return 0;
#endif
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
 * FIVE DECISION POINTS, IN THIS ORDER, AND THE ORDER IS PART OF THE CONTRACT: the path
 * is empty; the node could not be opened; its version could not be read; the version
 * differs from this build's; the context manager did not answer. Each carries one record
 * in the coverage branch manifest, so reordering them, merging two of them, or making
 * any of them conditional on a build macro invalidates that mapping. This is why the
 * CCEC_HAVE_BINDER_UAPI guard lives inside the DEFAULT PROBE rather than around this
 * body: a build without the binder kernel ABI definitions still runs all five arms and
 * still reaches the same verdict, false, by failing the version read with ENOSYS and
 * logging why.
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
 * @param [in]  probe                   - The four kernel-facing operations performed
 *                                        here, injected for the same reason
 *                                        @p binderDriverPath is a parameter and carrying
 *                                        it further: a path alone cannot produce a node
 *                                        that OPENS and then reports a mismatched
 *                                        protocol version, cannot produce one that
 *                                        reports a MATCHING version and then fails the
 *                                        context-manager check, and cannot produce a
 *                                        positive verdict on a host with no binder
 *                                        driver. Substituting these four makes all five
 *                                        arms reachable deterministically. Defaults to
 *                                        defaultBinderProbe(), the real syscalls, so
 *                                        production is unaffected.
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
 * @see DriverAidlImpl::BinderPreflightProbe
 * @see DriverAidlImpl::defaultBinderProbe()
 * @see DriverAidlImpl::expectedBinderProtocolVersion()
 * @see DriverAidlImpl::DEFAULT_BINDER_DRIVER_PATH
 * @see DriverAidlImpl::DEFAULT_CONTEXT_MANAGER_TIMEOUT_MS
 */
bool DriverAidlImpl::isBinderPreflightOk(const std::string &binderDriverPath,
                                         unsigned int contextManagerTimeoutMs,
                                         const BinderPreflightProbe &probe)
{
	/* Decision point 1 of 5: the path itself. */
	if (binderDriverPath.empty()) {
		CCEC_LOG( LOG_INFO, "DriverAidlImpl preflight: no binder driver path was given; treating the AIDL HAL as absent\r\n");
		return false;
	}

	/* Decision point 2 of 5: the node exists and can be opened. */
	const int driverFd = probe.openNode(binderDriverPath.c_str(), O_RDWR | O_CLOEXEC);

	if (driverFd < 0) {
		/*
		 * errno is captured BEFORE anything else runs, because every call between the
		 * failed open and the report - CCEC_LOG included - is free to overwrite it.
		 *
		 * THE TWO CAUSES ARE REPORTED SEPARATELY, and they are not the same platform
		 * condition. ENOENT means this kernel carries no binder driver at all, which is
		 * the ordinary, expected, entirely healthy state of a legacy-only SOC and the
		 * exact case the fallback exists to serve. Any other errno means the node IS
		 * there and this process could not use it - EACCES for a permission or policy
		 * problem, EMFILE or ENFILE for exhausted descriptors, ENODEV for a driver that
		 * unregistered - and those are misconfigurations an integrator has to act on.
		 * Collapsing them into one line made a broken platform indistinguishable from a
		 * legacy-only one, so both arms name what happened, and the second carries the
		 * errno TEXT as well as its number because nobody reads a build log with an
		 * errno table beside them.
		 */
		const int openErrno = errno;

		if (openErrno == ENOENT) {
			CCEC_LOG( LOG_INFO, "DriverAidlImpl preflight: no binder driver node exists at [%s]; this platform carries no binder transport, so the AIDL HAL is absent and the legacy back-end is the correct outcome\r\n", binderDriverPath.c_str());
		}
		else {
			CCEC_LOG( LOG_WARN, "DriverAidlImpl preflight: binder driver [%s] EXISTS but could not be opened, errno %d (%s); treating the AIDL HAL as absent. Unlike a missing node this is a platform fault rather than a legacy-only platform, and it needs an integrator's attention\r\n", binderDriverPath.c_str(), openErrno, strerror(openErrno));
		}

		return false;
	}

	/* Decision point 3 of 5: the node will tell us which protocol it speaks. */
	unsigned int protocolVersion = 0;

	if (0 != probe.readProtocolVersion(driverFd, &protocolVersion)) {
		/* Captured first, and reported with its text, for the reason stated at decision point 2. */
		const int ioctlErrno = errno;

		/*
		 * LOG_WARN, NOT LOG_INFO, AND THAT IS THE WHOLE POINT OF SPLITTING THE ABSENT NODE
		 * FROM THE BROKEN ONE.  A node that does not EXIST is a legacy-only SOC behaving
		 * exactly as designed, which is why that arm alone stays at LOG_INFO.  Reaching HERE
		 * means the node existed AND opened, and then refused the one ioctl every binder
		 * client issues before anything else.  No correctly provisioned platform does that:
		 * it is a mis-created device node, a driver that is not binder, or a kernel and a
		 * libbinder that disagree about the interface.  The fallback it causes is otherwise
		 * silent, so this line is the only place an integrator can find out.  It sits at the
		 * same level as its two siblings - the failed open above and the protocol mismatch
		 * below - because all three describe a platform fault rather than an absent HAL.
		 */
		CCEC_LOG( LOG_WARN, "DriverAidlImpl preflight: binder driver [%s] opened but REFUSED to report its protocol version, errno %d (%s); treating the AIDL HAL as absent. Unlike a missing node this is a PLATFORM FAULT rather than a legacy-only platform: the node exists and opens, so it is either not a binder driver or was mis-created\r\n", binderDriverPath.c_str(), ioctlErrno, strerror(ioctlErrno));
		probe.closeNode(driverFd);
		return false;
	}

	/*
	 * Decision point 4 of 5: EQUALITY, not a minimum. libbinder enforces an exact match
	 * when it opens the driver, so a difference in either direction fails every open and
	 * is reported here as "AIDL absent" rather than being left for libbinder to abort on.
	 */
	if (protocolVersion != expectedBinderProtocolVersion()) {
		CCEC_LOG( LOG_WARN, "DriverAidlImpl preflight: binder driver [%s] speaks protocol %d but this build expects %d; treating the AIDL HAL as absent\r\n", binderDriverPath.c_str(), (int)protocolVersion, (int)expectedBinderProtocolVersion());
		probe.closeNode(driverFd);
		return false;
	}

	/*
	 * Clamp before probing. The parameter is an unsigned int, so a caller can name a value
	 * far larger than any plausible servicemanager start-up delay, and every millisecond of
	 * it would be time LibCCEC::init() spends blocked. Clamping here keeps the ceiling in
	 * one place; the probe's own slicing then bounds each individual wait whatever value
	 * arrives. Zero is left alone deliberately - it means "do not wait at all", which is
	 * how the negative arm is exercised.
	 */
	unsigned int effectiveTimeoutMs = contextManagerTimeoutMs;

	if (effectiveTimeoutMs > MAX_CONTEXT_MANAGER_TIMEOUT_MS) {
		CCEC_LOG( LOG_WARN, "DriverAidlImpl preflight: a context manager timeout of %u ms was requested, above the %u ms ceiling; using the ceiling so that initialization stays bounded\r\n", contextManagerTimeoutMs, (unsigned int)MAX_CONTEXT_MANAGER_TIMEOUT_MS);
		effectiveTimeoutMs = MAX_CONTEXT_MANAGER_TIMEOUT_MS;
	}

	/* Decision point 5 of 5: a context manager is registered and answers, under bound. */
	const bool contextManagerReachable = probe.pingContextManager(driverFd, effectiveTimeoutMs);

	probe.closeNode(driverFd);

	if (contextManagerReachable) {
		CCEC_LOG( LOG_INFO, "DriverAidlImpl preflight: binder driver [%s] and its context manager are usable\r\n", binderDriverPath.c_str());
	}

	return contextManagerReachable;
}

namespace {

/*
 * THE THREE FALLBACK-REASON PHRASES, AND THE FIRST TWO ARE A CONTRACT.
 *
 * They are declared here rather than at the top of the file because isServiceAvailable()
 * below is the only thing that ever assigns one, and the selection helper in
 * ccec/src/Driver.cpp is the only thing that ever prints one - so keeping them beside
 * their sole producer is what stops a fourth arm being added without a phrase, or a
 * phrase being reworded away from the arm it names.
 *
 * REASON_TRANSPORT_UNAVAILABLE and REASON_NO_COMPATIBLE_SERVICE are the exact texts the
 * factory has always emitted, preserved WORD FOR WORD. They are transcribed by
 * tests/L1Tests/run_coverage.sh and by tests/L2Tests/ccec/test_DualPathIntegration.cpp,
 * and they are also deliberately worded so that neither contains the selected-path
 * literal - which is what keeps a grep for that literal at exactly one hit per process.
 * Rewording either breaks those consumers.
 *
 * REASON_QUERY_FAILED is the third arm and is new. It covers the catch-all in
 * isServiceAvailable(): a query that neither succeeded nor cleanly declined is a
 * different platform condition from an absent transport and from an unusable service,
 * and reporting it as either of those would misdescribe it.
 */
const char *const REASON_TRANSPORT_UNAVAILABLE = "the binder transport is unavailable on this platform";
const char *const REASON_NO_COMPATIBLE_SERVICE = "the binder transport is reachable but no compatible service resolved";
const char *const REASON_QUERY_FAILED          = "the service query failed unexpectedly, so no usable service could be established";

/** @brief Longest interface hash rendered into a log line by sanitizedInterfaceHash(). */
const size_t MAX_LOGGED_HASH_LENGTH = 16;

/**
 * @brief Renders a server-supplied interface hash safely for a log line
 *
 * The hash arrives from ANOTHER PROCESS over binder, so its content and its length are
 * both outside this process's control and neither may be pasted into a log unexamined.
 * A frozen AIDL hash is a short hexadecimal digest, but a broken or hostile server can
 * answer with anything at all - control characters that would corrupt a terminal or a
 * log parser, or a payload long enough to bury the rest of the diagnosis.
 *
 * Every byte outside printable ASCII becomes `?`, so the length and the shape of what
 * arrived are still visible while nothing it contains can act on a reader's tooling, and
 * anything longer than MAX_LOGGED_HASH_LENGTH is truncated with an ellipsis and its true
 * length reported - truncating silently would be its own small lie.
 *
 * @param [in] hash - The hash exactly as the server reported it. May be empty, may
 *                    contain any byte value, and may be arbitrarily long.
 *
 * @return std::string - A bounded, printable rendering safe to substitute into a log
 *                       line. Empty input yields the literal `<empty>` rather than
 *                       nothing, so an empty hash cannot read as a formatting fault.
 *
 * @pre None.
 * @post No state changed; the argument is not modified.
 * @warning For DIAGNOSTICS ONLY. No compatibility decision is ever taken on this value:
 *          halcompat::isCompatible() reads the unmodified hash and is the sole decision.
 *
 * @see DriverAidlImpl::isServiceAvailable()
 */
std::string sanitizedInterfaceHash(const std::string &hash)
{
	if (hash.empty()) {
		return std::string("<empty>");
	}

	const size_t rendered = (hash.size() > MAX_LOGGED_HASH_LENGTH) ? MAX_LOGGED_HASH_LENGTH : hash.size();
	std::string safe;

	safe.reserve(rendered + 32);

	for (size_t i = 0; i < rendered; i++) {
		const unsigned char byte = static_cast<unsigned char>(hash[i]);

		safe += ((byte >= 0x20) && (byte < 0x7F)) ? static_cast<char>(byte) : '?';
	}

	if (hash.size() > rendered) {
		char tail[64];

		snprintf(tail, sizeof(tail), "... (%zu bytes total)", hash.size());
		safe += tail;
	}

	return safe;
}

} // anonymous namespace

/**
 * @brief Names the CATEGORY of an interface hash observed after a compatibility rejection
 *
 * The header carries the reasoning. The short form is that this describes a value and
 * never attributes a cause to it: the three phrases below say what the observed string
 * IS, so that a reader can see whether the server answered with a frozen digest, with
 * the failure marker, or with an unfrozen development marker, without the code claiming
 * to know which of halcompat's rules did the rejecting.
 *
 * @param [in] hash - The observed hash, exactly as reported.
 * @return const char* - A static phrase naming the category. Never NULL.
 *
 * @pre None.
 * @post No state changed.
 * @warning Diagnostics only; no decision is taken on this value.
 *
 * @see DriverAidlImpl::isServiceAvailable()
 */
const char *DriverAidlImpl::describeObservedInterfaceHash(const std::string &hash)
{
	if (hash.empty()) {
		return "empty - the shape an interface-hash transaction takes when it FAILS, rather than a value a working server reports";
	}

	if (hash == "-1") {
		return "the \"-1\" failure marker - the shape an interface-hash transaction takes when it FAILS, rather than a value a working server reports";
	}

	if (hash == "notfrozen") {
		return "the \"notfrozen\" marker - an UNFROZEN development build, which makes no compatibility promise and is not accepted by default";
	}

	return "a frozen release digest - the shape a working, released server reports";
}

/**
 * @brief Whether a post-decision metadata SNAPSHOT would itself be accepted
 *
 * The header carries the reasoning, including why both arguments must come from ONE
 * snapshot. The short form is that a `true` answer means the observation DISAGREES with
 * the decision, which is evidence of changed or recovered metadata and is reported as
 * that rather than as a version mismatch.
 *
 * The version half is `halcompat::detail::isCompatible()` - the real rule, constexpr over
 * two ints, so it issues no transaction and cannot disagree with itself between calls.
 * The hash half mirrors halcompat's own gate order for the observed value only, treating
 * an unfrozen server as not accepted because production calls the predicate with its
 * `allowUnfrozen` default of false.
 *
 * @param [in] hash            - The observed hash, unmodified.
 * @param [in] clientVersion   - This client's compiled-in interface version.
 * @param [in] observedVersion - The observed server version, from the SAME snapshot.
 *
 * @return bool - True when this snapshot alone would have satisfied the rule.
 * @retval true  - Observation disagrees with the decision: metadata changed or recovered.
 * @retval false - Observation is consistent with a rejection, without establishing which
 *                 rule applied.
 *
 * @pre Both value arguments come from one snapshot.
 * @post No state changed.
 * @warning Diagnostics only; never used to select a back-end.
 *
 * @see DriverAidlImpl::isServiceAvailable()
 */
bool DriverAidlImpl::observedMetadataWouldBeAccepted(const std::string &hash,
                                                     int clientVersion,
                                                     int observedVersion)
{
	if (hash.empty() || hash == "-1" || hash == "notfrozen") {
		return false;
	}

	return halcompat::detail::isCompatible(clientVersion, observedVersion);
}


/**
 * @brief Emits the compatibility-rejection diagnostic for a service halcompat rejected
 *
 * See the declaration in DriverAidlImpl.hpp for why this is a static and why it is
 * reachable from a test. In short: one copy of the wording, and a function that cannot
 * touch the cause its caller recorded.
 *
 * @param[in] service        The rejected service, already established non-null by the caller.
 * @param[in] halServiceName The binder name it was resolved under.
 *
 * @return void
 *
 * @pre halcompat::isCompatible<IHdmiCec>(service) returned false and the caller recorded
 *      REASON_NO_COMPATIBLE_SERVICE.
 * @post No member state changes; structurally guaranteed by being static.
 * @warning Diagnostics only.
 *
 * @see DriverAidlImpl::isServiceAvailable()
 */
void DriverAidlImpl::emitCompatibilityRejectionDiagnostic(
	const ::android::sp< cechal::IHdmiCec > &service,
	const std::string &halServiceName)
{
	try {
		const int clientVersion = cechal::IHdmiCec::VERSION;

		/*
		 * THE ONE SNAPSHOT. Each value is read exactly once and nothing below re-reads
		 * either. This is the rule that closed the second of the two earlier defects: a
		 * predicate result combined with separately retried values could land in the
		 * not-compatible arm holding compatible metadata and blame the version rule.
		 */
		const std::string observedHash = service->getInterfaceHash();
		const int observedVersion = service->getInterfaceVersion();

		/*
		 * EMITTED AS SEVERAL SHORT LINES, AND THE REASON IS A HARD PLATFORM LIMIT RATHER
		 * THAN A PREFERENCE.
		 *
		 * CCEC_LOG formats through a 500-byte stack buffer -- MAX_LOG_BUFF in
		 * ccec/src/Util.cpp, which is not this migration's to change -- and vsnprintf
		 * TRUNCATES silently at 499. This diagnostic was first written as one message of
		 * about 900 characters, and the effect was the worst possible one: the sentence
		 * explaining that no cause is being claimed survived, and every OBSERVED VALUE it
		 * exists to report -- the hash, its classification, both versions and the recovery
		 * note -- was cut off. A contract-suite case that captures this output is what
		 * found it; before that case existed the truncation was invisible, because the
		 * surviving prefix reads like a complete message.
		 *
		 * So each line below is bounded independently, with the longest possible
		 * substitution in it, and the recovery note is a line of its own rather than a
		 * suffix -- as a suffix it could push the line it was appended to over the limit
		 * and take the values with it. Adding to any of these means re-checking its worst
		 * case against 499, not appending and hoping.
		 */
		CCEC_LOG( LOG_WARN, "DriverAidlImpl::isServiceAvailable : binder service [%s] is present but was REJECTED AS NOT COMPATIBLE by halcompat::isCompatible(), the SOLE decision. It rejects for one of three reasons - an unreadable interface hash, an UNFROZEN development server, or a version outside this client's era and major - and WHICH ONE APPLIED IS NOT REPORTED HERE: the metadata the decision read cannot be recovered, and a fresh read is a different transaction\r\n",
		          halServiceName.c_str());

		CCEC_LOG( LOG_WARN, "DriverAidlImpl::isServiceAvailable : service [%s] OBSERVED AFTER the decision, so this is evidence about the server rather than the cause of the rejection: interface hash [%s], which is %s\r\n",
		          halServiceName.c_str(),
		          sanitizedInterfaceHash(observedHash).c_str(),
		          describeObservedInterfaceHash(observedHash));

		CCEC_LOG( LOG_WARN, "DriverAidlImpl::isServiceAvailable : service [%s] OBSERVED AFTER the decision: server interface version %d against this client's %d. The AIDL HDMI CEC HAL is treated as absent and the legacy back-end is selected\r\n",
		          halServiceName.c_str(), observedVersion, clientVersion);

		if (observedMetadataWouldBeAccepted(observedHash, clientVersion, observedVersion)) {
			CCEC_LOG( LOG_WARN, "DriverAidlImpl::isServiceAvailable : service [%s] NOTE: that observation WOULD itself have been accepted, so the server's metadata changed or recovered after the decision - look for an INTERMITTENT METADATA TRANSACTION rather than a version mismatch\r\n",
			          halServiceName.c_str());
		}
	}
	catch(...) {
		/*
		 * The observation itself failed. That costs a less specific message and nothing
		 * else. The caller recorded the cause BEFORE calling this, and this function is
		 * static, so there is no path by which a failed observation can relabel an
		 * established compatibility rejection.
		 */
		CCEC_LOG( LOG_WARN, "DriverAidlImpl::isServiceAvailable : binder service [%s] is present but was REJECTED AS NOT COMPATIBLE by halcompat::isCompatible(), and its metadata could not be read back afterwards to describe the server, because that read failed too. The rejection is unaffected and its recorded cause unchanged: the AIDL HDMI CEC HAL is treated as absent and the legacy back-end is selected\r\n", halServiceName.c_str());
	}
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
	availabilityReason = NULL;

	try {
		if (!isBinderPreflightOk()) {
			CCEC_LOG( LOG_INFO, "DriverAidlImpl::isServiceAvailable : the binder preflight declined; the AIDL HDMI CEC HAL is treated as absent\r\n");
			availabilityReason = REASON_TRANSPORT_UNAVAILABLE;
			return false;
		}

		const std::string &halServiceName = cechal::IHdmiCec::serviceName();
		/* F10: synchronous lookup, no client-side deadline available - measured, not bounded. */
		const int64_t lookupStartedMs = halCallStarted();
		::android::sp<cechal::IHdmiCec> service = halcompat::getService<cechal::IHdmiCec>();

		warnIfHalCallSlow("IHdmiCec service lookup", lookupStartedMs);

		if (service == 0) {
			CCEC_LOG( LOG_INFO, "DriverAidlImpl::isServiceAvailable : binder service [%s] is not registered; the AIDL HDMI CEC HAL is treated as absent\r\n", halServiceName.c_str());
			availabilityReason = REASON_NO_COMPATIBLE_SERVICE;
			return false;
		}

		/* F10: getInterfaceHash()/getInterfaceVersion() are round trips - measured, not bounded. */
		const int64_t compatibilityStartedMs = halCallStarted();
		const bool compatible = halcompat::isCompatible<cechal::IHdmiCec>(service);

		warnIfHalCallSlow("IHdmiCec compatibility check", compatibilityStartedMs);

		if (!compatible) {
			/*
			 * THE DECISION HAS ALREADY BEEN TAKEN, AND IT WAS TAKEN BY halcompat.
			 * halcompat::isCompatible() is and remains the SINGLE implementation of the
			 * compatibility rule; nothing below re-implements it or re-decides anything.
			 *
			 * The cause is recorded HERE, before any observation, so that what explains
			 * the fallback is fixed by the decision itself and cannot be rewritten by
			 * anything that runs afterwards.
			 */
			availabilityReason = REASON_NO_COMPATIBLE_SERVICE;

			/*
			 * WHY THIS BLOCK REPORTS AN OBSERVATION AND NEVER A CAUSE.
			 *
			 * halcompat decided using ITS OWN metadata transactions, and those values
			 * cannot be recovered: they are locals inside a template in a read-only
			 * consumed header. Anything read HERE is a FRESH transaction, and the
			 * generated proxy does not make the two equivalent -- it CACHES a value it
			 * read successfully, but stores a FAILED read as -1 and RETRIES it on the
			 * next call. So a transaction that failed for the decision can succeed
			 * afterwards, and an in-process fake whose answers change between calls
			 * produces the same divergence.
			 *
			 * TWO EARLIER VERSIONS OF THIS BLOCK GOT THAT WRONG, IN TWO DIFFERENT WAYS,
			 * and both are recorded because the second looked like a fix for the first.
			 * The first asserted the two "can never disagree about the outcome" and
			 * blamed the era-and-major version rule whenever the observed hash looked
			 * frozen. The second asked the predicate a second time and THEN read the
			 * hash and version again -- so a predicate retry that still failed, combined
			 * with a metadata retry that succeeded, landed in the not-compatible arm
			 * holding compatible values and blamed the version rule all the same.
			 *
			 * THE RULE THIS BLOCK NOW FOLLOWS ADMITS NO SUCH COMBINATION:
			 *   * ONE SNAPSHOT. The hash and the version are read exactly once each, and
			 *     every statement below is a property of that ONE pair. No predicate
			 *     result is ever combined with separately retried values.
			 *   * NO CAUSAL CLAIM. Which of halcompat's three rules rejected this server
			 *     is NOT reported, because nothing here can know it. All three are named
			 *     as the possibilities they are, and the snapshot is labelled as evidence
			 *     about the server rather than as the reason it was rejected.
			 *   * NO REIMPLEMENTATION OF THE VERSION RULE. The version half of the
			 *     observation is evaluated by halcompat::detail::isCompatible(), the real
			 *     rule -- constexpr over two ints, so it performs no transaction and
			 *     cannot disagree with itself.
			 *
			 * The block is guarded on its own, and that guard is load-bearing rather than
			 * defensive habit: letting a failed observation reach the function's outer
			 * catch would relabel an established compatibility rejection as that
			 * handler's REASON_QUERY_FAILED, and the factory would then report a cause
			 * that did not produce the fallback.
			 */
			/*
			 * THE DIAGNOSTIC LIVES IN ONE PLACE, AND THAT PLACE IS REACHABLE FROM A TEST.
			 *
			 * It was written inline here until a review observed that no test could reach
			 * it on a host with no binder driver -- the preflight declines before this
			 * stage is ever entered -- so the wording went unverified exactly where the
			 * previous two defects had been. It is now a static, called from here and
			 * nowhere else in production, and driven directly by the contract suite.
			 *
			 * Being STATIC is the load-bearing part, not an implementation detail: with no
			 * `this` it cannot reach availabilityReason, so the cause recorded above
			 * survives the diagnostic by construction rather than by care. It also
			 * swallows its own failure, so the function-level catch below -- which would
			 * record REASON_QUERY_FAILED -- is unreachable from it.
			 */
			emitCompatibilityRejectionDiagnostic(service, halServiceName);

			return false;
		}

		hdmiCecService = service;

		CCEC_LOG( LOG_INFO, "DriverAidlImpl::isServiceAvailable : binder service [%s] is present and compatible\r\n", halServiceName.c_str());

		return true;
	}
	catch(...) {
		CCEC_LOG( LOG_EXP, "DriverAidlImpl::isServiceAvailable : unexpected failure while querying the AIDL HDMI CEC service; it is treated as absent\r\n");
		availabilityReason = REASON_QUERY_FAILED;
		return false;
	}
}

/**
 * @brief Reports WHY the last isServiceAvailable() declined, without asking again
 *
 * A pure read of the reason isServiceAvailable() recorded when it decided. The header
 * carries the reasoning for why the decision is preserved rather than reconstructed;
 * the short form is that re-running the bounded preflight to rebuild the answer pays
 * its context-manager timeout a second time and can return a different verdict than
 * the one that actually caused the fallback.
 *
 * @return const char* - The reason phrase, or NULL when there is none to report.
 * @retval NULL     - isServiceAvailable() has not run, or it returned true.
 * @retval non-NULL - A static-storage-duration phrase naming the platform condition.
 *
 * @pre None.
 * @post Nothing changed.
 * @warning Never throws and never touches binder.
 *
 * @see isServiceAvailable()
 * @see Driver::getInstance()
 */
const char *DriverAidlImpl::unavailabilityReason(void) const
{
	return availabilityReason;
}




CCEC_END_NAMESPACE


/** @} */
/** @} */
