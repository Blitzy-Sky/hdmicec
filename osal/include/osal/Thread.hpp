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
\brief This file defines interface of Thread class

*/
/*****************************************************************************/


/**
* @defgroup hdmicec HDMI-CEC Middleware
* @{
* @defgroup osal OS Abstraction Layer (OSAL)
* @{
**/


#include <stdint.h>
#include <string>

#ifndef HDMI_CCEC_OSAL_THREAD_HPP_
#define HDMI_CCEC_OSAL_THREAD_HPP_

#include "OSAL.hpp"

#include "Runnable.hpp"


CCEC_OSAL_BEGIN_NAMESPACE


/***************************************************************************/
/*!

A thread is a thread of execution in a program. 
An application could have multiple threads of execution running concurrently.

A class that needs thread functionality shall implement Runnable interface and 
object will be passed to Thread on creation of Thread object. 
On the invocation of start() method of Thread, runnable's run() will be executed 
in a threaded context.

\note Ownership: the Runnable target passed at construction is held by
      reference, not copied; the caller must keep the target alive for the
      entire lifetime of the running thread. The started thread runs detached
      and is not joined by the destructor.
*/
/**************************************************************************/

class Thread : public Runnable {
public:
/**************************************************************************/
/*! 
\brief Constructor

 Allocates a new Thread object
 \param target - Object implements Runnable interface.  
 \note The target is stored by reference and is not copied; the caller retains
       ownership and must keep it alive until the thread has finished running.
 */
 /************************************************************************/

	Thread(Runnable &target);
/**************************************************************************/
/*! 
\brief Constructor

 Allocates a new Thread object
 \param target - Object implementes Runnable interface.
 \param name - Name of the thread context.
 \note The name is retained in a member string but is not applied to the
       native thread: start() performs no pthread_setname_np (or equivalent)
       call, so the stored name has no effect on the created thread.
       Documented as-is.
 */
 /************************************************************************/

	Thread(Runnable &target, const int8_t* name);
/**************************************************************************/
/*! 
\brief Destructor

 Destroys the Thread object
 \note The destructor does not join or stop the underlying thread: because the
       thread is created detached it cannot be joined, and no stop is issued
       here. A thread already started may continue to run after this object is
       destroyed; the caller must ensure the Runnable target outlives it.
 */
 /************************************************************************/

	virtual ~Thread(void);
/*! 
\brief Executes the run() method of runnable object.

 If this object is created by passing reference to a runnable object, that 
 object's run() will be executed, otherwise this method does nothing and returns.
 \note Execution is synchronous in the context of the caller; run() does not
       spawn a new thread of execution (contrast with start()).
 */
 /************************************************************************/

	void run(void);

/************************************************************************/
/*!
\brief Starts excecution of the thread.

 Causes this thread to begin execution;This calls the run method of this thread.
 The result is that two threads are running concurrently: 
 the current thread (which returns from the call to the start method) 
 and the other thread (which executes its run method).
 \note The new thread is created in the detached state
       (PTHREAD_CREATE_DETACHED), so its resources are reclaimed automatically
       on termination and it cannot be joined.
 \warning If the underlying pthread_create call fails, the failure is not
          reported: no exception is thrown and no status is returned, so the
          caller cannot detect that the thread was not started.
          Documented as-is.
 */
/***********************************************************************/

	void start(void);
/************************************************************************/
/*!
\brief Stops execution of thread.

 Forces the thread to stop executing.
 \note This method is declared but is not defined in the OSAL sources; any
       odr-use (actually calling it) produces an undefined-reference link
       error. It therefore has no runtime effect on its own.
       Documented as-is.
 */
/***********************************************************************/

	void stop(void);
/************************************************************************/
/*!
\brief Detaches the thread.

 Detaches the thread.
 */
/***********************************************************************/

	void detach(void);
/************************************************************************/
/*!
\brief Returns native thread handle.

 Retrieves native thread handle if thread is started other wise returns null.
 \return native thread handle.
 \note Like stop(), this method is declared but is not defined in the OSAL
       sources; any odr-use (calling it) produces an undefined-reference link
       error. Documented as-is.
 */
/***********************************************************************/

	void *getNativeHandle(void);

private:
	Runnable &runnable;
	std::string name;
	void *nativeHandle;
	static void *CEntry(void * arg);
};

CCEC_OSAL_END_NAMESPACE

#endif


/** @} */
/** @} */
