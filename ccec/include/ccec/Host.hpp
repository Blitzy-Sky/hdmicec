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


#ifndef HDMI_CCECHOST_HPP_
#define HDMI_CCECHOST_HPP_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Host Plugin must implement the following.
 */

/**
 * @brief Host-plugin API return status codes.
 *
 * Enumerates the status values returned by the @c CECHost_* host-plugin
 * functions. @c CECHost_ERR_NONE (value 0) indicates success; all other
 * enumerators indicate a specific failure condition.
 */
typedef enum {
	CECHost_ERR_GENERAL = -1,  ///< General (unspecified) host-plugin error.
	CECHost_ERR_NONE,  ///< Success; the operation completed without error.
	CECHost_ERR_INVALID,  ///< An invalid argument or parameter was supplied.
	CECHost_ERR_SYMBOL,  ///< A required host-plugin symbol could not be resolved.
	CECHost_ERR_HOST,  ///< The underlying host platform reported a failure.
	CECHost_ERR_STATE,  ///< The operation is not valid in the current state.
} CECHost_Err_t;

/**
 * @def CECHost_HDMI_CONNECTED
 * @brief HDMI output connection state indicating the output is connected.
 */
#define CECHost_HDMI_CONNECTED 	0
/**
 * @def CECHost_HDMI_DISCONNECTED
 * @brief HDMI output connection state indicating the output is disconnected.
 */
#define CECHost_HDMI_DISCONNECTED 	1

/**
 * @def CECHost_POWERSTATE_ON
 * @brief Device power state indicating the device is powered on.
 */
#define CECHost_POWERSTATE_ON        0
/**
 * @def CECHost_POWERSTATE_STANDBY
 * @brief Device power state indicating the device is in standby.
 */
#define CECHost_POWERSTATE_STANDBY   1

/**
 * @brief Host power-management policy flags.
 *
 * Conveys the host's policy regarding whether attached devices may be
 * powered off in response to CEC activity.
 */
typedef struct _CECHost_Policy_t {
	int32_t turnOffTv;  ///< Host policy field concerning turning the TV off (value encoding defined by the host).
	int32_t turnOffSTB;  ///< Host policy field concerning turning the STB off (value encoding defined by the host).
} CECHost_Policy_t;

/**
 * @brief Identifies which kind of device status is carried by a
 *        @c CECHost_DeviceStatus_t instance.
 */
typedef enum _CECHost_DeviceStatusType_t{
	CECHost_POWER_STATUS = 1,  ///< The status payload carries a device power state.
	CECHost_OSD_NAME = 2,  ///< The status payload carries an OSD name.
	CECHost_CONNECTED_STATUS =3  ///< The status payload carries a connection state.
}CECHost_DeviceStatusType_t;

/**
 * @brief Describes a single status attribute of a logical CEC device.
 *
 * The @c statusType member selects which member of the @c data union is
 * valid for a given instance.
 */
typedef struct _CECHost_DeviceStatus_t
{
	CECHost_DeviceStatusType_t statusType;  ///< Discriminator selecting which @c data member is valid.
	union{
		int powerState;  ///< Power state; valid when statusType == CECHost_POWER_STATUS.
		int isConnected;  ///< Connection state; valid when statusType == CECHost_CONNECTED_STATUS.
		char osdName[14+1];  ///< OSD name buffer of 15 bytes (char[14+1]), storage capacity only; NUL termination is not guaranteed. Valid when statusType == CECHost_OSD_NAME.
	}data;  ///< Status payload; the active member is selected by statusType.
}CECHost_DeviceStatus_t;

/*
 * The following are a set of callbacks that the host module
 * use to notify CEC about its state change.
 */

/**
 * @brief Callback invoked by the host to notify CEC of an HDMI hotplug event.
 *
 * @param[in] connect The new HDMI output connection state, either
 *                    @c CECHost_HDMI_CONNECTED or @c CECHost_HDMI_DISCONNECTED.
 * @return @c CECHost_ERR_NONE on success, or another @c CECHost_Err_t error code.
 */
typedef CECHost_Err_t (*CECHost_HdmiHotplugCallback_t)	(int32_t connect);
/**
 * @brief Callback invoked by the host to notify CEC of a power-state change.
 *
 * @param[in] curState The power state prior to the change, either
 *                     @c CECHost_POWERSTATE_ON or @c CECHost_POWERSTATE_STANDBY.
 * @param[in] newState The power state being transitioned to, either
 *                     @c CECHost_POWERSTATE_ON or @c CECHost_POWERSTATE_STANDBY.
 * @return @c CECHost_ERR_NONE on success, or another @c CECHost_Err_t error code.
 */
typedef CECHost_Err_t (*CECHost_PowerStateCallback_t)	(int32_t curState, int32_t newState);
/**
 * @brief Callback invoked by the host to exchange device-manager status with CEC.
 *
 * @param[in]  ipStatus Input device-manager status flag supplied to CEC.
 * @param[out] opStatus Receives the resulting device-manager status.
 * @return @c CECHost_ERR_NONE on success, or another @c CECHost_Err_t error code.
 */
typedef CECHost_Err_t (*CECHost_DevMgrStatusCallback_t) (bool ipStatus,bool* opStatus);
/*
 * Description: Notify CEC of the latest OSD name.
 *
 * The 'name' need not be null terminated. if it is, the 'len' does
 * not include the 'null' termintator.
 *
 * @param name: the ASCII bytes of the OSD name.
 * @param len:  the number of ASCII bytes.
 */
/**
 * @brief Callback invoked by the host to notify CEC of the latest OSD name.
 *
 * @param[in] name the ASCII bytes of the OSD name.
 * @param[in] len  the number of ASCII bytes.
 * @return @c CECHost_ERR_NONE on success, or another @c CECHost_Err_t error code.
 */
typedef CECHost_Err_t (*CECHost_OSDNameCallback_t)		(uint8_t *name, size_t len);
/**
 * @brief Callback invoked by the host to notify CEC of a policy change.
 *
 * @param[in] policy The current host power-management policy.
 * @return @c CECHost_ERR_NONE on success, or another @c CECHost_Err_t error code.
 */
typedef CECHost_Err_t (*CECHost_PolicyCallback_t)		(CECHost_Policy_t policy);

/*
 * A Set of callback to notify CEC that the host state has changed.
 */
/**
 * @brief Set of callbacks the host uses to notify CEC of host state changes.
 */
typedef struct _CECHost_Callback_t {
	CECHost_HdmiHotplugCallback_t 	hotplugCb;  ///< HDMI hotplug notification callback.
	CECHost_PowerStateCallback_t  	pwrStateCb;  ///< Power-state change notification callback.
        CECHost_DevMgrStatusCallback_t  devMgrStatusCb;  ///< Device-manager status exchange callback.
	CECHost_OSDNameCallback_t 		osdCb;  ///< OSD-name notification callback.
	CECHost_PolicyCallback_t 		policyCb;  ///< Policy change notification callback.
} CECHost_Callback_t;

/**
 * @brief Initialize the host-plugin interface.
 *
 * @param[in] name Name identifying the host plugin to initialize.
 * @return @c CECHost_ERR_NONE on success, or another @c CECHost_Err_t error code.
 */
CECHost_Err_t CECHost_Init(const char *name);
/**
 * @brief Terminate the host-plugin interface and release its resources.
 *
 * @return @c CECHost_ERR_NONE on success, or another @c CECHost_Err_t error code.
 */
CECHost_Err_t CECHost_Term(void);

/**
 * @brief Register the set of host-to-CEC notification callbacks.
 *
 * @param[in] cb Structure containing the callback function pointers to register.
 * @return @c CECHost_ERR_NONE on success, or another @c CECHost_Err_t error code.
 */
CECHost_Err_t CECHost_SetCallback(CECHost_Callback_t cb);

/* HDMI  */
/**
 * @brief Retrieve the physical address of the HDMI output.
 *
 * The physical address is returned via four output byte parameters. Their
 * ordering and composition within the physical address are defined by the host
 * implementation and are not specified by this declaration.
 *
 * @param[out] byte0 Receives output byte @c byte0 of the HDMI output physical address.
 * @param[out] byte1 Receives output byte @c byte1 of the HDMI output physical address.
 * @param[out] byte2 Receives output byte @c byte2 of the HDMI output physical address.
 * @param[out] byte3 Receives output byte @c byte3 of the HDMI output physical address.
 * @return @c CECHost_ERR_NONE on success, or another @c CECHost_Err_t error code.
 */
CECHost_Err_t CECHost_GetHdmiOuputPhysicalAddress(uint8_t *byte0, uint8_t *byte1, uint8_t *byte2, uint8_t *byte3);
/**
 * @brief Query whether the HDMI output is currently connected.
 *
 * @param[out] connect Receives the connection state, either
 *                     @c CECHost_HDMI_CONNECTED or @c CECHost_HDMI_DISCONNECTED.
 * @return @c CECHost_ERR_NONE on success, or another @c CECHost_Err_t error code.
 */
CECHost_Err_t CECHost_IsHdmiOutputConnected(int32_t *connect);

/* Power */
/**
 * @brief Retrieve the current device power state.
 *
 * @param[out] state Receives the current power state, either
 *                   @c CECHost_POWERSTATE_ON or @c CECHost_POWERSTATE_STANDBY.
 * @return @c CECHost_ERR_NONE on success, or another @c CECHost_Err_t error code.
 */
CECHost_Err_t CECHost_GetPowerState(int32_t *state);
/**
 * @brief Set the device power state.
 *
 * @param[in] state The power state to apply, either
 *                  @c CECHost_POWERSTATE_ON or @c CECHost_POWERSTATE_STANDBY.
 * @return @c CECHost_ERR_NONE on success, or another @c CECHost_Err_t error code.
 */
CECHost_Err_t CECHost_SetPowerState(int32_t state);

/* Device Status */
/**
 * @brief Update the status of a logical CEC device.
 *
 * @param[in] logicalAddress The logical address of the device to update.
 * @param[in] deviceStatus   The status attribute to store for the device.
 * @return @c CECHost_ERR_NONE on success, or another @c CECHost_Err_t error code.
 */
CECHost_Err_t CECHost_SetDeviceStatus(int logicalAddress, CECHost_DeviceStatus_t *deviceStatus);

/* CEC Control */

/*
 * Description: Get the OSD name from Host module.
 *
 * The 'name' need not be null terminated. if it is, the 'len' does
 * not include the 'null' termintator.
 *
 * @param name: the ASCII bytes of the OSD name.
 * @param len:  the number of ASCII bytes.
 */
/**
 * @brief Get the OSD name from the host module.
 *
 * @param[out] name the ASCII bytes of the OSD name.
 * @param[out] len  the number of ASCII bytes.
 * @return @c CECHost_ERR_NONE on success, or another @c CECHost_Err_t error code.
 */

CECHost_Err_t CECHost_GetOSDName(uint8_t *name, size_t *len);
/**
 * @brief Retrieve the current host power-management policy.
 *
 * @param[out] policy Receives the current policy flags.
 * @return @c CECHost_ERR_NONE on success, or another @c CECHost_Err_t error code.
 */
CECHost_Err_t CECHost_GetPolicy(CECHost_Policy_t *policy);


/*
 * If box is an active source is different from its power state.
 * I.e. a PowerOn STB in lightsleep may not claim to be an
 * active source.
 *
 * STANDBY implies Inactive
 * ON does not imply Active
 * Active imples ON
 * Inactive does not imply STANDBY
 *
 */
/**
 * @brief Query whether the box is currently an active source.
 *
 * @param[out] active Receives non-zero if the box is an active source, zero otherwise.
 * @return @c CECHost_ERR_NONE on success, or another @c CECHost_Err_t error code.
 */
CECHost_Err_t CECHost_IsActive(int32_t *active);


/*
 * Host Plugin Need not implement these two APIs.
 */
/**
 * @brief Load the host plugin.
 *
 * @return @c CECHost_ERR_NONE on success, or another @c CECHost_Err_t error code.
 */
CECHost_Err_t CECHost_LoadPlugin(void);
/**
 * @brief Unload the host plugin.
 *
 * @return @c CECHost_ERR_NONE on success, or another @c CECHost_Err_t error code.
 */
CECHost_Err_t CECHost_UnloadPlugin(void);



#ifdef __cplusplus
}
#endif

#endif


/** @} */
/** @} */
