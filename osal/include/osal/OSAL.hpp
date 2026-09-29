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
 * @brief OSAL namespace-control scaffolding and include guard for the HDMI-CEC OSAL layer.
 *
 * Defines the include guard and the namespace-selection macros
 * (@ref CCEC_OSAL_NAMESPACE, @ref CCEC_OSAL_BEGIN_NAMESPACE, @ref CCEC_OSAL_END_NAMESPACE)
 * that wrap all OSAL declarations in the `CCEC_OSAL` C++ namespace when enabled.
 */
#ifndef HDMI_CCEC_OSAL_HPP_
#define HDMI_CCEC_OSAL_HPP_

/**
 * @def CCEC_OSAL_NAMESPACE
 * @brief When defined, selects the namespaced OSAL build so that all OSAL
 *        declarations are wrapped in the `CCEC_OSAL` C++ namespace.
 */
#define CCEC_OSAL_NAMESPACE

#ifdef CCEC_OSAL_NAMESPACE
/**
 * @def CCEC_OSAL_BEGIN_NAMESPACE
 * @brief Opens the OSAL namespace scope. Expands to `namespace CCEC_OSAL {`
 *        when @ref CCEC_OSAL_NAMESPACE is defined; expands to nothing otherwise.
 */
#define CCEC_OSAL_BEGIN_NAMESPACE namespace CCEC_OSAL {
/**
 * @def CCEC_OSAL_END_NAMESPACE
 * @brief Closes the OSAL namespace scope. Expands to `}` when
 *        @ref CCEC_OSAL_NAMESPACE is defined; expands to nothing otherwise.
 */
#define CCEC_OSAL_END_NAMESPACE }
#else
#define CCEC_OSAL_BEGIN_NAMESPACE
#define CCEC_OSAL_END_NAMESPACE
#endif

CCEC_OSAL_BEGIN_NAMESPACE

CCEC_OSAL_END_NAMESPACE
#endif


/** @} */
/** @} */
