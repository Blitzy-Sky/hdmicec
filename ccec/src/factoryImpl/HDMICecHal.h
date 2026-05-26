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

#ifndef HDMI_CEC_HAL_H
#define HDMI_CEC_HAL_H

#include <stdint.h>

/**
 * @brief Common status codes for HDMICecHal operations.
 *
 * Values are intentionally aligned with HDMI_CEC_STATUS from hdmi_cec_driver.h
 * so that legacy HAL implementations can cast directly without loss of meaning.
 * AIDL implementations must map binder/service error codes to these values.
 */
typedef enum HdmiCecStatus
{
    HDMICEC_HAL_SUCCESS = 0,                        ///< Operation successful
    HDMICEC_HAL_SENT_AND_ACKD,                      ///< Sent and acknowledged
    HDMICEC_HAL_SENT_BUT_NOT_ACKD,                  ///< Sent but not acknowledged
    HDMICEC_HAL_SENT_FAILED,                         ///< Send failed
    HDMICEC_HAL_NOT_OPENED,                          ///< Module not initialised
    HDMICEC_HAL_INVALID_ARGUMENT,                    ///< Invalid argument
    HDMICEC_HAL_LOGICALADDRESS_UNAVAILABLE,          ///< Logical address unavailable
    HDMICEC_HAL_GENERAL_ERROR,                       ///< General error
    HDMICEC_HAL_ALREADY_OPEN,                        ///< Already initialised
    HDMICEC_HAL_ALREADY_REMOVED,                     ///< Already removed
    HDMICEC_HAL_INVALID_OUTPUT,                      ///< Output argument out of valid range
    HDMICEC_HAL_INVALID_HANDLE,                      ///< Invalid handle
    HDMICEC_HAL_OPERATION_NOT_SUPPORTED,             ///< Operation not supported
    HDMICEC_HAL_NOT_ADDED,                           ///< Address was never added
    HDMICEC_HAL_MAX                                  ///< Sentinel - must be last
} HdmiCecStatus_t;

/**
 * @brief Callback invoked when a complete CEC message is received.
 *
 * The message data must be copied inside the callback; buf is invalid after return.
 *
 * @param[in] handle        HAL instance handle (non-zero).
 * @param[in] callbackData  Opaque user data supplied at registration time.
 * @param[in] buf           Buffer containing the received CEC message.
 * @param[in] len           Length of buf in bytes.
 */
typedef void (*HdmiCecRxCallback_t)(int handle, void *callbackData,
                                     unsigned char *buf, int len);

/**
 * @brief Callback invoked to report the status of an asynchronous transmit.
 *
 * @param[in] handle        HAL instance handle (non-zero).
 * @param[in] callbackData  Opaque user data supplied at registration time.
 * @param[in] result        Transmit result code (HdmiCecStatus_t value).
 */
typedef void (*HdmiCecTxCallback_t)(int handle, void *callbackData, int result);

/**
 * @brief Abstract base class for HDMI CEC hardware abstraction.
 *
 * Each virtual function maps 1:1 to a HAL operation. Subclasses provide
 * either a legacy (C HAL) implementation or an AIDL (binder) implementation.
 *
 * - Legacy subclass: delegates directly to HdmiCec* C functions.
 * - AIDL subclass:   communicates with the Android HDMI CEC AIDL service.
 */
class HDMICecHal {
public:
    virtual ~HDMICecHal() = default;

    /**
     * @brief Open the HDMI CEC HAL driver.
     *
     * Legacy: calls HdmiCecOpen().
     * AIDL:   connects to the AIDL binder service and obtains a session.
     *
     * @param[out] handle  Receives the driver handle.
     * @return HDMICEC_HAL_SUCCESS on success, or an error code.
     */
    virtual HdmiCecStatus_t open(int *handle) = 0;

    /**
     * @brief Close the HDMI CEC HAL driver.
     *
     * Legacy: calls HdmiCecClose().
     * AIDL:   disconnects from the binder service.
     *
     * @param[in] handle  The driver handle returned by open().
     * @return HDMICEC_HAL_SUCCESS on success, or an error code.
     */
    virtual HdmiCecStatus_t close(int handle) = 0;

    /**
     * @brief Set the receive callback for incoming CEC messages.
     *
     * Legacy: calls HdmiCecSetRxCallback().
     * AIDL:   registers an AIDL callback listener that bridges to cbfunc.
     *
     * @param[in] handle  The driver handle.
     * @param[in] cbfunc  Callback function invoked on message reception.
     * @param[in] data    Opaque user data forwarded to cbfunc.
     * @return HDMICEC_HAL_SUCCESS on success, or an error code.
     */
    virtual HdmiCecStatus_t setRxCallback(int handle, HdmiCecRxCallback_t cbfunc, void *data) = 0;

    /**
     * @brief Set the transmit callback for transmission status notification.
     *
     * Legacy: calls HdmiCecSetTxCallback().
     * AIDL:   registers an AIDL callback listener that bridges to cbfunc.
     *
     * @param[in] handle  The driver handle.
     * @param[in] cbfunc  Callback function invoked with transmit result.
     * @param[in] data    Opaque user data forwarded to cbfunc.
     * @return HDMICEC_HAL_SUCCESS on success, or an error code.
     */
    virtual HdmiCecStatus_t setTxCallback(int handle, HdmiCecTxCallback_t cbfunc, void *data) = 0;

    /**
     * @brief Transmit a CEC message synchronously.
     *
     * Legacy: calls HdmiCecTx().
     * AIDL:   sends message via binder and blocks until result is available.
     *
     * @param[in]  handle  The driver handle.
     * @param[in]  buf     Buffer containing the CEC message.
     * @param[in]  len     Length of the message in bytes.
     * @param[out] result  Receives the transmission result code.
     * @return HDMICEC_HAL_SUCCESS on success, or an error code.
     */
    virtual HdmiCecStatus_t tx(int handle, const unsigned char *buf, int len, int *result) = 0;

    /**
     * @brief Transmit a CEC message asynchronously.
     *
     * Legacy: calls HdmiCecTxAsync().
     * AIDL:   sends message via binder; result delivered through TxCallback.
     *
     * @param[in] handle  The driver handle.
     * @param[in] buf     Buffer containing the CEC message.
     * @param[in] len     Length of the message in bytes.
     * @return HDMICEC_HAL_SUCCESS on success, or an error code.
     */
    virtual HdmiCecStatus_t txAsync(int handle, const unsigned char *buf, int len) = 0;

    /**
     * @brief Add a logical address for receiving CEC messages.
     *
     * Legacy: calls HdmiCecAddLogicalAddress().
     * AIDL:   registers the logical address via binder.
     *
     * @param[in] handle          The driver handle.
     * @param[in] logicalAddress  Logical address to add (0-15).
     * @return HDMICEC_HAL_SUCCESS on success, or an error code.
     */
    virtual HdmiCecStatus_t addLogicalAddress(int handle, int logicalAddress) = 0;

    /**
     * @brief Remove a previously added logical address.
     *
     * Legacy: calls HdmiCecRemoveLogicalAddress().
     * AIDL:   unregisters the logical address via binder.
     *
     * @param[in] handle          The driver handle.
     * @param[in] logicalAddress  Logical address to remove (0-15).
     * @return HDMICEC_HAL_SUCCESS on success, or an error code.
     */
    virtual HdmiCecStatus_t removeLogicalAddress(int handle, int logicalAddress) = 0;

    /**
     * @brief Get the logical address of the device.
     *
     * Legacy: calls HdmiCecGetLogicalAddress().
     * AIDL:   queries the logical address via binder.
     *
     * @param[in]  handle          The driver handle.
     * @param[out] logicalAddress  Pointer to store the logical address.
     * @return HDMICEC_HAL_SUCCESS on success, or an error code.
     */
    virtual HdmiCecStatus_t getLogicalAddress(int handle, int *logicalAddress) = 0;

    /**
     * @brief Get the physical address of the device.
     *
     * Legacy: calls HdmiCecGetPhysicalAddress().
     * AIDL:   queries the physical address via binder.
     *
     * @param[in]  handle           The driver handle.
     * @param[out] physicalAddress  Pointer to store the physical address.
     * @return HDMICEC_HAL_SUCCESS on success, or an error code.
     */
    virtual HdmiCecStatus_t getPhysicalAddress(int handle, unsigned int *physicalAddress) = 0;
};

#endif // HDMI_CEC_HAL_H


