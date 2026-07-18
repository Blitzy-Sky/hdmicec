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
* @defgroup hdmicec HDMI-CEC Middleware
* @{
* @defgroup ccec CCEC Library
* @{
**/


#ifndef HDMI_CCEC_CONNECTION_HPP_
#define HDMI_CCEC_CONNECTION_HPP_

#include <stdlib.h>
#include <list>

#include "osal/Mutex.hpp"

#include "ccec/CCEC.hpp"
#include "ccec/FrameListener.hpp"
#include "ccec/Operands.hpp"
#include "ccec/Driver.hpp"
#include "ccec/LibCCEC.hpp"
#include "ccec/Exception.hpp"

using CCEC_OSAL::Mutex;

CCEC_BEGIN_NAMESPACE
class Bus;
class CECFrame;

/**
 * @brief The connection class provides APIs that allows the application to access CEC Bus.
 * A connection is a tap into the CEC bus. The application can use a connection to send raw bytes
 * (in form of CECFrame) onto CEC bus or receive raw bytes from it.
 * @ingroup HDMI_CEC_CONNECTION
 */
class Connection
{
public:
	/**
	 * @brief Constructs a connection bound to a source logical address.
	 *
	 * A Connection is a tap into the CEC bus for the given local logical
	 * address. When @p opened is @c true the connection is opened immediately
	 * (registering to receive inbound frames); otherwise open() must be called
	 * explicitly before frames are received.
	 *
	 * @param[in] source Local logical address this connection represents.
	 *                   Defaults to @c LogicalAddress::UNREGISTERED, which
	 *                   receives all frames destined to the host device.
	 * @param[in] opened Whether to open the connection immediately. Defaults to
	 *                   @c true.
	 * @param[in] name   Optional human-readable connection name. Defaults to an
	 *                   empty string.
	 *
	 * The @p source value is copied into this connection's internal inbound
	 * frame filter when the connection is constructed; that captured copy is
	 * what inbound filtering uses, and setSource() does not refresh it.
	 *
	 * @warning Startup order and exceptions: when @p opened is @c true (the
	 *          default) this constructor calls open() during construction, which
	 *          registers the connection's internal listener with the singleton
	 *          Bus. Bus registration throws InvalidStateException
	 *          (ccec/Exception.hpp) when the Bus has not been started, so
	 *          constructing an opened Connection before LibCCEC::init() has
	 *          started the Bus propagates that exception out of the constructor.
	 *          Pass @p opened = @c false to defer registration, then call open()
	 *          after the library has been initialized.
	 * @warning With @p source == @c LogicalAddress::UNREGISTERED the internal
	 *          filter performs no filtering: every inbound frame delivered to
	 *          this connection is forwarded to the registered listeners.
	 * @note The destructor does not close the connection; call close() explicitly
	 *       before destruction (see ~Connection()).
	 * @see hdmicec/ccec/src/Connection.cpp (Connection constructor calls open();
	 *      Bus::addFrameListener throws when the Bus is not started)
	 */
	Connection(const LogicalAddress &source = LogicalAddress::UNREGISTERED, bool opened = true, const std::string &name="");
	/**
	 * @brief Destroys the connection instance.
	 *
	 * Declared virtual so that instances can be safely destroyed through a
	 * base-class pointer.
	 *
	 * @warning The destructor does NOT close the connection: it neither
	 *          unregisters this connection's internal listener from the Bus nor
	 *          clears the registered application listeners. A caller that opened
	 *          the connection MUST call close() before destroying it; otherwise
	 *          the Bus keeps a dangling pointer to this connection's internal
	 *          listener and dereferences it on the next inbound frame
	 *          (use-after-free).
	 * @see close()
	 * @see hdmicec/ccec/src/Connection.cpp (~Connection has an empty body)
	 */
	virtual ~Connection(void);

	/**
	 * @brief Opens the connection so it starts receiving CEC frames from the bus.
	 *
	 * Registers this connection's internal frame listener with the CEC Bus.
	 * When the connection was created without a specific logical address
	 * (@c LogicalAddress::UNREGISTERED) it picks up all frames destined to the
	 * host device regardless of the roles the device holds, which is useful for
	 * sniffing all available CEC traffic on the bus.
	 *
	 * @throws InvalidStateException (ccec/Exception.hpp) if the Bus has not been
	 *         started; LibCCEC::init() must have started the Bus before a
	 *         connection is opened.
	 * @warning open() performs no idempotency check: calling it more than once
	 *          registers the internal listener with the Bus again, after which
	 *          inbound frames are delivered more than once.
	 * @see hdmicec/ccec/src/Bus.cpp (Bus::addFrameListener throws when the Bus
	 *      is not started)
	 */
	void open(void);
	/**
	 * @brief Closes the connection so it stops receiving CEC frames from the bus.
	 *
	 * Clears any registered frame listeners and unregisters this connection's
	 * internal listener from the CEC Bus.
	 */
	void close(void);

    /**
     * @brief Registers an observer to receive inbound CEC frames.
     *
     * The listener is appended to the connection's list of frame listeners and
     * is subsequently notified for each CECFrame (raw CEC byte stream) received
     * from the bus that passes this connection's filtering.
     *
     * @param[in] listener Frame listener to add to the notification list.
     *
     * @warning No de-duplication is performed: registering the same @p listener
     *          pointer N times stores it N times, and it is then notified N
     *          times for every matching inbound frame.
     * @warning Ownership and lifetime: @p listener is stored as a raw, non-owned
     *          pointer. The connection does not take ownership; the listener
     *          must outlive its registration and be removed (via
     *          removeFrameListener() or close()) before it is destroyed, or the
     *          connection is left holding a dangling pointer.
     * @warning Callback context: the listener's FrameListener::notify() is
     *          invoked synchronously on the Bus reader thread while the Bus
     *          receive mutex and this connection's mutex are both held during
     *          fan-out. notify() must return promptly (blocking stalls the
     *          single reader thread and hence all inbound CEC traffic), and
     *          must not call back into this connection's registration APIs (a
     *          re-entrant lock deadlocks). Regarding exceptions: an
     *          InvalidStateException propagated out of notify() is caught by the
     *          Bus reader loop (treated as the benign end-of-stream case), but any
     *          other exception type escapes the reader thread's entry point and
     *          terminates it, stopping all inbound CEC delivery.
     * @see hdmicec/ccec/src/Bus.cpp (Bus::Reader::run notifies under rMutex)
     *      and Connection.cpp (DefaultFrameListener::notify fans out under the
     *      connection mutex)
     */
    void addFrameListener(FrameListener *listener);
    /**
     * @brief Unregisters a previously added inbound-frame observer.
     *
     * Removes the given listener from the connection's list of frame listeners
     * so it no longer receives inbound CEC frames.
     *
     * @param[in] listener Frame listener to remove from the notification list.
     *
     * @note Removes every matching entry (std::list::remove), so a listener
     *       registered multiple times is fully unregistered by one call, and
     *       removing a listener that is not present is a no-op. Unregister a
     *       listener (here or via close()) before the listener object is
     *       destroyed.
     */
    void removeFrameListener(FrameListener *listener);

	/**
	 * @brief Sends a CEC frame onto the bus and reports failures by throwing.
	 *
	 * Transmits the raw @p frame through the CEC Bus. If the transmission fails
	 * (for example a missing acknowledgement or an I/O error) the underlying
	 * exception is propagated to the caller. This throwing overload is selected
	 * by the @p doThrow tag parameter.
	 *
	 * @param[in] frame   Raw CEC frame (byte stream) to transmit.
	 * @param[in] timeout Transmit retry budget in milliseconds, NOT a wall-clock
	 *                    upper bound. A value <= 0 attempts the transmit once; a
	 *                    positive value makes up to floor(@p timeout / 250) + 1
	 *                    attempts (see the @note for the exact arithmetic).
	 * @param[in] doThrow Throwing-mode tag that selects this overload.
	 * @throws Exception On transmit failure, such as a missing acknowledgement
	 *         (CECNoAckException) or an I/O error (IOException).
	 * @note Retry arithmetic (Bus::send): for a positive @p timeout the retry
	 *       count is the integer floor(@p timeout / 250); each attempt is
	 *       preceded by a 1 ms sleep and, on failure, followed by a 250 ms delay
	 *       before the next, giving up to floor(@p timeout / 250) + 1 attempts.
	 *       Total elapsed time therefore includes those sleeps plus the HAL
	 *       transmit duration and can exceed @p timeout; it is a retry budget,
	 *       not a deadline.
	 * @see hdmicec/ccec/src/Bus.cpp (Bus::send retry loop)
	 */
	void send(const CECFrame &frame, int timeout, const Throw_e &doThrow);
	/**
	 * @brief Sends a CEC frame to a specific logical address, throwing on failure.
	 *
	 * Builds a CEC header addressed from this connection's source to @p to,
	 * prepends it to @p frame, and transmits the combined frame. If the
	 * transmission fails the underlying exception is propagated to the caller.
	 * This throwing overload is selected by the @p doThrow tag parameter.
	 *
	 * @param[in] to      Destination logical address for the frame.
	 * @param[in] frame   Raw CEC frame (byte stream) to transmit.
	 * @param[in] timeout Transmit retry budget in milliseconds, NOT a wall-clock
	 *                    upper bound. A value <= 0 attempts the transmit once; a
	 *                    positive value makes up to floor(@p timeout / 250) + 1
	 *                    attempts with 1 ms pre-attempt sleeps and 250 ms
	 *                    inter-attempt delays plus the HAL transmit duration.
	 * @param[in] doThrow Throwing-mode tag that selects this overload.
	 * @throws Exception On transmit failure, such as a missing acknowledgement
	 *         (CECNoAckException) or an I/O error (IOException).
	 * @see send(const CECFrame &, int, const Throw_e &) for the exact retry
	 *      arithmetic and Bus::send in hdmicec/ccec/src/Bus.cpp.
	 */
	void sendTo(const LogicalAddress &to, const CECFrame &frame, int timeout, const Throw_e &doThrow);
	/**
	 * @brief Sends a CEC frame onto the bus, suppressing transmit errors.
	 *
	 * Transmits the raw @p frame through the CEC Bus. Any transmit exception is
	 * caught internally, so this overload does not report failures to the
	 * caller.
	 *
	 * @param[in] frame   Raw CEC frame (byte stream) to transmit.
	 * @param[in] timeout Transmit retry budget in milliseconds, NOT a wall-clock
	 *                    upper bound. Defaults to 0, which attempts the transmit
	 *                    once; a positive value makes up to
	 *                    floor(@p timeout / 250) + 1 attempts with 1 ms
	 *                    pre-attempt sleeps and 250 ms inter-attempt delays plus
	 *                    the HAL transmit duration.
	 * @see send(const CECFrame &, int, const Throw_e &) for the exact retry
	 *      arithmetic and Bus::send in hdmicec/ccec/src/Bus.cpp.
	 */
	void send(const CECFrame &frame, int timeout = 0);
	/**
	 * @brief Sends a CEC frame to a specific logical address, suppressing errors.
	 *
	 * Builds a CEC header addressed from this connection's source to @p to,
	 * prepends it to @p frame, and transmits the combined frame using the
	 * non-throwing send path.
	 *
	 * @param[in] to      Destination logical address for the frame.
	 * @param[in] frame   Raw CEC frame (byte stream) to transmit.
	 * @param[in] timeout Transmit retry budget in milliseconds, NOT a wall-clock
	 *                    upper bound. Defaults to 0, which attempts the transmit
	 *                    once; a positive value makes up to
	 *                    floor(@p timeout / 250) + 1 attempts with 1 ms
	 *                    pre-attempt sleeps and 250 ms inter-attempt delays plus
	 *                    the HAL transmit duration.
	 * @see send(const CECFrame &, int, const Throw_e &) for the exact retry
	 *      arithmetic and Bus::send in hdmicec/ccec/src/Bus.cpp.
	 */
	void sendTo(const LogicalAddress &to, const CECFrame &frame, int timeout = 0);
	/**
	 * @brief Sends a CEC frame to a specific logical address asynchronously.
	 *
	 * Builds a CEC header addressed from this connection's source to @p to,
	 * prepends it to @p frame, and queues the combined frame for asynchronous
	 * transmission, returning immediately without waiting for completion.
	 *
	 * @param[in] to    Destination logical address for the frame.
	 * @param[in] frame Raw CEC frame (byte stream) to transmit.
	 */
	void sendToAsync(const LogicalAddress &to, const CECFrame &frame);
	/**
	 * @brief Sends a CEC poll message on the bus, throwing on failure.
	 *
	 * Issues a poll for the given logical address; for a poll the initiator and
	 * follower addresses are the same. If the poll fails the underlying
	 * exception is propagated to the caller.
	 *
	 * @param[in] from    Logical address to poll; used as both the initiator and
	 *                    the follower.
	 * @param[in] doThrow Throwing-mode tag that selects the throwing behaviour.
	 * @throws Exception On failure, such as a missing acknowledgement
	 *         (CECNoAckException).
	 */
	void poll(const LogicalAddress &from, const Throw_e &doThrow);
	/**
	 * @brief Sends a CEC ping message between two logical addresses.
	 *
	 * Pings from @p from to @p to; for a ping the initiator and follower
	 * addresses differ. If the ping fails the underlying exception is
	 * propagated to the caller.
	 *
	 * @param[in] from    Initiator logical address originating the ping.
	 * @param[in] to      Follower logical address being pinged.
	 * @param[in] doThrow Throwing-mode tag that selects the throwing behaviour.
	 * @throws Exception On failure, such as a missing acknowledgement
	 *         (CECNoAckException).
	 */
	void ping(const LogicalAddress &from, const LogicalAddress &to, const Throw_e &doThrow);
		
	/**
	 * @brief Sends a CEC frame asynchronously using the connection's source.
	 *
	 * Queues the raw @p frame for asynchronous transmission on the CEC Bus and
	 * returns immediately without waiting for the transmission to complete.
	 *
	 * @param[in] frame Raw CEC frame (byte stream) to transmit.
	 */
	void sendAsync(const CECFrame &frame);

	/**
	 * @brief Returns the source logical address bound to this connection.
	 *
	 * @return Reference to the connection's source logical address.
	 */
	const LogicalAddress & getSource(void) {
		return source;
	}
	
	/**
	 * @brief Sets the source logical address bound to this connection.
	 *
	 * Updates only the connection's own @c source member, which is used when
	 * building outbound frame headers and by outbound source matching. It does
	 * NOT update the copy captured by the internal inbound frame filter at
	 * construction, so inbound filtering keeps using the source supplied to the
	 * constructor. To change the address used for inbound filtering, construct a
	 * new Connection with the desired source.
	 *
	 * @param[in] from New source logical address for this connection.
	 *
	 * @warning Stale-filter behaviour: after setSource() the outbound source and
	 *          the inbound-filter source can differ. This reflects the current
	 *          implementation and is documented as-is.
	 * @see hdmicec/ccec/src/Connection.cpp (DefaultFilter holds its own copied
	 *      source; setSource updates Connection::source only)
	 */
	void setSource(const LogicalAddress &from) {
		source = from;
	}

private:
	class DefaultFilter : public FrameFilter {
	public:
		DefaultFilter(LogicalAddress &source) : source(source) {}
		bool isFiltered(const CECFrame &frame);
    private:
	    LogicalAddress source;

	};

    class DefaultFrameListener : public FrameListener {
    public:
    	DefaultFrameListener(Connection &connection, FrameFilter &filter) : connection(connection), filter(filter) {
        }
    	void notify(const CECFrame &frame) const;
    private:
    	Connection &connection;
    	FrameFilter &filter;
    };

    void matchSource(const CECFrame &frame);
    std::string name;
    LogicalAddress source;
    Bus &bus;
    DefaultFilter busFrameFilter;
    DefaultFrameListener busFrameListener;
	std::list<FrameListener *> frameListeners;
	Mutex mutex;
};

CCEC_END_NAMESPACE

#endif


/** @} */
/** @} */
