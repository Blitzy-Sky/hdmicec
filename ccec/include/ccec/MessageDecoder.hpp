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


#ifndef HDMI_CCEC_MESSAGE_DECODER_HPP_
#define HDMI_CCEC_MESSAGE_DECODER_HPP_

#include "CCEC.hpp"

CCEC_BEGIN_NAMESPACE

class MessageProcessor;
class CECFrame;

/**
 * @brief When receiving the message, the raw bytes arrived in a CECFrame are converted to the
 * corresponding High-level message by MessageDecoder and then dispatched for processing via class
 * MessageProcessor or its extensions.
 * @ingroup HDMI_CEC_MSG_N_FRAME_CLASSES
 */
class MessageDecoder
{
public:
	/**
	 * @brief Binds the decoder to the @c MessageProcessor that will handle decoded messages.
	 *
	 * Stores the supplied processor reference for later use by decode(), which dispatches
	 * every decoded message to it. The reference is retained for the lifetime of this
	 * MessageDecoder, so the referenced @c MessageProcessor must outlive the decoder.
	 *
	 * @param[in] processor Reference to the @c MessageProcessor that receives the dispatched, decoded messages.
	 */
	MessageDecoder(MessageProcessor & processor) : processor(processor){};
	/**
	 * @brief Decodes a raw received CEC frame and dispatches the resulting message.
	 *
	 * Parses @p in as a CEC message (header byte, then opcode, then any operands),
	 * constructs the corresponding high-level typed message, and dispatches it to the
	 * bound @c MessageProcessor by invoking the matching process() overload. A
	 * single-byte frame is treated as a Polling message.
	 *
	 * @param[in] in The raw received CEC frame to decode and dispatch.
	 *
	 * @note Exceptions raised while parsing operands (for example @c InvalidParamException)
	 *       are caught and logged internally and are not propagated to the caller.
	 */
	void decode(const CECFrame &in);

private:
	MessageProcessor &processor;
};

CCEC_END_NAMESPACE
#endif


/** @} */
/** @} */
