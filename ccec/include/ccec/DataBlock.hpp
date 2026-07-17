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


#ifndef HDMI_CCEC_DATABLOCK_HPP_
#define HDMI_CCEC_DATABLOCK_HPP_

/**
 * @file DataBlock.hpp
 * @brief Declares DataBlock, the abstract base for every serializable CEC message block.
 *
 * A DataBlock is a self-describing unit that can render itself into the raw
 * bytes of a CEC frame and report the CEC opcode it represents. It is the
 * common base from which the header, opcode, operand, and concrete message
 * types are built, giving the CCEC encode path a single polymorphic type to
 * serialize and print. This header also declares the @c Op_t opcode type and
 * the @c GetOpName() helper used to render an opcode as human-readable text.
 *
 * @see DataBlock
 */

#include <iostream>

#include "CCEC.hpp"
#include "ccec/CECFrame.hpp"
#include "Assert.hpp"
#include "ccec/Util.hpp"

CCEC_BEGIN_NAMESPACE

/**
 * @brief Integral type that holds a single CEC operation code (opcode) value.
 *
 * A CEC opcode occupies one byte on the wire; it is carried here in a 32-bit
 * unsigned integer (@c uint32_t) so opcode constants and comparisons can be
 * passed uniformly across the CCEC message types.
 */
typedef uint32_t Op_t;

/**
 * @brief Maps a CEC opcode to its human-readable name.
 *
 * Looks up the supplied opcode value (an @c Op_t input) and returns a
 * constant, human-readable label for it (for example, @c ACTIVE_SOURCE maps to
 * "Active Source"). Intended for logging and diagnostics.
 *
 * @return Pointer to a constant, null-terminated name string for the opcode;
 *         the storage is owned by the callee and must not be modified or freed
 *         by the caller.
 */
extern "C" const char *GetOpName(Op_t);

/**
 * @brief Abstract polymorphic base for every serializable block of a CEC message.
 *
 * DataBlock defines the minimal contract shared by every unit that can appear
 * in a CEC message payload: the ability to serialize itself into a CECFrame,
 * to report the opcode it represents, and to render itself as text for
 * logging. The base class itself models an empty block (it contributes no
 * bytes); the concrete building blocks @c OpCode and @c Header, together with
 * all of the message types declared in Messages.hpp, derive from it and
 * override this contract.
 */
class DataBlock
{
public:
	/* Base is empty block */
    /**
     * @brief Returns a human-readable text representation of this block.
     *
     * The base implementation represents an empty block and returns the
     * literal "[No Operands]"; derived blocks override this to describe their
     * own contents.
     *
     * @return String representation of the block.
     */
    virtual std::string toString(void) const {return "[No Operands]";}
    /**
     * @brief Logs this block at debug level as "<opcode-name> : <toString()>".
     *
     * Emits the block's opcode name (resolved via GetOpName()) together with
     * its toString() rendering through the CCEC logging facility.
     */
    virtual void print(void) const {
        CCEC_LOG( LOG_DEBUG, "%s : %s \n",GetOpName(opCode()),toString().c_str());
    }
    /**
     * @brief Appends this block's bytes to the given CEC frame.
     *
     * The base implementation represents an empty block and returns @p frame
     * unchanged; derived blocks override this to append their encoded bytes.
     *
     * @param[in,out] frame CEC frame to which this block's bytes are appended.
     * @return Reference to the same @p frame, to allow call chaining.
     */
    virtual CECFrame &serialize(CECFrame &frame) const {
        /* Default: Empty Datablock */
    	return frame;
    };
    /**
     * @brief Returns the CEC opcode that this block represents.
     *
     * @return The opcode of this block as an @c Op_t value.
     * @note Pure virtual: every concrete DataBlock subclass must implement this.
     */
    virtual Op_t opCode(void) const = 0;
};

CCEC_END_NAMESPACE;


#endif


/** @} */
/** @} */
