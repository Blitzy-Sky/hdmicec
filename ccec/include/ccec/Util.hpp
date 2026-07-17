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


#ifndef HDMI_CCEC_UTIL_HPP_
#define HDMI_CCEC_UTIL_HPP_

#include "ccec/CCEC.hpp"

CCEC_BEGIN_NAMESPACE
class CECFrame;

/**
 * @brief Inert "no-operation" adapter exposing a CEC message-like
 *        serialize/deserialize/print surface where every operation does nothing.
 *
 * Noop is a null-object placeholder used wherever a do-nothing handler is
 * required in place of a real CEC message type: @c deserialize() consumes no
 * bytes, @c serialize() returns the frame unchanged, and @c print() emits no output.
 */
class Noop
{
public:
	/**
	 * @brief No-op deserialize; ignores the input frame and consumes no bytes.
	 * @param[in] frame    CEC frame a real implementation would parse; ignored here.
	 * @param[in] startPos Byte offset within @p frame at which parsing would begin;
	 *                     ignored (defaults to 0).
	 * @return Always 0, indicating that no bytes were consumed.
	 */
	inline size_t deserialize(const CECFrame &frame, size_t startPos = 0) {
		/* Noop */
		return 0;
	}
    /**
     * @brief No-op serialize; returns the supplied frame unchanged.
     * @param[in,out] frame CEC frame that a real implementation would append to;
     *                      left unmodified by this no-op.
     * @return Reference to the same @p frame, unchanged.
     */
    inline CECFrame &serialize(CECFrame &frame) const {
		/* Noop */
    	return frame;
    }

    /**
     * @brief No-op print; produces no output.
     */
    void print(void) const {

    }
};


/**
 * @def LOG_FATAL
 * @brief Log level for fatal, non-recoverable errors (numeric level 0, highest severity).
 */
#define LOG_FATAL 0 
/**
 * @def LOG_ERROR
 * @brief Log level for error conditions (numeric level 1).
 */
#define LOG_ERROR 1
/**
 * @def LOG_WARN
 * @brief Log level for warning conditions (numeric level 2).
 */
#define LOG_WARN 2
/**
 * @def LOG_EXP
 * @brief Log level for exception-related diagnostics (numeric level 3).
 */
#define LOG_EXP 3
/**
 * @def LOG_NOTICE
 * @brief Log level for normal but significant conditions (numeric level 4).
 */
#define LOG_NOTICE 4
/**
 * @def LOG_INFO
 * @brief Log level for informational messages (numeric level 5).
 */
#define LOG_INFO 5
/**
 * @def LOG_DEBUG
 * @brief Log level for debug-level diagnostic messages (numeric level 6).
 */
#define LOG_DEBUG 6
/**
 * @def LOG_TRACE
 * @brief Log level for fine-grained trace messages (numeric level 7, most verbose).
 */
#define LOG_TRACE 7
/**
 * @def LOG_MAX
 * @brief Sentinel upper bound for log levels (numeric level 8); one past the last valid level.
 */
#define LOG_MAX 8


/**
 * @brief Determines and refreshes the currently-enabled CEC logging verbosity
 *        threshold used to gate @ref CCEC_LOG output.
 * @see CCEC_LOG()
 */
void check_cec_log_status(void);
/**
 * @brief printf-style CEC logging entry point; emits a formatted message when
 *        @p level is within the currently-enabled verbosity threshold.
 * @param[in] level  Severity of the message; one of the valid @c LOG_* constants
 *                   from @ref LOG_FATAL through @ref LOG_TRACE. @ref LOG_MAX is a
 *                   sentinel one past the last valid level and is the exclusive
 *                   upper bound: output is gated by <tt>level < LOG_MAX</tt>.
 * @param[in] format printf-style format string describing the message.
 * @param[in] ...    Variadic arguments substituted into @p format, matching its
 *                   conversion specifiers.
 * @see check_cec_log_status()
 */
void CCEC_LOG(int level,const char *format, ...);
/**
 * @brief Writes a hexadecimal/diagnostic dump of a byte buffer to the CEC log output.
 * @param[in] buf Pointer to the byte buffer to dump.
 * @param[in] len Number of bytes from @p buf to dump.
 */
void dump_buffer(unsigned char * buf, int len);

//#define CCEC_DBG_PRINTF(x) do{printf x;}while(0)
//#define CCEC_ERR_PRINTF(x) do{printf x;}while(0)
//#define CCEC_EXP_PRINTF(x) do{printf x;}while(0)

/**
 * @def BYTE_TO_BCD
 * @brief Converts an 8-bit binary value (0-99) to its two-nibble Binary-Coded-Decimal (BCD) form.
 *
 * The tens digit is placed in the high nibble and the units digit in the low nibble.
 * @param[in] byte_ The binary value to convert.
 */
#define BYTE_TO_BCD(byte_) (((((byte_) / 10) & 0x0F) << 4) | (((byte_) % 10) & 0x0F))
/**
 * @def BCD_TO_BYTE
 * @brief Converts a two-nibble Binary-Coded-Decimal (BCD) value to its binary equivalent.
 *
 * The high nibble is interpreted as the tens digit and the low nibble as the
 * units digit, yielding @c ((high * 10) + low).
 * @param[in] byte_ The BCD-encoded value to convert.
 */
#define BCD_TO_BYTE(byte_) (((((byte_) & 0xF0) >> 4) * 10) + (((byte_) & 0x0F)))

CCEC_END_NAMESPACE
#endif


/** @} */
/** @} */
