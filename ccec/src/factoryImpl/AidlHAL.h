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

#ifndef AIDL_HAL_H
#define AIDL_HAL_H

#include "HDMICecHal.h"

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <utils/String16.h>
#include <com/rdk/hal/hdmicec/IHdmiCec.h>
#include <com/rdk/hal/hdmicec/IHdmiCecController.h>
#include <com/rdk/hal/hdmicec/IHdmiCecEventListener.h>
#include <com/rdk/hal/hdmicec/BnHdmiCecEventListener.h>
#include <com/rdk/hal/hdmicec/SendMessageStatus.h>
#include <com/rdk/hal/hdmicec/State.h>

#include "osal/Mutex.hpp"

using CCEC_OSAL::Mutex;

class AidlHAL : public HDMICecHal {
public:
    AidlHAL();
    ~AidlHAL() override;

    int open(int *handle,
             HdmiCecRxCallback_t rxCb,
             HdmiCecTxCallback_t txCb,
             void *cbData) override;

private:
    android::sp<com::rdk::hal::hdmicec::IHdmiCec> getAidlService();
    void initAidlService();

    android::sp<com::rdk::hal::hdmicec::IHdmiCec> mAidlService;
    android::sp<com::rdk::hal::hdmicec::IHdmiCecController> mAidlController;
    android::sp<com::rdk::hal::hdmicec::IHdmiCecEventListener> mEventListener;
    mutable Mutex mAidlMutex;
};

#endif // AIDL_HAL_H

