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
\brief This file defines interface of Condition class.

*/
/*****************************************************************************/


/**
* @defgroup hdmicec
* @{
* @defgroup osal
* @{
**/


#ifndef HDMI_CCEC_OSAL_CONDITION_HPP_
#define HDMI_CCEC_OSAL_CONDITION_HPP_

#include "OSAL.hpp"

CCEC_OSAL_BEGIN_NAMESPACE

/**
 * @brief Simple boolean state abstraction used by ConditionVariable.
 *
 * This is a simple class which abstract boolean functionality and is used by
 * ConditionVariable class.
 */

class Condition {
public:
/**
 * @brief Default constructor.
 *
 * Creates an Condition object with state set to false.
 *
 * @note This constructor initializes only the @c cond state (to false); it does
 *       not initialize the @c initial member. As a result @c initial is left
 *       indeterminate after default construction, which in turn makes reset()
 *       restore an indeterminate value (see reset()). Documented as-is; the
 *       production code is not modified.
 */

	Condition(void) : cond(false) {};
/**
 * @brief Constructor with an explicit initial state.
 *
 * Creates an Condition object with state set to the given parameter.
 *
 * @param[in] initial - initial state to be set. This will be set as default
 *            state of the object as well (it is stored and later restored by
 *            reset()).
 */

	Condition(bool initial) : cond(initial) {this->initial = initial;};
/**
 * @brief Destructor.
 *
 * Destroys the Condition object.
 */

	virtual ~Condition() {};

/**
 * @brief Set the state of the object to true.
 *
 * Unconditionally sets the boolean state of the object to true. This overload
 * takes no parameters.
 */

	virtual void set(void) {cond = true;};
	
/**
 * @brief Check the state of the object.
 *
 * Returns the state of the object, which will be either true/false.
 *
 * @return true if set and false if not set.
 */
	virtual bool isSet(void) {return cond;};
	
/**
 * @brief Resets the state of the object.
 *
 * Resets the state of the object to the default value captured at construction
 * time.
 *
 * @note The restored value is @c initial. @c initial is only assigned by the
 *       Condition(bool) constructor; the default constructor Condition(void)
 *       does not initialize it. Therefore, after default construction, reset()
 *       assigns an indeterminate (uninitialized) value to @c cond, which is
 *       undefined behavior. Documented as-is; the production code is not
 *       modified.
 */
	
	virtual void reset(void) {cond = initial;};
private:
	bool initial;
	bool cond;
};

CCEC_OSAL_END_NAMESPACE

#endif


/** @} */
/** @} */
