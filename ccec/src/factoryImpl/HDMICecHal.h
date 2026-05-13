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

typedef void (*HdmiCecRxCallback_t)(int handle, void *callbackData,
                                     unsigned char *buf, int len);
typedef void (*HdmiCecTxCallback_t)(int handle, void *callbackData,
                                     int result);

class HDMICecHal {
public:
    virtual ~HDMICecHal() = default;

    /**
     * @brief Open the HDMI CEC driver and register Rx/Tx callbacks.
     *
     * Legacy path: calls HdmiCecOpen + HdmiCecSetRxCallback + HdmiCecSetTxCallback.
     * AIDL path:   connects to the binder service and opens a controller with
     *              an event listener that bridges to the supplied callbacks.
     *
     * @param[out] handle  Receives the native handle (legacy) or a dummy handle (AIDL).
     * @param      rxCb    Receive callback (same signature as HdmiCecSetRxCallback).
     * @param      txCb    Transmit callback (same signature as HdmiCecSetTxCallback).
     * @param      cbData  Opaque data pointer forwarded to rxCb/txCb.
     * @return HDMI_CEC_IO_SUCCESS (0) on success, or an HDMI_CEC_IO_* error code.
     */
    virtual int open(int *handle,
                     HdmiCecRxCallback_t rxCb,
                     HdmiCecTxCallback_t txCb,
                     void *cbData) = 0;
};

#endif // HDMI_CEC_HAL_H

