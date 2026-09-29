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


#ifndef _HDMI_CCEC_DRIVER_HPP_
#define _HDMI_CCEC_DRIVER_HPP_

#include <queue>
#include <list>

#include "osal/Mutex.hpp"
#include "ccec/Exception.hpp"
#include "CECFrame.hpp"
#include "Operands.hpp"

using CCEC_OSAL::Mutex;

CCEC_BEGIN_NAMESPACE

/**
 * @brief Abstract HAL contract for HDMI-CEC transport.
 *
 * Driver is the pure-virtual interface the CCEC middleware calls to transmit
 * and receive CEC frames and to manage logical/physical addressing. It marks
 * the boundary between the CCEC middleware and the underlying platform HAL:
 * a single concrete adapter, DriverImpl (in ccec/src), implements this
 * contract and forms the HAL migration seam. In the current implementation
 * DriverImpl bridges exclusively to the legacy C HAL (libRCECHal.so, declared
 * in hdmi_cec_driver.h): every DriverImpl method calls the legacy @c HdmiCec*
 * C API (for example @c HdmiCecOpen(), @c HdmiCecTx() / @c HdmiCecTxAsync(),
 * @c HdmiCecGetLogicalAddress()), and no AIDL/Binder client code is present.
 * The AIDL/Binder HAL (com.rdk.hal.hdmicec) is the migration target this seam
 * is intended to move to; it is the future backend, not a second
 * run-time-selectable HAL today.
 *
 * Each member below describes the abstract contract an implementation MUST
 * honour; the concrete behaviour lives in DriverImpl and is out of scope in
 * this header.
 *
 * @note Callers obtain the concrete implementation through getInstance() rather
 *       than constructing a Driver directly.
 * @see hdmi_cec_driver.h (current legacy C HAL backend)
 * @see com.rdk.hal.hdmicec.IHdmiCecController (AIDL migration target)
 *
 * Source: hdmicec/ccec/src/DriverImpl.cpp (includes ccec/drivers/hdmi_cec_driver.h
 * and calls only the legacy HdmiCec* C API).
 */
class Driver {
public:
	/**
	 * @brief Returns the singleton concrete driver adapter.
	 *
	 * Provides access to the process-wide Driver implementation the CCEC
	 * middleware uses to reach the underlying HAL.
	 *
	 * @return Reference to the active Driver implementation.
	 */
	static Driver &getInstance(void);

	/**
	 * @brief Transmit result codes reported for a CEC frame transmission.
	 *
	 * These tri-state outcomes mirror the legacy HAL transmit status codes
	 * (the HDMI_CEC_IO_SENT_* values defined in hdmi_cec_driver.h).
	 */
	enum  {
		/** Message placed on the CEC bus and acknowledged by the follower. */
		SENT_AND_ACKD,     //On the bus and destination device ack'd
		/** Message could not be placed on the CEC bus (e.g., arbitration/line error). */
		SENT_FAILED = 1,   //Not getting on the bus.
		/** Message placed on the CEC bus but not acknowledged by any follower. */
		SENT_BUT_NOT_ACKD, //On the bus but no destination device.
	};


	/**
	 * @brief Constructs the abstract base.
	 *
	 * Inline default constructor for the abstract Driver base; concrete
	 * construction is performed by the DriverImpl adapter.
	 */
	Driver(void) {};

	/**
	 * @brief Opens and initializes the CEC device.
	 *
	 * Prepares the underlying HAL so the middleware can transmit and receive
	 * CEC frames.
	 *
	 * @note Declared noexcept(false): an implementation may throw on failure
	 *       (for example, IOException) if the device cannot be opened.
	 */
	virtual void  open(void) noexcept(false) = 0;
	/**
	 * @brief Closes and releases the CEC device.
	 *
	 * Releases the resources acquired by open().
	 *
	 * @note Declared noexcept(false): an implementation may throw on failure
	 *       (for example, IOException).
	 */
	virtual void  close(void) noexcept(false) = 0;
	/**
	 * @brief Reads the next received CEC frame.
	 *
	 * Retrieves the next inbound frame reported by the HAL.
	 *
	 * @param[out] frame Populated with the received CEC frame bytes.
	 * @note Declared noexcept(false): an implementation may throw on I/O error
	 *       (for example, IOException).
	 */
	virtual void  read(CECFrame &frame) noexcept(false) = 0; 
	/**
	 * @brief Synchronously transmits a CEC frame.
	 *
	 * Places the frame on the CEC bus and blocks until the transmit result is
	 * known.
	 *
	 * @param[in] frame The CEC frame to transmit.
	 * @note Declared noexcept(false): an implementation may throw on a failed or
	 *       unacknowledged transmission (for example, CECNoAckException,
	 *       IOException).
	 */
	virtual void  write(const CECFrame &frame) noexcept(false) = 0;
	/**
	 * @brief Queues a CEC frame for asynchronous transmission.
	 *
	 * Submits the frame to the HAL for transmission and returns without waiting
	 * for the transmit result.
	 *
	 * @param[in] frame The CEC frame to transmit.
	 * @note Declared noexcept(false): an implementation may throw if the frame
	 *       cannot be queued (for example, IOException).
	 */
	virtual void  writeAsync(const CECFrame &frame) noexcept(false) = 0;
	/**
	 * @brief Releases a previously-claimed logical address.
	 *
	 * @param[in] source The logical address to release.
	 */
	virtual void  removeLogicalAddress(const LogicalAddress &source)  = 0;
	/**
	 * @brief Claims (allocates) a logical address for this device.
	 *
	 * @param[in] source The logical address to claim.
	 * @retval true  The logical address was claimed successfully.
	 * @retval false The logical address could not be claimed.
	 */
	virtual bool  addLogicalAddress   (const LogicalAddress &source) = 0;
//	virtual void  getLogicalAddress(int devType, int *logicalAddress) = 0;
	/**
	 * @brief Obtains the logical address allocated for a device type.
	 *
	 * @param[in] devType The CEC device type to query.
	 * @return The allocated logical address as an int.
	 */
	virtual int  getLogicalAddress(int devType) = 0;
	/**
	 * @brief Retrieves the device's physical address.
	 *
	 * @param[out] physicalAddress Receives the packed physical address.
	 */
	virtual void getPhysicalAddress(unsigned int *physicalAddress) = 0;
	/**
	 * @brief Tests whether a logical address is valid for this device.
	 *
	 * @param[in] source The logical address to validate.
	 * @retval true  The logical address is valid.
	 * @retval false The logical address is not valid.
	 */
	virtual bool isValidLogicalAddress(const LogicalAddress &source) const = 0;
	/**
	 * @brief Performs a CEC ping/poll between two logical addresses.
	 *
	 * Probes whether a device at the destination logical address is present on
	 * the CEC bus.
	 *
	 * @param[in] from The initiator logical address.
	 * @param[in] to   The destination logical address to poll.
	 * @note Declared noexcept(false): an implementation may throw on I/O error
	 *       (for example, IOException).
	 */
	virtual void poll(const LogicalAddress &from, const LogicalAddress &to) noexcept(false) = 0;
	/**
	 * @brief Logs a human-readable breakdown of a CEC frame.
	 *
	 * @param[in] frame The CEC frame whose contents are logged.
	 * @note Declared noexcept(false): an implementation may throw during logging
	 *       (for example, IOException).
	 */
	virtual void printFrameDetails(const CECFrame &frame) noexcept(false) = 0;

	/**
	 * @brief Virtual destructor.
	 *
	 * Ensures safe polymorphic deletion of concrete Driver implementations
	 * through a base-class pointer.
	 */
	virtual ~Driver(void) {};
protected:

private:
	/**
	 * @brief Serializes creation and access of the singleton Driver instance.
	 */
	static Mutex instanceMutex;
};

CCEC_END_NAMESPACE
#endif


/** @} */
/** @} */
