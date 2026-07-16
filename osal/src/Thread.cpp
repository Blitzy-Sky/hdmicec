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


#include <stdlib.h>
#include <pthread.h>

#include "osal/Runnable.hpp"
#include "osal/Thread.hpp"

CCEC_OSAL_BEGIN_NAMESPACE

/**
 * @brief Static C-linkage entry trampoline used to launch a thread.
 *
 * Casts the supplied argument back to a Runnable pointer and invokes its run()
 * method. Passed as the start routine to @c pthread_create().
 *
 * @param[in] arg   Opaque pointer to the Runnable target to execute.
 * @return Always returns NULL when the runnable completes.
 */
void *Thread::CEntry(void * arg)
{
	Runnable *target = static_cast<Runnable *>(arg);
	target->run();
	return NULL;
}

/**
 * @brief Constructs a Thread bound to a Runnable target.
 *
 * @param[in] target   The Runnable whose run() method executes on the thread.
 */
Thread::Thread(Runnable &target) : runnable(target), nativeHandle(0)
{
}

/**
 * @brief Constructs a named Thread bound to a Runnable target.
 *
 * @param[in] target   The Runnable whose run() method executes on the thread.
 * @param[in] name     Human-readable name associated with the thread.
 */
Thread::Thread(Runnable &target, const int8_t* name) : runnable(target), name((const char *)name), nativeHandle(0)
{
}

/**
 * @brief Destroys the Thread object.
 *
 * Does not join or stop the underlying native thread; threads created by start()
 * are launched in the detached state.
 */
Thread::~Thread(void)
{
}

/**
 * @brief Runs the bound Runnable target on the calling thread.
 *
 * Invokes run() on the associated Runnable in the context of the caller
 * (does not spawn a new thread).
 *
 * @see start()
 */
void Thread::run(void)
{
	runnable.run();
}

/**
 * @brief Starts a new detached POSIX thread executing the Runnable target.
 *
 * Initializes thread attributes for a detached, @c SCHED_OTHER thread and creates
 * it via @c pthread_create() with CEntry() as the start routine. On success the
 * native thread identifier is stored for a later detach().
 *
 * @see run(), detach(), CEntry()
 */
void Thread::start()
{
	pthread_t tid;
	pthread_attr_t attr;
	int ret = 0;
    (void) pthread_attr_init(&attr);
    (void) pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    (void) pthread_attr_setschedpolicy(&attr, SCHED_OTHER);

    /*@TODO: Set priority */
	ret = pthread_create(&tid, &attr, Thread::CEntry, static_cast<void *>(&runnable));
	pthread_attr_destroy(&attr);

	if (ret != 0) {
		/*@TODO: Throw Exception */
	}
	else {
		nativeHandle = (void *)tid;
	}
}

/**
 * @brief Detaches the underlying native thread.
 *
 * Detaches the stored native thread handle via @c pthread_detach(), allowing its
 * resources to be reclaimed automatically upon termination.
 *
 * @see start()
 */
void Thread::detach(void)
{
	pthread_detach((pthread_t)nativeHandle);
}

CCEC_OSAL_END_NAMESPACE


/** @} */
/** @} */
