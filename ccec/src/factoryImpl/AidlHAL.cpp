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

#include <cstddef>
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
    }

    android::binder::Status onMessageSent(const std::vector<uint8_t>& /*message*/, SendMessageStatus status) override {
        if (mTxCb) {
            int result = (status == SendMessageStatus::ACK_STATE_0) ? HDMI_CEC_IO_SENT_AND_ACKD :
                         (status == SendMessageStatus::ACK_STATE_1) ? HDMI_CEC_IO_SENT_BUT_NOT_ACKD :
                                                                      HDMI_CEC_IO_SENT_FAILED;
            mTxCb(0, mCbData, result);
        }
        return android::binder::Status::ok();
    }

private:
    HdmiCecRxCallback_t mRxCb;
    HdmiCecTxCallback_t mTxCb;
    void *mCbData;
};

AidlHAL::AidlHAL()
    : mAidlService(nullptr),
      mAidlController(nullptr),
      mEventListener(nullptr)
{
}

AidlHAL::~AidlHAL()
{
    AutoLock lock_(mAidlMutex);
    mAidlController = nullptr;
    mAidlService = nullptr;
    mEventListener = nullptr;
}

android::sp<IHdmiCec> AidlHAL::getAidlService()
{
    AutoLock lock_(mAidlMutex);
    if (mAidlService == nullptr) {
        initAidlService();
    }
    return mAidlService;
}

void AidlHAL::initAidlService()
{
    android::ProcessState::self()->startThreadPool();

    sp<android::IServiceManager> sm = defaultServiceManager();
    if (sm != nullptr) {
        mAidlService = interface_cast<IHdmiCec>(
            sm->getService(String16(IHdmiCec::serviceName().c_str())));
    }
}

int AidlHAL::open(int *handle,
                   HdmiCecRxCallback_t rxCb,
                   HdmiCecTxCallback_t txCb,
                   void *cbData)
{
    CCEC_LOG(LOG_INFO, "AidlHAL::open invoked\r\n");

    android::sp<IHdmiCec> service = getAidlService();
    if (service == nullptr) {
        CCEC_LOG(LOG_ERROR, "AidlHAL::open failed: IHdmiCec service unavailable\r\n");
        return HDMI_CEC_IO_GENERAL_ERROR;
    }

    mEventListener = new AidlHALEventListener(rxCb, txCb, cbData);

    android::sp<IHdmiCecController> controller;
    android::binder::Status s = service->open(mEventListener, &controller);
    if (!s.isOk() || controller == nullptr) {
        CCEC_LOG(LOG_ERROR, "AidlHAL::open failed: service->open status not OK or controller is null\r\n");
        return HDMI_CEC_IO_GENERAL_ERROR;
    }

    mAidlController = controller;

    *handle = 1;  /* Dummy handle — AIDL uses controller object, not int handle */
    CCEC_LOG(LOG_INFO, "AidlHAL::open completed successfully\r\n");
    return HDMI_CEC_IO_SUCCESS;
}


