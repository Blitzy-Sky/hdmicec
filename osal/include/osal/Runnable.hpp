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


#ifndef HDMI_CCEC_OSAL_RUNNABLE_HPP_
#define HDMI_CCEC_OSAL_RUNNABLE_HPP_

#include "OSAL.hpp"

CCEC_OSAL_BEGIN_NAMESPACE

/**
 * @brief Interface for tasks that can be executed by a Thread.
 *
 * A class that requires thread functionality implements this interface and is
 * passed to a Thread on construction. The bound Thread drives run() either
 * synchronously via Thread::run() (on the calling thread) or asynchronously via
 * Thread::start() (on a new thread of execution).
 */
class Runnable {

public:
/**
 * @brief Pure-virtual work entry point implemented by tasks.
 *
 * Implementations perform the task's work in this method. It is invoked by the
 * bound Thread, and its execution context depends on how it is driven:
 * synchronously on the calling thread when invoked via Thread::run(), or
 * asynchronously on a new thread of execution when invoked via Thread::start().
 * This interface itself does not guarantee a single execution context.
 * @see Thread::run(), Thread::start()
 */
	virtual void  run(void) = 0;
};

CCEC_OSAL_END_NAMESPACE

#endif


/** @} */
/** @} */
