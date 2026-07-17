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


#ifndef HDMI_CCEC_ASSERT_HPP_
#define HDMI_CCEC_ASSERT_HPP_
#include "CCEC.hpp"

#include <assert.h>
#include <cstdio>
#include <stdexcept>

/**
 * @def Assert
 * @brief Project assertion macro that logs the failing expression's source location, then calls the standard C @c assert().
 *
 * This is the active assertion facility for the CCEC middleware. When @p expr
 * evaluates to @c false the macro prints a diagnostic of the form
 * <tt>[Assert ] failed at [file][line]</tt>, embedding the source file
 * (@c __FILE__) and line number (@c __LINE__), and then invokes the standard C
 * library @c assert(expr). The macro expands to a <tt>do { ... } while (0)</tt>
 * block, so it behaves as a single statement and yields no value.
 *
 * @param expr Boolean expression expected to be @c true; when it evaluates to
 *             @c false the diagnostic is printed and @c assert(expr) is invoked.
 *
 * @note An alternative class-based @c Assert implementation is retained under the
 *       disabled <tt>#if 0</tt> block below and is intentionally compiled out; the
 *       macro form below is the one in effect.
 */
CCEC_BEGIN_NAMESPACE

#if 0
class Assert {
public:
	Assert(bool isTrue) : cond(isTrue) {
		if (!isTrue) {
            assert(isTrue);
		}
	}
private:
	const bool cond;
};
#else

#define Assert(expr) do{\
	if (!(expr)) printf("[%s] failed at [%s][%d]\r\n", "Assert ", __FILE__, __LINE__);\
	assert(expr);\
} while (0)
#endif

CCEC_END_NAMESPACE
#endif


/** @} */
/** @} */
