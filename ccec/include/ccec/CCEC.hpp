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


#ifndef HDMI_CCEC_HPP_
#define HDMI_CCEC_HPP_

/**
 * @file CCEC.hpp
 * @brief Namespace-macro anchor for the CCEC (Consumer CEC) subsystem.
 *
 * This header establishes the optional @c CCEC C++ namespace scaffolding that
 * is shared across the CCEC middleware. It is included first by the other CCEC
 * public headers so that the namespace wrapper macros are defined before any
 * CCEC type or function is declared. The header contains preprocessor
 * definitions only; it declares no classes or functions.
 *
 * @see CCEC_BEGIN_NAMESPACE
 * @see CCEC_END_NAMESPACE
 */

/**
 * @def CEC_NAMESPACE
 * @brief Feature marker that enables the CEC namespace scaffolding for the module.
 *
 * Defined unconditionally by this header as an empty object-like macro to flag
 * that the CCEC namespace scaffolding is available to translation units that
 * include it.
 */
#define CEC_NAMESPACE

/**
 * @def CCEC_BEGIN_NAMESPACE
 * @brief Opens the @c CCEC namespace scope for CCEC declarations.
 *
 * When @c CCEC_NAMESPACE is defined, this macro expands to
 * <tt>namespace CCEC {</tt>, wrapping all subsequent CCEC declarations in the
 * @c CCEC namespace; otherwise it expands to nothing and the declarations are
 * emitted in the global namespace.
 *
 * @def CCEC_END_NAMESPACE
 * @brief Closes the namespace scope opened by @c CCEC_BEGIN_NAMESPACE.
 *
 * When @c CCEC_NAMESPACE is defined, this macro expands to the closing
 * <tt>}</tt> of the @c CCEC namespace; otherwise it expands to nothing. Every
 * CCEC header pairs @c CCEC_BEGIN_NAMESPACE with @c CCEC_END_NAMESPACE to
 * bracket its declarations.
 */
#ifdef CCEC_NAMESPACE
#define CCEC_BEGIN_NAMESPACE namespace CCEC {
#define CCEC_END_NAMESPACE }
#else
#define CCEC_BEGIN_NAMESPACE
#define CCEC_END_NAMESPACE
#endif

#endif


/** @} */
/** @} */
