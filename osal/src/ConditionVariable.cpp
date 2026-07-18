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
* @defgroup osal OS Abstraction Layer (OSAL)
* @{
**/


#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>

#include "osal/ConditionVariable.hpp"
#include "osal/Condition.hpp"
#include "osal/Mutex.hpp"
#include "osal/Util.hpp"

CCEC_OSAL_BEGIN_NAMESPACE

/**
 * @brief Constructs a ConditionVariable and its backing primitives.
 *
 * Allocates the associated boolean Condition (initial state @c false), an OSAL
 * Mutex, and a native @c pthread_cond_t, then initializes the condition variable.
 *
 * @see wait(), notify(), notifyAll()
 */
ConditionVariable::ConditionVariable() : cond(NULL), mutex(NULL), nativeHandle(NULL)
{
	cond = new  Condition(false);
    mutex = new Mutex();
    nativeHandle = Malloc(sizeof(pthread_cond_t));
    pthread_cond_init( (pthread_cond_t *) nativeHandle, NULL );
}

/**
 * @brief Destroys the ConditionVariable and releases all resources.
 *
 * When allocated, destroys and frees the native @c pthread_cond_t, then deletes
 * the backing Mutex and Condition objects.
 */
ConditionVariable::~ConditionVariable()
{
	if (nativeHandle != NULL) {
		pthread_cond_destroy((pthread_cond_t *) nativeHandle);
		Free(nativeHandle);
	}
	delete mutex;
	delete cond;
}

/**
 * @brief Sets the associated condition state under lock.
 *
 * Acquires the internal mutex, sets the backing Condition, then releases the
 * mutex. Does not wake waiters; use notify()/notifyAll() to signal threads.
 *
 * @see reset(), isSet(), notify()
 */
void ConditionVariable::set(void)
{
	mutex->lock();
	cond->set();
	mutex->unlock();
}

/**
 * @brief Resets (clears) the associated condition state under lock.
 *
 * Acquires the internal mutex, clears the backing Condition, then releases the mutex.
 *
 * @see set(), isSet()
 */
void ConditionVariable::reset(void)
{
	mutex->lock();
	cond->reset();
	mutex->unlock();
}

/**
 * @brief Returns the current condition state under lock.
 *
 * Acquires the internal mutex, reads the backing Condition state, releases the
 * mutex, and returns the value.
 *
 * @return The current condition state.
 * @retval true   The condition is set.
 * @retval false  The condition is not set.
 */
bool ConditionVariable::isSet(void)
{
	mutex->lock();
	bool set = cond->isSet();
	mutex->unlock();
	return set;
}

/**
 * @brief Waits indefinitely until the condition is set.
 *
 * Convenience overload that delegates to wait(long) with a timeout of 0,
 * blocking the calling thread until another thread sets and signals the condition.
 *
 * @see wait(long), notify(), notifyAll()
 */
void ConditionVariable::wait(void)
{
	wait(0);
}

/**
 * @brief Waits for the condition to be set, optionally bounded by a timeout.
 *
 * Acquires the internal mutex and blocks until the backing Condition is set.
 * When @p timeout is 0 the call waits indefinitely via @c pthread_cond_wait();
 * otherwise it computes an absolute wake time from the current time of day and
 * waits with @c pthread_cond_timedwait(), returning early on @c ETIMEDOUT.
 * The mutex is released before returning.
 *
 * @note Parameter and return semantics are documented on the declaration in
 *       ConditionVariable.hpp; this block records the implementation behavior.
 * @note Timeout-to-timespec conversion advances tv_sec by (timeout / 1000) and
 *       sets tv_nsec to the current microseconds plus (timeout % 1000) * 1000000.
 *       The one-second carry is applied only when tv_nsec is strictly greater
 *       than 1,000,000,000, so a value of exactly 1,000,000,000 nanoseconds is
 *       left in place, forming an invalid timespec that pthread_cond_timedwait()
 *       rejects with EINVAL.
 * @note A negative timeout yields a wake time at or before now (negative seconds
 *       and/or nanoseconds), so the timed wait does not block for any positive
 *       duration.
 * @warning The wait loop treats only ETIMEDOUT as a timeout and breaks on it; any
 *          other non-zero return (for example EINVAL from the invalid timespec
 *          above) is ignored while the condition remains unset, so the loop
 *          re-arms the same absolute wake time and busy-spins on
 *          pthread_cond_timedwait(). Documented as-is; the production code is
 *          unchanged.
 */
long ConditionVariable::wait(long timeout)
{
    long timeLeft = 1;
    mutex->lock();

    int ret = 0;
    if (timeout == 0) {
        while (!cond->isSet()) {
            ret = pthread_cond_wait(
                (pthread_cond_t *)nativeHandle,
                (pthread_mutex_t *)mutex->getNativeHandle()
            );
            if (ret < 0) {
                // @TODO Throw Exception
                break;
            }
        }
    } else {
        struct timeval curTime;
        memset(&curTime, 0, sizeof(curTime));
        gettimeofday(&curTime, NULL);

        struct timespec wakeTime;
        memset(&wakeTime, 0, sizeof(wakeTime));
        wakeTime.tv_nsec = curTime.tv_usec * 1000 + (timeout % 1000) * 1000000;
        wakeTime.tv_sec = curTime.tv_sec + (timeout / 1000);
        if (wakeTime.tv_nsec > 1000000000) {
            wakeTime.tv_nsec -= 1000000000;
            wakeTime.tv_sec++;
        }

        while (!cond->isSet()) {
            ret = pthread_cond_timedwait(
                (pthread_cond_t *)nativeHandle,
                (pthread_mutex_t *)mutex->getNativeHandle(),
                &wakeTime
            );

            if ((ret != 0) && !cond->isSet() && ret == ETIMEDOUT) {
                timeLeft = 0;
                break;
            }
        }
    }

    mutex->unlock();
    return timeLeft;
}

/**
 * @brief Sets the condition and wakes a single waiting thread.
 *
 * Acquires the internal mutex, sets the backing Condition, signals one waiter
 * via @c pthread_cond_signal(), then releases the mutex.
 *
 * @see notifyAll(), wait(long)
 */
void ConditionVariable::notify(void)
{
	mutex->lock();
	cond->set();
	pthread_cond_signal((pthread_cond_t *)nativeHandle);
	mutex->unlock();
}

/**
 * @brief Sets the condition and wakes all waiting threads.
 *
 * Acquires the internal mutex, sets the backing Condition, broadcasts to all
 * waiters via @c pthread_cond_broadcast(), then releases the mutex.
 *
 * @see notify(), wait(long)
 */
void ConditionVariable::notifyAll(void)
{
	mutex->lock();
	cond->set();
	pthread_cond_broadcast((pthread_cond_t *)nativeHandle);
	mutex->unlock();
}

/**
 * @brief Returns the opaque native condition-variable handle.
 *
 * @return Pointer to the underlying native @c pthread_cond_t, returned as a
 *         @c void* opaque handle (may be NULL if not initialized).
 */
void * ConditionVariable::getNativeHandle(void)
{
	return nativeHandle;
}

CCEC_OSAL_END_NAMESPACE


/** @} */
/** @} */
