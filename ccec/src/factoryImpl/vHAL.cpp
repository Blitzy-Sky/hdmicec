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

/*
 * vHAL.cpp — Legacy C HAL implementation of HDMICecHal.
 *
 * Each function wraps the corresponding HdmiCec* C API from hdmi_cec_driver.h.
 */

#include <cstddef>
#include "vHAL.h"
#include "ccec/Util.hpp"

/* -----------------------------------------------------------------------
 * open
 * Calls HdmiCecOpen() to initialise the driver and obtain a handle.
 * -------------------------------------------------------------------- */
int vHAL::open(int *handle)
{
    CCEC_LOG(LOG_INFO, "vHAL::open\r\n");
    int ret = ::HdmiCecOpen(handle);
    CCEC_LOG(LOG_INFO, "vHAL::open ret=%d handle=%d\r\n", ret, (handle ? *handle : -1));
    return ret;
}

/* -----------------------------------------------------------------------
 * close
 * Calls HdmiCecClose() to release driver resources.
 * -------------------------------------------------------------------- */
int vHAL::close(int handle)
{
    CCEC_LOG(LOG_INFO, "vHAL::close handle=%d\r\n", handle);
    int ret = ::HdmiCecClose(handle);
    CCEC_LOG(LOG_INFO, "vHAL::close ret=%d\r\n", ret);
    return ret;
}

/* -----------------------------------------------------------------------
 * setRxCallback
 * Calls HdmiCecSetRxCallback() to register the incoming-message callback.
 * -------------------------------------------------------------------- */
int vHAL::setRxCallback(int handle, HdmiCecRxCallback_t cbfunc, void *data)
{
    CCEC_LOG(LOG_INFO, "vHAL::setRxCallback handle=%d\r\n", handle);
    int ret = ::HdmiCecSetRxCallback(handle, cbfunc, data);
    CCEC_LOG(LOG_INFO, "vHAL::setRxCallback ret=%d\r\n", ret);
    return ret;
}

/* -----------------------------------------------------------------------
 * setTxCallback
 * Calls HdmiCecSetTxCallback() to register the transmit-status callback.
 * -------------------------------------------------------------------- */
int vHAL::setTxCallback(int handle, HdmiCecTxCallback_t cbfunc, void *data)
{
    CCEC_LOG(LOG_INFO, "vHAL::setTxCallback handle=%d\r\n", handle);
    int ret = ::HdmiCecSetTxCallback(handle, cbfunc, data);
    CCEC_LOG(LOG_INFO, "vHAL::setTxCallback ret=%d\r\n", ret);
    return ret;
}

/* -----------------------------------------------------------------------
 * tx
 * Calls HdmiCecTx() for a synchronous transmit; blocks until ACK/NACK.
 * -------------------------------------------------------------------- */
int vHAL::tx(int handle, const unsigned char *buf, int len, int *result)
{
    CCEC_LOG(LOG_INFO, "vHAL::tx handle=%d len=%d\r\n", handle, len);
    int ret = ::HdmiCecTx(handle, buf, len, result);
    CCEC_LOG(LOG_INFO, "vHAL::tx ret=%d sendResult=%d\r\n", ret, (result ? *result : -1));
    return ret;
}

/* -----------------------------------------------------------------------
 * txAsync
 * Calls HdmiCecTxAsync() for a fire-and-forget transmit.
 * -------------------------------------------------------------------- */
int vHAL::txAsync(int handle, const unsigned char *buf, int len)
{
    CCEC_LOG(LOG_INFO, "vHAL::txAsync handle=%d len=%d\r\n", handle, len);
    int ret = ::HdmiCecTxAsync(handle, buf, len);
    CCEC_LOG(LOG_INFO, "vHAL::txAsync ret=%d\r\n", ret);
    return ret;
}

/* -----------------------------------------------------------------------
 * addLogicalAddress
 * Calls HdmiCecAddLogicalAddress() to claim a logical address on the bus.
 * -------------------------------------------------------------------- */
int vHAL::addLogicalAddress(int handle, int logicalAddress)
{
    CCEC_LOG(LOG_INFO, "vHAL::addLogicalAddress handle=%d addr=%d\r\n", handle, logicalAddress);
    int ret = ::HdmiCecAddLogicalAddress(handle, logicalAddress);
    CCEC_LOG(LOG_INFO, "vHAL::addLogicalAddress ret=%d\r\n", ret);
    return ret;
}

/* -----------------------------------------------------------------------
 * removeLogicalAddress
 * Calls HdmiCecRemoveLogicalAddress() to release a previously claimed
 * logical address.
 * -------------------------------------------------------------------- */
int vHAL::removeLogicalAddress(int handle, int logicalAddress)
{
    CCEC_LOG(LOG_INFO, "vHAL::removeLogicalAddress handle=%d addr=%d\r\n", handle, logicalAddress);
    int ret = ::HdmiCecRemoveLogicalAddress(handle, logicalAddress);
    CCEC_LOG(LOG_INFO, "vHAL::removeLogicalAddress ret=%d\r\n", ret);
    return ret;
}

/* -----------------------------------------------------------------------
 * getLogicalAddress
 * Calls HdmiCecGetLogicalAddress() to retrieve the current logical address.
 * -------------------------------------------------------------------- */
int vHAL::getLogicalAddress(int handle, int *logicalAddress)
{
    CCEC_LOG(LOG_INFO, "vHAL::getLogicalAddress handle=%d\r\n", handle);
    int ret = ::HdmiCecGetLogicalAddress(handle, logicalAddress);
    CCEC_LOG(LOG_INFO, "vHAL::getLogicalAddress ret=%d addr=%d\r\n",
             ret, (logicalAddress ? *logicalAddress : -1));
    return ret;
}

/* -----------------------------------------------------------------------
 * getPhysicalAddress
 * Calls HdmiCecGetPhysicalAddress() to retrieve the device physical address.
 * -------------------------------------------------------------------- */
int vHAL::getPhysicalAddress(int handle, unsigned int *physicalAddress)
{
    CCEC_LOG(LOG_INFO, "vHAL::getPhysicalAddress handle=%d\r\n", handle);
    int ret = ::HdmiCecGetPhysicalAddress(handle, physicalAddress);
    CCEC_LOG(LOG_INFO, "vHAL::getPhysicalAddress ret=%d addr=0x%x\r\n",
             ret, (physicalAddress ? *physicalAddress : 0));
    return ret;
}


