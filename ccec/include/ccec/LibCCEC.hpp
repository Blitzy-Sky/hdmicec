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
 * queries. A single process-wide instance is shared across the middleware;
 * callers obtain it through getInstance() rather than constructing LibCCEC
 * directly.
 *
 * @note The instance holds an internal Mutex member for synchronization.
 * @see Connection
 * @see Driver
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
	 * @note Not normally called directly; obtain the shared instance through
	 *       getInstance() instead.
	 */
	LibCCEC(void);
	/**
	 * @brief Initializes the CCEC library and the underlying HAL.
	 *
	 * @param[in] name Optional client/instance name; defaults to 0 (NULL) when
	 *                 not supplied.
	 */
	void init(const char * name= 0);
	/**
	 * @brief Tears down the CCEC library and releases HAL resources.
	 */
	void term(void);
	/**
	 * @brief Obtains the logical address for the given device type.
	 *
	 * @param[in] devType The CEC device type to query.
	 * @return The logical address as an int.
	 */
	int getLogicalAddress(int devType);
	/**
	 * @brief Retrieves the device's physical address.
	 *
	 * @param[out] physicalAddress Receives the packed physical address.
	 */
	void getPhysicalAddress(unsigned int *physicalAddress);
	/**
	 * @brief Claims (allocates) a logical address for this device.
	 *
	 * @param[in] source The logical address to claim.
	 * @return The result of the claim operation as an int.
	 * @note This facade method returns an int, whereas Driver::addLogicalAddress
	 *       returns a bool; the int value is reported as-is by the CCEC library.
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
