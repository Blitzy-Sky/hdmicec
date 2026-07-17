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


#ifndef HDMI_CCEC_MESSAGE_ENCODER_H_
#define HDMI_CCEC_MESSAGE_ENCODER_H_

#include "CCEC.hpp"
#include "ccec/CECFrame.hpp"
#include "DataBlock.hpp"
#include "OpCode.hpp"
#include "Header.hpp"

CCEC_BEGIN_NAMESPACE

/**
 * @brief High-level messages are encoded by the MessageEncoder into raw bytes and placed in a CECFrame.
 *
 * A CECFrame is then sent out via an opened connection.
 * @ingroup HDMI_CEC_MSG_N_FRAME_CLASSES
 */
class MessageEncoder {

public:
	/**
	 * @brief Constructs a MessageEncoder instance.
	 *
	 * MessageEncoder exposes only static encode() helpers and therefore holds no
	 * instance state; the default constructor is provided for completeness.
	 */
	MessageEncoder(void) {};
	/**
	 * @brief Destroys the MessageEncoder instance.
	 *
	 * A MessageEncoder owns no resources, so destruction performs no work.
	 */
	~MessageEncoder(void) {};


	/**
	 * @brief Encodes a CEC message header together with a data block into an existing CECFrame.
	 *
	 * Serializes the header bytes first and then delegates to the data-block overload,
	 * which appends the opcode and its operands, so that the caller-supplied frame ends
	 * up holding the complete CEC message.
	 *
	 * @param[in]     h    CEC message header carrying the initiator and destination logical addresses.
	 * @param[in]     m    CEC data block carrying the opcode and its operands.
	 * @param[in,out] out  The @c CECFrame to which the encoded header and data-block bytes are appended.
	 * @return Reference to @p out, now holding the fully encoded CEC message, to support call chaining.
	 */
	static CECFrame & encode(const Header &h, const DataBlock &m, CECFrame &out)
    {
        h.serialize(out);
        return encode(m, out);
    }

	/**
	 * @brief Encodes a CEC message header together with a data block into a new CECFrame.
	 *
	 * Allocates a fresh CECFrame, serializes the header followed by the data block's
	 * opcode and operands into it, and returns the frame by value.
	 *
	 * @param[in] h  CEC message header carrying the initiator and destination logical addresses.
	 * @param[in] m  CEC data block carrying the opcode and its operands.
	 * @return A newly constructed @c CECFrame containing the fully encoded CEC message.
	 */
	static CECFrame encode(const Header &h, const DataBlock &m)
    {
        CECFrame out;
        h.serialize(out);
        return encode(m, out);
    }

	/**
	 * @brief Encodes a CEC data block into an existing CECFrame.
	 *
	 * Serializes the data block's opcode followed by its operands, appending the
	 * resulting bytes to the caller-supplied frame.
	 *
	 * @param[in]     m    CEC data block carrying the opcode and its operands.
	 * @param[in,out] out  The @c CECFrame to which the encoded opcode and operand bytes are appended.
	 * @return Reference to @p out, now holding the encoded data block, to support call chaining.
	 */
	static CECFrame & encode(const DataBlock &m, CECFrame &out)
	{
        OpCode(m.opCode()).serialize(out);
        m.serialize(out);
        return out;
	}

	/**
	 * @brief Encodes a CEC data block into a new CECFrame.
	 *
	 * Allocates a fresh CECFrame and serializes the data block's opcode followed by
	 * its operands into it, returning the frame by value.
	 *
	 * @param[in] m  CEC data block carrying the opcode and its operands.
	 * @return A newly constructed @c CECFrame containing the encoded data block.
	 */
	static CECFrame encode(const DataBlock &m) {
        CECFrame out;
        return encode(m, out);
    }
};

CCEC_END_NAMESPACE
#endif


/** @} */
/** @} */
