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


#ifndef HDMI_CCEC_MESSAGE_PROCESSOR_HPP_
#define HDMI_CCEC_MESSAGE_PROCESSOR_HPP_

#include <iostream>

#include "Messages.hpp"
#include "Header.hpp"

CCEC_BEGIN_NAMESPACE

/**
 * @brief The MessageProcessor class implements a set of overloaded process() methods, with each handling a specific message type.
 *
 * When a CEC frame is received, the MessageDecoder converts the raw bytes into a message object and invoke the
 * corresponding process() function.
 * @n @n
 * Application that desires to process certain CEC messages should extend MessageProcessor class and provide customized
 * implementation of the overloaded process() method. The default processing in the base class logs the message (each base
 * overload calls header.print() and msg.print()) and then takes no CEC protocol action; because it issues no response, an
 * unoverridden handler effectively acts as a message filter for the application.
 * @n @n
 * Here is an example code that sends an ActiveSource message by a Tuner device,
 * @code
 * CECFrame frame = (MessageEncoder().encode(
 *     Header(LogicalAddress(LogicalAddress::TUNER_1), LogicalAddress(LogicalAddress::TV)),
 *        ActiveSource(PhysicalAddress(phy0, phy1, phy2, phy3))));
 * Connection(LogicalAddress(LogicalAddress::TUNER_1)).send(frame);
 * @endcode
 * @ingroup HDMI_CEC_MSG_N_FRAME_CLASSES
 */
class MessageProcessor
{
public:
	/**
	 * @brief Constructs a MessageProcessor.
	 *
	 * The base MessageProcessor is stateless; construction performs no action.
	 * Subclasses typically add their own state and override the process()
	 * handlers for the CEC messages they are interested in.
	 */
	MessageProcessor(void) {}

	/**
	 * @brief Default handler for the Active Source (ACTIVE_SOURCE) CEC message.
	 *
	 * Broadcast by a source device to announce that it has become the active source.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded ActiveSource message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const ActiveSource &msg, const Header &header)   				{header.print();msg.print();}
	/**
	 * @brief Default handler for the Inactive Source (INACTIVE_SOURCE) CEC message.
	 *
	 * Sent by a source device to indicate that it is no longer the active source.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded InActiveSource message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const InActiveSource &msg, const Header &header) 				{header.print();msg.print();}
	/**
	 * @brief Default handler for the Image View On (IMAGE_VIEW_ON) CEC message.
	 *
	 * Requests a TV to power on (leave standby) so that an image can be displayed.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded ImageViewOn message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const ImageViewOn &msg, const Header &header)    				{header.print();msg.print();}
	/**
	 * @brief Default handler for the Text View On (TEXT_VIEW_ON) CEC message.
	 *
	 * Requests a TV to power on (leave standby) so that text can be displayed.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded TextViewOn message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const TextViewOn &msg, const Header &header) 	 				{header.print();msg.print();}
	/**
	 * @brief Default handler for the Request Active Source (REQUEST_ACTIVE_SOURCE) CEC message.
	 *
	 * Broadcast requesting the current active source to identify itself.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded RequestActiveSource message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const RequestActiveSource &msg, const Header &header) 		{header.print();msg.print();}
	/**
	 * @brief Default handler for the Standby (STANDBY) CEC message.
	 *
	 * Requests one device, or all devices, to enter the standby (low-power) state.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded Standby message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const Standby &msg, const Header &header)						{header.print();msg.print();}
	/**
	 * @brief Default handler for the Get CEC Version (GET_CEC_VERSION) CEC message.
	 *
	 * Requests the CEC protocol version supported by the addressed device.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded GetCECVersion message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const GetCECVersion &msg, const Header &header) 				{header.print();msg.print();}
	/**
	 * @brief Default handler for the CEC Version (CEC_VERSION) CEC message.
	 *
	 * Reports the CEC protocol version supported by the sending device.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded CECVersion message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const CECVersion &msg, const Header &header) 		 			{header.print();msg.print();}
	/**
	 * @brief Default handler for the Set Menu Language (SET_MENU_LANGUAGE) CEC message.
	 *
	 * Informs devices of the menu language used by the initiator.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded SetMenuLanguage message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const SetMenuLanguage &msg, const Header &header) 			{header.print();msg.print();}
	/**
	 * @brief Default handler for the Give OSD Name (GIVE_OSD_NAME) CEC message.
	 *
	 * Requests the addressed device to report its preferred OSD (on-screen display) name.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded GiveOSDName message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const GiveOSDName &msg, const Header &header) 		 		{header.print();msg.print();}
	/**
	 * @brief Default handler for the Give Physical Address (GIVE_PHYSICAL_ADDRESS) CEC message.
	 *
	 * Requests the addressed device to report its physical address and device type.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded GivePhysicalAddress message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const GivePhysicalAddress &msg, const Header &header) 		{header.print();msg.print();}
	/**
	 * @brief Default handler for the Give Device Vendor ID (GIVE_DEVICE_VENDOR_ID) CEC message.
	 *
	 * Requests the addressed device to report its vendor ID.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded GiveDeviceVendorID message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const GiveDeviceVendorID &msg, const Header &header) 		 	{header.print();msg.print();}
	/**
	 * @brief Default handler for the Set OSD String (SET_OSD_STRING) CEC message.
	 *
	 * Requests the TV to display a text string on its on-screen display.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded SetOSDString message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const SetOSDString &msg, const Header &header) 		 		{header.print();msg.print();}
	/**
	 * @brief Default handler for the Set OSD Name (SET_OSD_NAME) CEC message.
	 *
	 * Reports the preferred OSD name of the sending device.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded SetOSDName message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const SetOSDName &msg, const Header &header) 		 			{header.print();msg.print();}
	/**
	 * @brief Default handler for the Routing Change (ROUTING_CHANGE) CEC message.
	 *
	 * Broadcast indicating that the active routing path has changed from one address to another.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded RoutingChange message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const RoutingChange &msg, const Header &header) 			 	{header.print();msg.print();}
	/**
	 * @brief Default handler for the Routing Information (ROUTING_INFORMATION) CEC message.
	 *
	 * Broadcast reporting the current routing information for a physical address.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded RoutingInformation message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const RoutingInformation &msg, const Header &header) 	 		{header.print();msg.print();}
	/**
	 * @brief Default handler for the Set Stream Path (SET_STREAM_PATH) CEC message.
	 *
	 * Requests the device at the given physical address to become the active source.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded SetStreamPath message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const SetStreamPath &msg, const Header &header) 	 			{header.print();msg.print();}
	/**
	 * @brief Default handler for the Get Menu Language (GET_MENU_LANGUAGE) CEC message.
	 *
	 * Requests the addressed device to report its current menu language.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded GetMenuLanguage message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const GetMenuLanguage &msg, const Header &header) 			{header.print();msg.print();}
	/**
	 * @brief Default handler for the Report Physical Address (REPORT_PHYSICAL_ADDRESS) CEC message.
	 *
	 * Broadcast reporting the physical address and device type of the sending device.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded ReportPhysicalAddress message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const ReportPhysicalAddress &msg, const Header &header) 		{header.print();msg.print();}
	/**
	 * @brief Default handler for the Device Vendor ID (DEVICE_VENDOR_ID) CEC message.
	 *
	 * Broadcast reporting the vendor ID of the sending device.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded DeviceVendorID message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const DeviceVendorID &msg, const Header &header) 				{header.print();msg.print();}
	/**
	 * @brief Default handler for the User Control Released (USER_CONTROL_RELEASED) CEC message.
	 *
	 * Indicates that a remote-control user-control button has been released.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded UserControlReleased message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const UserControlReleased &msg, const Header &header) 		{header.print();msg.print();}
	/**
	 * @brief Default handler for the User Control Pressed (USER_CONTROL_PRESSED) CEC message.
	 *
	 * Indicates that a remote-control user-control button has been pressed.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded UserControlPressed message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const UserControlPressed &msg, const Header &header) 		{header.print();msg.print();}
	/**
	 * @brief Default handler for the Give Device Power Status (GIVE_DEVICE_POWER_STATUS) CEC message.
	 *
	 * Requests the addressed device to report its current power status.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded GiveDevicePowerStatus message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const GiveDevicePowerStatus &msg, const Header &header) 		{header.print();msg.print();}
	/**
	 * @brief Default handler for the Report Power Status (REPORT_POWER_STATUS) CEC message.
	 *
	 * Reports the current power status of the sending device.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded ReportPowerStatus message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const ReportPowerStatus &msg, const Header &header) 			{header.print();msg.print();}
	/**
	 * @brief Default handler for the Feature Abort (FEATURE_ABORT) CEC message.
	 *
	 * Indicates that a received message could not be processed, together with the abort reason.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded FeatureAbort message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const FeatureAbort &msg, const Header &header) 				{header.print();msg.print();}
	/**
	 * @brief Default handler for the Abort (ABORT) CEC message.
	 *
	 * General-purpose Abort feature request used to test a device's message handling.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded Abort message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const Abort &msg, const Header &header) 						{header.print();msg.print();}
	/**
	 * @brief Default handler for the Polling (POLLING) CEC message.
	 *
	 * Polling message used for logical-address allocation and device-presence detection.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded Polling message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const Polling &msg, const Header &header) 					{header.print();msg.print();}
    /**
     * @brief Default handler for the Initiate ARC (INITIATE_ARC) CEC message.
     *
     * Requests initiation of the Audio Return Channel (ARC).
     * The base-class implementation logs the header followed by the message
     * (via header.print() and msg.print()) and takes no further action.
     *
     * @param[in] msg    The decoded InitiateArc message to be processed.
     * @param[in] header The message header carrying the initiator (source) and
     *                   destination logical addresses.
     *
     * @note This method is virtual and is intended to be overridden by a
     *       subclass to provide application-specific handling. The default
     *       base implementation only logs the message and performs no CEC
     *       protocol response.
     */
    virtual void process (const InitiateArc &msg, const Header &header)                             {header.print();msg.print();}
    /**
     * @brief Default handler for the Terminate ARC (TERMINATE_ARC) CEC message.
     *
     * Requests termination of the Audio Return Channel (ARC).
     * The base-class implementation logs the header followed by the message
     * (via header.print() and msg.print()) and takes no further action.
     *
     * @param[in] msg    The decoded TerminateArc message to be processed.
     * @param[in] header The message header carrying the initiator (source) and
     *                   destination logical addresses.
     *
     * @note This method is virtual and is intended to be overridden by a
     *       subclass to provide application-specific handling. The default
     *       base implementation only logs the message and performs no CEC
     *       protocol response.
     */
    virtual void process (const TerminateArc &msg, const Header &header)                             {header.print();msg.print();}
	/**
	 * @brief Default handler for the Request Short Audio Descriptor (REQUEST_SHORT_AUDIO_DESCRIPTOR) CEC message.
	 *
	 * Requests one or more Short Audio Descriptors for the specified audio formats.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded RequestShortAudioDescriptor message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const RequestShortAudioDescriptor &msg,const Header &header)               {header.print();msg.print();}
	/**
	 * @brief Default handler for the Report Short Audio Descriptor (REPORT_SHORT_AUDIO_DESCRIPTOR) CEC message.
	 *
	 * Reports the requested Short Audio Descriptors.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded ReportShortAudioDescriptor message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const ReportShortAudioDescriptor &msg, const Header &header)               {header.print();msg.print();}
	/**
	 * @brief Default handler for the System Audio Mode Request (SYSTEM_AUDIO_MODE_REQUEST) CEC message.
	 *
	 * Requests the amplifier to enable or disable System Audio Mode.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded SystemAudioModeRequest message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const SystemAudioModeRequest &msg , const Header &header)                  {header.print();msg.print();}
	/**
	 * @brief Default handler for the Set System Audio Mode (SET_SYSTEM_AUDIO_MODE) CEC message.
	 *
	 * Turns System Audio Mode on or off.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded SetSystemAudioMode message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const SetSystemAudioMode &msg , const Header &header)                      {header.print();msg.print();}
	/**
	 * @brief Default handler for the Report Audio Status (REPORT_AUDIO_STATUS) CEC message.
	 *
	 * Reports the audio status (mute state and volume level) of the sending device.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded ReportAudioStatus message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const ReportAudioStatus  &msg, const Header &header)                       {header.print();msg.print();}
	/**
	 * @brief Default handler for the Give Features (GIVE_FEATURES) CEC message.
	 *
	 * Requests the addressed device to report its CEC features (CEC 2.0).
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded GiveFeatures message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const GiveFeatures &msg, const Header &header)            {header.print();msg.print();}
	/**
	 * @brief Default handler for the Report Features (REPORT_FEATURES) CEC message.
	 *
	 * Reports the CEC version, device features and RC profile of the sending device (CEC 2.0).
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded ReportFeatures message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const ReportFeatures &msg, const Header &header)            {header.print();msg.print();}
	/**
	 * @brief Default handler for the Request Current Latency (REQUEST_CURRENT_LATENCY) CEC message.
	 *
	 * Requests the current video/audio latency for the given physical address.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded RequestCurrentLatency message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const RequestCurrentLatency &msg, const Header &header)			 {header.print();msg.print();}
	/**
	 * @brief Default handler for the Report Current Latency (REPORT_CURRENT_LATENCY) CEC message.
	 *
	 * Reports the current video/audio latency for the given physical address.
	 * The base-class implementation logs the header followed by the message
	 * (via header.print() and msg.print()) and takes no further action.
	 *
	 * @param[in] msg    The decoded ReportCurrentLatency message to be processed.
	 * @param[in] header The message header carrying the initiator (source) and
	 *                   destination logical addresses.
	 *
	 * @note This method is virtual and is intended to be overridden by a
	 *       subclass to provide application-specific handling. The default
	 *       base implementation only logs the message and performs no CEC
	 *       protocol response.
	 */
	virtual void process (const ReportCurrentLatency &msg, const Header &header)               	 {header.print();msg.print();}
	/**
	 * @brief Virtual destructor.
	 *
	 * Declared virtual so that a derived MessageProcessor can be destroyed
	 * safely through a base-class pointer. Performs no action in the base class.
	 */
	virtual ~MessageProcessor(void) {}

private:
};

CCEC_END_NAMESPACE
#endif


/** @} */
/** @} */
