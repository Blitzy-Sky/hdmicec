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


#ifndef HDMI_CCEC_FRAME_
#define HDMI_CCEC_FRAME_

/**
 * @file CECFrame.hpp
 * @brief Declares CECFrame, the fixed-capacity byte container for raw CEC frames.
 *
 * A CECFrame stores the raw bytes of a single HDMI-CEC frame in a fixed-size
 * internal buffer and provides helpers to build, inspect, slice, and hex-dump
 * the frame contents. It is the low-level byte carrier used throughout the CCEC
 * transmit and receive paths, beneath the header, operand, and message types
 * that interpret those bytes.
 *
 * @see CECFrame
 */

#include <stdint.h>
#include <stddef.h>
#include <stdexcept>
#include "CCEC.hpp"

CCEC_BEGIN_NAMESPACE

/**
 * @brief Fixed-capacity byte container that holds a raw CEC frame's bytes.
 *
 * CECFrame pairs a fixed-size internal buffer (@c MAX_LENGTH bytes) with a
 * current length, modelling the raw byte payload of a single HDMI-CEC frame.
 * It supports construction from an existing byte range, byte-wise and bulk
 * append, sub-range extraction, indexed access, and a hexadecimal dump helper.
 * The type performs no CEC protocol interpretation itself; higher-level CCEC
 * types (headers, operands, and messages) parse and build on top of these
 * bytes.
 */
class CECFrame {
    public:
        /**
         * @brief Constructs a frame, optionally copying bytes from a source buffer.
         *
         * When @p buf is non-NULL, the first @p len bytes are copied into the
         * frame's internal storage and the length is set accordingly; otherwise
         * an empty frame (length 0) is created.
         *
         * @param[in] buf Source bytes to copy into the frame. May be NULL to
         *                create an empty frame. Defaults to NULL.
         * @param[in] len Number of bytes to copy from @p buf. Defaults to 0.
         */
        CECFrame(const uint8_t *buf = NULL, size_t len = 0);
        /**
         * @brief Clears the frame, resetting its length to zero.
         *
         * Discards any buffered bytes so the frame becomes empty. The backing
         * storage capacity (@c MAX_LENGTH) is unchanged.
         */
        void reset(void);
        /**
         * @brief Returns a copy of a contiguous sub-range of this frame.
         *
         * Extracts the bytes beginning at index @p start and returns them as a
         * new CECFrame. A @p len of 0 selects all bytes from @p start to the
         * current end of the frame.
         *
         * @param[in] start Zero-based index of the first byte to extract.
         * @param[in] len   Number of bytes to extract; 0 means to the end of the
         *                  frame. Defaults to 0.
         * @return A new CECFrame containing the extracted sub-range.
         */
        CECFrame subFrame(size_t start, size_t len = 0) const;
        /**
         * @brief Appends a single byte to the end of the frame.
         *
         * @param[in] byte The byte value to append.
         */
        void append(uint8_t byte);
        /**
         * @brief Appends a sequence of bytes to the end of the frame.
         *
         * @param[in] buf Pointer to the source bytes to append.
         * @param[in] len Number of bytes to append from @p buf.
         */
        void append(const uint8_t *buf, size_t len);
        /**
         * @brief Appends the bytes of another frame to the end of this frame.
         *
         * @param[in] frame The frame whose bytes are appended.
         */
        void append(const CECFrame &frame);
        /**
         * @brief Exposes the internal byte buffer pointer and its current length.
         *
         * @param[out] buf Receives a pointer to the frame's internal byte buffer.
         * @param[out] len Receives the current number of bytes in the frame.
         */
        void getBuffer(const uint8_t **buf, size_t *len) const;
        /**
         * @brief Returns a pointer to the internal byte buffer.
         *
         * @return Pointer to the frame's internal byte buffer.
         */
        const uint8_t * getBuffer(void) const;
        /**
         * @brief Returns the byte stored at the given index.
         *
         * @param[in] i Zero-based index of the byte to read.
         * @return The byte value stored at index @p i.
         */
        uint8_t at(size_t i) const;
        /**
         * @brief Returns the current number of bytes held in the frame.
         *
         * @return The current frame length, in bytes.
         */
        size_t length(void) const;
        /**
         * @brief Logs the frame's bytes as hexadecimal at the given log level.
         *
         * @param[in] level Log level at which the hex dump is emitted. Defaults
         *                  to 6.
         */
        void hexDump(int level=6) const;
        /**
         * @brief Provides mutable indexed access to a byte of the frame.
         *
         * @param[in] i Zero-based index of the byte to access.
         * @return A reference to the byte stored at index @p i.
         */
        uint8_t & operator[](size_t i);

        /**
         * @brief Compile-time capacity constants for the frame buffer.
         */
        enum {
            MAX_LENGTH = 128,  ///< Maximum storage capacity of the frame buffer, in bytes (128).
        };
    private:
        uint8_t buf_[MAX_LENGTH];
        size_t len_;
};

CCEC_END_NAMESPACE

#endif



/** @} */
/** @} */
