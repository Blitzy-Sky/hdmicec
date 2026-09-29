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

#ifndef HDMI_CCEC_OPERANDS_HPP_
#define HDMI_CCEC_OPERANDS_HPP_
#include <stdint.h>

#include <cstring>
#include <cstdio>
#include <vector>
#include <bitset>
#include <string>

#include <sstream>

#include "CCEC.hpp"
#include "Assert.hpp"
#include "Operand.hpp"
#include "Util.hpp"
#include "ccec/CECFrame.hpp"
#include "ccec/Exception.hpp"

CCEC_BEGIN_NAMESPACE

/**
 * @brief Base class providing fixed-capacity byte storage for CEC operands.
 *
 * CECBytes is the common base for every concrete operand type declared in
 * this header. It stores an operand's value as a sequence of bytes (held in
 * the protected @c str member) whose length must not exceed the
 * operand-specific maximum returned by getMaxLen(), and it supplies the shared
 * encode (serialize) and comparison logic that all operands reuse. Concrete
 * subclasses provide their own getMaxLen() and, where appropriate, override
 * toString() and validate().
 *
 * @see Operand
 * @see CECFrame
 */
class CECBytes : public Operand
{
protected:
    /**
     * @brief Constructs a single-byte operand from one byte value.
     *
     * @param[in] val The byte value used as the operand's sole content.
     */
    CECBytes(const uint8_t val) : str(1, val) {}
	/**
	 * @brief Constructs an operand by copying a buffer of bytes.
	 *
	 * Copies @p len bytes from @p buf into the internal byte store. When @p buf
	 * is NULL or @p len is zero, the operand is left empty.
	 *
	 * @param[in] buf Pointer to the source bytes to copy; may be NULL.
	 * @param[in] len Number of bytes to copy from @p buf.
	 * @note This constructor performs no length validation: it does not call
	 *       validate(), so the resulting operand may hold zero bytes or more than
	 *       getMaxLen() bytes. Callers requiring a bounded operand must supply a
	 *       buffer whose length is already within the operand's valid range.
	 */
	CECBytes(const uint8_t *buf, size_t len) {
        if (buf && len) {
            for (size_t i = 0; i < len; i++) {
                str.push_back(buf[i]);
            }
        }
	}

	/**
	 * @brief Validates that the stored byte count is within the allowed range.
	 *
	 * @return @c true when the operand holds at least one byte and no more than
	 *         getMaxLen() bytes; @c false otherwise.
	 */
	bool validate(void) const {
		return (str.size() && (str.size() <= getMaxLen()));
	}

	/**
	 * @brief Constructs an operand by decoding bytes from a CEC frame.
	 *
	 * Reads the frame's backing buffer and consumes up to @p len bytes starting at
	 * @p startPos. When @p startPos lies within the frame and fewer bytes remain
	 * than requested (@p startPos + @p len exceeds the frame length), the count is
	 * clamped so that only the bytes from @p startPos to the end of the frame are
	 * consumed. The resulting operand is validated on construction.
	 *
	 * @param[in] frame    The CEC frame to read the operand bytes from.
	 * @param[in] startPos Zero-based byte offset within @p frame at which to start.
	 * @param[in] len      Maximum number of bytes to consume from @p frame.
	 * @pre @p startPos must not exceed the frame length (@c frame.length()). The
	 *      byte count is computed as @c frameLen-startPos with unsigned arithmetic,
	 *      so a @p startPos greater than the frame length underflows to a very
	 *      large value.
	 * @warning When @p startPos is greater than the frame length the underflowed
	 *          count makes the constructor read past the end of the frame buffer
	 *          (out-of-bounds access, undefined behavior); no bounds check guards
	 *          this case. Callers must ensure @p startPos <= @c frame.length().
	 *          This documents the code exactly as implemented.
	 * @throws InvalidParamException If the decoded byte sequence fails validate().
	 */
	CECBytes(const CECFrame &frame, size_t startPos, size_t len) {
    	/*
    	 * For HDMI CEC definition, the [OSD Name] and [OSD STring] are always the one
    	 * and only one operands in the message. It is not clear if these strings are
    	 * null terminated.  Therefore, consume all remaining bytes in the frame as
    	 * CECBytes
    	 */
        const uint8_t *buf = 0;
        size_t frameLen = 0;
        frame.getBuffer(&buf, &frameLen);
        str.clear();
        len = ((startPos + len) > frameLen) ? frameLen - startPos : len;
        str.insert(str.begin(), buf + startPos, buf + startPos + len);
        if (!validate())
        {
            throw InvalidParamException();
        }
	}

public:
	/**
	 * @brief Appends the operand's bytes to a CEC frame.
	 *
	 * Encodes the operand by appending each stored byte, in order, to @p frame.
	 *
	 * @param[in,out] frame The frame to which the operand's bytes are appended.
	 * @return Reference to the same @p frame, enabling call chaining.
	 */
	CECFrame &serialize(CECFrame &frame) const {
		for (size_t i = 0; i < str.size(); i++) {
			frame.append(str[i]);
		}

		return frame;
	}
    /**
     * @brief Brings the base Operand::serialize(void) overload into scope alongside the override.
     *
     * Makes the inherited zero-argument @c Operand::serialize(void) overload
     * visible on @c CECBytes in addition to the @c serialize(CECFrame&)
     * override declared above, so both overloads participate in overload
     * resolution on this type.
     *
     * @return The inherited zero-argument overload returns a new @c CECFrame
     *         containing this operand's encoded bytes; the @c serialize(CECFrame&)
     *         override declared above instead appends to, and returns a reference
     *         to, the caller-supplied frame.
     */
    using Operand::serialize;

	/**
	 * @brief Destroys the operand; the internal byte store is released automatically.
	 */
	~CECBytes(void) {}

	/**
	 * @brief Returns a human-readable representation of the operand's bytes.
	 *
	 * The base implementation renders each stored byte as a hexadecimal value.
	 *
	 * @return A string containing the hexadecimal encoding of the stored bytes.
	 */
	virtual const std::string toString(void) const {
		std::stringstream stream;
		for (size_t i = 0; i < str.size(); i++) {
			stream << std::hex << (int)str[i];
		}
		return stream.str();
    };

	/**
	 * @brief Compares two operands for equality by their stored bytes.
	 *
	 * @param[in] in The operand to compare against.
	 * @return @c true when both operands hold an identical byte sequence; @c false otherwise.
	 */
	bool operator == (const CECBytes &in) const {
		return this->str == in.str;
	}

	/**
	 * @brief Compares two operands for inequality by their stored bytes.
	 *
	 * @param[in] in The operand to compare against.
	 * @return @c true when the operands differ in their byte sequence; @c false otherwise.
	 */
	bool operator != (const CECBytes &in) const {
		return this->str != in.str;
	}

protected:
    std::vector<uint8_t> str;  ///< Backing byte storage holding the operand's raw value.
    /**
     * @brief Returns the maximum number of bytes this operand may hold.
     *
     * @return The operand's maximum length in bytes. The base implementation returns
     *         @c CECFrame::MAX_LENGTH; subclasses override with their own limit.
     */
    virtual size_t getMaxLen(void) const {
        return CECFrame::MAX_LENGTH;
    }
};

/**
 * @brief Operand carrying a free-form On-Screen Display (OSD) string.
 *
 * Holds the text of an OSD string exchanged via CEC, up to a maximum of
 * 13 bytes (@c MAX_LEN).
 */
class OSDString : public CECBytes 
{
public:
	/**
	 * @brief Operand size limit.
	 */
	enum {
		MAX_LEN = 13,  ///< Maximum OSD string length in bytes (13).
	};

    /**
     * @brief Constructs an OSD string operand from a C string.
     *
     * The C string length is measured with @c strlen and its bytes are copied by
     * the base @c CECBytes(const uint8_t*, size_t) constructor.
     *
     * @param[in] str NUL-terminated character string providing the OSD text.
     * @pre @p str must be a non-null, NUL-terminated C string.
     * @warning Passing a NULL @p str is undefined behavior because the length is
     *          obtained with @c strlen(str). The subsequent validate() call is
     *          invoked but its result is not checked, so an empty or over-length
     *          string is not rejected here. Documents the code as implemented.
     */
    OSDString(const char *str) : CECBytes((const uint8_t *)str, strlen(str)) {
        validate();
    }

    /**
     * @brief Constructs an OSD string operand by decoding it from a CEC frame.
     *
     * @param[in] frame    The CEC frame to read the string bytes from.
     * @param[in] startPos Zero-based byte offset within @p frame at which to start.
     */
    OSDString(const CECFrame &frame, size_t startPos) : CECBytes(frame, startPos, MAX_LEN) {
    }

	/**
	 * @brief Returns the OSD string as text.
	 *
	 * @return The stored bytes interpreted as a character string.
	 */
	const std::string toString(void) const {
		return std::string(str.begin(), str.end());
	}
protected:
	/**
	 * @brief Returns the maximum OSD string length.
	 *
	 * @return The maximum length in bytes (@c MAX_LEN, 13).
	 */
	size_t getMaxLen() const {return MAX_LEN;}
};

/**
 * @brief Operand carrying a device's OSD (On-Screen Display) name.
 *
 * Holds the human-readable name a device advertises via CEC, up to a maximum
 * of 14 bytes (@c MAX_LEN).
 */
class OSDName : public CECBytes 
{
public:
	/**
	 * @brief Operand size limit.
	 */
	enum {
		MAX_LEN = 14,  ///< Maximum OSD name length in bytes (14).
	};

    /**
     * @brief Constructs an OSD name operand from a C string.
     *
     * The C string length is measured with @c strlen and its bytes are copied by
     * the base @c CECBytes(const uint8_t*, size_t) constructor.
     *
     * @param[in] str NUL-terminated character string providing the OSD name.
     * @pre @p str must be a non-null, NUL-terminated C string.
     * @warning Passing a NULL @p str is undefined behavior because the length is
     *          obtained with @c strlen(str). The subsequent validate() call is
     *          invoked but its result is not checked, so an empty or over-length
     *          string is not rejected here. Documents the code as implemented.
     */
    OSDName(const char *str) : CECBytes((const uint8_t *)str, strlen(str)) {
        validate();
    }

    /**
     * @brief Constructs an OSD name operand by decoding it from a CEC frame.
     *
     * @param[in] frame    The CEC frame to read the name bytes from.
     * @param[in] startPos Zero-based byte offset within @p frame at which to start.
     */
    OSDName(const CECFrame &frame, size_t startPos) : CECBytes(frame, startPos, MAX_LEN) {
    }

	/**
	 * @brief Returns the OSD name as text.
	 *
	 * @return The stored bytes interpreted as a character string.
	 */
	const std::string toString(void) const {
		return std::string(str.begin(), str.end());
	}
protected:
	/**
	 * @brief Returns the maximum OSD name length.
	 *
	 * @return The maximum length in bytes (@c MAX_LEN, 14).
	 */
	size_t getMaxLen() const {return MAX_LEN;}
};

/**
 * @brief Operand encoding the reason code carried by a Feature Abort message.
 */
class AbortReason : public CECBytes 
{
public :
    /**
     * @brief Operand size limit.
     */
    enum {
        MAX_LEN = 1,  ///< Abort reason length in bytes (1).
    };

	/**
	 * @brief Enumerates the defined Feature Abort reason codes.
	 */
	enum  {
		UNRECOGNIZED_OPCODE,  ///< The opcode of the aborted message was not recognized.
		NOT_IN_CORRECT_MODE_TO_RESPOND,  ///< The device is not in the correct mode to respond.
		CANNOT_OVERIDE_SOURCE,  ///< The device cannot provide the requested source.
		INVALID_OPERAND,  ///< One or more operands of the message were invalid.
		REFUSED,  ///< The device refused to act on the message.
		UNABLE_TO_DETERMINE,  ///< The device was unable to determine a response.
	};

	/**
	 * @brief Constructs an abort-reason operand from a reason code.
	 *
	 * @param[in] reason The abort reason code (one of the enumerated values).
	 */
	AbortReason(int reason) : CECBytes((uint8_t)reason) { }

	/**
	 * @brief Returns a human-readable description of the abort reason.
	 *
	 * @return The descriptive text for the reason code, or "Unknown" when the
	 *         stored value is not a recognized reason.
	 */
	const std::string toString(void) const {
		static const char *names_[] = {
			"Unrecognized opcode",
			"Not in correct mode to respond",
			"Cannot provide source",
			"Invalid operand",
			"Refused",
			"Unable to determine",
		};

		if (validate())
		{
			return names_[str[0]];
		}
		else
		{
			CCEC_LOG(LOG_WARN,"Unknown abort reason:%x\n", str[0]);
			return "Unknown";
		}
	}

	/**
	 * @brief Validates that the stored reason code is a recognized value.
	 *
	 * @return @c true when the reason code is within the defined range; @c false otherwise.
	 */
	bool validate(void) const {
		return (/*(str[0]>= UNRECOGNIZED_OPCODE) && */(str[0]<= UNABLE_TO_DETERMINE));
	}

	/**
	 * @brief Returns the abort reason as its numeric code.
	 *
	 * @return The stored abort reason code as an integer.
	 */
	int toInt(void) const {
        return str[0];
    }

	/**
	 * @brief Constructs an abort-reason operand by decoding it from a CEC frame.
	 *
	 * @param[in] frame    The CEC frame to read the reason byte from.
	 * @param[in] startPos Zero-based byte offset within @p frame at which to start.
	 */
	AbortReason(const CECFrame &frame, size_t startPos) : CECBytes(frame, startPos, MAX_LEN) { } 


protected:
	/**
	 * @brief Returns the maximum abort-reason length.
	 *
	 * @return The maximum length in bytes (@c MAX_LEN, 1).
	 */
	size_t getMaxLen() const {return MAX_LEN;}

};

/**
 * @brief Operand encoding a CEC device type.
 */
class DeviceType : public CECBytes 
{
public :
    /**
     * @brief Operand size limit.
     */
    enum {
        MAX_LEN = 1,  ///< Device type length in bytes (1).
    };

	/**
	 * @brief Enumerates the CEC device types.
	 */
	enum  {
		TV = 0x0,  ///< Television.
		RECORDING_DEVICE,  ///< Recording device.
		RESERVED,  ///< Reserved value.
		TUNER,  ///< Tuner.
		PLAYBACK_DEVICE,  ///< Playback device.
		AUDIO_SYSTEM,  ///< Audio system.
		PURE_CEC_SWITCH,  ///< Pure CEC switch.
		VIDEO_PROCESSOR,  ///< Video processor.
	};

	/**
	 * @brief Constructs a device-type operand from a type code.
	 *
	 * @param[in] type The device type code (one of the enumerated values).
	 */
	DeviceType(int type) : CECBytes((uint8_t)type) {}

	/**
	 * @brief Returns a human-readable description of the device type.
	 *
	 * @return The descriptive text for the device type, or "Unknown" when the
	 *         stored value is not a recognized type.
	 */
	const std::string toString(void) const {
		static const char *names_[] = {
				"TV",
				"Recording Device",
				"Reserved",
				"Tuner",
				"Playback Device",
				"Audio System",
				"Pure CEC Switch",
				"Video Processor",
		};

		if (validate())
		{
			return names_[str[0]];
		}
		else
		{
			CCEC_LOG(LOG_WARN,"Unknown device type:%x\n", str[0]);
			return "Unknown";
		}
	}

	/**
	 * @brief Validates that the stored device type is a recognized value.
	 *
	 * @return @c true when the device type is within the defined range; @c false otherwise.
	 */
	bool validate(void) const {
		return (/*(str[0] >= TV) && */(str[0] <= VIDEO_PROCESSOR));
	}

	/**
	 * @brief Constructs a device-type operand by decoding it from a CEC frame.
	 *
	 * @param[in] frame    The CEC frame to read the device-type byte from.
	 * @param[in] startPos Zero-based byte offset within @p frame at which to start.
	 */
	DeviceType(const CECFrame &frame, size_t startPos) : CECBytes(frame, startPos, MAX_LEN) {}

	/**
	 * @brief Destroys the device-type operand.
	 */
	~DeviceType(void) {}

protected:
	/**
	 * @brief Returns the maximum device-type length.
	 *
	 * @return The maximum length in bytes (@c MAX_LEN, 1).
	 */
	size_t getMaxLen() const {return MAX_LEN;}
};

/**
 * @brief Operand carrying an ISO 639 language code.
 *
 * Holds a language code up to a maximum of 3 bytes (@c MAX_LEN).
 */
class Language : public CECBytes 
{
public:
	/**
	 * @brief Operand size limit.
	 */
	enum {
		MAX_LEN = 3,  ///< Maximum language code length in bytes (3).
	};

    /**
     * @brief Constructs a language operand from a C string.
     *
     * The C string length is measured with @c strlen and its bytes are copied by
     * the base @c CECBytes(const uint8_t*, size_t) constructor.
     *
     * @param[in] str NUL-terminated character string providing the language code.
     * @pre @p str must be a non-null, NUL-terminated C string no longer than
     *      @c MAX_LEN bytes.
     * @warning Passing a NULL @p str is undefined behavior because the length is
     *          obtained with @c strlen(str). An @c Assert enforces
     *          @c strlen(str) <= @c MAX_LEN only in assertion-enabled builds.
     *          Documents the code as implemented.
     */
    Language(const char *str) : CECBytes((const uint8_t*)str, strlen(str)) {
        Assert(strlen(str) <= MAX_LEN);
    }

    /**
     * @brief Constructs a language operand by decoding it from a CEC frame.
     *
     * @param[in] frame    The CEC frame to read the language bytes from.
     * @param[in] startPos Zero-based byte offset within @p frame at which to start.
     */
    Language(const CECFrame &frame, size_t startPos) : CECBytes(frame, startPos, MAX_LEN) {
    }

	/**
	 * @brief Returns the language code as text.
	 *
	 * @return The stored bytes interpreted as a character string.
	 */
	const std::string toString(void) const {
		return std::string(str.begin(), str.end());
	}
protected:
	/**
	 * @brief Returns the maximum language code length.
	 *
	 * @return The maximum length in bytes (@c MAX_LEN, 3).
	 */
	size_t getMaxLen() const {return MAX_LEN;}
};


/**
 * @brief Operand encoding a 24-bit CEC vendor identifier.
 *
 * Holds a three-byte (@c MAX_LEN) IEEE-assigned vendor ID.
 */
class VendorID : public CECBytes
{
public:
	/**
	 * @brief Operand size limit.
	 */
	enum {
		MAX_LEN = 3,  ///< Vendor ID length in bytes (3).
	};

	/**
	 * @brief Constructs a vendor-ID operand from three bytes.
	 *
	 * @param[in] byte0 Most significant byte of the vendor ID.
	 * @param[in] byte1 Middle byte of the vendor ID.
	 * @param[in] byte2 Least significant byte of the vendor ID.
	 */
	VendorID(uint8_t byte0, uint8_t byte1, uint8_t byte2) : CECBytes (NULL, 0) {
        uint8_t bytes[MAX_LEN];
        bytes[0] = byte0;
        bytes[1] = byte1;
        bytes[2] = byte2;
        str.insert(str.begin(), bytes, bytes + MAX_LEN);
	}

	/**
	 * @brief Constructs a vendor-ID operand from a byte buffer.
	 *
	 * At most @c MAX_LEN bytes are copied from @p buf.
	 *
	 * @param[in] buf Pointer to the source bytes.
	 * @param[in] len Number of bytes available in @p buf.
	 */
	VendorID(const uint8_t *buf, size_t len) : CECBytes (buf, len > MAX_LEN ? MAX_LEN : len) {
    }

	/**
	 * @brief Constructs a vendor-ID operand by decoding it from a CEC frame.
	 *
	 * @param[in] frame    The CEC frame to read the vendor-ID bytes from.
	 * @param[in] startPos Zero-based byte offset within @p frame at which to start.
	 */
	VendorID(const CECFrame &frame, size_t startPos) : CECBytes (frame, startPos, MAX_LEN) {
    };

protected:
	/**
	 * @brief Returns the maximum vendor-ID length.
	 *
	 * @return The maximum length in bytes (@c MAX_LEN, 3).
	 */
	size_t getMaxLen() const {return MAX_LEN;}

};

/**
 * @brief Operand encoding a four-nibble HDMI physical address (a.b.c.d).
 *
 * A physical address is stored in two bytes (@c MAX_LEN), packing the four
 * address nibbles that describe a device's position in the HDMI topology.
 */
class PhysicalAddress : public CECBytes
{
    public:
	/**
	 * @brief Operand size limit.
	 */
	enum {
		MAX_LEN = 2,  ///< Physical address length in bytes (2).
	};

	/**
	 * @brief Constructs a physical address from its four nibble values.
	 *
	 * @param[in] byte0 First (most significant) address nibble.
	 * @param[in] byte1 Second address nibble.
	 * @param[in] byte2 Third address nibble.
	 * @param[in] byte3 Fourth (least significant) address nibble.
	 */
	PhysicalAddress(uint8_t byte0, uint8_t byte1, uint8_t byte2, uint8_t byte3) : CECBytes (NULL, 0) {
        uint8_t bytes[MAX_LEN];
        bytes[0] = (byte0 & 0x0F)<< 4 | (byte1 & 0x0F);
        bytes[1] = (byte2 & 0x0F)<< 4 | (byte3 & 0x0F);
        str.insert(str.begin(), bytes, bytes + MAX_LEN);
    }

    /**
     * @brief Constructs a physical address from a byte buffer.
     *
     * @param[in] buf Pointer to the two source bytes of the packed address.
     * @param[in] len Number of bytes available in @p buf; must be at least @c MAX_LEN.
     */
    PhysicalAddress(uint8_t *buf, size_t len = MAX_LEN) : CECBytes(buf, MAX_LEN) {
        Assert(len >= MAX_LEN);
    }

	/**
	 * @brief Constructs a physical address by decoding it from a CEC frame.
	 *
	 * @param[in] frame    The CEC frame to read the address bytes from.
	 * @param[in] startPos Zero-based byte offset within @p frame at which to start.
	 */
	PhysicalAddress(const CECFrame &frame, size_t startPos) : CECBytes (frame, startPos, MAX_LEN) {
    };

	/**
	 * @brief Constructs a physical address by parsing a dotted string.
	 *
	 * Parses a seven-character "a.b.c.d" representation (four hexadecimal nibbles
	 * separated by dots) into the packed two-byte address.
	 *
	 * @param[in,out] addr The dotted address string to parse; consumed during parsing.
	 * @pre @p addr must be exactly seven characters long ("a.b.c.d"). An @c Assert
	 *      enforces @c addr.length()==7 in assertion-enabled builds; other lengths
	 *      are not otherwise guarded. Documents the code as implemented.
	 */
	PhysicalAddress(std::string &addr)         : CECBytes (NULL, 0) {
		uint8_t byte[4];
		uint8_t bytes[MAX_LEN];
		size_t dotposition = 0;
		int i = 0;

		Assert((addr.length() != 0 && addr.length() == 7));
		
		while (addr.length()    && i < 4)
		{
		  byte[i++] = stoi(addr,&dotposition,16);
		  if (addr.length() > 1)
		  {
		   	 addr = addr.substr(dotposition+1);
		  }
		  else
		  {
		 	 break; 	
		  }
		}
		
        bytes[0] = (byte[0] & 0x0F)<< 4 | (byte[1] & 0x0F);
        bytes[1] = (byte[2] & 0x0F)<< 4 | (byte[3] & 0x0F);
        str.insert(str.begin(), bytes, bytes + MAX_LEN);
    }

	/**
	 * @brief Returns one of the four address nibbles.
	 *
	 * @param[in] index Zero-based nibble index in the range 0..3.
	 * @return The nibble value at @p index (0..15).
	 */
	uint8_t getByteValue( int index) const {
		uint8_t val = 0;

		Assert(index < 4);

		switch(index)
		{
			case 0: 
			{
				val = (int) ((str[0] & 0xF0) >> 4);
			}
				break;
			case 1: 
			{
				val = (int) (str[0] & 0x0F);
			}
				break;
			
			case 2: 
			{
				val = (int) ((str[1] & 0xF0) >> 4);
			}
				break;

			case 3: 
			{
				val = (int) (str[1] & 0x0F);
			}
				break;
		}

		return val;
    }

    /**
     * @brief Returns the physical address in dotted "a.b.c.d" form.
     *
     * @return The address formatted as four dot-separated nibble values.
     */
    const std::string toString(void) const {
		std::stringstream stream;
        stream << (int)((str[0] & 0xF0) >> 4)<< "." << (int)(str[0] & 0x0F) << "." << (int)((str[1] & 0xF0) >> 4) << "." << (int)(str[1] & 0x0F);
		return stream.str();
    }

    /**
     * @brief Returns the operand's type name.
     *
     * @return The string "PhysicalAddress".
     */
    const std::string name(void) const {
        return "PhysicalAddress";
    }

protected:
	/**
	 * @brief Returns the maximum physical-address length.
	 *
	 * @return The maximum length in bytes (@c MAX_LEN, 2).
	 */
	size_t getMaxLen() const {return MAX_LEN;}
};

/**
 * @brief Operand encoding a 4-bit CEC logical address.
 *
 * A logical address identifies a device's CEC role (for example, TV or
 * Audio System) and is stored in a single byte (@c MAX_LEN).
 */
class  LogicalAddress : public CECBytes 
{
public:
    /**
     * @brief Operand size limit.
     */
    enum {
        MAX_LEN = 1,  ///< Logical address length in bytes (1).
    };

    /**
     * @brief Predefined logical-address constant intended to represent the TV
     *        (logical address TV = 0).
     *
     * @warning Declaration only: this static member is declared here but has no
     *          out-of-class definition anywhere in the module, so it is not a
     *          usable constant. Any ODR-use - taking its address, binding a
     *          reference to it, or otherwise using it where a definition is
     *          required - produces an "undefined reference to
     *          CCEC::LogicalAddress::kTv" link error. This documents the code
     *          exactly as implemented; the missing definition is intentionally
     *          not added here because this is a documentation-only task.
     */
    static const LogicalAddress kTv;

    /**
     * @brief Enumerates the CEC logical addresses.
     *
     * A CEC logical address identifies a device's role on the bus. The
     * enumerators and their assigned address values are:
     * - @c TV (0): Television.
     * - @c RECORDING_DEVICE_1 (1): Recording device 1.
     * - @c RECORDING_DEVICE_2 (2): Recording device 2.
     * - @c TUNER_1 (3): Tuner 1.
     * - @c PLAYBACK_DEVICE_1 (4): Playback device 1.
     * - @c AUDIO_SYSTEM (5): Audio system.
     * - @c TUNER_2 (6): Tuner 2.
     * - @c TUNER_3 (7): Tuner 3.
     * - @c PLAYBACK_DEVICE_2 (8): Playback device 2.
     * - @c RECORDING_DEVICE_3 (9): Recording device 3.
     * - @c TUNER_4 (10): Tuner 4.
     * - @c PLAYBACK_DEVICE_3 (11): Playback device 3.
     * - @c RESERVED_12 (12): Reserved (12).
     * - @c RESERVED_13 (13): Reserved (13).
     * - @c SPECIFIC_USE (14): Specific use.
     * - @c UNREGISTERED (15): Unregistered / unallocated device.
     * - @c BROADCAST (alias of @c UNREGISTERED, 15): Broadcast address.
     */
    enum {
    	TV 						= 0,
    	RECORDING_DEVICE_1 		= 1,
    	RECORDING_DEVICE_2		= 2,
        TUNER_1 				= 3,
        PLAYBACK_DEVICE_1 		= 4,
        AUDIO_SYSTEM 			= 5,
        TUNER_2 				= 6,
        TUNER_3 				= 7,
        PLAYBACK_DEVICE_2 		= 8,
        RECORDING_DEVICE_3 		= 9,
        TUNER_4 				= 10,
        PLAYBACK_DEVICE_3 		= 11,
        RESERVED_12 			= 12,
        RESERVED_13 			= 13,
        SPECIFIC_USE 			= 14,
        UNREGISTERED 			= 15,
        BROADCAST				= UNREGISTERED,
    };

    /**
     * @brief Constructs a logical-address operand.
     *
     * @param[in] addr The logical address value; defaults to @c UNREGISTERED.
     */
    LogicalAddress(int addr = UNREGISTERED) : CECBytes((uint8_t)addr) { };

    /**
     * @brief Returns a human-readable description of the logical address.
     *
     * @return The descriptive text for the logical address, or "Unknown" when the
     *         stored value is not a recognized address.
     */
    const std::string toString(void) const
    {
    	static const char *names_[] = {
    	"TV",
    	"Recording Device 1",
    	"Recording Device 2",
    	"Tuner 1",
    	"Playback Device 1",
    	"Audio System",
    	"Tuner 2",
    	"Tuner 3",
    	"Playback Device 2",
    	"Recording Device 3",
    	"Tuner 4",
    	"Playback Device 3",
    	"Reserved 12",
    	"Reserved 13",
    	"Specific Use",
    	"Broadcast/Unregistered",
    	};

	if (validate())
	{
		return names_[str[0]];
	}
	else
	{
		CCEC_LOG(LOG_WARN,"Unknown logical address:%x\n", str[0]);
		return "Unknown";
	}
    }

	/**
	 * @brief Returns the logical address as its numeric value.
	 *
	 * @return The stored logical address as an integer.
	 */
	int toInt(void) const {
		return str[0];
	}

	/**
	 * @brief Validates that the stored logical address is within range.
	 *
	 * @return @c true when the address is less than or equal to @c BROADCAST; @c false otherwise.
	 */
	bool validate(void) const {
		return ((str[0] <= BROADCAST));
	}

    /**
     * @brief Returns the CEC device type associated with this logical address.
     *
     * Maps the logical address to its corresponding DeviceType value.
     *
     * @return The associated device type (a DeviceType enumerator value).
     * @throws InvalidParamException If the logical address is not valid (see validate()).
     */
    int getType(void) const {

        if (!validate()) {
            throw InvalidParamException();
        }

        static int _type[] = {
            DeviceType::TV,
            DeviceType::RECORDING_DEVICE,
            DeviceType::RECORDING_DEVICE,
            DeviceType::TUNER,
            DeviceType::PLAYBACK_DEVICE,
            DeviceType::AUDIO_SYSTEM,
            DeviceType::TUNER,
            DeviceType::TUNER,
            DeviceType::PLAYBACK_DEVICE,
            DeviceType::RECORDING_DEVICE,
            DeviceType::TUNER,
            DeviceType::PLAYBACK_DEVICE,
            DeviceType::RESERVED,
            DeviceType::RESERVED,
            DeviceType::RESERVED,
            DeviceType::RESERVED,
        };

        return _type[str[0]];
    }

	/**
	 * @brief Constructs a logical-address operand by decoding it from a CEC frame.
	 *
	 * @param[in] frame    The CEC frame to read the address byte from.
	 * @param[in] startPos Zero-based byte offset within @p frame at which to start.
	 */
	LogicalAddress(const CECFrame &frame, size_t startPos) : CECBytes (frame, startPos, MAX_LEN) {
    };
protected:
	/**
	 * @brief Returns the maximum logical-address length.
	 *
	 * @return The maximum length in bytes (@c MAX_LEN, 1).
	 */
	size_t getMaxLen() const {return MAX_LEN;}
};

/**
 * @brief Operand encoding the CEC protocol version.
 */
class Version : public CECBytes 
{
public:
    /**
     * @brief Operand size limit.
     */
    enum {
        MAX_LEN = 1,  ///< Version length in bytes (1).
    };

    /**
     * @brief Enumerates the CEC version codes.
     *
     * Each enumerator corresponds to a CEC specification version code:
     * - @c V_RESERVED_0: Reserved version code 0.
     * - @c V_RESERVED_1: Reserved version code 1.
     * - @c V_RESERVED_2: Reserved version code 2.
     * - @c V_RESERVED_3: Reserved version code 3.
     * - @c V_1_3a: CEC version 1.3a.
     * - @c V_1_4: CEC version 1.4.
     * - @c V_2_0: CEC version 2.0.
     */
    enum {
    	V_RESERVED_0,
    	V_RESERVED_1,
    	V_RESERVED_2,
    	V_RESERVED_3,
    	V_1_3a,
    	V_1_4,
	V_2_0,
    };

	/**
	 * @brief Constructs a version operand from a version code.
	 *
	 * @param[in] version The CEC version code (one of the enumerated values).
	 */
	Version(int version) : CECBytes((uint8_t)version) { };

	/**
	 * @brief Validates that the stored version code is a supported value.
	 *
	 * @return @c true when the version code is between @c V_1_3a and @c V_2_0
	 *         inclusive; @c false otherwise.
	 */
	bool validate(void) const {
		return ((str[0] <= V_2_0) && (str[0] >= V_1_3a));
	}

	/**
	 * @brief Returns a human-readable description of the CEC version.
	 *
	 * @return The descriptive text for the version, or "Unknown" when the stored
	 *         value is not a recognized version.
	 */
	const std::string toString(void) const
	{
		static const char *names_[] = {
				"Reserved",
				"Reserved",
				"Reserved",
				"Reserved",
				"Version 1.3a",
				"Version 1.4",
				"Version 2.0"
		};

		if (validate())
		{
			return names_[str[0]];
		}
		else
		{
			CCEC_LOG(LOG_WARN,"Unknown version:%x\n", str[0]);
			return "Unknown";
		}
	}

	/**
	 * @brief Constructs a version operand by decoding it from a CEC frame.
	 *
	 * @param[in] frame    The CEC frame to read the version byte from.
	 * @param[in] startPos Zero-based byte offset within @p frame at which to start.
	 */
	Version (const CECFrame &frame, size_t startPos) : CECBytes (frame, startPos, MAX_LEN) {
    };
protected:
	/**
	 * @brief Returns the maximum version length.
	 *
	 * @return The maximum length in bytes (@c MAX_LEN, 1).
	 */
	size_t getMaxLen() const {return MAX_LEN;}
};

/**
 * @brief Operand encoding a device's power status.
 */
class PowerStatus : public CECBytes 
{
public:
    /**
     * @brief Operand size limit.
     */
    enum {
        MAX_LEN = 1,  ///< Power status length in bytes (1).
    };

    /**
     * @brief Enumerates the CEC power-status values.
     */
    enum {
        ON = 0,  ///< Device is powered on.
        STANDBY = 0x01,  ///< Device is in standby.
        IN_TRANSITION_STANDBY_TO_ON = 0x02,  ///< Device is transitioning from standby to on.
        IN_TRANSITION_ON_TO_STANDBY = 0x03,  ///< Device is transitioning from on to standby.
        POWER_STATUS_NOT_KNOWN = 0x4,   ///< Power status is not known.
        POWER_STATUS_FEATURE_ABORT = 0x05,  ///< Power status could not be provided (feature abort).
   };

	/**
	 * @brief Constructs a power-status operand from a status code.
	 *
	 * @param[in] status The power-status code (one of the enumerated values).
	 */
	PowerStatus(int status) : CECBytes((uint8_t)status) { };

	/**
	 * @brief Validates that the stored power status is a recognized value.
	 *
	 * @return @c true when the status is at most @c IN_TRANSITION_ON_TO_STANDBY;
	 *         @c false otherwise.
	 */
	bool validate(void) const {
		return ((str[0] <= IN_TRANSITION_ON_TO_STANDBY)/* && (str[0] >= ON)*/);
	}

	/**
	 * @brief Returns a human-readable description of the power status.
	 *
	 * @return The descriptive text for the power status, or "Unknown" when the
	 *         stored value is not a recognized status.
	 */
	const std::string toString(void) const
	{
		static const char *names_[] = {
			"On",
			"Standby",
			"In transition Standby to On",
			"In transition On to Standby",
			"Not Known",
			"Feature Abort"
		};

		if (validate())
		{
			return names_[str[0]];
		}
		else
		{
			CCEC_LOG(LOG_WARN,"Unknown powerstatus:%x\n", str[0]);
			return "Unknown";
		}
	}

	/**
	 * @brief Returns the power status as its numeric code.
	 *
	 * @return The stored power-status code as an integer.
	 */
	int toInt(void) const {
		return str[0];
	}

	/**
	 * @brief Constructs a power-status operand by decoding it from a CEC frame.
	 *
	 * @param[in] frame    The CEC frame to read the status byte from.
	 * @param[in] startPos Zero-based byte offset within @p frame at which to start.
	 */
	PowerStatus (const CECFrame &frame, size_t startPos) : CECBytes (frame, startPos, MAX_LEN) {
	};

protected:
	/**
	 * @brief Returns the maximum power-status length.
	 *
	 * @return The maximum length in bytes (@c MAX_LEN, 1).
	 */
	size_t getMaxLen() const {return MAX_LEN;}
};

/**
 * @brief Operand encoding a requested audio format (audio format ID and code).
 */
class RequestAudioFormat : public CECBytes
{
  public :
    /**
     * @brief Operand size limit.
     */
    enum {
        MAX_LEN = 1,  ///< Request-audio-format length in bytes (1).
    };

	/**
	 * @brief Enumerates the short audio descriptor (SAD) format codes.
	 */
	enum {

          /// Linear PCM (code 1).
          SAD_FMT_CODE_LPCM =1 ,		       // 1
          /// AC-3 (code 2).
 	  SAD_FMT_CODE_AC3,	      // 2
          /// MPEG-1 (code 3).
 	  SAD_FMT_CODE_MPEG1,		    //   3
          /// MP3 (code 4).
          SAD_FMT_CODE_MP3,	      // 4
          /// MPEG-2 (code 5).
          SAD_FMT_CODE_MPEG2,	  //     5
	  /// AAC LC (code 6).
	  SAD_FMT_CODE_AAC_LC,	      // 6
          /// DTS (code 7).
          SAD_FMT_CODE_DTS,	      // 7
          /// ATRAC (code 8).
          SAD_FMT_CODE_ATRAC,	      // 8
          /// One Bit Audio (code 9).
          SAD_FMT_CODE_ONE_BIT_AUDIO,  // 9
          /// Enhanced AC-3 / E-AC-3 (code 10).
          SAD_FMT_CODE_ENHANCED_AC3,	 // 10
          /// DTS-HD (code 11).
          SAD_FMT_CODE_DTS_HD,	 // 11
          /// MAT (code 12).
          SAD_FMT_CODE_MAT,	    //  12
          /// DST (code 13).
          SAD_FMT_CODE_DST,	    //  13
          /// WMA Pro (code 14).
          SAD_FMT_CODE_WMA_PRO, 	 // 14
          /// Extended audio format (code 15).
          SAD_FMT_CODE_EXTENDED,		//  15
	};
	/**
	 * @brief Constructs a request-audio-format operand from a packed byte.
	 *
	 * @param[in] AudioFormatIdCode Packed byte holding the audio format ID (upper
	 *                              bits) and audio format code (lower bits).
	 */
	RequestAudioFormat(uint8_t AudioFormatIdCode) : CECBytes((uint8_t)AudioFormatIdCode) { };
        /**
         * @brief Constructs a request-audio-format operand by decoding it from a CEC frame.
         *
         * @param[in] frame    The CEC frame to read the operand byte from.
         * @param[in] startPos Zero-based byte offset within @p frame at which to start.
         */
        RequestAudioFormat(const CECFrame &frame, size_t startPos) : CECBytes (frame, startPos,MAX_LEN) { };
	/**
	 * @brief Returns a human-readable description of the requested audio format.
	 *
	 * @return The descriptive text for the audio format code.
	 */
	const std::string toString(void) const
        {
		static const char *AudioFormtCode[] = {
				"Reserved",
				"LPCM",
				"AC3",
				"MPEG1",
				"MP3",
				"MPEG2",
				"AAC",
				"DTS",
				"ATRAC",
				"One Bit Audio",
				"E-AC3",
				"DTS-HD",
				"MAT",
				"DST",
				"WMA PRO",
				"Reserved for Audio format 15",
    	};
		/* audio formt code uses 6 bits but codes are defined only for 15 codes */
    	return AudioFormtCode[str[0] & 0xF];
        }
	/**
	 * @brief Returns the audio format ID field.
	 *
	 * @return The audio format ID (the upper bits of the stored byte).
	 */
	int getAudioformatId(void) const {

            return (str[0]>>6);
		};

	/**
	 * @brief Returns the audio format code field.
	 *
	 * @return The audio format code (the lower 6 bits of the stored byte).
	 */
	int getAudioformatCode(void) const {

		return (str[0] & 0x3F);

	};
protected:
	/**
	 * @brief Returns the maximum request-audio-format length.
	 *
	 * @return The maximum length in bytes (@c MAX_LEN, 1).
	 */
	size_t getMaxLen() const {return MAX_LEN;}
};

/**
 * @brief Operand encoding a Short Audio Descriptor (SAD).
 *
 * Carries a three-byte (@c MAX_LEN) descriptor of an audio format's
 * capabilities.
 */
class ShortAudioDescriptor : public CECBytes
{
  public :
    /**
     * @brief Operand size limit.
     */
    enum {
        MAX_LEN = 3,  ///< Short audio descriptor length in bytes (3).
    };

	/**
	 * @brief Enumerates the short audio descriptor (SAD) format codes.
	 */
	enum {

          /// Linear PCM (code 1).
          SAD_FMT_CODE_LPCM =1 ,		       // 1
          /// AC-3 (code 2).
 		  SAD_FMT_CODE_AC3,	      // 2
          /// MPEG-1 (code 3).
 		  SAD_FMT_CODE_MPEG1,		    //   3
          /// MP3 (code 4).
          SAD_FMT_CODE_MP3,	      // 4
          /// MPEG-2 (code 5).
          SAD_FMT_CODE_MPEG2,	  //     5
		  /// AAC LC (code 6).
		  SAD_FMT_CODE_AAC_LC,	      // 6
          /// DTS (code 7).
          SAD_FMT_CODE_DTS,	      // 7
          /// ATRAC (code 8).
          SAD_FMT_CODE_ATRAC,	      // 8
          /// One Bit Audio (code 9).
          SAD_FMT_CODE_ONE_BIT_AUDIO,  // 9
          /// Enhanced AC-3 / E-AC-3 (code 10).
          SAD_FMT_CODE_ENHANCED_AC3,	 // 10
          /// DTS-HD (code 11).
          SAD_FMT_CODE_DTS_HD,	 // 11
          /// MAT (code 12).
          SAD_FMT_CODE_MAT,	    //  12
          /// DST (code 13).
          SAD_FMT_CODE_DST,	    //  13
          /// WMA Pro (code 14).
          SAD_FMT_CODE_WMA_PRO, 	 // 14
          /// Extended audio format (code 15).
          SAD_FMT_CODE_EXTENDED,		//  15

	};

	/**
	 * @brief Constructs a short audio descriptor from a byte buffer.
	 *
	 * @param[in] buf Pointer to the descriptor bytes.
	 * @param[in] len Number of bytes available in @p buf; must be at least @c MAX_LEN.
	 */
	ShortAudioDescriptor(uint8_t *buf, size_t len = MAX_LEN) : CECBytes(buf, MAX_LEN) {
               Assert(len >= MAX_LEN);
        };
        /**
         * @brief Constructs a short audio descriptor by decoding it from a CEC frame.
         *
         * @param[in] frame    The CEC frame to read the descriptor bytes from.
         * @param[in] startPos Zero-based byte offset within @p frame at which to start.
         */
        ShortAudioDescriptor(const CECFrame &frame, size_t startPos) : CECBytes (frame, startPos,MAX_LEN) {
		   };
	/**
	 * @brief Returns a human-readable description of the audio format.
	 *
	 * @return The descriptive text for the descriptor's audio format code.
	 */
	const std::string toString(void) const
        {

		static const char *AudioFormtCode[] = {
				"Reserved",
				"LPCM",
				"AC3",
				"MPEG1",
				"MP3",
				"MPEG2",
				"AAC",
				"DTS",
				"ATRAC",
				"One Bit Audio",
				"E-AC3",
				"DTS-HD",
				"MAT",
				"DST",
				"WMA PRO",
				"Reserved for Audio format 15",
    	        };
   	/* audio formt code uses 6 bits but codes are defined only for 15 codes */
        return AudioFormtCode[(str[0] >> 3) & 0xF];
    }

	/**
	 * @brief Returns the raw 24-bit audio descriptor value.
	 *
	 * @return The three descriptor bytes combined into a single 32-bit value.
	 */
	uint32_t getAudiodescriptor(void) const	{
         uint32_t audiodescriptor;

		audiodescriptor =  (str[0] | str[1] << 8 | str[2] << 16);
		return audiodescriptor;

	};

	/**
	 * @brief Returns the audio format code carried by the descriptor.
	 *
	 * @return The audio format code extracted from the descriptor's first byte.
	 */
	int getAudioformatCode(void) const	{
         uint8_t audioformatCode;
         audioformatCode = ((str[0] >> 3) & 0xF);
		 return audioformatCode;

	};
	/**
	 * @brief Reports whether the descriptor indicates Dolby Atmos support.
	 *
	 * @return Non-zero when Atmos support is indicated; zero otherwise.
	 */
	uint8_t getAtmosbit(void) const {

        bool atmosSupport = false;
		if ((((str[0] >> 3) & 0xF) >= 9))
		{
                      if((str[2] & 0x3) != 0)
                      {
                         atmosSupport = true;
                      }

		}
            return atmosSupport;
	};

protected:
	/**
	 * @brief Returns the maximum short-audio-descriptor length.
	 *
	 * @return The maximum length in bytes (@c MAX_LEN, 3).
	 */
	size_t getMaxLen() const {return MAX_LEN;}

};
/**
 * @brief Operand encoding the System Audio Mode status.
 */
class SystemAudioStatus : public CECBytes
{
public:
    /**
     * @brief Operand size limit.
     */
    enum {
        MAX_LEN = 1,  ///< System-audio-status length in bytes (1).
    };

    /**
     * @brief Enumerates the system audio mode states.
     */
    enum {
            OFF = 0x00,  ///< System audio mode is off.
	    ON = 0x01,  ///< System audio mode is on.
         };

	/**
	 * @brief Constructs a system-audio-status operand from a status code.
	 *
	 * @param[in] status The system audio mode status (@c ON or @c OFF).
	 */
	SystemAudioStatus(int status) : CECBytes((uint8_t)status) { };

	/**
	 * @brief Validates that the stored status is a recognized value.
	 *
	 * @return @c true when the status is at most @c ON; @c false otherwise.
	 */
	bool validate(void) const {
		return ((str[0] <= ON) );
	}

	/**
	 * @brief Returns a human-readable description of the system audio status.
	 *
	 * @return "On" or "Off", or "Unknown" when the stored value is not recognized.
	 */
	const std::string toString(void) const
	{
		static const char *names_[] = {
			"Off",
			"On",
		};

		if (validate())
		{
			return names_[str[0]];
		}
		else
		{
			CCEC_LOG(LOG_WARN,"Unknown SystemAudioStatus:%x\n", str[0]);
			return "Unknown";
		}
	}

	/**
	 * @brief Returns the system audio status as its numeric code.
	 *
	 * @return The stored status code as an integer.
	 */
	int toInt(void) const {
		return str[0];
	}

	/**
	 * @brief Constructs a system-audio-status operand by decoding it from a CEC frame.
	 *
	 * @param[in] frame    The CEC frame to read the status byte from.
	 * @param[in] startPos Zero-based byte offset within @p frame at which to start.
	 */
	SystemAudioStatus (const CECFrame &frame, size_t startPos) : CECBytes (frame, startPos, MAX_LEN) {
	};

protected:
	/**
	 * @brief Returns the maximum system-audio-status length.
	 *
	 * @return The maximum length in bytes (@c MAX_LEN, 1).
	 */
	size_t getMaxLen() const {return MAX_LEN;}
};
/**
 * @brief Operand encoding audio status: mute state plus volume level.
 */
class AudioStatus : public CECBytes
{
   public:
   /**
    * @brief Operand size limit.
    */
   enum {
        MAX_LEN = 1,  ///< Audio-status length in bytes (1).
   };

   /**
    * @brief Enumerates the audio mute states.
    */
   enum {
          AUDIO_MUTE_OFF = 0x00,  ///< Audio is not muted.
          AUDIO_MUTE_ON  = 0x01,  ///< Audio is muted.
        };
	/**
	 * @brief Constructs an audio-status operand from a packed byte.
	 *
	 * @param[in] status Packed byte holding the mute bit (bit 7) and volume (bits 0..6).
	 */
	AudioStatus(uint8_t status) : CECBytes((uint8_t)status) { };

	/**
	 * @brief Returns a human-readable description of the mute state.
	 *
	 * @return "Audio Mute On" or "Audio Mute Off" according to the mute bit.
	 */
	const std::string toString(void) const
	{
		static const char *names_[] = {
			"Audio Mute Off",
			"Audio Mute On",
		};
		return names_[((str[0] & 0x80) >> 7)];
	}
	/**
	 * @brief Returns the audio mute state.
	 *
	 * @return 1 when audio is muted, 0 otherwise (the most significant bit of the byte).
	 */
	int getAudioMuteStatus(void) const {
            return ((str[0] & 0x80) >> 7);
           };
	/**
	 * @brief Returns the audio volume level.
	 *
	 * @return The volume level in the range 0..127 (the low 7 bits of the byte).
	 */
	int getAudioVolume(void) const {
		return (str[0] & 0x7F);
        }
	/**
	 * @brief Constructs an audio-status operand by decoding it from a CEC frame.
	 *
	 * @param[in] frame    The CEC frame to read the status byte from.
	 * @param[in] startPos Zero-based byte offset within @p frame at which to start.
	 */
	AudioStatus ( const CECFrame &frame, size_t startPos) : CECBytes (frame, startPos, MAX_LEN) {
	};
protected:
	/**
	 * @brief Returns the maximum audio-status length.
	 *
	 * @return The maximum length in bytes (@c MAX_LEN, 1).
	 */
	size_t getMaxLen() const {return MAX_LEN;}
};
/**
 * @brief Operand encoding a User Control (remote-key) command code.
 */
class UICommand : public CECBytes
{
public:
    /**
     * @brief Operand size limit.
     */
    enum {
        MAX_LEN = 1,  ///< UI command length in bytes (1).
    };

    /**
     * @brief Enumerates the supported user-control (UI) command codes.
     */
    enum {
           UI_COMMAND_VOLUME_UP          = 0x41,  ///< Volume Up.
           UI_COMMAND_VOLUME_DOWN        = 0x42,  ///< Volume Down.
           UI_COMMAND_MUTE               = 0x43,  ///< Mute toggle.
           UI_COMMAND_MUTE_FUNCTION      = 0x65,  ///< Mute Function (mute on).
           UI_COMMAND_RESTORE_FUNCTION   = 0x66,  ///< Restore Volume Function (mute off).
           UI_COMMAND_POWER_OFF_FUNCTION = 0x6C,  ///< Power Off Function.
           UI_COMMAND_POWER_ON_FUNCTION  = 0x6D,  ///< Power On Function.
	   UI_COMMAND_UP                 = 0x01,  ///< Navigate Up.
	   UI_COMMAND_DOWN               = 0x02,  ///< Navigate Down.
	   UI_COMMAND_LEFT               = 0x03,  ///< Navigate Left.
	   UI_COMMAND_RIGHT              = 0x04,  ///< Navigate Right.
	   UI_COMMAND_SELECT             = 0x00,  ///< Select / OK.
	   UI_COMMAND_HOME               = 0x09,  ///< Home menu.
	   UI_COMMAND_BACK               = 0x0D,  ///< Back / Return.
	   UI_COMMAND_NUM_0              = 0x20,  ///< Numeric key 0.
	   UI_COMMAND_NUM_1              = 0x21,  ///< Numeric key 1.
	   UI_COMMAND_NUM_2              = 0x22,  ///< Numeric key 2.
	   UI_COMMAND_NUM_3              = 0x23,  ///< Numeric key 3.
	   UI_COMMAND_NUM_4              = 0x24,  ///< Numeric key 4.
	   UI_COMMAND_NUM_5              = 0x25,  ///< Numeric key 5.
	   UI_COMMAND_NUM_6              = 0x26,  ///< Numeric key 6.
	   UI_COMMAND_NUM_7              = 0x27,  ///< Numeric key 7.
	   UI_COMMAND_NUM_8              = 0x28,  ///< Numeric key 8.
	   UI_COMMAND_NUM_9              = 0x29,  ///< Numeric key 9.
        };

    /**
     * @brief Constructs a UI-command operand from a command code.
     *
     * @param[in] command The user-control command code (one of the enumerated values).
     */
    UICommand(int command) : CECBytes((uint8_t)command) { };

    /**
     * @brief Returns the UI command as its numeric code.
     *
     * @return The stored user-control command code as an integer.
     */
    int toInt(void) const {
        return str[0];
    }

    /**
     * @brief Constructs a UI-command operand by decoding it from a CEC frame.
     *
     * @param[in] frame    The CEC frame to read the command byte from.
     * @param[in] startPos Zero-based byte offset within @p frame at which to start.
     */
    UICommand ( const CECFrame &frame, size_t startPos) : CECBytes (frame, startPos, MAX_LEN) {
	            };

protected:
	/**
	 * @brief Returns the maximum UI-command length.
	 *
	 * @return The maximum length in bytes (@c MAX_LEN, 1).
	 */
	size_t getMaxLen() const {return MAX_LEN;}
};
/**
 * @brief Operand encoding a bitmask of CEC device types.
 */
class AllDeviceTypes : public CECBytes
{
public:
    /**
     * @brief Operand size limit.
     */
    enum {
        MAX_LEN = 1,  ///< All-device-types length in bytes (1).
    };

    /**
     * @brief Bit positions of each device type within the bitmask.
     */
    enum {
        CEC_SWITCH = 2,  ///< CEC switch bit position (2).
        AUDIO_SYSTEM,  ///< Audio system bit position (3).
        PLAYBACK_DEVICE,  ///< Playback device bit position (4).
        TUNER,  ///< Tuner bit position (5).
        RECORDING_DEVICE,  ///< Recording device bit position (6).
        TV,  ///< TV bit position (7).
    };

    /**
     * @brief Constructs an all-device-types operand from a bitmask byte.
     *
     * @param[in] types Bitmask byte whose set bits indicate present device types.
     */
    AllDeviceTypes(uint8_t types) : CECBytes((uint8_t)types) { };


    /**
     * @brief Constructs an all-device-types operand by decoding it from a CEC frame.
     *
     * @param[in] frame    The CEC frame to read the bitmask byte from.
     * @param[in] startPos Zero-based byte offset within @p frame at which to start.
     */
    AllDeviceTypes( const CECFrame &frame, size_t startPos) : CECBytes (frame, startPos, MAX_LEN) {
    }

    /**
     * @brief Returns the set device types as human-readable strings.
     *
     * @return A vector naming each device type whose bit is set in the bitmask.
     */
    std::vector<std::string> getAllDeviceTypes(void) const {
        std::bitset <8> device_type_bits(str[0]);
        std::vector<std::string> all_device_types_str;
            for(int i = 7; i >= 2; i--){
                if(device_type_bits[i]){
                    switch(i){
                        case TV:
                            all_device_types_str.push_back("TV");
                            break;
                        case RECORDING_DEVICE:
                            all_device_types_str.push_back("Recording Device");
                            break;
                        case TUNER:
                            all_device_types_str.push_back("Tuner");
                            break;
                        case PLAYBACK_DEVICE:
                            all_device_types_str.push_back("Playback Device");
                           break;
                        case AUDIO_SYSTEM:
                            all_device_types_str.push_back("Audio System");
                            break;
                        case CEC_SWITCH:
                            all_device_types_str.push_back("CEC Switch");
                            break;
                    }
                }
            }
            return all_device_types_str;
    }

    /**
     * @brief Reports whether the TV bit is set.
     *
     * @return @c true when the TV device-type bit is set; @c false otherwise.
     */
    bool isDeviceTypeTV(void) const {
            bool deviceTypeTV = false;
            std::bitset<8> device_type_bits(str[0]);
            if(device_type_bits[TV]){
                    deviceTypeTV = true;
            }
            return deviceTypeTV;
    }
    /**
     * @brief Reports whether the recording-device bit is set.
     *
     * @return @c true when the recording-device bit is set; @c false otherwise.
     */
    bool isRecordingDevice(void) const {
            bool recordingDevice = false;
            std::bitset<8> device_type_bits(str[0]);
            if(device_type_bits[RECORDING_DEVICE]){
                    recordingDevice = true;
            }
            return recordingDevice;
    }
    /**
     * @brief Reports whether the tuner bit is set.
     *
     * @return @c true when the tuner bit is set; @c false otherwise.
     */
    bool isDeviceTypeTuner(void) const {
            bool deviceTypeTuner = false;
            std::bitset<8> device_type_bits(str[0]);
            if(device_type_bits[TUNER]){
                    deviceTypeTuner = true;
            }
            return deviceTypeTuner;
    }
    /**
     * @brief Reports whether the playback-device bit is set.
     *
     * @return @c true when the playback-device bit is set; @c false otherwise.
     */
    bool isPlaybackDevice(void) const {
            bool playbackDevice = false;
            std::bitset<8> device_type_bits(str[0]);
            if(device_type_bits[PLAYBACK_DEVICE]){
                    playbackDevice = true;
            }
            return playbackDevice;
    }
    /**
     * @brief Reports whether the audio-system bit is set.
     *
     * @return @c true when the audio-system bit is set; @c false otherwise.
     */
    bool isDeviceTypeAudioSystem(void) const {
            bool audioSystem = false;
            std::bitset<8> device_type_bits(str[0]);
            if(device_type_bits[AUDIO_SYSTEM]){
                    audioSystem = true;
            }
            return audioSystem;
    }
    /**
     * @brief Reports whether the CEC-switch bit is set.
     *
     * @return @c true when the CEC-switch bit is set; @c false otherwise.
     */
    bool isDeviceTypeCECSwitch(void) const {
            bool cecSwitch = false;
            std::bitset<8> device_type_bits(str[0]);
            if(device_type_bits[CEC_SWITCH]){
                    cecSwitch = true;
            }
            return cecSwitch;
    }

protected:
    /**
     * @brief Returns the maximum all-device-types length.
     *
     * @return The maximum length in bytes (@c MAX_LEN, 1).
     */
    size_t getMaxLen() const {return MAX_LEN;}
};
/**
 * @brief Operand encoding a Remote Control (RC) profile.
 *
 * Describes the remote-control profile a device supports, stored in up to
 * four bytes (@c MAX_LEN).
 */
class RcProfile : public CECBytes
{
public:
    /**
     * @brief Operand size limit.
     */
    enum {
        MAX_LEN = 4,  ///< Remote-control profile length in bytes (4).
    };

    /**
     * @brief Bit positions and flags used within the RC profile byte.
     */
    enum {
        MEDIA_CONTEXT_MENU,  ///< Media context-sensitive menu bit position (0).
        MEDIA_TOP_MENU,  ///< Media top menu bit position (1).
        CONTENTS_MENU,  ///< Contents menu bit position (2).
        DEVICE_SETUP_MENU,  ///< Device setup menu bit position (3).
        DEVICE_ROOT_MENU,  ///< Device root menu bit position (4).
        RC_PROFILE_BIT = 6,  ///< Profile-type selector bit position (6): 0 = TV profile, 1 = source profile.
    };

    /**
     * @brief Constructs an RC-profile operand from a profile byte.
     *
     * @param[in] info The remote-control profile byte.
     */
    RcProfile(uint8_t info) : CECBytes((uint8_t)info) { };


    /**
     * @brief Constructs an RC-profile operand by decoding it from a CEC frame.
     *
     * @param[in] frame    The CEC frame to read the profile bytes from.
     * @param[in] startPos Zero-based byte offset within @p frame at which to start.
     * @param[in] len      Number of bytes to consume for the profile.
     * @note This frame constructor does NOT enforce the advertised @c MAX_LEN
     *       (4). The base CECBytes(frame, startPos, len) constructor validates
     *       length by calling the virtual getMaxLen() during base-class
     *       construction, where virtual dispatch resolves to
     *       CECBytes::getMaxLen() (== @c CECFrame::MAX_LENGTH, 128), not
     *       RcProfile::getMaxLen(). It therefore accepts any @p len the caller
     *       supplies, up to the frame's remaining byte count.
     * @pre Callers must pass @p len <= @c MAX_LEN (4) to obtain a correctly
     *      bounded RC-profile operand; larger values are accepted but violate
     *      the operand's advertised size.
     */
    RcProfile( const CECFrame &frame, size_t startPos, size_t len) : CECBytes (frame, startPos, len) {
    };

    /**
     * @brief Returns the decoded RC profile as human-readable strings.
     *
     * @return A vector describing the profile type and the supported menus.
     */
    std::vector<std::string> getRcProfile(void) const {

            std::vector<std::string> rc_profile_str;
            std::bitset <8> profile_bit(str[0]);
            if(!profile_bit[RC_PROFILE_BIT]){
                rc_profile_str.push_back("RC Profile TV");
                    if((str[0] & 0xE) == 0xE){
                       rc_profile_str.push_back("RC Profile 4");
                    }
                    else if((str[0] & 0xA) == 0xA){
                        rc_profile_str.push_back("RC Profile 3");
                    }
                    else if((str[0] & 0x6) == 0x6){
                        rc_profile_str.push_back("RC Profile 2");
                    }
                    else if((str[0] & 0x2) == 0x2){
                        rc_profile_str.push_back("RC Profile 1");
                    }
                    else{
                        rc_profile_str.push_back("None of the Profiles");
                    }
            }

            else if(profile_bit[RC_PROFILE_BIT]){
                rc_profile_str.push_back("RC Profile Source");
                for(int i = 4; i >= 0; i--){
                    if(profile_bit[i]){
                        switch(i){
                            case DEVICE_ROOT_MENU:
                                rc_profile_str.push_back("Source can handle Device Root Menu - 0x9");
                                break;
                            case DEVICE_SETUP_MENU:
                                rc_profile_str.push_back("Source can handle Device Setup Menu - 0x0A");
                                break;
                            case CONTENTS_MENU:
                                rc_profile_str.push_back("Source can handle Contents Menu - 0x0B");
                                break;
                            case MEDIA_TOP_MENU:
                                rc_profile_str.push_back("Source can handle Media Top Menu - 0x10");
                                break;
                            case MEDIA_CONTEXT_MENU:
                                rc_profile_str.push_back("Source can handle Media Context-Sensitive Menu - 0x11 ");
                                break;
                        }
                    }
                }
            }

    return rc_profile_str;
    }
    /**
     * @brief Returns the raw RC-profile bytes.
     *
     * Collects successive profile bytes while the continuation bit (bit 7) is set.
     *
     * @return A vector of the raw profile bytes.
     */
    std::vector<uint8_t> getRcProfileVal(void) const {
        std::vector<uint8_t> rcProfileVal;
        size_t i = 0;
        while(i < str.size()) {
                rcProfileVal.push_back(str[i]);
                if((str[i] & 0x80)){
                        i++;
                }
                else{
                        break;
                }
        }
        return rcProfileVal;
    }

    /**
     * @brief Reports whether this is a TV remote-control profile.
     *
     * @return @c true when the profile-type bit indicates a TV profile; @c false otherwise.
     */
    bool isRcProfileTv(void) const {
        bool rcProfileTv = false;
        std::bitset<8> profile_bit(str[0]);
        if(!profile_bit[RC_PROFILE_BIT]){
            rcProfileTv = true;
        }
        return rcProfileTv;
    }

    /**
     * @brief Reports whether this is a source remote-control profile.
     *
     * @return @c true when the profile-type bit indicates a source profile; @c false otherwise.
     */
    bool isRcProfileSource(void) const {
        bool rcProfileSource = false;
        std::bitset<8> profile_bit(str[0]);
        if(profile_bit[RC_PROFILE_BIT]) {
            rcProfileSource = true;
        }
        return rcProfileSource;
    }

    /**
     * @brief Reports whether the device handles the Device Root Menu.
     *
     * @return @c true when the root-menu bit is set; @c false otherwise.
     */
    bool rootMenuHandling(void) const {
        bool rootMenuHandle = false;
        std::bitset<8> profile_bit(str[0]);
        if(profile_bit[DEVICE_ROOT_MENU]) {
            rootMenuHandle = true;
        }
        return rootMenuHandle;
    }

    /**
     * @brief Reports whether the device handles the Device Setup Menu.
     *
     * @return @c true when the setup-menu bit is set; @c false otherwise.
     */
    bool setupMenuHandling(void) const {
        bool setupMenuHandle = false;
        std::bitset<8> profile_bit(str[0]);
        if(profile_bit[DEVICE_SETUP_MENU]) {
            setupMenuHandle = true;
        }
        return setupMenuHandle;
    }

    /**
     * @brief Reports whether the device handles the Contents Menu.
     *
     * @return @c true when the contents-menu bit is set; @c false otherwise.
     */
    bool contentsMenuHandling(void) const {
        bool contentsMenuHandle = false;
        std::bitset<8> profile_bit(str[0]);
        if(profile_bit[CONTENTS_MENU]) {
            contentsMenuHandle = true;
        }
        return contentsMenuHandle;
    }

    /**
     * @brief Reports whether the device handles the Media Top Menu.
     *
     * @return @c true when the media-top-menu bit is set; @c false otherwise.
     */
    bool mediaTopMenuHandling(void) const {
        bool mediaTopMenuHandle = false;
        std::bitset<8> profile_bit(str[0]);
        if(profile_bit[MEDIA_TOP_MENU]) {
            mediaTopMenuHandle = true;
        }
        return mediaTopMenuHandle;
    }

    /**
     * @brief Reports whether the device handles the Media Context-Sensitive Menu.
     *
     * @return @c true when the media-context-menu bit is set; @c false otherwise.
     */
    bool contextSensitiveMenuHandling(void) const {
        bool contextSensitiveMenuHandle = false;
        std::bitset<8> profile_bit(str[0]);
        if(profile_bit[MEDIA_CONTEXT_MENU]) {
            contextSensitiveMenuHandle = true;
        }
        return contextSensitiveMenuHandle;
    }

protected:
        /**
         * @brief Returns the maximum RC-profile length.
         *
         * @return The maximum length in bytes (@c MAX_LEN, 4).
         */
        size_t getMaxLen() const {return MAX_LEN;}
};
/**
 * @brief Operand encoding a device-features bitmask.
 *
 * Advertises the optional CEC features a device supports, stored in up to
 * four bytes (@c MAX_LEN).
 */
class DeviceFeatures : public CECBytes
{
public:
    /**
     * @brief Operand size limit.
     */
    enum {
        MAX_LEN = 4,  ///< Device-features length in bytes (4).
    };

    /**
     * @brief Bit positions of each supported feature within the features byte.
     */
    enum {
        RESERVED,  ///< Reserved bit position (0).
        ARC_RX_SUPPORT,  ///< Source supports ARC Rx bit position (1).
        SINK_ARC_TX_SUPPORT,  ///< Sink supports ARC Tx bit position (2).
        SET_AUDIO_RATE_SUPPORT,  ///< Source supports "Set Audio Rate" bit position (3).
        CONTROLLED_BY_DECK,  ///< Supports being controlled by deck control bit position (4).
        SET_OSD_STRING_SUPPORT,  ///< TV supports "Set OSD String" bit position (5).
        RECORD_TV_SCREEN_SUPPORT,  ///< TV supports "Record TV Screen" bit position (6).
    };

    /**
     * @brief Constructs a device-features operand from a features byte.
     *
     * @param[in] info The device-features bitmask byte.
     */
    DeviceFeatures(uint8_t info) : CECBytes((uint8_t)info) { };


    /**
     * @brief Constructs a device-features operand by decoding it from a CEC frame.
     *
     * @param[in] frame    The CEC frame to read the features bytes from.
     * @param[in] startPos Zero-based byte offset within @p frame at which to start.
     * @param[in] len      Number of bytes to consume for the features.
     * @note This frame constructor does NOT enforce the advertised @c MAX_LEN
     *       (4). The base CECBytes(frame, startPos, len) constructor validates
     *       length by calling the virtual getMaxLen() during base-class
     *       construction, where virtual dispatch resolves to
     *       CECBytes::getMaxLen() (== @c CECFrame::MAX_LENGTH, 128), not
     *       DeviceFeatures::getMaxLen(). It therefore accepts any @p len the
     *       caller supplies, up to the frame's remaining byte count.
     * @pre Callers must pass @p len <= @c MAX_LEN (4) to obtain a correctly
     *      bounded device-features operand; larger values are accepted but
     *      violate the operand's advertised size.
     */
    DeviceFeatures( const CECFrame &frame, size_t startPos, size_t len) : CECBytes (frame, startPos, len) {};

    /**
     * @brief Returns the supported features as human-readable strings.
     *
     * @return A vector naming each feature whose bit is set in the features byte.
     */
    std::vector<std::string> getDeviceFeatures() const{

            std::vector<std::string> device_features_str;
            std::bitset<8> features_bit(str[0]);

            for(int i = 6; i >= 0; i--){
                if(features_bit[i]){
                    switch(i){
                        case RECORD_TV_SCREEN_SUPPORT:
                            device_features_str.push_back("TV Supports <Record TV Screen>");
                            break;
                        case SET_OSD_STRING_SUPPORT:
                            device_features_str.push_back("TV supports <Set OSD String>");
                            break;
                        case CONTROLLED_BY_DECK:
                            device_features_str.push_back("Supports being controlled by Deck Control");
                            break;
                        case SET_AUDIO_RATE_SUPPORT:
                            device_features_str.push_back("Source supports <Set Audio Rate>");
                           break;
                        case SINK_ARC_TX_SUPPORT:
                            device_features_str.push_back("Sink supports ARC Tx");
                            break;
                        case ARC_RX_SUPPORT:
                            device_features_str.push_back("Source supports ARC Rx");
                            break;
                        case RESERVED:
                            device_features_str.push_back("Reserved");
                            break;
                    }
                }
            }

            return device_features_str;
      }

      /**
       * @brief Returns the raw device-features bytes.
       *
       * Collects successive feature bytes while the continuation bit (bit 7) is set.
       *
       * @return A vector of the raw feature bytes.
       */
      std::vector<uint8_t> getDeviceFeaturesVal(void) const {
        std::vector<uint8_t> deviceFeaturesVal;
        size_t i = 0;
        while(i < str.size()) {
                deviceFeaturesVal.push_back(str[i]);
                if((str[i] & 0x80)){
                        i++;
                }
                else{
                        break;
                }
        }
        return deviceFeaturesVal;
    }

    /**
     * @brief Reports whether the TV supports "Record TV Screen".
     *
     * @return @c true when the corresponding feature bit is set; @c false otherwise.
     */
    bool tvRecordScreenSupportBit(void) const {
        bool tvRecordScreenSupport = false;
        std::bitset<8> features_bit(str[0]);
        if(features_bit[RECORD_TV_SCREEN_SUPPORT]){
            tvRecordScreenSupport = true;
        }
        return tvRecordScreenSupport;
    }

    /**
     * @brief Reports whether the TV supports "Set OSD String".
     *
     * @return @c true when the corresponding feature bit is set; @c false otherwise.
     */
    bool tVSetOSDStringSupportBit(void) const {
        bool tVSetOSDStringSupport = false;
        std::bitset<8> features_bit(str[0]);
        if(features_bit[SET_OSD_STRING_SUPPORT]){
            tVSetOSDStringSupport = true;
        }
        return tVSetOSDStringSupport;
    }

    /**
     * @brief Reports whether the device supports being controlled by deck control.
     *
     * @return @c true when the corresponding feature bit is set; @c false otherwise.
     */
    bool controlledByDeckSupportBit(void) const {
        bool controlledByDeckSupport = false;
        std::bitset<8> features_bit(str[0]);
        if(features_bit[CONTROLLED_BY_DECK]){
            controlledByDeckSupport = true;
        }
        return controlledByDeckSupport;
    }

    /**
     * @brief Reports whether the source supports "Set Audio Rate".
     *
     * @return @c true when the corresponding feature bit is set; @c false otherwise.
     */
    bool setAudioRateSupportBit(void) const {
        bool  setAudioRateSupport = false;
        std::bitset<8> features_bit(str[0]);
        if(features_bit[SET_AUDIO_RATE_SUPPORT]){
            setAudioRateSupport = true;
        }
        return setAudioRateSupport;
    }

    /**
     * @brief Reports whether the sink supports ARC Tx.
     *
     * @return @c true when the corresponding feature bit is set; @c false otherwise.
     */
    bool arcTxSupportBit(void) const {
        bool  arcTxSupport = false;
        std::bitset<8> features_bit(str[0]);
        if(features_bit[SINK_ARC_TX_SUPPORT]){
            arcTxSupport = true;
        }
        return arcTxSupport;
    }

    /**
     * @brief Reports whether the source supports ARC Rx.
     *
     * @return @c true when the corresponding feature bit is set; @c false otherwise.
     */
    bool arcRxSupportBit(void) const {
        bool  arcRxSupport = false;
        std::bitset<8> features_bit(str[0]);
        if(features_bit[ARC_RX_SUPPORT]){
            arcRxSupport = true;
        }
        return arcRxSupport;
    }

protected:
        /**
         * @brief Returns the maximum device-features length.
         *
         * @return The maximum length in bytes (@c MAX_LEN, 4).
         */
        size_t getMaxLen() const {return MAX_LEN;}
};
/**
 * @brief Operand encoding audio/video latency information.
 *
 * Carries video latency, latency flags, and (optionally) audio output delay
 * in up to three bytes (@c MAX_LEN).
 */
class LatencyInfo : public CECBytes
{
public:
    /**
     * @brief Operand size limit.
     */
    enum {
        MAX_LEN = 3,  ///< Latency-info length in bytes (3).
    };
    /**
     * @brief Constructs a latency-info operand from a single byte.
     *
     * @param[in] info The initial latency-info byte.
     * @warning Single-byte construction: this constructor stores exactly ONE
     *          byte (str.size() == 1). getLatencyFlags() and
     *          getAudioOutputDelay() access str[1] (the latter reads it
     *          unconditionally - see those methods), so calling them on an
     *          operand built with this constructor reads past the single
     *          stored byte: an out-of-bounds access with undefined behavior.
     *          Only getVideoLatency() (which reads str[0]) is safe on a
     *          one-byte operand.
     */
    LatencyInfo(uint8_t info) : CECBytes((uint8_t)info) { };


    /**
     * @brief Constructs a latency-info operand by decoding it from a CEC frame.
     *
     * @param[in] frame    The CEC frame to read the latency bytes from.
     * @param[in] startPos Zero-based byte offset within @p frame at which to start.
     * @param[in] len      Number of bytes to consume for the latency info.
     * @note This frame constructor does NOT enforce the advertised @c MAX_LEN
     *       (3). The base CECBytes(frame, startPos, len) constructor validates
     *       length by calling the virtual getMaxLen() during base-class
     *       construction, where virtual dispatch resolves to
     *       CECBytes::getMaxLen() (== @c CECFrame::MAX_LENGTH, 128), not
     *       LatencyInfo::getMaxLen(). It therefore accepts any @p len the
     *       caller supplies, up to the frame's remaining byte count.
     * @pre Callers must pass @p len <= @c MAX_LEN (3) to obtain a correctly
     *      bounded latency-info operand; larger values are accepted but
     *      violate the operand's advertised size.
     */
    LatencyInfo ( const CECFrame &frame, size_t startPos, size_t len) : CECBytes (frame, startPos, len) {};

    /**
     * @brief Returns the video latency value.
     *
     * @return The video latency byte.
     */
    uint8_t getVideoLatency(void) const {
        return str[0];
    }

    /**
     * @brief Returns the latency flags value.
     *
     * @return The latency-flags byte.
     * @pre The operand must hold at least two bytes (str.size() >= 2).
     * @warning No bounds check: this accessor returns str[1] directly. When
     *          the operand holds fewer than two bytes (for example one built
     *          from the single-byte LatencyInfo(uint8_t) constructor), this is
     *          an out-of-bounds read with undefined behavior.
     */
    uint8_t getLatencyFlags(void) const {
            return str[1];
    }

    /**
     * @brief Returns the audio output delay, when present.
     *
     * @return When the latency-flags byte (str[1]) equals 0x3 and exactly three
     *         bytes are stored, the audio output delay byte (str[2]); the value
     *         0xFF is returned only when that combined condition is false and no
     *         out-of-bounds access occurs (see the warning below).
     * @pre The operand must hold at least two bytes (str.size() >= 2); a delay
     *      value is only meaningful when it holds three (str.size() == 3).
     * @warning The guard is written with the bitwise operator
     *          @c (str[1]==0x3) & (str.size()==3), which is NOT short-circuiting,
     *          so str[1] is evaluated unconditionally BEFORE the size check. On
     *          an operand holding fewer than two bytes this reads out of bounds
     *          (undefined behavior), and the "otherwise 0xFF" result is
     *          therefore NOT guaranteed for such short operands.
     */
    int getAudioOutputDelay(void) const {
            int audio_output_delay = 0xFF;
            if((str[1] == 0x3) & (str.size() == 3)) {
                audio_output_delay = str[2];
            }
             return audio_output_delay;
    }


/**
 * @brief Returns the maximum latency-info length.
 *
 * @return The maximum length in bytes (@c MAX_LEN, 3).
 */
protected:
        size_t getMaxLen() const {return MAX_LEN;}
};

CCEC_END_NAMESPACE

#endif


/** @} */
/** @} */
