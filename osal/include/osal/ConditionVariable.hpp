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

/*****************************************************************************/
/*!
\file
\brief This file defines interface of ConditionVariable class.

*/
/*****************************************************************************/



/**
* @defgroup hdmicec HDMI-CEC Middleware
* @{
* @defgroup osal OS Abstraction Layer (OSAL)
* @{
**/


#ifndef HDMI_CCEC_OSAL_CONDITION_VARIABLE_HPP_
#define HDMI_CCEC_OSAL_CONDITION_VARIABLE_HPP_

#include "OSAL.hpp"
#include "Mutex.hpp"
#include "Condition.hpp"

CCEC_OSAL_BEGIN_NAMESPACE
/***************************************************************************/
/*!

ConditionVariable factors out the Object monitor methods (wait, notify and notifyAll)
into distinct objects to give the effect of having multiple wait-sets per object, 
by combining them with the use of arbitrary Lock implementations. 

ConditionVariables provide a means for one thread to suspend execution (to "wait") 
until notified by another thread that some state condition may now be true. 
Because access to this shared state information occurs in different threads, 
it must be protected, so a lock of some form is associated with the condition. 
The key property that waiting for a condition provides is that it atomically 
releases the associated lock and suspends the current thread.

A ConditionVariable instance is intrinsically bound to a lock. 
*/
/**************************************************************************/


class ConditionVariable {
public:
/***************************************************************************/
/*!
\brief Constructor.
Creates a ConditionVariable object.

*/
/**************************************************************************/
	ConditionVariable(void);
/***************************************************************************/

/*!
\brief Destructor.
Destroys the ConditionVariable object.

*/
/**************************************************************************/

	~ConditionVariable(void);
/***************************************************************************/
/*!
\brief sets the condition.
Sets the condition assosiated with the ConditionVariable object.

*/
/**************************************************************************/

	void set(void);
/***************************************************************************/
/*!
\brief resets the condition.
Resets the condition assosiated with the ConditionVariable object to default
(false).

*/
/**************************************************************************/

	void reset(void);
/***************************************************************************/
/*!
\brief Checks the status of the condition.
Returns the status of the condition associated. Could be true/false.

\return - boolean value of the associated condition.
*/
/**************************************************************************/
	bool isSet(void);
/***************************************************************************/
/*!
\brief Wait until the conditional variable is signalled.
Causes the current thread to wait until it is signalled or interrupted. 
Calling thread will wait undefinetly until some other thread signals the 
ConditionVariable by calling notify/notifyAll.
*/
/**************************************************************************/

	void wait(void);
/***************************************************************************/
/*!
\brief Wait until the conditional variable is signalled.
Causes the current thread to wait until it is signalled or interrupted . 
Calling thread will wait undefinetly until some other thread signals the 
ConditionVariable by calling notify/notifyAll.
*/
/**************************************************************************/

/**
 * @param[in] timeout - Maximum time to wait, expressed in milliseconds. A value
 *            of 0 waits indefinitely (blocks until signalled via notify() /
 *            notifyAll()); a non-zero value performs a timed wait of that many
 *            milliseconds.
 * @return Signalled/timeout indicator.
 * @retval 1 - The condition was signalled (also returned for the indefinite
 *             wait when timeout is 0).
 * @retval 0 - The non-zero timed wait expired before the condition was set.
 * @note A negative timeout is not interpreted as "wait forever"; it is converted
 *       directly to an absolute wake time at or before the current time, so the
 *       timed wait expires without blocking for any positive duration.
 * @warning The millisecond-to-timespec conversion only carries into tv_sec when
 *          the nanoseconds field is strictly greater than 1,000,000,000, so a
 *          timeout whose remainder lands exactly on the one-second boundary
 *          leaves an invalid timespec (tv_nsec == 1,000,000,000) that the
 *          underlying pthread_cond_timedwait() rejects with EINVAL. Because the
 *          wait loop breaks only on ETIMEDOUT, such an error is ignored while the
 *          condition is unset and the call busy-spins rather than timing out.
 *          See the definition in ConditionVariable.cpp. Documented as-is.
 */
	long wait(long timeout);
/**
 * @brief Signals the condition variable, waking one thread waiting on it.
 *
 * Sets the associated condition and wakes a single waiting thread.
 */
	void notify(void);
/**
 * @brief Signals the condition variable, waking all threads waiting on it.
 *
 * Sets the associated condition and wakes every waiting thread.
 */
	void notifyAll(void);
/**
 * @brief Retrieves the opaque handle to the underlying native condition-variable implementation.
 * @return Pointer to the native handle (the underlying pthread_cond_t*).
 */
	void *getNativeHandle(void);
private:
	Condition *cond;
	Mutex *mutex;
	void *nativeHandle;

	ConditionVariable(const ConditionVariable &); /* Not allowed */
	ConditionVariable & operator = (const ConditionVariable &); /* Not allowed */
};

CCEC_OSAL_END_NAMESPACE
#endif


/** @} */
/** @} */
