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


#ifndef HDMI_CCEC_HEADER_BLOCK_
#define HDMI_CCEC_HEADER_BLOCK_

#include "CCEC.hpp"
#include "Assert.hpp"
#include "DataBlock.hpp"
#include "Operands.hpp"
#include "Util.hpp"

CCEC_BEGIN_NAMESPACE

/**
 * @brief Header block of a CEC message.
 *
 * The first byte of every HDMI-CEC frame is a header that carries the two
 * logical addresses of the exchange: the initiator (source) is packed into
 * the upper nibble (bits 7..4) and the follower (destination) into the lower
 * nibble (bits 3..0). This class models that single-byte header, supporting
 * construction from explicit logical addresses, decoding from a received
 * @c CECFrame, and serialization back into a frame.
 */
class Header 
{
public:
	enum {
		MAX_LEN = 1,  ///< Encoded length of a CEC header in a frame: 1 byte (source nibble + destination nibble).
	};

	/**
	 * @brief Constructs a header from explicit source and destination logical addresses.
	 *
	 * @param[in] from The initiator (source) logical address of the CEC message.
	 * @param[in] to   The follower (destination) logical address of the CEC message.
	 *
	 * @note This constructor logs the constructed header by calling print().
	 */
	Header(const LogicalAddress &from, const LogicalAddress &to) : from(from), to(to) {
        print();
    };

	/**
	 * @brief Decodes a header from a byte within a CEC frame.
	 *
	 * Reads the header byte at @p startPos in @p frame and splits it into its two
	 * logical addresses: the source is taken from the upper nibble (bits 7..4)
	 * and the destination from the lower nibble (bits 3..0).
	 *
	 * @param[in] frame    The CEC frame containing the encoded header byte.
	 * @param[in] startPos Byte offset of the header within @p frame (defaults to 0).
	 */
	Header(const CECFrame &frame, size_t startPos = 0) {
        from = LogicalAddress((frame.at(startPos) & 0xF0) >> 4);
        to   = LogicalAddress((frame.at(startPos) & 0x0F) >> 0);
	}

	/**
	 * @brief Serializes the header into a CEC frame.
	 *
	 * Appends the single header byte to @p frame, packing the source logical
	 * address into the upper nibble (bits 7..4) and the destination logical
	 * address into the lower nibble (bits 3..0).
	 *
	 * @param[in,out] frame The frame to which the encoded header byte is appended.
	 *
	 * @return Reference to the same @c CECFrame passed in @p frame, to support call chaining.
	 */
	CECFrame &serialize(CECFrame &frame) const {
		CCEC_LOG( LOG_DEBUG, "Serialing header  %s   %s \n",from.toString().c_str(),to.toString().c_str());
        /* serialize to same byte */
        to.serialize(frame);
        uint8_t &byte = frame[frame.length() - 1];
        Assert(byte == to.toInt());
        byte = from.toInt() << 4 | byte;
        return frame;
	}

	/**
	 * @brief Destroys the header.
	 */
	virtual ~Header(void) {};

    /**
     * @brief Logs the header's source and destination logical addresses.
     */
    void print(void) const {
        CCEC_LOG( LOG_DEBUG, "Header : From : %s \n", from.toString().c_str());
        CCEC_LOG( LOG_DEBUG, "Header : to   : %s \n", to.toString().c_str());
    }


public:
	LogicalAddress from;  ///< Source (initiator) logical address carried in the header.
	LogicalAddress to;  ///< Destination (follower) logical address carried in the header.
};


CCEC_END_NAMESPACE

#endif



/** @} */
/** @} */
