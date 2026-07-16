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


#ifndef HDMI_CCEC_OSAL_EXCEPTION_HPP_
#define HDMI_CCEC_OSAL_EXCEPTION_HPP_
#include "OSAL.hpp"

CCEC_OSAL_BEGIN_NAMESPACE

/**
 * @brief Base exception type for the OSAL layer.
 *
 * Derives from std::exception and serves as the root of the OSAL exception hierarchy.
 */
class OSException : public std::exception
{
public:
	/**
	 * @brief Returns a human-readable description of the exception.
	 * @return The fixed C-string literal "Base Exception..".
	 */
	virtual const char* what() const noexcept
	{
		return "Base Exception..";
	}
private:
};


/**
 * @brief Exception indicating that a requested OSAL operation is not supported.
 */
class OperationNotSupportedException : public OSException
{
public:
	/**
	 * @brief Returns a human-readable description of the exception.
	 * @return The fixed C-string literal "Operation Not Supported..".
	 */
	virtual const char* what() const noexcept
	{
		return "Operation Not Supported..";
	}
};


/**
 * @brief Exception indicating that an OSAL object is in an invalid state for the requested operation.
 */
class InvalidStateException : public OSException
{
public:
	/**
	 * @brief Returns a human-readable description of the exception.
	 * @return The fixed C-string literal "Invalid State Exception..".
	 */
	virtual const char* what() const noexcept
	{
		return "Invalid State Exception..";
	}
};

CCEC_OSAL_END_NAMESPACE


#endif


/** @} */
/** @} */
