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
 * @defgroup HDMI_CEC_MSG_N_FRAME HDMI-CEC Messages and Frames
 * The CECFrame is a byte buffer that provides access to raw CEC bytes. CECFrame is guaranteed to be a
 * complete CEC Message that has the necessary data blocks:
 * @n @n
 * - The Header Data block (The byte that contains the initiator and destination address)
 * - The OpCode Data Block (The byte that contains the opcode).
 * - The Operand Data Block.(The bytes that contains the operands).
 *
 * In most cases application need not access CECFrame directly, but manipulate the raw bytes through the Message API.
 * @n @n
 * The Message API allows the application to send or receive high-level CEC message construct instead of raw bytes.
 * Basically for each CEC message (such as ActiveSource), there is a C++ class implementation representing it.
 * Each message class provides necessary getter and setter methods to access the properties of each message.
 * @n @n
 * This header defines the complete set of message classes present in the source.
 * There are 43 such classes, each deriving from @c DataBlock: ActiveSource,
 * ImageViewOn, TextViewOn, InActiveSource, RequestActiveSource, Standby,
 * GetCECVersion, CECVersion, SetMenuLanguage, GetMenuLanguage, GiveOSDName,
 * SetOSDName, SetOSDString, GivePhysicalAddress, ReportPhysicalAddress,
 * GiveDeviceVendorID, DeviceVendorID, GiveDevicePowerStatus, ReportPowerStatus,
 * Abort, FeatureAbort, RoutingChange, RoutingInformation, SetStreamPath,
 * RequestShortAudioDescriptor, ReportShortAudioDescriptor, SystemAudioModeRequest,
 * SetSystemAudioMode, GiveAudioStatus, ReportAudioStatus, UserControlPressed,
 * UserControlReleased, Polling, RequestArcInitiation, ReportArcInitiation,
 * RequestArcTermination, ReportArcTermination, InitiateArc, TerminateArc,
 * GiveFeatures, ReportFeatures, RequestCurrentLatency, and ReportCurrentLatency.
 * @n @n
 * Of these 43 classes, five have no corresponding MessageProcessor overload and
 * are therefore not dispatched to a typed handler: GiveAudioStatus,
 * RequestArcInitiation, ReportArcInitiation, RequestArcTermination, and
 * ReportArcTermination. The remaining 38 classes each have a MessageProcessor
 * overload. This inventory documents the source exactly as it exists: the header
 * contains 43 message classes (not 44).
 * @ingroup HDMI_CEC
 *
 * @defgroup HDMI_CEC_MSG_N_FRAME_CLASSES HDMI-CEC Messages and Frames Classes
 * Described the details of High Level Class diagram and its functionalities.
 * @ingroup HDMI_CEC_MSG_N_FRAME
 *
 * @defgroup HDMI_CEC_MULTI_SYNC HDMI-CEC Messages - Asynchronous Vs. Synchronous
 * When messages converge on the logical buses, they are queued for sending opportunities on the physical bus.
 * The waiting time for such send to complete, though short in most cases, can be problematic to some interactive
 * real-time applications. It is recommended that the applications always send CEC messages asynchronously via
 * the Connection API and use the listener APIs to monitor response messages or device state changes.
 * The CEC library offers abundant APIs to facilitate such asynchronous implementation and the application is
 * encouraged to make full use of them.
 * @n @n
 * Given the vast variance of HDMI-CEC support from the off-the-self media devices, it is not recommended that
 * application wait for the response from a destination device. Even if the request message is sent out successfully,
 * the destination device may choose to ignore the request. The recommended approach is again to send the request
 * asynchronously and use the listener to monitor responses.
 * @n @n
 * Overall, given the asynchronous nature of HDMI-CEC, application should always opt to use Asynchronous APIs
 * as first choice. And for same reasons, the RDK CEC library offers only limited support for Synchronous APIs.
 * @ingroup HDMI_CEC_MSG_N_FRAME
 */

/**
* @defgroup hdmicec HDMI-CEC Middleware
* @{
* @defgroup ccec CCEC Library
* @{
**/


#ifndef HDMI_CCEC_MESSAGES_HPP_
#define HDMI_CCEC_MESSAGES_HPP_

#include <stdint.h>

#include <iostream>
#include "CCEC.hpp"
#include "Assert.hpp"
#include "ccec/CECFrame.hpp"
#include "DataBlock.hpp"
#include "OpCode.hpp"
#include "Operands.hpp"
#include "ccec/Util.hpp"

CCEC_BEGIN_NAMESPACE
/**
 * @brief The Message API allows the application to send or receive high-level CEC message construct instead of raw bytes.
 *
 * Basically for each CEC message (such as ActiveSource), there is a C++ class implementation representing it.
 * Each message class provides necessary getter and setter methods to access the properties of each message.
 * @ingroup HDMI_CEC_MSG_N_FRAME_CLASSES
 */
class ActiveSource : public DataBlock 
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c ACTIVE_SOURCE opcode.
     */
    Op_t opCode(void) const {return ACTIVE_SOURCE;}

	/**
	 * @brief Constructs the Active Source message from the supplied operand(s).
	 *
	 * @param[in] physicalAddress  Physical address of the device announcing itself as the active source.
	 */
	ActiveSource(const PhysicalAddress &physicalAddress) : physicalAddress(physicalAddress) {
    }

	/**
	 * @brief Constructs the Active Source message by decoding it from a received CEC frame.
	 *
	 * @param[in] frame     The received CEC frame to decode the message from.
	 * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
	 */
	ActiveSource(const CECFrame &frame, int startPos = 0) 
    : physicalAddress(frame, startPos)
    {
    }
	/**
	 * @brief Serializes this message's operand(s) by appending them to a CEC frame.
	 *
	 * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
	 * @return Reference to @p frame to allow call chaining.
	 */
	CECFrame &serialize(CECFrame &frame) const {
        CCEC_LOG( LOG_DEBUG, "%s \n",physicalAddress.toString().c_str());
        return physicalAddress.serialize(frame);
	}

    /**
     * @brief Logs the contents of this message for debugging.
     */
    void print(void) const {
        CCEC_LOG( LOG_DEBUG, "%s : %s  : %s \n",GetOpName(opCode()),physicalAddress.name().c_str(),physicalAddress.toString().c_str());
    }
public:
    PhysicalAddress physicalAddress;  ///< Physical address of the active source device.
};

/**
 * @brief Image View On (@c IMAGE_VIEW_ON) CEC message.
 *
 * Wake a TV from standby to display an image.
 * This message carries no operands.
 *
 * @see DataBlock
 */
class ImageViewOn : public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c IMAGE_VIEW_ON opcode.
     */
    Op_t opCode(void) const {return IMAGE_VIEW_ON;}
};


/**
 * @brief Text View On (@c TEXT_VIEW_ON) CEC message.
 *
 * Wake a TV from standby to display text.
 * This message carries no operands.
 *
 * @see DataBlock
 */
class TextViewOn : public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c TEXT_VIEW_ON opcode.
     */
    Op_t opCode(void) const {return TEXT_VIEW_ON;}
};

/**
 * @brief Inactive Source (@c INACTIVE_SOURCE) CEC message.
 *
 * A source informs the TV that it is no longer active.
 * Carries the source device's @ref PhysicalAddress.
 *
 * @see DataBlock
 */
class InActiveSource : public DataBlock 
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c INACTIVE_SOURCE opcode.
     */
    Op_t opCode(void) const {return INACTIVE_SOURCE;}

	/**
	 * @brief Constructs the Inactive Source message from the supplied operand(s).
	 *
	 * @param[in] physicalAddress  Physical address of the source that is becoming inactive.
	 */
	InActiveSource(const PhysicalAddress &physicalAddress) : physicalAddress(physicalAddress) {}

	/**
	 * @brief Constructs the Inactive Source message by decoding it from a received CEC frame.
	 *
	 * @param[in] frame     The received CEC frame to decode the message from.
	 * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
	 */
	InActiveSource(const CECFrame &frame, int startPos = 0) 
    : physicalAddress(frame, startPos)
    {
    }

	/**
	 * @brief Serializes this message's operand(s) by appending them to a CEC frame.
	 *
	 * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
	 * @return Reference to @p frame to allow call chaining.
	 */
	CECFrame &serialize(CECFrame &frame) const {
        CCEC_LOG( LOG_DEBUG, "%s \n",physicalAddress.toString().c_str());
        return physicalAddress.serialize(frame);
	}

    /**
     * @brief Logs the contents of this message for debugging.
     */
    void print(void) const {
        CCEC_LOG( LOG_DEBUG, "%s : %s : %s  \n",GetOpName(opCode()),physicalAddress.name().c_str(),physicalAddress.toString().c_str());
    }
public:
    PhysicalAddress physicalAddress;  ///< Physical address of the source that is no longer active.
};

/**
 * @brief Request Active Source (@c REQUEST_ACTIVE_SOURCE) CEC message.
 *
 * Ask the current active source to identify itself.
 * This message carries no operands.
 *
 * @see DataBlock
 */
class RequestActiveSource : public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c REQUEST_ACTIVE_SOURCE opcode.
     */
    Op_t opCode(void) const {return REQUEST_ACTIVE_SOURCE;}
};

/**
 * @brief Standby (@c STANDBY) CEC message.
 *
 * Switch a device (or all devices) into standby.
 * This message carries no operands.
 *
 * @see DataBlock
 */
class Standby : public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c STANDBY opcode.
     */
    Op_t opCode(void) const {return STANDBY;}
};

/**
 * @brief Get CEC Version (@c GET_CEC_VERSION) CEC message.
 *
 * Request the CEC version supported by a device.
 * This message carries no operands.
 *
 * @see DataBlock
 */
class GetCECVersion : public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c GET_CEC_VERSION opcode.
     */
    Op_t opCode(void) const {return GET_CEC_VERSION;}
};

/**
 * @brief CEC Version (@c CEC_VERSION) CEC message.
 *
 * Report the CEC version supported by a device.
 * Carries the reported CEC @ref Version.
 *
 * @see DataBlock
 */
class CECVersion : public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c CEC_VERSION opcode.
     */
    Op_t opCode(void) const {return CEC_VERSION;}

    /**
     * @brief Constructs the CEC Version message from the supplied operand(s).
     *
     * @param[in] version  CEC version to report.
     */
    CECVersion(const Version &version) : version(version) {}

    /**
     * @brief Constructs the CEC Version message by decoding it from a received CEC frame.
     *
     * @param[in] frame     The received CEC frame to decode the message from.
     * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
     */
    CECVersion(const CECFrame &frame, int startPos = 0)
    : version(frame, startPos)
    {
    }

	/**
	 * @brief Serializes this message's operand(s) by appending them to a CEC frame.
	 *
	 * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
	 * @return Reference to @p frame to allow call chaining.
	 */
	CECFrame &serialize(CECFrame &frame) const {
        return version.serialize(frame);
	}

    /**
     * @brief Logs the contents of this message for debugging.
     */
    void print(void) const {
        CCEC_LOG( LOG_DEBUG, "Version : %s \n",version.toString().c_str());
    }

    Version version;  ///< CEC version reported by the device.
};

/**
 * @brief Set Menu Language (@c SET_MENU_LANGUAGE) CEC message.
 *
 * Inform devices of the selected menu language.
 * Carries the selected menu @ref Language.
 *
 * @see DataBlock
 */
class SetMenuLanguage : public DataBlock
{

public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c SET_MENU_LANGUAGE opcode.
     */
    Op_t opCode(void) const {return SET_MENU_LANGUAGE;}

    /**
     * @brief Constructs the Set Menu Language message from the supplied operand(s).
     *
     * @param[in] language  Menu language to set.
     */
    SetMenuLanguage(const Language &language) : language(language) {};

    /**
     * @brief Constructs the Set Menu Language message by decoding it from a received CEC frame.
     *
     * @param[in] frame     The received CEC frame to decode the message from.
     * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
     */
    SetMenuLanguage(const CECFrame &frame, int startPos = 0)
    : language(frame, startPos)
    {
    }

	/**
	 * @brief Serializes this message's operand(s) by appending them to a CEC frame.
	 *
	 * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
	 * @return Reference to @p frame to allow call chaining.
	 */
	CECFrame &serialize(CECFrame &frame) const {
	    return language.serialize(frame);
	}

    /**
     * @brief Logs the contents of this message for debugging.
     */
    void print(void) const {
        CCEC_LOG( LOG_DEBUG, "Language : %s \n",language.toString().c_str());
    }

    Language language;  ///< Selected menu language.
};

/**
 * @brief Get Menu Language (@c GET_MENU_LANGUAGE) CEC message.
 *
 * Request the menu language of a device.
 * This message carries no operands.
 *
 * @see DataBlock
 */
class GetMenuLanguage: public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c GET_MENU_LANGUAGE opcode.
     */
    Op_t opCode(void) const {return GET_MENU_LANGUAGE;}
};

/**
 * @brief Give OSD Name (@c GIVE_OSD_NAME) CEC message.
 *
 * Request a device to report its preferred OSD name.
 * This message carries no operands.
 *
 * @see DataBlock
 */
class GiveOSDName: public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c GIVE_OSD_NAME opcode.
     */
    Op_t opCode(void) const {return GIVE_OSD_NAME;}
};

/**
 * @brief Set OSD Name (@c SET_OSD_NAME) CEC message.
 *
 * Report the device's preferred OSD name.
 * Carries the device's @ref OSDName.
 *
 * @see DataBlock
 */
class SetOSDName: public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c SET_OSD_NAME opcode.
     */
    Op_t opCode(void) const {return SET_OSD_NAME;}

    /**
     * @brief Constructs the Set OSD Name message from the supplied operand(s).
     *
     * @param[in] osdName  OSD name to report.
     */
    SetOSDName(const OSDName &osdName) : osdName(osdName) {};

    /**
     * @brief Constructs the Set OSD Name message by decoding it from a received CEC frame.
     *
     * @param[in] frame     The received CEC frame to decode the message from.
     * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
     */
    SetOSDName(const CECFrame &frame, int startPos = 0)
    : osdName(frame, startPos)
    {
    }

	/**
	 * @brief Serializes this message's operand(s) by appending them to a CEC frame.
	 *
	 * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
	 * @return Reference to @p frame to allow call chaining.
	 */
	CECFrame &serialize(CECFrame &frame) const {
	    return osdName.serialize(frame);
	}

    /**
     * @brief Logs the contents of this message for debugging.
     */
    void print(void) const {
        CCEC_LOG( LOG_DEBUG,"OSDName : %s\n",osdName.toString().c_str());
    }

    OSDName osdName;  ///< Device's preferred OSD name.
};

/**
 * @brief Set OSD String (@c SET_OSD_STRING) CEC message.
 *
 * Display a text string on the TV's on-screen display.
 * Carries the @ref OSDString to display.
 *
 * @see DataBlock
 */
class SetOSDString : public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c SET_OSD_STRING opcode.
     */
    Op_t opCode(void) const {return SET_OSD_STRING;}

    /**
     * @brief Constructs the Set OSD String message from the supplied operand(s).
     *
     * @param[in] osdString  OSD string to display on screen.
     */
    SetOSDString(const OSDString &osdString) : osdString(osdString) {};

    /**
     * @brief Constructs the Set OSD String message by decoding it from a received CEC frame.
     *
     * @param[in] frame     The received CEC frame to decode the message from.
     * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
     */
    SetOSDString(const CECFrame &frame, int startPos = 0)
    : osdString(frame, startPos)
    {
    }

	/**
	 * @brief Serializes this message's operand(s) by appending them to a CEC frame.
	 *
	 * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
	 * @return Reference to @p frame to allow call chaining.
	 */
	CECFrame &serialize(CECFrame &frame) const {
	    return osdString.serialize(frame);
	}

    /**
     * @brief Logs the contents of this message for debugging.
     */
    void print(void) const {
        CCEC_LOG( LOG_DEBUG,"OSDString : %s\n",osdString.toString().c_str());
    }

    OSDString osdString;  ///< Text string to display on the on-screen display.
};

/**
 * @brief Give Physical Address (@c GIVE_PHYSICAL_ADDRESS) CEC message.
 *
 * Request a device to report its physical address.
 * This message carries no operands.
 *
 * @see DataBlock
 */
class GivePhysicalAddress : public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c GIVE_PHYSICAL_ADDRESS opcode.
     */
    Op_t opCode(void) const {return GIVE_PHYSICAL_ADDRESS;}
};

/**
 * @brief Report Physical Address (@c REPORT_PHYSICAL_ADDRESS) CEC message.
 *
 * Broadcast a device's physical address and device type.
 * Carries the device's @ref PhysicalAddress and its @ref DeviceType.
 *
 * @see DataBlock
 */
class ReportPhysicalAddress : public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c REPORT_PHYSICAL_ADDRESS opcode.
     */
    Op_t opCode(void) const {return REPORT_PHYSICAL_ADDRESS;}

    /**
     * @brief Constructs the Report Physical Address message from the supplied operand(s).
     *
     * @param[in] physicalAddress  Physical address to report.
     * @param[in] deviceType  Device type associated with the physical address.
     */
    ReportPhysicalAddress(const PhysicalAddress &physicalAddress, const DeviceType &deviceType)
    : physicalAddress(physicalAddress), deviceType(deviceType) {
    }

    /**
     * @brief Constructs the Report Physical Address message by decoding it from a received CEC frame.
     *
     * @param[in] frame     The received CEC frame to decode the message from.
     * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
     */
    ReportPhysicalAddress(const CECFrame &frame, int startPos = 0)
    : physicalAddress(frame, startPos), deviceType(frame, startPos + PhysicalAddress::MAX_LEN)
    {
    }

	/**
	 * @brief Serializes this message's operand(s) by appending them to a CEC frame.
	 *
	 * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
	 * @return Reference to @p frame to allow call chaining.
	 */
	CECFrame &serialize(CECFrame &frame) const {
		return deviceType.serialize(physicalAddress.serialize(frame));
	}

    /**
     * @brief Logs the contents of this message for debugging.
     */
    void print(void) const {
        CCEC_LOG( LOG_DEBUG,"Physical Address : %s\n",physicalAddress.toString().c_str());
        CCEC_LOG( LOG_DEBUG,"Device Type : %s\n",deviceType.toString().c_str());
    }

	PhysicalAddress physicalAddress;  ///< Physical address being reported.
	DeviceType deviceType;  ///< Device type associated with the reported physical address.
};

/**
 * @brief Give Device Vendor ID (@c GIVE_DEVICE_VENDOR_ID) CEC message.
 *
 * Request a device to broadcast its vendor ID.
 * This message carries no operands.
 *
 * @see DataBlock
 */
class GiveDeviceVendorID : public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c GIVE_DEVICE_VENDOR_ID opcode.
     */
    Op_t opCode(void) const {return GIVE_DEVICE_VENDOR_ID;}
};

/**
 * @brief Device Vendor ID (@c DEVICE_VENDOR_ID) CEC message.
 *
 * Broadcast the device's IEEE vendor identifier.
 * Carries the device's @ref VendorID.
 *
 * @see DataBlock
 */
class DeviceVendorID : public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c DEVICE_VENDOR_ID opcode.
     */
    Op_t opCode(void) const {return DEVICE_VENDOR_ID;}

    /**
     * @brief Constructs the Device Vendor ID message from the supplied operand(s).
     *
     * @param[in] vendorId  IEEE vendor identifier to broadcast.
     */
    DeviceVendorID(const VendorID &vendorId) : vendorId(vendorId) {}

    /**
     * @brief Constructs the Device Vendor ID message by decoding it from a received CEC frame.
     *
     * @param[in] frame     The received CEC frame to decode the message from.
     * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
     */
    DeviceVendorID(const CECFrame &frame, int startPos = 0)
    : vendorId(frame, startPos)
    {
    }

	/**
	 * @brief Serializes this message's operand(s) by appending them to a CEC frame.
	 *
	 * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
	 * @return Reference to @p frame to allow call chaining.
	 */
	CECFrame &serialize(CECFrame &frame) const {
		return vendorId.serialize(frame);
	}

    /**
     * @brief Logs the contents of this message for debugging.
     */
    void print(void) const {
        CCEC_LOG( LOG_DEBUG,"VendorID : %s\n",vendorId.toString().c_str());
    }

    VendorID vendorId;  ///< IEEE vendor identifier of the device.
};

/**
 * @brief Give Device Power Status (@c GIVE_DEVICE_POWER_STATUS) CEC message.
 *
 * Request a device to report its power status.
 * This message carries no operands.
 *
 * @see DataBlock
 */
class GiveDevicePowerStatus : public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c GIVE_DEVICE_POWER_STATUS opcode.
     */
    Op_t opCode(void) const {return GIVE_DEVICE_POWER_STATUS;}
};

/**
 * @brief Report Power Status (@c REPORT_POWER_STATUS) CEC message.
 *
 * Report the device's current power status.
 * Carries the device's @ref PowerStatus.
 *
 * @see DataBlock
 */
class ReportPowerStatus : public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c REPORT_POWER_STATUS opcode.
     */
    Op_t opCode(void) const {return REPORT_POWER_STATUS;}

    /**
     * @brief Constructs the Report Power Status message from the supplied operand(s).
     *
     * @param[in] status  Power status to report.
     */
    ReportPowerStatus(const PowerStatus &status) : status(status) {}

    /**
     * @brief Constructs the Report Power Status message by decoding it from a received CEC frame.
     *
     * @param[in] frame     The received CEC frame to decode the message from.
     * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
     */
    ReportPowerStatus(const CECFrame &frame, int startPos = 0)
    : status(frame, startPos)
    {
    }

	/**
	 * @brief Serializes this message's operand(s) by appending them to a CEC frame.
	 *
	 * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
	 * @return Reference to @p frame to allow call chaining.
	 */
	CECFrame &serialize(CECFrame &frame) const {
		return status.serialize(frame);
	}

    /**
     * @brief Logs the contents of this message for debugging.
     */
    void print(void) const {
        CCEC_LOG( LOG_DEBUG,"Power Status: %s",status.toString().c_str());
    }

    PowerStatus status;  ///< Current power status of the device.
};

/**
 * @brief Abort (@c ABORT) CEC message.
 *
 * Generic abort message used to test or reject a feature.
 * This message carries no operands.
 *
 * @see DataBlock
 */
class Abort : public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c ABORT opcode.
     */
    Op_t opCode(void) const {return ABORT;}
};

/**
 * @brief Feature Abort (@c FEATURE_ABORT) CEC message.
 *
 * Indicate that a received command cannot be processed.
 * Carries the @ref OpCode of the aborted feature and the @ref AbortReason.
 *
 * @see DataBlock
 */
class FeatureAbort: public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c FEATURE_ABORT opcode.
     */
    Op_t opCode(void) const {return FEATURE_ABORT;}

    /**
     * @brief Constructs the Feature Abort message from the supplied operand(s).
     *
     * @param[in] feature  Opcode of the feature/command being aborted.
     * @param[in] reason  Reason the feature was aborted.
     */
    FeatureAbort(const OpCode &feature, const AbortReason &reason) : feature(feature), reason(reason) {}

    /**
     * @brief Constructs the Feature Abort message by decoding it from a received CEC frame.
     *
     * @param[in] frame     The received CEC frame to decode the message from.
     * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
     */
    FeatureAbort(const CECFrame &frame, int startPos = 0)
    : feature(frame, startPos), reason(frame, startPos + OpCode::MAX_LEN)
    {
    }

	/**
	 * @brief Serializes this message's operand(s) by appending them to a CEC frame.
	 *
	 * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
	 * @return Reference to @p frame to allow call chaining.
	 */
	CECFrame &serialize(CECFrame &frame) const {
		return reason.serialize(feature.serialize(frame));
	}

    /**
     * @brief Logs the contents of this message for debugging.
     */
    void print(void) const {
        CCEC_LOG( LOG_DEBUG,"Abort For Feature : %s\n",feature.toString().c_str());
        CCEC_LOG( LOG_DEBUG,"Abort Reason : %s\n",reason.toString().c_str());
    }

    OpCode feature;  ///< Opcode of the feature/command being aborted.
    AbortReason reason;  ///< Reason the feature was aborted.
};

/**
 * @brief Routing Change (@c ROUTING_CHANGE) CEC message.
 *
 * Notify a change of the active routing path between two physical addresses.
 * Carries the originating and destination @ref PhysicalAddress values.
 *
 * @see DataBlock
 */
class RoutingChange: public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c ROUTING_CHANGE opcode.
     */
    Op_t opCode(void) const {return ROUTING_CHANGE;}

    /**
     * @brief Constructs the Routing Change message from the supplied operand(s).
     *
     * @param[in] from  Originating (previous) physical address of the routing path.
     * @param[in] to  Destination (new) physical address of the routing path.
     */
    RoutingChange(const PhysicalAddress &from, const PhysicalAddress &to) : from(from), to(to) {}

    /**
     * @brief Constructs the Routing Change message by decoding it from a received CEC frame.
     *
     * @param[in] frame     The received CEC frame to decode the message from.
     * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
     */
    RoutingChange(const CECFrame &frame, int startPos = 0)
    : from(frame, startPos), to(frame, startPos + PhysicalAddress::MAX_LEN)
    {
    }

	/**
	 * @brief Serializes this message's operand(s) by appending them to a CEC frame.
	 *
	 * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
	 * @return Reference to @p frame to allow call chaining.
	 */
	CECFrame &serialize(CECFrame &frame) const {
		return to.serialize(from.serialize(frame));
	}

    /**
     * @brief Logs the contents of this message for debugging.
     */
    void print(void) const {
        CCEC_LOG( LOG_DEBUG,"Routing Change From : %s\n",from.toString().c_str());
        CCEC_LOG( LOG_DEBUG,"Routing Change to : %s\n",to.toString().c_str());
    }

    PhysicalAddress from;  ///< Originating (previous) physical address of the routing path.
    PhysicalAddress to;  ///< Destination (new) physical address of the routing path.
};

/**
 * @brief Routing Information (@c ROUTING_INFORMATION) CEC message.
 *
 * Report the current routing path (physical address).
 * Carries the current routing path as a @ref PhysicalAddress.
 *
 * @see DataBlock
 */
class RoutingInformation: public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c ROUTING_INFORMATION opcode.
     */
    Op_t opCode(void) const {return ROUTING_INFORMATION;}

    /**
     * @brief Constructs the Routing Information message from the supplied operand(s).
     *
     * @param[in] toSink  Physical address describing the current routing path.
     */
    RoutingInformation(const PhysicalAddress &toSink) : toSink(toSink) {}

    /**
     * @brief Constructs the Routing Information message by decoding it from a received CEC frame.
     *
     * @param[in] frame     The received CEC frame to decode the message from.
     * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
     */
    RoutingInformation(const CECFrame &frame, int startPos = 0)
    : toSink(frame, startPos)
    {
    }

	/**
	 * @brief Serializes this message's operand(s) by appending them to a CEC frame.
	 *
	 * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
	 * @return Reference to @p frame to allow call chaining.
	 */
	CECFrame &serialize(CECFrame &frame) const {
		return toSink.serialize(frame);
	}

    /**
     * @brief Logs the contents of this message for debugging.
     */
    void print(void) const {
        CCEC_LOG( LOG_DEBUG,"Routing Information to Sink : %s\n",toSink.toString().c_str());
    }

    PhysicalAddress toSink;  ///< Physical address describing the current routing path.
};


/**
 * @brief Set Stream Path (@c SET_STREAM_PATH) CEC message.
 *
 * Request the device at a physical address to become the active source.
 * Carries the target @ref PhysicalAddress requested to become the active source.
 *
 * @see DataBlock
 */
class SetStreamPath: public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c SET_STREAM_PATH opcode.
     */
    Op_t opCode(void) const {return SET_STREAM_PATH;}

    /**
     * @brief Constructs the Set Stream Path message from the supplied operand(s).
     *
     * @param[in] toSink  Physical address requested to become the active source.
     */
    SetStreamPath(const PhysicalAddress &toSink) : toSink(toSink) {}

    /**
     * @brief Constructs the Set Stream Path message by decoding it from a received CEC frame.
     *
     * @param[in] frame     The received CEC frame to decode the message from.
     * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
     */
    SetStreamPath(const CECFrame &frame, int startPos = 0)
    : toSink(frame, startPos)
    {
    }

	/**
	 * @brief Serializes this message's operand(s) by appending them to a CEC frame.
	 *
	 * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
	 * @return Reference to @p frame to allow call chaining.
	 */
	CECFrame &serialize(CECFrame &frame) const {
		return toSink.serialize(frame);
	}

    /**
     * @brief Logs the contents of this message for debugging.
     */
    void print(void) const {
        CCEC_LOG( LOG_DEBUG,"Set Stream Path to Sink : %s\n",toSink.toString().c_str());
    }

    PhysicalAddress toSink;  ///< Physical address requested to become the active source.
};

/**
 * @brief Request Short Audio Descriptor (@c REQUEST_SHORT_AUDIO_DESCRIPTOR) CEC message.
 *
 * Request a device's short audio descriptors.
 * Carries one or more @ref RequestAudioFormat descriptors (up to four).
 *
 * @see DataBlock
 */
class RequestShortAudioDescriptor : public DataBlock
{

public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c REQUEST_SHORT_AUDIO_DESCRIPTOR opcode.
     */
    Op_t opCode(void) const {return REQUEST_SHORT_AUDIO_DESCRIPTOR;}

      /**
       * @brief Constructs the Request Short Audio Descriptor message from the supplied operand(s).
       *
       * @param[in] formatid  Audio format IDs, one per requested descriptor.
       * @param[in] audioFormatCode  Audio format codes, one per requested descriptor.
       * @param[in] number_of_descriptor  Number of descriptors to request (capped at 4; defaults to 1).
       * @pre @p formatid and @p audioFormatCode must each contain at least as many
       *      elements as the effective descriptor count, i.e. @c min(number_of_descriptor,4).
       * @warning The effective count is capped at 4 but is not validated against the
       *          sizes of @p formatid or @p audioFormatCode; the loop indexes both
       *          vectors up to that count, so supplying shorter vectors causes an
       *          out-of-bounds std::vector element access (undefined behavior). Each
       *          descriptor is packed as (formatid[i] << 6) | (audioFormatCode[i] & 0x3f),
       *          so only the low six bits of each @p audioFormatCode entry are kept.
       *          Documents the code as implemented.
       */
      RequestShortAudioDescriptor(const std::vector<uint8_t> formatid, const std::vector<uint8_t> audioFormatCode, uint8_t number_of_descriptor = 1)
      {
	    uint8_t audioFormatIdCode;
	    numberofdescriptor = number_of_descriptor > 4 ? 4 : number_of_descriptor;
	    for (uint8_t i=0 ; i < numberofdescriptor ;i++)
	    {
		   audioFormatIdCode = (formatid[i] << 6) | ( (audioFormatCode[i])& 0x3f) ;
		   requestAudioFormat.push_back(RequestAudioFormat(audioFormatIdCode));
	    }
       }
	 /* called by the messaged_decoder */
     /**
      * @brief Constructs the Request Short Audio Descriptor message by decoding it from a received CEC frame.
      *
      * @param[in] frame     The received CEC frame to decode the message from.
      * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
      */
     RequestShortAudioDescriptor(const CECFrame &frame, int startPos = 0)
     {
	uint8_t len = frame.length();
        numberofdescriptor = len > 4 ? 4:len;
        for (uint8_t i=0; i< numberofdescriptor ; i++)
        {
	   requestAudioFormat.push_back(RequestAudioFormat(frame,startPos + i ));
        }
     }
     /* called by the message encoder */
     /**
      * @brief Serializes this message's operand(s) by appending them to a CEC frame.
      *
      * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
      * @return Reference to @p frame to allow call chaining.
      */
     CECFrame &serialize(CECFrame &frame) const {

	  for (uint8_t i=0; i < numberofdescriptor ; i++)
	  {
	  requestAudioFormat[i].serialize(frame);

	  }
	  return frame;
	}

     /**
      * @brief Logs the contents of this message for debugging.
      */
     void print(void) const {
	    uint8_t i=0;
		for(i=0;i < numberofdescriptor;i++)
		{

                  CCEC_LOG( LOG_DEBUG,"audio format id %d audioFormatCode : %s\n",requestAudioFormat[i].getAudioformatId(),requestAudioFormat[i].toString().c_str());

		}
      }
     std::vector<RequestAudioFormat> requestAudioFormat ;  ///< Requested audio formats, one @ref RequestAudioFormat entry per descriptor.
     uint8_t  numberofdescriptor;  ///< Number of audio-format descriptors carried (capped at 4).
};
/**
 * @brief Report Short Audio Descriptor (@c REPORT_SHORT_AUDIO_DESCRIPTOR) CEC message.
 *
 * Report supported audio formats (EDID short audio descriptors).
 * Carries one or more @ref ShortAudioDescriptor operands.
 *
 * @see DataBlock
 */
class ReportShortAudioDescriptor : public DataBlock
{

public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c REPORT_SHORT_AUDIO_DESCRIPTOR opcode.
     */
    Op_t opCode(void) const {return REPORT_SHORT_AUDIO_DESCRIPTOR;}

	      /**
	       * @brief Constructs the Report Short Audio Descriptor message from the supplied operand(s).
	       *
	       * @param[in] shortaudiodescriptor  Packed short audio descriptor values to report.
	       * @param[in] numberofdescriptor  Number of descriptors to report (capped at 4; defaults to 1).
	       * @pre @p shortaudiodescriptor must contain at least as many elements as the
	       *      effective descriptor count, i.e. @c min(numberofdescriptor,4).
	       * @warning The effective count is capped at 4 but is not validated against the
	       *          size of @p shortaudiodescriptor; the loop indexes the vector up to
	       *          that count, so a shorter vector causes an out-of-bounds std::vector
	       *          element access (undefined behavior). For each descriptor three
	       *          bytes are extracted as (value & 0xF), ((value >> 8) & 0xF) and
	       *          ((value >> 16) & 0xF): only the low nibble (4 bits) of each of the
	       *          three bytes is retained; the high nibble of each byte is truncated
	       *          (discarded). Documents the code as implemented.
	       */
	      ReportShortAudioDescriptor( const std::vector <uint32_t> shortaudiodescriptor, uint8_t numberofdescriptor = 1)
	      {

	       	uint8_t bytes[3];
	        numberofdescriptor = numberofdescriptor > 4 ? 4 : numberofdescriptor;
	        for (uint8_t i=0; i < numberofdescriptor ;i++)
	        {
                  bytes[0] = (shortaudiodescriptor[i] & 0xF);
	          bytes[1] = ((shortaudiodescriptor[i] >> 8) & 0xF);
	          bytes[2] = ((shortaudiodescriptor[i] >> 16) & 0xF);
	          shortAudioDescriptor.push_back(ShortAudioDescriptor(bytes));

	        }
	      }
	       /* called by the messaged_decoder */
             /**
              * @brief Constructs the Report Short Audio Descriptor message by decoding it from a received CEC frame.
              *
              * @param[in] frame     The received CEC frame to decode the message from.
              * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
              */
             ReportShortAudioDescriptor(const CECFrame &frame, int startPos = 0)
            {
               numberofdescriptor = (frame.length())/3;
               for (uint8_t i=0; i< numberofdescriptor ;i++)
	       {
	          shortAudioDescriptor.push_back(ShortAudioDescriptor(frame,startPos + i*3 ));
	       }
             }

	     /* called by the message encoder*/
	   /**
	    * @brief Serializes this message's operand(s) by appending them to a CEC frame.
	    *
	    * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
	    * @return Reference to @p frame to allow call chaining.
	    */
	   CECFrame &serialize(CECFrame &frame) const {
		 for (uint8_t i=0; i < numberofdescriptor ; i++)
		 {
		 //just do the append of the stored cec bytes
		 shortAudioDescriptor[i].serialize(frame);

		 }
		 return frame;
	   }
           /**
            * @brief Logs the contents of this message for debugging.
            */
           void print(void) const {
                for(uint8_t i=0;i < numberofdescriptor;i++)
                {
			CCEC_LOG( LOG_DEBUG," audioFormatCode : %s audioFormatCode %d Atmos = %d\n",shortAudioDescriptor[i].toString().c_str(),shortAudioDescriptor[i].getAudioformatCode(),shortAudioDescriptor[i].getAtmosbit());
		}
	   }
    std::vector <ShortAudioDescriptor> shortAudioDescriptor ;  ///< Reported short audio descriptors.
    uint8_t numberofdescriptor;  ///< Number of short audio descriptors carried.
};
/**
 * @brief System Audio Mode Request (@c SYSTEM_AUDIO_MODE_REQUEST) CEC message.
 *
 * Request activation of system audio mode.
 * Optionally carries the requested audio source @ref PhysicalAddress; an address of 0xFFFF terminates system audio mode.
 *
 * @see DataBlock
 */
class SystemAudioModeRequest : public DataBlock
{

public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c SYSTEM_AUDIO_MODE_REQUEST opcode.
     */
    Op_t opCode(void) const {return SYSTEM_AUDIO_MODE_REQUEST;}
	/**
	 * @brief Constructs the System Audio Mode Request message from the supplied operand(s).
	 *
	 * @param[in] physicaladdress  Physical address of the requested audio source; defaults to 0xFFFF, which terminates system audio mode.
	 */
	SystemAudioModeRequest(const PhysicalAddress &physicaladdress = {0xf,0xf,0xf,0xf} ): physicaladdress(physicaladdress) {}
	 /* called by the messaged_decoder */
	/**
	 * @brief Constructs the System Audio Mode Request message by decoding it from a received CEC frame.
	 *
	 * @param[in] frame     The received CEC frame to decode the message from.
	 * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
	 */
	SystemAudioModeRequest(const CECFrame &frame, int startPos = 0):physicaladdress(frame, startPos)
        {
           if (frame.length() == 0 )
	   {
              physicaladdress= PhysicalAddress((uint8_t) 0xf,(uint8_t) 0xf,(uint8_t)0xf,(uint8_t)0xf);
	   }
        }
      /**
       * @brief Serializes this message's operand(s) by appending them to a CEC frame.
       *
       * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
       * @return Reference to @p frame to allow call chaining.
       *
       * @note When the physical address is 0xFFFF (unspecified) no operand bytes are appended.
       */
      CECFrame &serialize(CECFrame &frame) const {
        if ( (physicaladdress.getByteValue(3) == 0xF) && (physicaladdress.getByteValue(2) == 0xF) && (physicaladdress.getByteValue(1) == 0xF) &&  (physicaladdress.getByteValue(0) == 0xF))
	{
	    return frame;
	 } else{
                return physicaladdress.serialize(frame);
	 }
	 }
     /**
      * @brief Logs the contents of this message for debugging.
      */
     void print(void) const {
        CCEC_LOG( LOG_DEBUG,"Set SystemAudioModeRequest : %s\n",physicaladdress.toString().c_str());
    }
  PhysicalAddress physicaladdress;  ///< Physical address of the requested audio source (0xFFFF indicates system audio mode should terminate).
};
/**
 * @brief Set System Audio Mode (@c SET_SYSTEM_AUDIO_MODE) CEC message.
 *
 * Turn the system audio (ARC) mode on or off.
 * Carries the @ref SystemAudioStatus to set.
 *
 * @see DataBlock
 */
class SetSystemAudioMode: public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c SET_SYSTEM_AUDIO_MODE opcode.
     */
    Op_t opCode(void) const {return SET_SYSTEM_AUDIO_MODE;}

	/**
	 * @brief Constructs the Set System Audio Mode message from the supplied operand(s).
	 *
	 * @param[in] status  System audio mode status to set.
	 */
	SetSystemAudioMode( const SystemAudioStatus &status ) : status(status) { }

	/**
	 * @brief Constructs the Set System Audio Mode message by decoding it from a received CEC frame.
	 *
	 * @param[in] frame     The received CEC frame to decode the message from.
	 * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
	 */
	SetSystemAudioMode(const CECFrame &frame, int startPos = 0) : status(frame, startPos)
       {
       }
	/**
	 * @brief Serializes this message's operand(s) by appending them to a CEC frame.
	 *
	 * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
	 * @return Reference to @p frame to allow call chaining.
	 */
	CECFrame &serialize(CECFrame &frame) const {
		return status.serialize(frame);
	}

	SystemAudioStatus status;  ///< System audio (ARC) mode status.
};

/**
 * @brief Give Audio Status (@c GIVE_AUDIO_STATUS) CEC message.
 *
 * Request an amplifier to report its volume and mute status.
 * This message carries no operands.
 *
 * @see DataBlock
 */
class GiveAudioStatus : public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c GIVE_AUDIO_STATUS opcode.
     */
    Op_t opCode(void) const {return GIVE_AUDIO_STATUS;}
};
/**
 * @brief Report Audio Status (@c REPORT_AUDIO_STATUS) CEC message.
 *
 * Report the amplifier's volume level and mute state.
 * Carries the reported @ref AudioStatus (volume and mute).
 *
 * @see DataBlock
 */
class ReportAudioStatus: public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c REPORT_AUDIO_STATUS opcode.
     */
    Op_t opCode(void) const {return REPORT_AUDIO_STATUS;}

    /**
     * @brief Constructs the Report Audio Status message from the supplied operand(s).
     *
     * @param[in] status  Audio status to report.
     */
    ReportAudioStatus( const AudioStatus &status ) : status(status) { }
    /**
     * @brief Constructs the Report Audio Status message by decoding it from a received CEC frame.
     *
     * @param[in] frame     The received CEC frame to decode the message from.
     * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
     */
    ReportAudioStatus(const CECFrame &frame, int startPos = 0):status(frame, startPos)
    {
    }
    /**
     * @brief Serializes this message's operand(s) by appending them to a CEC frame.
     *
     * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
     * @return Reference to @p frame to allow call chaining.
     */
    CECFrame &serialize(CECFrame &frame) const {
	return status.serialize(frame);
    }
    AudioStatus status;  ///< Reported audio status (volume level and mute state).
};

/**
 * @brief User Control Pressed (@c USER_CONTROL_PRESSED) CEC message.
 *
 * Forward a remote-control key press.
 * Carries the @ref UICommand that was pressed.
 *
 * @see DataBlock
 */
class UserControlPressed: public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c USER_CONTROL_PRESSED opcode.
     */
    Op_t opCode(void) const {return USER_CONTROL_PRESSED;}

	/**
	 * @brief Constructs the User Control Pressed message from the supplied operand(s).
	 *
	 * @param[in] command  Remote-control UI command that was pressed.
	 */
	UserControlPressed( const UICommand &command ) : uiCommand(command) { }
    /**
     * @brief Constructs the User Control Pressed message by decoding it from a received CEC frame.
     *
     * @param[in] frame     The received CEC frame to decode the message from.
     * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
     */
    UserControlPressed(const CECFrame &frame, int startPos = 0):uiCommand(frame, startPos)
	{
	}

	/**
	 * @brief Serializes this message's operand(s) by appending them to a CEC frame.
	 *
	 * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
	 * @return Reference to @p frame to allow call chaining.
	 */
	CECFrame &serialize(CECFrame &frame) const {
		return uiCommand.serialize(frame);
	}

	UICommand uiCommand;  ///< Remote-control UI command being pressed.
};

/**
 * @brief User Control Released (@c USER_CONTROL_RELEASED) CEC message.
 *
 * Forward a remote-control key release.
 * This message carries no operands.
 *
 * @see DataBlock
 */
class UserControlReleased: public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c USER_CONTROL_RELEASED opcode.
     */
    Op_t opCode(void) const {return USER_CONTROL_RELEASED;}
};


/**
 * @brief Polling (@c POLLING) CEC message.
 *
 * CEC polling/ping message used for logical-address discovery.
 * This message carries no operands; @c POLLING is a pseudo-opcode that is not transmitted as an opcode byte on the CEC wire.
 *
 * @see DataBlock
 */
class Polling : public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c POLLING opcode.
     */
    Op_t opCode(void) const {return POLLING;}
};

/**
 * @brief Request ARC Initiation (@c REQUEST_ARC_INITIATION) CEC message.
 *
 * Request that ARC initiation begin.
 * This message carries no operands.
 *
 * @see DataBlock
 */
class RequestArcInitiation:public DataBlock
{
  public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c REQUEST_ARC_INITIATION opcode.
     */
    Op_t opCode(void) const {return REQUEST_ARC_INITIATION;}

};

/**
 * @brief Report ARC Initiated (@c REPORT_ARC_INITIATED) CEC message.
 *
 * Confirm that the Audio Return Channel has been initiated.
 * This message carries no operands.
 *
 * @see DataBlock
 */
class ReportArcInitiation:public DataBlock
{
  public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c REPORT_ARC_INITIATED opcode.
     */
    Op_t opCode(void) const {return REPORT_ARC_INITIATED;}

};
/**
 * @brief Request ARC Termination (@c REQUEST_ARC_TERMINATION) CEC message.
 *
 * Request that the Audio Return Channel be terminated.
 * This message carries no operands.
 *
 * @see DataBlock
 */
class RequestArcTermination:public DataBlock
{
  public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c REQUEST_ARC_TERMINATION opcode.
     */
    Op_t opCode(void) const {return REQUEST_ARC_TERMINATION;}

};
/**
 * @brief Report ARC Terminated (@c REPORT_ARC_TERMINATED) CEC message.
 *
 * Confirm that the Audio Return Channel has been terminated.
 * This message carries no operands.
 *
 * @see DataBlock
 */
class ReportArcTermination:public DataBlock
{
  public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c REPORT_ARC_TERMINATED opcode.
     */
    Op_t opCode(void) const {return REPORT_ARC_TERMINATED;}

};
/**
 * @brief Initiate ARC (@c INITIATE_ARC) CEC message.
 *
 * Request initiation of the Audio Return Channel.
 * This message carries no operands.
 *
 * @see DataBlock
 */
class InitiateArc:public DataBlock
{
  public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c INITIATE_ARC opcode.
     */
    Op_t opCode(void) const {return INITIATE_ARC;}
};
/**
 * @brief Terminate ARC (@c TERMINATE_ARC) CEC message.
 *
 * Request termination of the Audio Return Channel.
 * This message carries no operands.
 *
 * @see DataBlock
 */
class TerminateArc:public DataBlock
{
  public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c TERMINATE_ARC opcode.
     */
    Op_t opCode(void) const {return TERMINATE_ARC;}
};
/**
 * @brief Give Features (@c GIVE_FEATURES) CEC message.
 *
 * Request a device to report its feature set (CEC 2.0).
 * This message carries no operands.
 *
 * @see DataBlock
 */
class GiveFeatures : public DataBlock
{
public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c GIVE_FEATURES opcode.
     */
    Op_t opCode(void) const {return GIVE_FEATURES;}
};
/**
 * @brief Report Features (@c REPORT_FEATURES) CEC message.
 *
 * Report CEC version, device types, and RC profile/features (CEC 2.0).
 * Carries the CEC @ref Version, @ref AllDeviceTypes, and the variable-length @ref RcProfile and @ref DeviceFeatures operands.
 *
 * @see DataBlock
 */
class ReportFeatures : public DataBlock
{

  public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c REPORT_FEATURES opcode.
     */
    Op_t opCode(void) const {return REPORT_FEATURES;}
        /**
         * @brief Constructs the Report Features message from the supplied operand(s).
         *
         * @param[in] version  CEC version of the device.
         * @param[in] allDeviceTypes  All device types supported by the device.
         * @param[in] rc_Profile  Remote-control profiles supported by the device.
         * @param[in] device_Features  Device features supported by the device.
         */
        ReportFeatures(const Version &version,const AllDeviceTypes &allDeviceTypes,const std::vector<RcProfile> rc_Profile,std::vector<DeviceFeatures> device_Features) : version(version), allDeviceTypes(allDeviceTypes)
        {
                for(uint8_t i = 0; i < rc_Profile.size(); i++){
                       rcProfile.push_back(RcProfile(rc_Profile[i]));
                }

                for(uint8_t i = 0; i < device_Features.size(); i++){
                    deviceFeatures.push_back(DeviceFeatures(device_Features[i]));
                }
        }

        /* called by the messaged_decoder */
        /**
         * @brief Constructs the Report Features message by decoding it from a received CEC frame.
         *
         * @param[in] frame     The received CEC frame to decode the message from.
         * @param[in] startPos  Nominal byte offset within @p frame at which this
         *                      message's operands begin.
         * @warning The member initializer list contains the assignment
         *          `version(frame, startPos = 0)`: this resets @p startPos to 0
         *          before it is used, so any nonzero caller-supplied @p startPos is
         *          ignored. The @c version operand is decoded from offset 0, and the
         *          subsequent `allDeviceTypes(frame, startPos + Version::MAX_LEN)`
         *          and the body's `RcProfile(frame, startPos + 2, ...)` all observe
         *          @p startPos equal to 0. In effect this constructor always decodes
         *          from the start of the frame. This documents the code exactly as
         *          implemented; correcting the behavior is out of scope.
         */
        ReportFeatures(const CECFrame &frame, int startPos = 0) : version(frame, startPos = 0) , allDeviceTypes(frame, startPos+Version::MAX_LEN) {
                const uint8_t *buf = 0;
                size_t frameLen = 0, rc_len = 1, features_len = 1;

                frame.getBuffer(&buf, &frameLen);

                //The frame is stored in buffer
                CCEC_LOG( LOG_INFO, "Features Buffer ----> Frame Length:  %zu\n",frameLen);
                for(size_t i = 0; i < frameLen ;i++){
                        CCEC_LOG( LOG_INFO, "%02x :\n",(unsigned int)buf[i]);
                }

                //[RC Profile] length is calculated based on Extension bit
                for(size_t i = 2; i< frame.length(); i++) {
                    if(buf[i] & 0x80){
                        rc_len++;
                    }
                    else{
                        break;
                    }
                }

                //[RC Profile] and [Device Features] are Variable Length
                //Bit 7 of each byte corresponds to Extension bit
                //If Bit7 = 1, then next byte corresponds to same Operand. If Bit7 = 0, then Operand ends with that byte

                rcProfile.push_back(RcProfile(frame, startPos + 2, rc_len));

                //[Device Features] length is calculated
                for(size_t i = rc_len + 2; i < frame.length(); i++){
                    if(buf[i] & 0x80){
                        features_len++;
                    }
                    else{
                        break;
                    }
                }
                deviceFeatures.push_back(DeviceFeatures(frame, startPos + rc_len + 2 , features_len));
        }

        /**
         * @brief Serializes this message's operand(s) by appending them to a CEC frame.
         *
         * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
         * @return Reference to @p frame to allow call chaining.
         */
        CECFrame &serialize(CECFrame &frame) const {
            allDeviceTypes.serialize(version.serialize(frame));

            for (uint8_t i = 0; i < rcProfile.size() ; i++) {
                rcProfile[i].serialize(frame);
            }

            for (uint8_t i = 0; i < deviceFeatures.size() ; i++) {
                deviceFeatures[i].serialize(frame);
            }

            return frame;
        }

          /**
           * @brief Logs the contents of this message for debugging.
           */
          void print(void) const {
                std::vector<std::string> all_device_types, rc_profile, device_features;
                std::vector<uint8_t> rc_profile_val, device_features_val;

                //Operand - [CEC Version]
                CCEC_LOG( LOG_INFO, "Version : %s \n",version.toString().c_str());


                //Operand - [All Device Types]
                all_device_types = allDeviceTypes.getAllDeviceTypes();
                CCEC_LOG( LOG_INFO, "All Device types:\n");
                for(size_t i = 0; i < all_device_types.size(); i++){
                    CCEC_LOG( LOG_INFO, "%s\t", all_device_types[i].c_str());
                }

                CCEC_LOG( LOG_INFO, "Device Type TV: %d, Recording Device: %d, Tuner: %d, Playback Device: %d, Audio Sytem: %d, CEC Switch: %d", \
                allDeviceTypes.isDeviceTypeTV(), allDeviceTypes.isRecordingDevice(), allDeviceTypes.isDeviceTypeTuner(), allDeviceTypes.isPlaybackDevice(),\
                allDeviceTypes.isDeviceTypeAudioSystem(), allDeviceTypes.isDeviceTypeCECSwitch());

                //Operand - [RC Profile]
                for(size_t i = 0; i < rcProfile.size(); i++){
                    rc_profile = rcProfile[i].getRcProfile();
                    CCEC_LOG(LOG_INFO, "RC Profile TV: %d, RC Profile Source: %d", rcProfile[i].isRcProfileTv(), rcProfile[i].isRcProfileSource());
                    if(rcProfile[i].isRcProfileSource()){
                            CCEC_LOG(LOG_INFO, "Source can handle Device Root Menu: %d, Source can handle Device Setup Menu: %d, Source can handle Contents Menu: %d, Source can handle Media Top Menu: %d, Source can handle Media Context-Sensitive Menu: %d", rcProfile[i].rootMenuHandling(),rcProfile[i].setupMenuHandling(),rcProfile[i].contentsMenuHandling(),rcProfile[i].mediaTopMenuHandling(),rcProfile[i].contextSensitiveMenuHandling());
                    }
                }
                CCEC_LOG( LOG_INFO, "RC Profiles:\n");
                for(size_t i = 0; i < rc_profile.size(); i++){
                        CCEC_LOG( LOG_INFO, " %s\t", rc_profile[i].c_str());
                }

                //CEC Byte Values corresponding to RC Profile Operand
                for(size_t i = 0; i < rcProfile.size(); i++){
                    rc_profile_val = rcProfile[i].getRcProfileVal();
                }
                CCEC_LOG( LOG_INFO, "RC Profile Byte Values:\n");
                for(size_t i = 0; i < rc_profile_val.size(); i++){
                        CCEC_LOG( LOG_INFO, " %0x\t", rc_profile_val[i]);
                }


                //Operand - [Device Features]
                for(size_t i = 0; i < deviceFeatures.size(); i++){
                    device_features = deviceFeatures[i].getDeviceFeatures();
                    CCEC_LOG( LOG_INFO, "Tv Record Screen Support: %d, TV SetOSD string Support: %d, Supports being controlled by Deck Control:%d,\
                    Source supports Set Audio Rate: %d, Sink supports ARC Tx: %d, Source supports ARC Rx: %d", deviceFeatures[i].tvRecordScreenSupportBit(), \
                    deviceFeatures[i].tVSetOSDStringSupportBit(), deviceFeatures[i].controlledByDeckSupportBit(), deviceFeatures[i].setAudioRateSupportBit(), \
                    deviceFeatures[i].arcTxSupportBit() , deviceFeatures[i].arcRxSupportBit());
                }
                CCEC_LOG( LOG_INFO, "Device Features\n");
                for(size_t i = 0; i < device_features.size(); i++){
                        CCEC_LOG( LOG_INFO, " %s\t",device_features[i].c_str());
                }

                //CEC Byte Values corresponding to Device Features Operand
                for(size_t i = 0; i < deviceFeatures.size(); i++){
                    device_features_val = deviceFeatures[i].getDeviceFeaturesVal();
                }
                CCEC_LOG( LOG_INFO, "Device Features Byte values:\n");
                for(size_t i = 0; i < device_features_val.size(); i++){
                        CCEC_LOG( LOG_INFO, " %0x\t", device_features_val[i]);
                }

          }

        Version version;  ///< CEC version of the device.
        AllDeviceTypes allDeviceTypes;  ///< All device types supported by the device.
        std::vector<RcProfile> rcProfile;  ///< Remote-control profile operand(s).
	std::vector<DeviceFeatures> deviceFeatures;  ///< Device feature operand(s).
};
/**
 * @brief Request Current Latency (@c REQUEST_CURRENT_LATENCY) CEC message.
 *
 * Request the current audio/video latency for a path (CEC 2.0).
 * Carries the target @ref PhysicalAddress whose latency is requested.
 *
 * @see DataBlock
 */
class RequestCurrentLatency : public DataBlock
{

  public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c REQUEST_CURRENT_LATENCY opcode.
     */
    Op_t opCode(void) const {return REQUEST_CURRENT_LATENCY;}
        /**
         * @brief Constructs the Request Current Latency message from the supplied operand(s).
         *
         * @param[in] physicaladdress  Physical address of the path whose latency is requested; defaults to 0xFFFF.
         */
        RequestCurrentLatency(const PhysicalAddress &physicaladdress = {0xf,0xf,0xf,0xf} ): physicaladdress(physicaladdress) {}
         /* called by the messaged_decoder */
        /**
         * @brief Constructs the Request Current Latency message by decoding it from a received CEC frame.
         *
         * @param[in] frame     The received CEC frame to decode the message from.
         * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
         */
        RequestCurrentLatency(const CECFrame &frame, int startPos = 0):physicaladdress(frame, startPos)
        {
           if (frame.length() == 0 )
           {
              physicaladdress= PhysicalAddress((uint8_t) 0xf,(uint8_t) 0xf,(uint8_t)0xf,(uint8_t)0xf);
           }
        }
        /**
         * @brief Serializes this message's operand(s) by appending them to a CEC frame.
         *
         * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
         * @return Reference to @p frame to allow call chaining.
         *
         * @note When the physical address is 0xFFFF (unspecified) no operand bytes are appended.
         */
        CECFrame &serialize(CECFrame &frame) const {
          if ( (physicaladdress.getByteValue(3) == 0xF) && (physicaladdress.getByteValue(2) == 0xF) && (physicaladdress.getByteValue(1) == 0xF) &&  (physicaladdress.getByteValue(0) == 0xF)) {
            return frame;
          }
	  else {
            return physicaladdress.serialize(frame);
          }
      }
      /**
       * @brief Logs the contents of this message for debugging.
       */
      void print(void) const {
        CCEC_LOG( LOG_DEBUG,"RequestCurrentLatency : %s\n",physicaladdress.toString().c_str());
      }
  PhysicalAddress physicaladdress;  ///< Physical address of the path whose latency is requested (0xFFFF when unspecified).
};
/**
 * @brief Report Current Latency (@c REPORT_CURRENT_LATENCY) CEC message.
 *
 * Report the current audio/video latency for a path (CEC 2.0).
 * Carries the target @ref PhysicalAddress and the associated @ref LatencyInfo values.
 *
 * @see DataBlock
 */
class ReportCurrentLatency : public DataBlock
{

  public:
    /**
     * @brief Returns the CEC opcode identifying this message.
     *
     * @return The @c REPORT_CURRENT_LATENCY opcode.
     */
    Op_t opCode(void) const {return REPORT_CURRENT_LATENCY;}
        /**
         * @brief Constructs the Report Current Latency message from the supplied operand(s).
         *
         * @param[in] physicaladdress  Physical address the latency information applies to.
         * @param[in] videoLatency  Video latency value.
         * @param[in] latencyFlags  Latency flags describing the reported latencies.
         * @param[in] audioOutputDelay  Audio output delay; included only when the latency flags request it (defaults to 0).
         */
        ReportCurrentLatency(const PhysicalAddress &physicaladdress, uint8_t videoLatency, uint8_t latencyFlags, uint8_t audioOutputDelay = 0) : physicaladdress(physicaladdress) {

	    latencyInfo.push_back(LatencyInfo(videoLatency));
	    latencyInfo.push_back(LatencyInfo(latencyFlags));

	    /* Audio Output Delay is expected only if Latency Flags is equal to 3*/
            if(((latencyFlags) & 0x3) == 0x3){
                CCEC_LOG( LOG_DEBUG, "Audio Output compensated \n Audio Ouput Delay is %d ms\n", audioOutputDelay);
		latencyInfo.push_back(LatencyInfo(audioOutputDelay));
            }
	}
         /* called by the messaged_decoder */
        /**
         * @brief Constructs the Report Current Latency message by decoding it from a received CEC frame.
         *
         * @param[in] frame     The received CEC frame to decode the message from.
         * @param[in] startPos  Byte offset within @p frame at which this message's operands begin.
         */
        ReportCurrentLatency(const CECFrame &frame, int startPos = 0) :physicaladdress(frame, startPos) {
            uint8_t frame_len = frame.length();
            frame_len = frame_len > 5 ? 5 : frame_len ;
            latencyInfo.push_back(LatencyInfo(frame,startPos + 2, frame_len - 2));
        }


        /**
         * @brief Serializes this message's operand(s) by appending them to a CEC frame.
         *
         * @param[in,out] frame  The CEC frame to append this message's operand bytes to.
         * @return Reference to @p frame to allow call chaining.
         */
        CECFrame &serialize(CECFrame &frame) const {
	    physicaladdress.serialize(frame);
            for (uint8_t i=0; i < latencyInfo.size() ; i++) {
	        latencyInfo[i].serialize(frame);
            }

	    return frame;
        }

     /**
      * @brief Logs the contents of this message for debugging.
      */
     void print(void) const {
        CCEC_LOG( LOG_DEBUG,"Physical address: %s \n LatencyInfo : \n",physicaladdress.toString().c_str());
        for(uint8_t i = 0 ; i < latencyInfo.size() ; i++) {
            CCEC_LOG( LOG_DEBUG,"Video Latency : %d, Latency Flags : %d, Audio output Delay : %d\n", latencyInfo[i].getVideoLatency(), latencyInfo[i].getLatencyFlags(), latencyInfo[i].getAudioOutputDelay() );
        }
     }
  PhysicalAddress physicaladdress;  ///< Physical address the latency information applies to.
  std::vector<LatencyInfo> latencyInfo;  ///< Latency information: video latency, latency flags, and optional audio output delay.
};
#endif


/** @} */
/** @} */
