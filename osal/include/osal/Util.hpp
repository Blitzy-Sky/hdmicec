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

/**
 * @file
 * @brief OSAL allocation-alias utility header.
 *
 * Provides thin aliases (@ref Malloc, @ref Free) over the standard C library
 * allocation routines used throughout the OSAL layer.
 */

#ifndef HDMI_CCEC_OSAL_UTIL_
#define HDMI_CCEC_OSAL_UTIL_

#include "OSAL.hpp"

/**
 * @def Malloc
 * @brief Alias for the standard C library `malloc` used by the OSAL layer.
 */
/**
 * @def Free
 * @brief Alias for the standard C library `free` used by the OSAL layer.
 */
CCEC_OSAL_BEGIN_NAMESPACE

#define Malloc malloc
#define Free free

CCEC_OSAL_END_NAMESPACE

#endif


/** @} */
/** @} */
