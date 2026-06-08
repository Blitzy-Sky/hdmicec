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
#include "ccec/Exception.hpp"

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
    AidlHALEventListener(AidlHAL *AidlHal)
        : mAidlHal(AidlHal) {}

    android::binder::Status onMessageReceived(const std::vector<uint8_t>& message) override {
        if (mAidlHal && message.size() > 0) {
            mAidlHal->dispatchRx(const_cast<unsigned char*>(message.data()), static_cast<int>(message.size()));
        }
        return android::binder::Status::ok();
    }

    android::binder::Status onStateChanged(State oldState, State newState) override {
        // Handle state changes if needed
        return android::binder::Status::ok();
    }

    android::binder::Status onMessageSent(const std::vector<uint8_t>& message, SendMessageStatus status) override {
        if (mAidlHal) {
            int result = (status == SendMessageStatus::ACK_STATE_0) ? 1 :
                         (status == SendMessageStatus::ACK_STATE_1) ? 2 : 3; 
            mAidlHal->dispatchTx(result);
        }
        return android::binder::Status::ok();
    }

private:
    AidlHAL *mAidlHal;
};

AidlHAL::AidlHAL()
    : mAidlService(nullptr),
      mAidlController(nullptr),
    mEventListener(nullptr),
    mRxCb(nullptr),
    mTxCb(nullptr),
    mRxCbData(nullptr),
    mTxCbData(nullptr)
{
}

AidlHAL::~AidlHAL()
{
    AutoLock lock_(mAidlMutex);
    mAidlController = nullptr;
    mAidlService = nullptr;
    mEventListener = nullptr;
}

bool AidlHAL::parseLogicalAddressField(const std::string& line, const char* field, int& value)
{
	const size_t keyPos = line.find(field);
	if (keyPos == std::string::npos) {
		return false;
	}

	const size_t valueStart = line.find_first_not_of(" \t", keyPos + strlen(field));
	if (valueStart == std::string::npos) {
		return false;
	}

	char* endPtr = nullptr;
	const long parsed = std::strtol(line.c_str() + valueStart, &endPtr, 10);
	if (endPtr == (line.c_str() + valueStart)) {
		return false;
	}

	value = static_cast<int>(parsed);
	return true;
}

bool AidlHAL::isPresentInVdeviceTopology(const uint8_t destination)
{
	std::ifstream topology(kVdeviceTopologyDump);
	if (!topology.is_open()) {
		return false;
	}

	std::string line;
	while (std::getline(topology, line)) {
		int logicalAddr = -1;
		if (parseLogicalAddressField(line, "Logical-1:", logicalAddr) && logicalAddr == static_cast<int>(destination)) {
			return true;
		}

		logicalAddr = -1;
		if (parseLogicalAddressField(line, "Logical-2:", logicalAddr) && logicalAddr == static_cast<int>(destination)) {
			return true;
		}
	}

	return false;
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
        if (mAidlService == nullptr) {
            CCEC_LOG(LOG_EXP, "Failed to get AIDL HdmiCec service\r\n");
            throw IOException();
        } 
        CCEC_LOG(LOG_DEBUG, "Successfully obtained AIDL HdmiCec service\r\n");
    } else {
        CCEC_LOG(LOG_EXP, "Failed to get service manager\n");
        throw IOException();
    }
}

int AidlHAL::open(int *handle)
{
    CCEC_LOG(LOG_INFO, "AidlHAL::open invoked\n");

    android::sp<IHdmiCec> service = getAidlService();
    if (service == nullptr) {
        CCEC_LOG(LOG_ERROR, "AidlHAL::open failed: IHdmiCec service unavailable\r\n");
        throw IOException();
    }

    // Create event listener
    mEventListener = new AidlHALEventListener(this);

    // Open AIDL interface
    android::sp<IHdmiCecController> controller;
    android::binder::Status status = service->open(mEventListener, &controller);
    if (!status.isOk() || controller == nullptr) {
        CCEC_LOG(LOG_ERROR, "AidlHAL::open failed: service->open status not OK or controller is null\r\n");
        throw IOException();
    }

    mAidlController = controller;

    *handle = 1;  /* Dummy handle — AIDL uses controller object */

    CCEC_LOG(LOG_INFO, "AidlHAL::open completed successfully\r\n");
    return 0;
}

int AidlHAL::close(int handle)
{
    CCEC_LOG(LOG_INFO, "AidlHAL::close invoked\n");
	(void)handle;

    if (mAidlController != nullptr) {
        android::sp<IHdmiCec> service = getAidlService();
        if (service != nullptr) {
            bool result = false;
            android::binder::Status status = service->close(mAidlController, &result);
            if (!status.isOk()) {
                CCEC_LOG(LOG_ERROR, "AidlHAL::close failed: service->close status not OK\n");
                throw IOException();
            }
            if (!result) {
                CCEC_LOG(LOG_ERROR, "AidlHAL::close failed: service->close returned false\n");
                throw IOException();
            }
        }
        mAidlController = nullptr;
    }

    mEventListener = nullptr;

    CCEC_LOG(LOG_DEBUG, "Successfully closed AIDL HdmiCec interface\r\n");
    return 0;
}

int AidlHAL::addLogicalAddress(int handle, int logicalAddresses)
{
    if (mAidlController == nullptr) {
        throw IOException();
    }

    std::vector<int32_t> addresses;
    addresses.push_back(logicalAddresses);
    bool result = false;
    android::binder::Status status = mAidlController->addLogicalAddresses(addresses, &result);

    if (!status.isOk()) {
    	CCEC_LOG(LOG_EXP, "Failed to add logical address via AIDL: %s\r\n", status.toString8().c_str());
		throw IOException();
    }

    if (!result) {
        throw AddressNotAvailableException();
    }

    CCEC_LOG(LOG_DEBUG, "Successfully added logical address via AIDL\n");
    return 0;
}

int AidlHAL::removeLogicalAddress(int handle, int logicalAddresses)
{
    if (mAidlController == nullptr) {
        throw IOException();
    }

    std::vector<int32_t> addresses;
    addresses.push_back(logicalAddresses);
    bool result = false;
    android::binder::Status status = mAidlController->removeLogicalAddresses(addresses, &result);
	if (!status.isOk() || !result) {
		CCEC_LOG(LOG_EXP, "Failed to remove logical address via AIDL: %s\n", status.toString8().c_str());
		throw IOException();
	}

    CCEC_LOG(LOG_DEBUG, "Successfully removed logical address via AIDL\n");
    return 0;
}

int AidlHAL::getLogicalAddress(int handle, int *logicalAddress)
{
    if (logicalAddress == nullptr) {
        CCEC_LOG(LOG_ERROR, "AidlHAL::getLogicalAddress invalid output pointer\r\n");
        throw IOException();
    }

    *logicalAddress = 0;

	if (mAidlService == nullptr) {
		android::sp<IHdmiCec> service = getAidlService();
		if (service == nullptr) {
            throw IOException();
		}
		mAidlService = service;
	}

    std::vector<int32_t> addresses;
    android::binder::Status status = mAidlService->getLogicalAddresses(&addresses);
    if (status.isOk() && addresses.size() > 0) {
        *logicalAddress = addresses[0];
    }
    
    CCEC_LOG( LOG_DEBUG, "AidlHAL::getLogicalAddress completed\r\n");

    return 0;
}

int AidlHAL::getPhysicalAddress(int handle, unsigned int *physicalAddress)
{
    if (physicalAddress != nullptr) {
        *physicalAddress = 0;
    }

    CCEC_LOG( LOG_DEBUG, "AidlHAL::getPhysicalAddress completed\r\n");

    return 0;
}

void AidlHAL::dispatchRx(unsigned char *buf, int len)
{
    if (mRxCb == nullptr) {
        CCEC_LOG(LOG_DEBUG, "AidlHAL::dispatchRx callback not registered\r\n");
        return;
    }

    mRxCb(0, mRxCbData, buf, len);
}

void AidlHAL::dispatchTx(int result)
{
    if (mTxCb == nullptr) {
        CCEC_LOG(LOG_DEBUG, "AidlHAL::dispatchTx callback not registered\r\n");
        return;
    }

    mTxCb(0, mTxCbData, result);
}

int AidlHAL::setRxCallback(int handle, HdmiCecRxCallback_t cbfunc, void *data)
{
    (void)handle;

    mRxCb = cbfunc;
    mRxCbData = data;

    CCEC_LOG(LOG_DEBUG, "AidlHAL::setRxCallback invoked\r\n");
    return 0;
}

int AidlHAL::setTxCallback(int handle, HdmiCecTxCallback_t cbfunc, void *data)
{
    (void)handle;

    mTxCb = cbfunc;
    mTxCbData = data;

    CCEC_LOG(LOG_DEBUG, "AidlHAL::setTxCallback invoked\r\n");
    return 0;
}

int AidlHAL::tx(int handle, const unsigned char *buf, int len, int *result)
{
    if (mAidlController == nullptr) {
		throw IOException();
	}

    if (result == nullptr) {
        CCEC_LOG(LOG_ERROR, "AidlHAL::tx invalid result pointer\n");
        throw IOException();
    }

    if (buf == nullptr || len <= 0) {
        CCEC_LOG(LOG_ERROR, "AidlHAL::tx invalid buffer or length\n");
        throw IOException();
    }

    std::vector<uint8_t> message(buf, buf + len);
    SendMessageStatus sendStatus;
    android::binder::Status status = mAidlController->sendMessage(message, &sendStatus);
    if (!status.isOk()) {
        CCEC_LOG(LOG_ERROR, "AIDL sendMessage failed: %s\r\n", status.toString8().c_str());
		throw IOException();
    }

    // Map AIDL SendMessageStatus to HAL error codes
    *result = 0;  // HDMI_CEC_IO_SUCCESS
    if (sendStatus == SendMessageStatus::ACK_STATE_0) {
        *result = 1; // HDMI_CEC_IO_SENT_AND_ACKD
    } else if (sendStatus == SendMessageStatus::ACK_STATE_1) {
        *result = 2; // HDMI_CEC_IO_SENT_BUT_NOT_ACKD
    } else if (sendStatus == SendMessageStatus::BUSY){
        *result = 3; // HDMI_CEC_IO_SENT_FAILED
        throw IOException();
    }

    CCEC_LOG( LOG_DEBUG, "AIDL sendMessage DONE, result %x\r\n", *result);

    return 0;
}

int AidlHAL::txAsync(int handle, const unsigned char *buf, int len)
{
    if (mAidlController == nullptr) {
        throw IOException();
    }
    if (buf == nullptr || len <= 0) {
        CCEC_LOG(LOG_ERROR, "AidlHAL::txAsync invalid buffer or length\n");
        throw IOException();
    }

    std::vector<uint8_t> message(buf, buf + len);
    SendMessageStatus sendStatus;
    android::binder::Status status = mAidlController->sendMessage(message, &sendStatus);
    if (!status.isOk()) {
        CCEC_LOG(LOG_ERROR, "AIDL sendMessage failed: %s\r\n", status.toString8().c_str());
		throw IOException();
    }

    if (sendStatus == SendMessageStatus::BUSY) {
        CCEC_LOG(LOG_ERROR, "AIDL sendMessage busy in txAsync\r\n");
        throw IOException();
    }

    CCEC_LOG( LOG_DEBUG, "AIDL sendMessage completed, status: %d\r\n", static_cast<int>(sendStatus));

    CCEC_LOG( LOG_DEBUG, "Send Async Completed\n");

    return 0;
}

bool AidlHAL::skipFrameOfUnsupportedLength(size_t length) {
	if (length < kAidlMinCecFrameSize || length > kAidlMaxCecFrameSize) {
		/* AIDL sendMessage accepts only 2..16 byte CEC frames. */
		CCEC_LOG(LOG_WARN,
			"DriverImpl::writeAsync skipping unsupported CEC frame length=%zu on AIDL backend (valid range: 2..16).\r\n",
			length);
		return;
	}

    return false;
}

bool AidlHAL::emulateAckForPollFrames(const unsigned char *buf, int len)
{
    if (length <= 1) {
        /*
            * Poll frame (header only): emulate ACK based on vdevice topology file.
            * This keeps HdmiCecSource ping-based discovery working on AIDL backend.
            */
        const uint8_t destination = (buf != NULL) ? (buf[0] & 0x0F) : 0xFF;
        const bool addressPresent = (destination <= 0x0E) ? isPresentInVdeviceTopology(destination) : false;

        if (addressPresent) {
            CCEC_LOG(LOG_DEBUG,
                "DriverImpl::write poll-frame destination=0x%X present in topology. Emulating ack.\r\n",
                destination);
            return true;
        }

        CCEC_LOG(LOG_DEBUG,
            "DriverImpl::write poll-frame destination=0x%X not present in topology. Returning no-ack.\r\n",
            destination);
        throw CECNoAckException();
    }

    if (length > kAidlMaxCecFrameSize) {
        CCEC_LOG(LOG_EXP,
            "DriverImpl::write blocking unsupported CEC frame length=%zu on AIDL backend (valid range: 2..16).\r\n",
            length);
        throw IOException();
    }

    return false;
}
