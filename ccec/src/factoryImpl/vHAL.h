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

#ifndef V_HAL_H
#define V_HAL_H

#include "HDMICecHal.h"

/**
 * @brief Legacy (C HAL) implementation of HDMICecHal.
 *
 * Each override delegates directly to the corresponding HdmiCec* C function
 * from hdmi_cec_driver.h. Return values are cast to HdmiCecStatus_t, which
 * is value-aligned with HDMI_CEC_STATUS so no information is lost.
 */
class vHAL : public HDMICecHal {
public:
    /**
     * @brief Open the HDMI CEC HAL driver.
     * Calls HdmiCecOpen().
     */
    HdmiCecStatus_t open(int *handle) override;

    /**
     * @brief Close the HDMI CEC HAL driver.
     * Calls HdmiCecClose().
     */
    HdmiCecStatus_t close(int handle) override;

    /**
     * @brief Register the receive callback.
     * Calls HdmiCecSetRxCallback().
     */
    HdmiCecStatus_t setRxCallback(int handle, HdmiCecRxCallback_t cbfunc, void *data) override;

    /**
     * @brief Register the transmit-status callback.
     * Calls HdmiCecSetTxCallback().
     */
    HdmiCecStatus_t setTxCallback(int handle, HdmiCecTxCallback_t cbfunc, void *data) override;

    /**
     * @brief Synchronous CEC transmit.
     * Calls HdmiCecTx(). The send result is written to *result.
     */
    HdmiCecStatus_t tx(int handle, const unsigned char *buf, int len, int *result) override;

    /**
     * @brief Asynchronous CEC transmit.
     * Calls HdmiCecTxAsync(). Result is delivered via the TxCallback.
     */
    HdmiCecStatus_t txAsync(int handle, const unsigned char *buf, int len) override;

    /**
     * @brief Add a logical address.
     * Calls HdmiCecAddLogicalAddress().
     */
    HdmiCecStatus_t addLogicalAddress(int handle, int logicalAddress) override;

    /**
     * @brief Remove a logical address.
     * Calls HdmiCecRemoveLogicalAddress().
     */
    HdmiCecStatus_t removeLogicalAddress(int handle, int logicalAddress) override;

    /**
     * @brief Get the device logical address.
     * Calls HdmiCecGetLogicalAddress().
     */
    HdmiCecStatus_t getLogicalAddress(int handle, int *logicalAddress) override;

    /**
     * @brief Get the device physical address.
     * Calls HdmiCecGetPhysicalAddress().
     */
    HdmiCecStatus_t getPhysicalAddress(int handle, unsigned int *physicalAddress) override;
};

#endif // V_HAL_H


