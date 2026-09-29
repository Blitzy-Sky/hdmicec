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
	 * Parses @p in as a CEC message and dispatches it to the bound
	 * @c MessageProcessor by invoking the matching process() overload. The header
	 * byte (offset 0) is parsed first, then the opcode byte (offset 1), then any
	 * operands (from offset 2). A single-byte frame (length 1) is treated as a
	 * Polling message and dispatched immediately, before opcode decoding.
	 *
	 * Dispatch coverage (documents the decoder exactly as implemented):
	 * - Supported / dispatched: 38 message types reach a process() overload - 37
	 *   opcode cases in the decode switch plus the Polling path above.
	 * - Recognized but ignored: four vendor opcodes have a case label whose
	 *   process() call is commented out, so they are matched and then dropped with
	 *   no action - `VENDOR_COMMAND`, `VENDOR_COMMAND_WITH_ID`,
	 *   `VENDOR_REMOTE_BUTTON_DOWN` and `VENDOR_REMOTE_BUTTON_UP`.
	 * - Unsupported: five message classes have neither a decode case nor a
	 *   MessageProcessor overload and are never produced by the decoder -
	 *   GiveAudioStatus, RequestArcInitiation, ReportArcInitiation,
	 *   RequestArcTermination and ReportArcTermination.
	 * - Unknown opcodes: the switch `default` logs "Unhandled Message Received"
	 *   and prints the opcode; no message is dispatched.
	 *
	 * @param[in] in The raw received CEC frame to decode and dispatch.
	 *
	 * @warning Exception boundary: the header (`Header(in, 0)`) is constructed
	 *          BEFORE the internal try block, so an exception thrown there is NOT
	 *          caught and propagates to the caller. In particular, an empty frame
	 *          makes the header byte read out of range and throws
	 *          `std::out_of_range`, which escapes decode().
	 * @note Caught exception categories (documents the two internal catch handlers
	 *       exactly): an exception thrown INSIDE the try block is caught only if it
	 *       is an `InvalidParamException` or derives from `std::exception` (for
	 *       example `std::out_of_range` raised while parsing operands). Because
	 *       every CCEC exception type derives from `std::exception`, all of them are
	 *       caught here. Caught exceptions are logged internally (the
	 *       `std::exception` handler also hex-dumps the frame) and are NOT
	 *       propagated to the caller.
	 * @note A throwable that does NOT derive from `std::exception` (for example a
	 *       thrown integer or other non-standard exception type) matches neither
	 *       handler and therefore escapes decode() to the caller.
	 */
	void decode(const CECFrame &in);

private:
	MessageProcessor &processor;
};

CCEC_END_NAMESPACE
#endif


/** @} */
/** @} */
