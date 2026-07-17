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
	 */
	Connection(const LogicalAddress &source = LogicalAddress::UNREGISTERED, bool opened = true, const std::string &name="");
	/**
	 * @brief Destroys the connection instance.
	 *
	 * Declared virtual so that instances can be safely destroyed through a
	 * base-class pointer.
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
     */
    void addFrameListener(FrameListener *listener);
    /**
     * @brief Unregisters a previously added inbound-frame observer.
     *
     * Removes the given listener from the connection's list of frame listeners
     * so it no longer receives inbound CEC frames.
     *
     * @param[in] listener Frame listener to remove from the notification list.
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
	 * @param[in] timeout Transmit timeout; an upper bound on the time to wait so
	 *                    the call does not block indefinitely during sending.
	 * @param[in] doThrow Throwing-mode tag that selects this overload.
	 * @throws Exception On transmit failure, such as a missing acknowledgement
	 *         (CECNoAckException) or an I/O error (IOException).
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
	 * @param[in] timeout Transmit timeout; an upper bound on the time to wait so
	 *                    the call does not block indefinitely during sending.
	 * @param[in] doThrow Throwing-mode tag that selects this overload.
	 * @throws Exception On transmit failure, such as a missing acknowledgement
	 *         (CECNoAckException) or an I/O error (IOException).
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
	 * @param[in] timeout Transmit timeout; an upper bound on the time to wait so
	 *                    the call does not block indefinitely during sending.
	 *                    Defaults to 0.
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
	 * @param[in] timeout Transmit timeout; an upper bound on the time to wait so
	 *                    the call does not block indefinitely during sending.
	 *                    Defaults to 0.
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
	 * @param[in] from New source logical address for this connection.
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
