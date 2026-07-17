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


#ifndef HDMI_CCEC_LIB_HPP_
#define HDMI_CCEC_LIB_HPP_
#include <list>
#include "osal/Mutex.hpp"
#include "ccec/CCEC.hpp"
#include "Operands.hpp"
using CCEC_OSAL::Mutex;

CCEC_BEGIN_NAMESPACE

class PhysicalAddress;

/**
 * @brief Singleton facade owning CCEC library lifecycle and address queries.
 *
 * LibCCEC is the primary entry point an application uses before opening a
 * Connection. It owns initialization and teardown of the CCEC library and the
 * underlying HAL, and exposes the local device's logical and physical address
 * queries. getInstance() returns a process-wide instance created on first use
 * (a function-local static), and callers are expected to share that instance
 * rather than construct their own. This shared use is a convention, not an
 * enforced singleton: the constructor is public, so additional LibCCEC
 * instances can be created, and each would drive the same process-wide Driver
 * and Bus singletons.
 *
 * @note Thread safety is limited: the internal Mutex serializes only the
 *       init() and term() lifecycle transitions with respect to one another.
 *       The address queries (getLogicalAddress(), getPhysicalAddress(),
 *       addLogicalAddress()) do not take this mutex and are not synchronized
 *       by it, so this class does not provide general all-method thread
 *       safety.
 * @see Connection
 * @see Driver
 * @see hdmicec/ccec/src/LibCCEC.cpp
 */
class LibCCEC {
public:
	/**
	 * @brief Accessor for the process-wide LibCCEC singleton.
	 *
	 * @return Reference to the shared LibCCEC instance.
	 */
	static LibCCEC &getInstance(void);

	/**
	 * @brief Constructs the LibCCEC facade.
	 *
	 * Initializes the internal state flags (initialized/connected) to false; it
	 * does not touch the Driver or Bus. Sharing the getInstance() instance is
	 * preferred but not enforced: because this constructor is public, callers
	 * can construct their own LibCCEC instances.
	 *
	 * @note Prefer the shared instance from getInstance() rather than
	 *       constructing LibCCEC directly.
	 */
	LibCCEC(void);
	/**
	 * @brief Initializes the CCEC library and the underlying HAL.
	 *
	 * Must be called before opening a Connection or issuing address queries. On
	 * success it opens the Driver and then starts the Bus, and marks the library
	 * initialized.
	 *
	 * @param[in] name Optional CEC log-prefix string. When non-NULL it is copied
	 *                 (truncated to fit) into the global CEC log prefix used by
	 *                 CCEC log lines; when NULL (the default) the log prefix is
	 *                 cleared. It is not a client identity or handle.
	 * @throws InvalidStateException (ccec/Exception.hpp) if the library is
	 *         already initialized (init() is not re-entrant and must be paired
	 *         with term()).
	 * @note May also propagate exceptions raised while opening the Driver or
	 *       starting the Bus (for example IOException).
	 * @see hdmicec/ccec/src/LibCCEC.cpp (init: Driver::open() then Bus::start())
	 */
	void init(const char * name= 0);
	/**
	 * @brief Tears down the CCEC library and releases HAL resources.
	 *
	 * Stops the Bus and then closes the Driver, and marks the library
	 * uninitialized.
	 *
	 * @throws InvalidStateException (ccec/Exception.hpp) if the library is not
	 *         currently initialized.
	 * @see hdmicec/ccec/src/LibCCEC.cpp (term: Bus::stop() then Driver::close())
	 */
	void term(void);
	/**
	 * @brief Obtains the logical address currently allocated to this device.
	 *
	 * @param[in] devType Device type argument. It is forwarded to the driver but
	 *                    is currently ignored by the legacy HAL query
	 *                    (HdmiCecGetLogicalAddress does not take a device type),
	 *                    so the value returned is not specific to @p devType.
	 * @return The allocated logical address as a non-zero int.
	 * @throws InvalidStateException (ccec/Exception.hpp) if the library is not
	 *         initialized, or if the driver reports logical address 0. Because 0
	 *         is the TV logical address, a genuine TV allocation is rejected by
	 *         this 0-means-unallocated check (documented as-is).
	 * @see hdmicec/ccec/src/LibCCEC.cpp (getLogicalAddress)
	 */
	int getLogicalAddress(int devType);
	/**
	 * @brief Retrieves the device's physical address.
	 *
	 * @param[out] physicalAddress Non-null pointer that receives the packed
	 *                             physical address. The pointer is forwarded
	 *                             directly to the driver and dereferenced without
	 *                             a null check, so the caller must supply a valid,
	 *                             non-null pointer.
	 * @throws InvalidStateException (ccec/Exception.hpp) if the library is not
	 *         initialized.
	 * @see hdmicec/ccec/src/LibCCEC.cpp (getPhysicalAddress)
	 */
	void getPhysicalAddress(unsigned int *physicalAddress);
	/**
	 * @brief Claims (allocates) a logical address for this device.
	 *
	 * Forwards the request to Driver::addLogicalAddress().
	 *
	 * @param[in] source The logical address to claim.
	 * @return Always the integer 1 when it returns normally. The driver's bool
	 *         result is not inspected, so the return value does not indicate
	 *         whether the claim actually succeeded.
	 * @throws InvalidStateException (ccec/Exception.hpp) if the library is not
	 *         initialized. Failures detected by the driver are reported by
	 *         exceptions it throws (for example AddressNotAvailableException or
	 *         IOException), which propagate out of this method.
	 * @see hdmicec/ccec/src/LibCCEC.cpp (addLogicalAddress returns true
	 *      unconditionally)
	 */
	int addLogicalAddress(const LogicalAddress &source);

private:
//	int logicalAddresses;
	bool initialized;
	bool connected;
	Mutex mutex;
};

CCEC_END_NAMESPACE

#endif


/** @} */
/** @} */
