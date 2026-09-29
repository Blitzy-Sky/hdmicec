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
* @defgroup ccec CCEC Library
* @{
**/


#ifndef HDMI_CCEC_OPERAND_HPP_
#define HDMI_CCEC_OPERAND_HPP_

#include <stdint.h>

#include <vector>
#include <string>


#include "CCEC.hpp"
#include "ccec/CECFrame.hpp"

CCEC_BEGIN_NAMESPACE

class CECFrame;

/**
 * @brief Abstract base class for a single CEC message operand.
 *
 * An operand is a typed field within the payload of a CEC (Consumer
 * Electronics Control) message. Each concrete subclass represents one
 * operand type and knows how to serialize its value into the byte stream
 * of a @c CECFrame as well as how to describe itself. This class defines
 * the common contract that every operand implementation shares.
 *
 * @see CECFrame
 */
class Operand
{
public:
    /**
     * @brief Appends this operand's byte encoding to the supplied frame.
     *
     * Concrete operands implement this method to emit their value as one or
     * more bytes onto the end of @p frame, following the CEC wire format
     * defined for the operand type.
     *
     * @param[in,out] frame The frame to which the operand's encoded bytes are appended.
     * @return Reference to the same @p frame, enabling call chaining.
     * @note This method is pure virtual; every concrete operand must implement it.
     */
    virtual CECFrame &serialize(CECFrame &frame) const = 0; 

    /**
     * @brief Returns a human-readable string describing this operand.
     *
     * @return A descriptive string for the operand. The base-class implementation
     *         returns "Not Implemented" and is overridden by concrete operands.
     */
    virtual const std::string toString(void) const {
    	return "Not Implemented";
    }
    /**
     * @brief Returns the operand's human-readable type name.
     *
     * @return A string naming the operand. The base-class implementation returns "Operand".
     */
    virtual const std::string name(void) const {
    	return "Operand";
    }
    /**
     * @brief Reports whether the operand's current value is valid per the CEC specification.
     *
     * @retval true  The operand's value is valid.
     * @retval false The operand's value is invalid.
     */
    virtual bool validate(void) const {
    	return true;
    }
    /**
     * @brief Serializes this operand into a newly created frame.
     *
     * Convenience overload that constructs a new, empty @c CECFrame and
     * delegates to @c serialize(CECFrame&) const to populate it with this
     * operand's bytes.
     *
     * @return A new @c CECFrame containing this operand's encoded bytes.
     */
    CECFrame serialize(void) const {
        CECFrame frame;
        return serialize(frame);
    }
};

/**
 * @brief Addressing-mode flags identifying the message contexts in which an operand is valid.
 */
enum
{
	BROADCAST = (0x01),      ///< Operand is valid in broadcast (unaddressed) messages.
	UNICAST   = (0x01 << 1), ///< Operand is valid in directly-addressed (unicast) messages.
};


/*
 * BYTE_TO_BCD(byte_): converts a binary byte value (typically 0-99) to its
 * Binary-Coded Decimal (BCD) representation - the tens digit is packed into
 * the high nibble and the units digit into the low nibble of the result.
 *
 * This macro is also defined identically in ccec/Util.hpp. To avoid a
 * duplicate generated API entry, the canonical Doxygen documentation lives in
 * ccec/Util.hpp (see BYTE_TO_BCD there); this block is deliberately a plain
 * non-Doxygen comment so only the single canonical entry is generated.
 * Comment only - the macro definition below is unchanged.
 */
#define BYTE_TO_BCD(byte_) (((((byte_) / 10) & 0x0F) << 4) | (((byte_) % 10) & 0x0F))
/*
 * BCD_TO_BYTE(byte_): converts a Binary-Coded Decimal (BCD) byte to its binary
 * value - the tens digit is taken from the high nibble and the units digit
 * from the low nibble of byte_, combined as ((high * 10) + low).
 *
 * This macro is also defined identically in ccec/Util.hpp. To avoid a
 * duplicate generated API entry, the canonical Doxygen documentation lives in
 * ccec/Util.hpp (see BCD_TO_BYTE there); this block is deliberately a plain
 * non-Doxygen comment so only the single canonical entry is generated.
 * Comment only - the macro definition below is unchanged.
 */
#define BCD_TO_BYTE(byte_) (((((byte_) & 0xF0) >> 4) * 10) + (((byte_) & 0x0F)))

CCEC_END_NAMESPACE
#endif


/** @} */
/** @} */
