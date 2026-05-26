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
 * Each function wraps the corresponding HdmiCec* C API from hdmi_cec_driver.h
 * and casts its return value to HdmiCecStatus_t. The enum values are
 * value-aligned (both ordered identically), so the cast is lossless.
 */

#include "ccec/drivers/hdmi_cec_driver.h"   /* HdmiCec* C APIs, HDMI_CEC_STATUS */
#include "vHAL.h"
#include "ccec/Util.hpp"

/* -------------------------------------------------------------------------
 * Helper: cast HDMI_CEC_STATUS (or plain int from older driver headers)
 * to the common HdmiCecStatus_t.  Both enums share the same ordinal values.
 * ---------------------------------------------------------------------- */
static inline HdmiCecStatus_t toHalStatus(int status)
{
    return static_cast<HdmiCecStatus_t>(status);
}

/* -----------------------------------------------------------------------
 * open
 * Calls HdmiCecOpen() to initialise the driver and obtain a handle.
 * -------------------------------------------------------------------- */
HdmiCecStatus_t vHAL::open(int *handle)
{
    CCEC_LOG(LOG_INFO, "vHAL::open\r\n");
    HdmiCecStatus_t ret = toHalStatus(::HdmiCecOpen(handle));
    CCEC_LOG(LOG_DEBUG, "vHAL::open ret=%d handle=%d\r\n", ret, (handle ? *handle : -1));
    return ret;
}

/* -----------------------------------------------------------------------
 * close
 * Calls HdmiCecClose() to release driver resources.
 * -------------------------------------------------------------------- */
HdmiCecStatus_t vHAL::close(int handle)
{
    CCEC_LOG(LOG_INFO, "vHAL::close handle=%d\r\n", handle);
    HdmiCecStatus_t ret = toHalStatus(::HdmiCecClose(handle));
    CCEC_LOG(LOG_DEBUG, "vHAL::close ret=%d\r\n", ret);
    return ret;
}

/* -----------------------------------------------------------------------
 * setRxCallback
 * Calls HdmiCecSetRxCallback() to register the incoming-message callback.
 * -------------------------------------------------------------------- */
HdmiCecStatus_t vHAL::setRxCallback(int handle, HdmiCecRxCallback_t cbfunc, void *data)
{
    CCEC_LOG(LOG_INFO, "vHAL::setRxCallback handle=%d\r\n", handle);
    HdmiCecStatus_t ret = toHalStatus(::HdmiCecSetRxCallback(handle, cbfunc, data));
    CCEC_LOG(LOG_DEBUG, "vHAL::setRxCallback ret=%d\r\n", ret);
    return ret;
}

/* -----------------------------------------------------------------------
 * setTxCallback
 * Calls HdmiCecSetTxCallback() to register the transmit-status callback.
 * -------------------------------------------------------------------- */
HdmiCecStatus_t vHAL::setTxCallback(int handle, HdmiCecTxCallback_t cbfunc, void *data)
{
    CCEC_LOG(LOG_INFO, "vHAL::setTxCallback handle=%d\r\n", handle);
    HdmiCecStatus_t ret = toHalStatus(::HdmiCecSetTxCallback(handle, cbfunc, data));
    CCEC_LOG(LOG_DEBUG, "vHAL::setTxCallback ret=%d\r\n", ret);
    return ret;
}

/* -----------------------------------------------------------------------
 * tx
 * Calls HdmiCecTx() for a synchronous transmit; blocks until ACK/NACK.
 * *result receives one of: HDMICEC_HAL_SENT_AND_ACKD,
 *   HDMICEC_HAL_SENT_BUT_NOT_ACKD, HDMICEC_HAL_SENT_FAILED.
 * -------------------------------------------------------------------- */
HdmiCecStatus_t vHAL::tx(int handle, const unsigned char *buf, int len, int *result)
{
    CCEC_LOG(LOG_INFO, "vHAL::tx handle=%d len=%d\r\n", handle, len);
    HdmiCecStatus_t ret = toHalStatus(::HdmiCecTx(handle, buf, len, result));
    CCEC_LOG(LOG_DEBUG, "vHAL::tx ret=%d sendResult=%d\r\n", ret, (result ? *result : -1));
    return ret;
}

/* -----------------------------------------------------------------------
 * txAsync
 * Calls HdmiCecTxAsync() for a fire-and-forget transmit.
 * The send result is reported later via the registered TxCallback.
 * -------------------------------------------------------------------- */
HdmiCecStatus_t vHAL::txAsync(int handle, const unsigned char *buf, int len)
{
    CCEC_LOG(LOG_INFO, "vHAL::txAsync handle=%d len=%d\r\n", handle, len);
    HdmiCecStatus_t ret = toHalStatus(::HdmiCecTxAsync(handle, buf, len));
    CCEC_LOG(LOG_DEBUG, "vHAL::txAsync ret=%d\r\n", ret);
    return ret;
}

/* -----------------------------------------------------------------------
 * addLogicalAddress
 * Calls HdmiCecAddLogicalAddress() to claim a logical address on the bus.
 * Only applicable for sink devices; source devices return
 * HDMICEC_HAL_OPERATION_NOT_SUPPORTED.
 * -------------------------------------------------------------------- */
HdmiCecStatus_t vHAL::addLogicalAddress(int handle, int logicalAddress)
{
    CCEC_LOG(LOG_INFO, "vHAL::addLogicalAddress handle=%d addr=%d\r\n", handle, logicalAddress);
    HdmiCecStatus_t ret = toHalStatus(::HdmiCecAddLogicalAddress(handle, logicalAddress));
    CCEC_LOG(LOG_DEBUG, "vHAL::addLogicalAddress ret=%d\r\n", ret);
    return ret;
}

/* -----------------------------------------------------------------------
 * removeLogicalAddress
 * Calls HdmiCecRemoveLogicalAddress() to release a previously claimed
 * logical address. Resets the address to the default 0xF.
 * -------------------------------------------------------------------- */
HdmiCecStatus_t vHAL::removeLogicalAddress(int handle, int logicalAddress)
{
    CCEC_LOG(LOG_INFO, "vHAL::removeLogicalAddress handle=%d addr=%d\r\n", handle, logicalAddress);
    HdmiCecStatus_t ret = toHalStatus(::HdmiCecRemoveLogicalAddress(handle, logicalAddress));
    CCEC_LOG(LOG_DEBUG, "vHAL::removeLogicalAddress ret=%d\r\n", ret);
    return ret;
}

/* -----------------------------------------------------------------------
 * getLogicalAddress
 * Calls HdmiCecGetLogicalAddress() to retrieve the current logical address.
 * For source devices, the SOC assigns this during HdmiCecOpen().
 * For sink devices, this reflects the last successfully added address.
 * -------------------------------------------------------------------- */
HdmiCecStatus_t vHAL::getLogicalAddress(int handle, int *logicalAddress)
{
    CCEC_LOG(LOG_INFO, "vHAL::getLogicalAddress handle=%d\r\n", handle);
    HdmiCecStatus_t ret = toHalStatus(::HdmiCecGetLogicalAddress(handle, logicalAddress));
    CCEC_LOG(LOG_DEBUG, "vHAL::getLogicalAddress ret=%d addr=%d\r\n",
             ret, (logicalAddress ? *logicalAddress : -1));
    return ret;
}

/* -----------------------------------------------------------------------
 * getPhysicalAddress
 * Calls HdmiCecGetPhysicalAddress() to retrieve the device physical address.
 * The sink at root reports 0.0.0.0; valid range is less than F.F.F.F.
 * -------------------------------------------------------------------- */
HdmiCecStatus_t vHAL::getPhysicalAddress(int handle, unsigned int *physicalAddress)
{
    CCEC_LOG(LOG_INFO, "vHAL::getPhysicalAddress handle=%d\r\n", handle);
    HdmiCecStatus_t ret = toHalStatus(::HdmiCecGetPhysicalAddress(handle, physicalAddress));
    CCEC_LOG(LOG_DEBUG, "vHAL::getPhysicalAddress ret=%d addr=0x%x\r\n",
             ret, (physicalAddress ? *physicalAddress : 0));
    return ret;
}


