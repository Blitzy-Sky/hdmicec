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


#ifndef HDMI_CCEC_OPCODE_HPP_
#define HDMI_CCEC_OPCODE_HPP_

#include <iostream>

#include "CCEC.hpp"
#include "ccec/CECFrame.hpp"
#include "Assert.hpp"

#include "DataBlock.hpp"


CCEC_BEGIN_NAMESPACE

/**
 * @brief Maps a CEC opcode to its human-readable command name.
 *
 * Looks up the symbolic name string for a HDMI-CEC opcode value (for example
 * the opcode 0x82 maps to the string "Active Source"). The returned pointer
 * refers to a statically stored, null-terminated C string owned by the
 * implementation; callers must neither free nor modify it.
 *
 * @par Input parameter (direction [in])
 * The single argument is an input: the CEC opcode value (an @c Op_t) to
 * translate to a name. Its @c [in] direction is documented here in prose rather
 * than with an @c \@param tag because the same @c extern "C" function is also
 * declared without a parameter name in DataBlock.hpp, so a named @c \@param
 * would not resolve consistently across both declarations.
 *
 * @return Pointer to a const, null-terminated name string for the opcode; the
 *         storage is owned by the callee and must not be modified or freed.
 */
extern "C" const char *GetOpName(Op_t op);

/**
 * @brief HDMI-CEC command opcodes.
 *
 * Enumerates the one-byte operation codes carried in the opcode block of a CEC
 * message, as defined by the HDMI-CEC specification, together with two
 * middleware-internal sentinel values (@c POLLING and @c UNKNOWN) that never
 * appear on the CEC wire. For the specification opcodes, each value's numeric
 * code is the single byte transmitted in a CEC frame; the two sentinels are the
 * exceptions: @c POLLING (@c 0x200) and @c UNKNOWN (@c 0xFFFF) are not one-byte
 * wire values and are never transmitted.
 */
enum
{
	ACTIVE_SOURCE 					= 0x82, ///< Active Source: a source announces that it has become the active source.
	IMAGE_VIEW_ON 					= 0x04, ///< Image View On: wake a TV from standby to display an image.
	TEXT_VIEW_ON 					= 0x0D, ///< Text View On: wake a TV from standby to display text.
	INACTIVE_SOURCE 				= 0x9D, ///< Inactive Source: a source informs the TV that it is no longer active.
	REQUEST_ACTIVE_SOURCE 			= 0x85, ///< Request Active Source: ask the current active source to identify itself.
	ROUTING_CHANGE                  = 0x80, ///< Routing Change: notify a change of the active routing path between two physical addresses.
	ROUTING_INFORMATION             = 0x81, ///< Routing Information: report the current routing path (physical address).
	SET_STREAM_PATH                 = 0x86, ///< Set Stream Path: request the device at a physical address to become the active source.
	STANDBY 						= 0x36, ///< Standby: switch a device (or all devices) into standby.
	RECORD_OFF                      = 0X0B, ///< Record Off: request a recording device to stop recording.
	RECORD_ON                       = 0X09, ///< Record On: request a recording device to start recording.
	RECORD_STATUS                   = 0X0A, ///< Record Status: report the status of a recording request.
	RECORD_TV_SCREEN                = 0X0F, ///< Record TV Screen: request recording of the currently displayed TV source.
	CLEAR_ANALOGUE_TIMER            = 0X33, ///< Clear Analogue Timer: clear an analogue timer recording block.
	CLEAR_DIGITAL_TIMER             = 0X99, ///< Clear Digital Timer: clear a digital timer recording block.
	CLEAR_EXTERNAL_TIMER            = 0XA1, ///< Clear External Timer: clear an external timer recording block.
	SET_ANALOG_TIMER                = 0X34, ///< Set Analogue Timer: set up an analogue timer recording block.
	SET_DIGITAL_TIMER               = 0X97, ///< Set Digital Timer: set up a digital timer recording block.
	SET_EXTERNAL_TIMER              = 0XA2, ///< Set External Timer: set up an external timer recording block.
	SET_TIMER_PROGRAM_TITLE         = 0X67, ///< Set Timer Program Title: set the program title for a scheduled timer.
	TIMER_CLEARED_STATUS            = 0X43, ///< Timer Cleared Status: report the result of a clear-timer request.
	TIMER_STATUS                    = 0X35, ///< Timer Status: report the status and overlap of a programmed timer.
	GET_CEC_VERSION 				= 0x9F, ///< Get CEC Version: request the CEC version supported by a device.
	CEC_VERSION 					= 0x9E, ///< CEC Version: report the CEC version supported by a device.
	GIVE_PHYSICAL_ADDRESS 			= 0x83, ///< Give Physical Address: request a device to report its physical address.
	GET_MENU_LANGUAGE               = 0X91, ///< Get Menu Language: request the menu language of a device.
	REPORT_PHYSICAL_ADDRESS 		= 0x84, ///< Report Physical Address: broadcast a device's physical address and device type.
	SET_MENU_LANGUAGE               = 0X32, ///< Set Menu Language: inform devices of the selected menu language.
	DECK_CONTROL                    = 0X42, ///< Deck Control: control a playback deck (play, stop, eject, and so on).
	DECK_STATUS                     = 0X1B, ///< Deck Status: report the status of a playback deck.
	GIVE_DECK_STATUS                = 0X1A, ///< Give Deck Status: request a device to report its deck status.
	PLAY                            = 0X41, ///< Play: control the playback mode of a deck.
	GIVE_TUNER_DEVICE_STATUS        = 0X08, ///< Give Tuner Device Status: request a tuner to report its status.
	SELECT_ANALOGUE_SERVICE         = 0X92, ///< Select Analogue Service: tune to a specified analogue service.
	SELECT_DIGITAL_SERVICE          = 0X93, ///< Select Digital Service: tune to a specified digital service.
	TUNER_DEVICE_STATUS             = 0X07, ///< Tuner Device Status: report the status of a tuner device.
	TUNER_STEP_DECREMENT            = 0X06, ///< Tuner Step Decrement: tune to the previous service in the list.
	TUNER_STEP_INCREMENT            = 0X05, ///< Tuner Step Increment: tune to the next service in the list.
	DEVICE_VENDOR_ID 				= 0x87, ///< Device Vendor ID: broadcast the device's IEEE vendor identifier.
	GIVE_DEVICE_VENDOR_ID 			= 0x8C, ///< Give Device Vendor ID: request a device to broadcast its vendor ID.
	VENDOR_COMMAND                  = 0X89, ///< Vendor Command: send a vendor-specific command.
	VENDOR_COMMAND_WITH_ID          = 0XA0, ///< Vendor Command With ID: send a vendor-specific command carrying a vendor ID.
	VENDOR_REMOTE_BUTTON_DOWN       = 0X8A, ///< Vendor Remote Button Down: vendor-specific remote-control button press.
	VENDOR_REMOTE_BUTTON_UP         = 0X8B, ///< Vendor Remote Button Up: vendor-specific remote-control button release.
	SET_OSD_STRING 					= 0x64, ///< Set OSD String: display a text string on the TV's on-screen display.
	GIVE_OSD_NAME 					= 0x46, ///< Give OSD Name: request a device to report its preferred OSD name.
	SET_OSD_NAME 					= 0x47, ///< Set OSD Name: report the device's preferred OSD name.
	MENU_REQUEST                    = 0X8D, ///< Menu Request: request a device to show, hide, or query its menu.
	MENU_STATUS                     = 0X8E, ///< Menu Status: report the device's current menu state.
	USER_CONTROL_PRESSED            = 0X44, ///< User Control Pressed: forward a remote-control key press.
	USER_CONTROL_RELEASED           = 0X45, ///< User Control Released: forward a remote-control key release.
	GIVE_DEVICE_POWER_STATUS 		= 0x8F, ///< Give Device Power Status: request a device to report its power status.
	REPORT_POWER_STATUS 			= 0x90, ///< Report Power Status: report the device's current power status.
	FEATURE_ABORT 					= 0x00, ///< Feature Abort: indicate that a received command cannot be processed.
	ABORT 							= 0xFF, ///< Abort: generic abort message used to test or reject a feature.
	GIVE_AUDIO_STATUS               = 0X71, ///< Give Audio Status: request an amplifier to report its volume and mute status.
	GIVE_SYSTEM_AUDIO_MODE_STATUS   = 0X7D, ///< Give System Audio Mode Status: request the current system audio mode status.
	REPORT_AUDIO_STATUS             = 0X7A, ///< Report Audio Status: report the amplifier's volume level and mute state.
	REPORT_SHORT_AUDIO_DESCRIPTOR   = 0XA3, ///< Report Short Audio Descriptor: report supported audio formats (EDID short audio descriptors).
	REQUEST_SHORT_AUDIO_DESCRIPTOR  = 0XA4, ///< Request Short Audio Descriptor: request a device's short audio descriptors.
	SET_SYSTEM_AUDIO_MODE           = 0X72, ///< Set System Audio Mode: turn the system audio (ARC) mode on or off.
	SYSTEM_AUDIO_MODE_REQUEST       = 0X70, ///< System Audio Mode Request: request activation of system audio mode.
	SYSTEM_AUDIO_MODE_STATUS        = 0X7E, ///< System Audio Mode Status: report whether system audio mode is on or off.
	SET_AUDIO_RATE                  = 0X9A, ///< Set Audio Rate: set the audio rate used by the audio system.
	INITIATE_ARC                    = 0XC0, ///< Initiate ARC: request initiation of the Audio Return Channel.
	REPORT_ARC_INITIATED            = 0XC1, ///< Report ARC Initiated: confirm that the Audio Return Channel has been initiated.
	REPORT_ARC_TERMINATED           = 0XC2, ///< Report ARC Terminated: confirm that the Audio Return Channel has been terminated.
	REQUEST_ARC_INITIATION          = 0XC3, ///< Request ARC Initiation: request that ARC initiation begin.
	REQUEST_ARC_TERMINATION         = 0XC4, ///< Request ARC Termination: request that the Audio Return Channel be terminated.
	TERMINATE_ARC                   = 0XC5, ///< Terminate ARC: request termination of the Audio Return Channel.
	CDC_MESSAGE                     = 0XF8, ///< CDC Message: CEC Discovery and Configuration channel message.
	POLLING 						= 0x200, ///< Pseudo-opcode used for CEC polling/ping (not a wire opcode; 0x200).

	GIVE_FEATURES                   = 0xA5, ///< Give Features (CEC 2.0): request a device to report its feature set.
	REPORT_FEATURES                 = 0xA6, ///< Report Features (CEC 2.0): report CEC version, device types, and RC profile/features.
	REQUEST_CURRENT_LATENCY		= 0xA7, ///< Request Current Latency (CEC 2.0): request the current audio/video latency for a path.
	REPORT_CURRENT_LATENCY		= 0xA8, ///< Report Current Latency (CEC 2.0): report the current audio/video latency for a path.
	UNKNOWN                         = 0xFFFF ///< Sentinel for an unrecognized/unset opcode (0xFFFF).
};

/**
 * @brief Opcode data block of a CEC message.
 *
 * Wraps a single HDMI-CEC opcode value and implements the @ref DataBlock
 * serialization and decoding contract for it. An opcode occupies exactly one
 * byte on the CEC wire (see @c MAX_LEN). An @ref OpCode can be built directly
 * from an @c Op_t value or decoded from a received @ref CECFrame, and it renders
 * its symbolic name through @ref GetOpName for logging and diagnostics.
 *
 * @see DataBlock
 * @see CECFrame
 */
class OpCode : public DataBlock 
{
public:
    enum {
        MAX_LEN = 1, ///< Encoded length of an opcode in a CEC frame: 1 byte.
    };

    /**
     * @brief Constructs an opcode block from an opcode value.
     *
     * @param[in] opCode  The HDMI-CEC opcode to wrap (see the opcode enumeration).
     */
    OpCode(Op_t opCode) : opCode_(opCode) {};
    /**
     * @brief Constructs an opcode block by decoding it from a CEC frame.
     *
     * Reads the single opcode byte located at @p startPos within @p frame.
     *
     * @param[in] frame     The received CEC frame to decode the opcode from.
     * @param[in] startPos  Byte offset within @p frame at which the opcode is located.
     */
	OpCode(const CECFrame &frame, int startPos) : opCode_(frame.at(startPos)) {
    }
    /**
     * @brief Appends the opcode byte to a CEC frame.
     *
     * Serializes the wrapped opcode into @p frame by appending its single byte.
     *
     * @param[in,out] frame  The CEC frame to append the opcode byte to.
     * @return Reference to @p frame to allow call chaining.
     * @note When the wrapped opcode is @c POLLING no byte is appended, because a
     *       polling message carries no opcode byte on the CEC wire.
     */
	CECFrame &serialize(CECFrame &frame) const {
        if (opCode_ != POLLING) {
            frame.append(opCode_);
        }
        return frame;
    }
    /**
     * @brief Returns the wrapped opcode value.
     *
     * @return The HDMI-CEC opcode carried by this block.
     */
    virtual Op_t opCode(void) const {return opCode_;};
    /**
     * @brief Returns the human-readable name of the opcode.
     *
     * @return The symbolic opcode name as produced by @ref GetOpName.
     */
    virtual std::string toString(void) const {return GetOpName(opCode_);}
    /**
     * @brief Logs the opcode's symbolic name at debug level.
     */
    virtual void print(void) const {
    	CCEC_LOG( LOG_DEBUG , "Opcode : %s \n",toString().c_str());
    };
private:
    Op_t opCode_; ///< The wrapped HDMI-CEC opcode value.
};



CCEC_END_NAMESPACE;


#endif


/** @} */
/** @} */
