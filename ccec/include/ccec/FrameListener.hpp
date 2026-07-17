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


#ifndef HDMI_CCEC_FRAME_LISENER_
#define HDMI_CCEC_FRAME_LISENER_

#include "CCEC.hpp"

CCEC_BEGIN_NAMESPACE

class CECFrame;

/**
 * @brief Observer interface for receiving inbound CEC frames.
 *
 * FrameListener is the callback contract implemented by CCEC components that
 * wish to be notified when a CEC frame is received from the bus. Subscribers
 * register a concrete FrameListener with the receive path and have their
 * notify() method invoked for each delivered frame. The interface is abstract
 * (pure-virtual) and holds no state of its own.
 *
 * @see FrameFilter
 * @see CECFrame
 */
class FrameListener
{
public:
	/**
	 * @brief Called by the CCEC layer when a matching CEC frame arrives.
	 *
	 * Implementations receive the inbound CEC frame and act on its contents;
	 * the const-qualified signature indicates the callback does not modify the
	 * observer's own logical state. The supplied frame carries the raw received
	 * CEC bytes and remains owned by the caller for the duration of the call.
	 *
	 * @note Pure-virtual; implemented by frame subscribers.
	 */
	virtual void notify(const CECFrame &) const = 0;
	/**
	 * @brief Virtual destructor enabling polymorphic deletion of subscribers.
	 */
	virtual ~FrameListener(void) {}
};

/**
 * @brief Predicate that decides whether an inbound CEC frame is filtered out.
 *
 * FrameFilter is the abstract contract used by the CCEC receive path to decide,
 * on a per-frame basis, whether a received frame should be ignored (filtered)
 * or delivered to registered FrameListener observers. Implementations inspect
 * the frame - typically its destination logical address - and return their
 * verdict. The interface is abstract (pure-virtual) and holds no state of its
 * own.
 *
 * @see FrameListener
 * @see CECFrame
 */
class FrameFilter
{
public:
	/**
	 * @brief Determines whether an inbound CEC frame should be filtered out.
	 *
	 * Invoked by the receive path for each incoming frame before it is
	 * dispatched to observers. A frame reported as filtered is dropped and not
	 * delivered; otherwise it is forwarded to the registered FrameListener(s).
	 *
	 * @param[in] frame The received CEC frame to evaluate.
	 * @retval true  The frame should be filtered out (ignored / not delivered).
	 * @retval false The frame should be delivered to the frame listeners.
	 * @note Pure-virtual; implemented by concrete filters.
	 */
	virtual bool isFiltered(const CECFrame &frame)  = 0;
	/**
	 * @brief Virtual destructor enabling polymorphic deletion of filters.
	 */
	virtual ~FrameFilter(void) {}
};

CCEC_END_NAMESPACE

#endif



/** @} */
/** @} */
