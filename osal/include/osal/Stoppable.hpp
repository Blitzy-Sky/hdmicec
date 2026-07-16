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
* @defgroup osal
* @{
**/


#ifndef HDMI_CCEC_OSAL_STOPPABLE_HPP_
#define HDMI_CCEC_OSAL_STOPPABLE_HPP_

#include "OSAL.hpp"

CCEC_OSAL_BEGIN_NAMESPACE

/**
 * @brief Abstract lifecycle helper that tracks running/stopping/stopped state.
 *
 * Provides a cooperative stop contract plus protected state-transition helpers
 * for components that run on their own thread of execution and must be shut down
 * in a controlled manner.
 */
class Stoppable {
	enum {
		RUNNING,
		STOPPING,
		STOPPED,
	};
public:
	/**
	 * @brief Constructs a Stoppable with an initial lifecycle state.
	 * @param[in] state - Initial lifecycle state; defaults to RUNNING.
	 */
	Stoppable(int state = RUNNING) : state(state) {}

	/**
	 * @brief Requests the component to stop (pure-virtual; implemented by subclasses).
	 * @param[in] block - When true, block until the stop has completed; when false,
	 *                    request the stop and return immediately.
	 */
	virtual void  stop(bool block = false) = 0;
	/**
	 * @brief Indicates whether the component has fully stopped.
	 * @return true if the current state is STOPPED, false otherwise.
	 * @retval true  - The component is stopped.
	 * @retval false - The component is not stopped.
	 */
	virtual bool  isStopped(void) {
		return state == STOPPED;
	}
protected:

	/**
	 * @brief Marks the component as started by setting the state to RUNNING.
	 */
	virtual void runStarted(void) {
		state = RUNNING;
	}
	/**
	 * @brief Begins the stop transition, moving the state from RUNNING to STOPPING.
	 */
	virtual void  stopStarted(void) {
		if (state == RUNNING) state = STOPPING;
	}
	/**
	 * @brief Indicates whether a stop is in progress or already complete.
	 * @return true if the state is STOPPING or STOPPED, false otherwise.
	 */
	virtual bool  isStopping(void) {
		return ((state == STOPPING) || (state == STOPPED));
	}
	/**
	 * @brief Indicates whether the component is currently running.
	 * @return true if the state is RUNNING, false otherwise.
	 */
	virtual bool isRunning(void) {
		return state == RUNNING;
	}
	/**
	 * @brief Marks the stop as complete by setting the state to STOPPED.
	 */
	virtual void stopCompleted(void) {
		state = STOPPED;
	}
private:
	volatile int state;

};

CCEC_OSAL_END_NAMESPACE

#endif


/** @} */
/** @} */
