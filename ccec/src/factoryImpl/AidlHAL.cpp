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

#include "AidlHAL.h"

#include <vector>

#include "ccec/Util.hpp"

//#include "ccec/drivers/hdmi_cec_driver.h"  --not required---

using CCEC_OSAL::AutoLock;
using android::sp;
using android::String16;
using android::defaultServiceManager;
using android::interface_cast;
using namespace com::rdk::hal::hdmicec;

/**
 * @brief AIDL Event Listener — bridges binder callbacks to the
 *        C-style callback function pointers stored in AidlHAL.
 */
class AidlHALEventListener : public BnHdmiCecEventListener {
public:
    AidlHALEventListener(HdmiCecRxCallback_t rxCb,
                          HdmiCecTxCallback_t txCb,
                          void *cbData)
        : mRxCb(rxCb), mTxCb(txCb), mCbData(cbData) {}

    android::binder::Status onMessageReceived(const std::vector<uint8_t>& message) override {
        if (mRxCb && message.size() > 0) {
            mRxCb(0, mCbData, const_cast<unsigned char*>(message.data()), message.size());
        }
        return android::binder::Status::ok();
    }

    android::binder::Status onStateChanged(State /*oldState*/, State /*newState*/) override {
        return android::binder::Status::ok();

