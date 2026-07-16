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

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include "safec_lib.h"
#include "osal/Mutex.hpp"
#include "osal/Util.hpp"

CCEC_OSAL_BEGIN_NAMESPACE

/**
 * @brief Constructs a Mutex, allocating and initializing a recursive POSIX mutex.
 *
 * Allocates storage for a native @c pthread_mutex_t and initializes it with the
 * @c PTHREAD_MUTEX_RECURSIVE_NP attribute, so the owning thread may lock the
 * mutex multiple times without deadlocking. The temporary mutex attribute object
 * is destroyed before the constructor returns.
 *
 * @note The mutex is recursive; each lock() must be balanced by a matching unlock().
 * @see lock(), unlock(), getNativeHandle()
 */
Mutex::Mutex(void) : nativeHandle(NULL)
{
    pthread_mutexattr_t attr;
    nativeHandle = Malloc(sizeof(pthread_mutex_t));
    pthread_mutexattr_init( &attr);
    pthread_mutexattr_settype (&attr, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init( (pthread_mutex_t *) nativeHandle, &attr );
    pthread_mutexattr_destroy(&attr);
}

/**
 * @brief Copy-constructs a Mutex by duplicating the native handle storage.
 *
 * Allocates a new native @c pthread_mutex_t buffer and byte-copies the source
 * mutex's native handle contents into it.
 *
 * @param[in] rhs   The source Mutex whose native handle contents are copied.
 */
Mutex::Mutex(const Mutex &rhs)
{
    nativeHandle = Malloc(sizeof(pthread_mutex_t));
    MEMCPY_S(nativeHandle,sizeof(pthread_mutex_t), rhs.nativeHandle, sizeof(pthread_mutex_t));
}

/**
 * @brief Copy-assigns from another Mutex using the copy-and-swap idiom.
 *
 * Constructs a temporary copy of @p rhs and swaps its native handle with this
 * object's handle; the temporary releases the previously held handle on destruction.
 *
 * @param[in] rhs   The source Mutex to copy-assign from.
 * @return Reference to this Mutex (@c *this).
 */
Mutex & Mutex::operator = (const Mutex &rhs)
{
	Mutex temp(rhs);
	void *tempHandle = temp.nativeHandle;
	temp.nativeHandle = nativeHandle;
	nativeHandle = tempHandle;

	return *this;
}

/**
 * @brief Destroys the Mutex and releases its native resources.
 *
 * When the native handle is non-NULL, destroys the underlying @c pthread_mutex_t
 * and frees the allocated storage.
 */
Mutex::~Mutex(void)
{
	if (nativeHandle != NULL) {
		pthread_mutex_destroy((pthread_mutex_t *) nativeHandle);
		Free(nativeHandle);
	}
}

/**
 * @brief Acquires (locks) the recursive mutex.
 *
 * Blocks the calling thread until the underlying pthread mutex is acquired.
 * Because the mutex is recursive, the owning thread may lock it multiple times;
 * a matching number of unlock() calls is required to release it.
 *
 * @see unlock()
 */
void Mutex::lock(void)
{
	pthread_mutex_lock((pthread_mutex_t *)nativeHandle);
}

/**
 * @brief Releases (unlocks) the recursive mutex.
 *
 * Decrements the recursive lock count of the underlying pthread mutex; the mutex
 * becomes available to other threads once the count reaches zero.
 *
 * @see lock()
 */
void Mutex::unlock(void)
{
	pthread_mutex_unlock((pthread_mutex_t *)nativeHandle);
}

/**
 * @brief Returns the opaque native mutex handle.
 *
 * @return Pointer to the underlying native @c pthread_mutex_t, returned as a
 *         @c void* opaque handle (may be NULL if not initialized).
 */
void * Mutex::getNativeHandle(void)
{
	return nativeHandle;
}

CCEC_OSAL_END_NAMESPACE


/** @} */
/** @} */
