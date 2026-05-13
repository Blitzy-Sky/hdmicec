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

#include "vHAL.h"

#include "ccec/drivers/hdmi_cec_driver.h"
#include "ccec/Util.hpp"

int vHAL::open(int *handle,
               HdmiCecRxCallback_t rxCb,
               HdmiCecTxCallback_t txCb,
               void *cbData)
{
    CCEC_LOG(LOG_INFO, "vHAL::open invoked\r\n");

    int err = ::HdmiCecOpen(handle);
    if (err != HDMI_CEC_IO_SUCCESS) {
        CCEC_LOG(LOG_ERR, "vHAL::open failed: HdmiCecOpen returned %d\r\n", err);
        return err;
    }

    ::HdmiCecSetRxCallback(*handle, rxCb, cbData);
    ::HdmiCecSetTxCallback(*handle, txCb, cbData);

    CCEC_LOG(LOG_INFO, "vHAL::open completed successfully, handle=%d\r\n", *handle);
    return HDMI_CEC_IO_SUCCESS;
}

