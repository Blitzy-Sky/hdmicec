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


#ifndef HDMI_CCEC_EXCEPTION_HPP_
#define HDMI_CCEC_EXCEPTION_HPP_
#include "CCEC.hpp"


CCEC_BEGIN_NAMESPACE

/**
 * @brief Empty tag type used to select the "throw on error" overloads of the CCEC APIs.
 *
 * @c Throw_e carries no data members; it exists purely as a disambiguating parameter type.
 * In this repository it is used only by the overloaded @c Connection send/sendTo/poll/ping
 * operations: passing a @c Throw_e argument selects the variant that reports failures by
 * throwing a CCEC @c Exception, as opposed to the variant that reports failure through a
 * return value. Its presence in a function signature therefore signals "throw on error"
 * semantics at the call site.
 */
typedef struct _Throw_e{
	/*Empty Struct */
}Throw_e;


/**
 * @brief Base class for all CCEC exceptions.
 *
 * Serves as the common root of the CCEC exception hierarchy and derives from
 * @c std::exception so that CCEC errors integrate with standard C++ exception handling.
 * Specific error conditions are represented by the derived types (for example
 * @c CECNoAckException or @c IOException), each of which overrides what() to return a
 * type-specific, human-readable description.
 */
class Exception : public std::exception
{
public:
	/**
	 * @brief Returns a human-readable description of the exception.
	 *
	 * @return A null-terminated C string describing the error. For the base class this is
	 *         the literal message "Base Exception..".
	 */
	virtual const char* what() const noexcept
	{
		return "Base Exception..";
	}
private:
};

/**
 * @brief Exception raised when a transmitted CEC message is not acknowledged by the follower.
 *
 * Indicates that a CEC frame was placed on the bus but the addressed follower device did not
 * acknowledge (ACK) it. This establishes only that the frame was not acknowledged; it does
 * not by itself prove whether the intended target received the frame.
 */
class CECNoAckException : public Exception
{
public:
	/**
	 * @brief Returns a human-readable description of the exception.
	 *
	 * @return A null-terminated C string; for this type the literal message "Ack not received..".
	 */
	virtual const char* what() const noexcept
	{
		return "Ack not received..";
	}
};

/**
 * @brief Exception raised when a requested operation is not supported.
 *
 * Thrown when a caller invokes functionality that the current CCEC configuration or the
 * underlying HAL implementation does not provide.
 */
class OperationNotSupportedException : public Exception
{
public:
	/**
	 * @brief Returns a human-readable description of the exception.
	 *
	 * @return A null-terminated C string; for this type the literal message "Operation Not Supported..".
	 */
	virtual const char* what() const noexcept
	{
		return "Operation Not Supported..";
	}
};


/**
 * @brief Exception raised when an I/O failure occurs at the HAL/driver boundary.
 *
 * Signals that a read or write against the underlying CEC HAL/driver failed, for example
 * when opening the device, transmitting a frame, or reading incoming data.
 */
class IOException : public Exception
{
public:
	/**
	 * @brief Returns a human-readable description of the exception.
	 *
	 * @return A null-terminated C string; for this type the literal message "IO Exception..".
	 */
	virtual const char* what() const noexcept
	{
		return "IO Exception..";
	}
};


/**
 * @brief Exception raised when an API is called while the module is in an invalid or uninitialized state.
 *
 * Thrown when an operation is attempted out of sequence, for example before the CCEC library
 * has been initialized or after it has been terminated.
 */
class InvalidStateException : public Exception
{
public:
	/**
	 * @brief Returns a human-readable description of the exception.
	 *
	 * @return A null-terminated C string; for this type the literal message "Invalid State Exception..".
	 */
	virtual const char* what() const noexcept
	{
		return "Invalid State Exception..";
	}
};

/**
 * @brief Exception raised when a parameter fails validation.
 *
 * Thrown when an argument supplied to a CCEC API is out of range, malformed, or otherwise
 * fails the operation's precondition checks.
 */
class InvalidParamException : public Exception
{
public:
	/**
	 * @brief Returns a human-readable description of the exception.
	 *
	 * @return A null-terminated C string; for this type the literal message "Invalid Param Exception..".
	 */
	virtual const char* what() const noexcept
	{
		return "Invalid Param Exception..";
	}
};

/**
 * @brief Exception raised when no logical address is available or can be allocated.
 *
 * Thrown when the CCEC layer is unable to obtain or assign a CEC logical address, for example
 * when all candidate addresses for the device type are already in use on the bus.
 */
class AddressNotAvailableException : public Exception
{
public:
	/**
	 * @brief Returns a human-readable description of the exception.
	 *
	 * @return A null-terminated C string; for this type the literal message "Address Not Available Exception..".
	 */
	virtual const char* what() const noexcept
	{
		return "Address Not Available Exception..";
	}
};

CCEC_END_NAMESPACE


#endif


/** @} */
/** @} */
