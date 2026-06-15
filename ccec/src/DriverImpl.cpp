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


/**
* @defgroup hdmicec
* @{
* @defgroup ccec
* @{
**/


#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>

#include "osal/EventQueue.hpp"
#include "osal/Exception.hpp"
#include "ccec/Util.hpp"
#include "ccec/Exception.hpp"
#include "DriverImpl.hpp"
#include "ccec/OpCode.hpp"

// AIDL includes
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <utils/String16.h>
#include <com/rdk/hal/hdmicec/IHdmiCec.h>
#include <com/rdk/hal/hdmicec/IHdmiCecController.h>
#include <com/rdk/hal/hdmicec/IHdmiCecEventListener.h>
#include <com/rdk/hal/hdmicec/BnHdmiCecEventListener.h>
#include <com/rdk/hal/hdmicec/SendMessageStatus.h>
#include <com/rdk/hal/hdmicec/State.h>

using CCEC_OSAL::AutoLock;
using android::sp;
using android::String16;
using android::defaultServiceManager;
using android::interface_cast;
using namespace com::rdk::hal::hdmicec;

CCEC_BEGIN_NAMESPACE

// Forward declaration
class DriverImpl;

// AIDL Event Listener class to handle callbacks
class DriverImplEventListener : public BnHdmiCecEventListener {
public:
    DriverImplEventListener(DriverImpl* driver) : mDriver(driver) {}
    
    android::binder::Status onMessageReceived(const std::vector<uint8_t>& message) override {
        if (mDriver && message.size() > 0) {
            DriverImpl::DriverReceiveCallback(0, mDriver, const_cast<unsigned char*>(message.data()), message.size());
        }
        return android::binder::Status::ok();
    }
    
    android::binder::Status onStateChanged(State oldState, State newState) override {
        // Handle state changes if needed
        return android::binder::Status::ok();
    }
    
    android::binder::Status onMessageSent(const std::vector<uint8_t>& message, SendMessageStatus status) override {
        if (mDriver) {
            int result = (status == SendMessageStatus::ACK_STATE_0) ? 1 : 
                        (status == SendMessageStatus::ACK_STATE_1) ? 2 : 3;
            DriverImpl::DriverTransmitCallback(0, mDriver, result);
        }
        return android::binder::Status::ok();
    }
    
private:
    DriverImpl* mDriver;
};

namespace {
const size_t kAidlMinCecFrameSize = 2;
const size_t kAidlMaxCecFrameSize = 16;

// Return the standard logical-address candidates for a given device type.
// Used only as a fallback when AIDL reports no allocated addresses yet.
std::vector<int32_t> preferredLogicalAddressesForDeviceType(int devType)
{
	// Common CEC type ids used in middleware:
	// 0: TV, 1: RecordingDevice, 3: Tuner, 4: PlaybackDevice, 5: AudioSystem.
	switch (devType) {
		case 0: return std::vector<int32_t>{0}; // TV
		case 1: return std::vector<int32_t>{1, 2, 9}; // Recorder
		case 3: return std::vector<int32_t>{3, 6, 7, 10}; // Tuner
		case 5: return std::vector<int32_t>{5}; // AudioSystem
		case 4:
		default:
			return std::vector<int32_t>{4, 8, 11}; // PlaybackDevice fallback
	}
}
}

size_t write(const unsigned char *buf, size_t len);

void DriverImpl::DriverReceiveCallback(int handle, void *callbackData, unsigned char *buf, int len)
{
	CECFrame *frame = new CECFrame();
	frame->append((unsigned char *)buf, (size_t)len);

	CCEC_LOG( LOG_DEBUG, ">>>>>>> >>>>> >>>> >> >> >\r\n");

        dump_buffer((unsigned char*)buf,len);

	CCEC_LOG(LOG_DEBUG, "==========================\r\n");

	// Track initiator LA from inbound frames so 1-byte poll can be emulated
	// locally on AIDL backends that reject 1-byte sendMessage payloads.
	if (buf != nullptr && len >= 1) {
		const uint8_t srcLA = static_cast<uint8_t>((buf[0] >> 4) & 0x0F);
		if (srcLA <= 0x0E) {
			DriverImpl& self = static_cast<DriverImpl&>(Driver::getInstance());
			AutoLock lock_(self.mutex);
			self.mSeenLogicalAddresses.insert(srcLA);
			CCEC_LOG(LOG_DEBUG, "DriverReceiveCallback: recorded srcLA 0x%X\r\n", srcLA);
		}
	}

	try {
		static_cast<DriverImpl &>(Driver::getInstance()).getIncomingQueue(handle).offer(frame);
	}
	catch(...) {
		CCEC_LOG( LOG_EXP, "Exception during frame offer...discarding\r\n");
		delete frame;
	}
	CCEC_LOG( LOG_DEBUG, "frame offered\r\n");
}

void DriverImpl::DriverTransmitCallback(int handle, void *callbackData, int result)
{
	if (result != 0) {  // HDMI_CEC_IO_SUCCESS = 0
		CCEC_LOG( LOG_DEBUG, "======== AIDL TxCallback received. Result: %d\r\n", result);
	}
}

android::sp<com::rdk::hal::hdmicec::IHdmiCec> DriverImpl::getAidlService()
{
    AutoLock lock_(mAidlMutex);
    if (mAidlService == nullptr) {
        initAidlService();
    }
    return mAidlService;
}

void DriverImpl::initAidlService()
{
    // Initialize binder process state if not already done
    android::ProcessState::self()->startThreadPool();
    
    // Get the AIDL service
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
        CCEC_LOG(LOG_EXP, "Failed to get service manager\r\n");
        throw IOException();
    }
}

DriverImpl::DriverImpl() : status(CLOSED), nativeHandle(0), mAidlService(nullptr), mAidlController(nullptr)
{
	CCEC_LOG( LOG_DEBUG, "Creating DriverImpl done\r\n");
}

DriverImpl::~DriverImpl()
{
    {AutoLock lock_(mutex);
		if (status != CLOSED) {
			try{
                this->close();
	        }
	        catch(Exception &e)
	        {
                CCEC_LOG( LOG_EXP, "DriverImpl: Caught Exception while calling ~DriverImpl::close()\r\n");

            }
		}
    }
    {AutoLock lock_(mAidlMutex);
        mAidlController = nullptr;
        mAidlService = nullptr;
        mEventListener = nullptr;
    }
}

void DriverImpl::open(void) noexcept(false)
{
    {AutoLock lock_(mutex);
		if (status != CLOSED) {
			#if 0
				throw InvalidStateException();
			#else
				return;
			#endif
		}

		// Get AIDL service
		android::sp<IHdmiCec> service = getAidlService();
		if (service == nullptr) {
			throw IOException();
		}
		
		// Create event listener
		mEventListener = new DriverImplEventListener(this);
		
		// Open AIDL interface
		android::sp<IHdmiCecController> controller;
		android::binder::Status status = service->open(mEventListener, &controller);
		if (!status.isOk() || controller == nullptr) {
			CCEC_LOG(LOG_EXP, "Failed to open AIDL HdmiCec interface: %s\r\n", status.toString8().c_str());
			throw IOException();
		}
		
		mAidlController = controller;
		nativeHandle = 1;  // Dummy handle for compatibility
		this->status = OPENED;
		
		CCEC_LOG(LOG_DEBUG, "Successfully opened AIDL HdmiCec interface\r\n");
    }
}

void  DriverImpl::close(void) noexcept(false)
{

    {AutoLock lock_(mutex);
		if (status != OPENED) {
			#if 0
				throw InvalidStateException();
			#else
				return;
			#endif
		}
		status = CLOSING;

		/* Use NULL as sentinel */
		rQueue.offer(0);

		// Close AIDL interface
		if (mAidlController != nullptr) {
			android::sp<IHdmiCec> service = getAidlService();
			if (service != nullptr) {
				bool result = false;
				android::binder::Status closeStatus = service->close(mAidlController, &result);
				if (!closeStatus.isOk()) {
					CCEC_LOG(LOG_EXP, "Failed to close AIDL HdmiCec interface: %s\r\n", closeStatus.toString8().c_str());
				}
			}
			mAidlController = nullptr;
		}
		
		mEventListener = nullptr;
		nativeHandle = 0;
		this->status = CLOSED;
		
		CCEC_LOG(LOG_DEBUG, "Successfully closed AIDL HdmiCec interface\r\n");
    }
}

void  DriverImpl::read(CECFrame &frame)  noexcept(false)
{
    {AutoLock lock_(mutex);
		if (status != OPENED) {
			throw InvalidStateException();
		}
    }

    CCEC_LOG( LOG_DEBUG, "DriverImpl::Read()\r\n");

    bool backToPoll = false;
	do {
		backToPoll = false;

		CECFrame * inFrame = rQueue.poll();

		if (inFrame != 0) {
			frame = *inFrame;
			delete inFrame;
		}
		else {AutoLock lock_(mutex);

			if (status != OPENED) {
				/* Flush and return */
				while (rQueue.size() > 0) {
					inFrame = rQueue.poll();
					frame = *inFrame;
					delete inFrame;
				}
				throw InvalidStateException();
			}
			else {
				backToPoll = true;
			}
		}
    } while(backToPoll);
}

/*
 * Only 1 write is allowed at a time. Queue the write request and wait for response.
 */
void  DriverImpl::writeAsync(const CECFrame &frame)  noexcept(false)
{

	const uint8_t *buf = NULL;
	size_t length = 0;

	frame.getBuffer(&buf, &length);
	printFrameDetails(frame);

	if (length < kAidlMinCecFrameSize || length > kAidlMaxCecFrameSize) {
		/* AIDL sendMessage accepts only 2..16 byte CEC frames. */
		CCEC_LOG(LOG_WARN,
			"DriverImpl::writeAsync skipping unsupported CEC frame length=%zu on AIDL backend (valid range: 2..16).\\r\\n",
			length);
		return;
	}

    {AutoLock lock_(mutex);
    	if (status != OPENED) {
    		throw InvalidStateException();
    	}
		CCEC_LOG( LOG_DEBUG, "DriverImpl::writeAsync to call AIDL sendMessage\r\n");

		if (mAidlController == nullptr) {
			throw IOException();
		}
		
		// Convert buffer to vector
		std::vector<uint8_t> message(buf, buf + length);
		SendMessageStatus sendStatus;
		android::binder::Status status = mAidlController->sendMessage(message, &sendStatus);
		
		CCEC_LOG( LOG_DEBUG, ">>>>>>> >>>>> >>>> >> >> >\r\n");

		dump_buffer((unsigned char*)buf,length);

		CCEC_LOG(LOG_DEBUG, "==========================\r\n");

		if (!status.isOk()) {
			CCEC_LOG(LOG_EXP, "DriverImpl:: AIDL sendMessage failed: %s\r\n", status.toString8().c_str());
			throw IOException();
		}
		
		CCEC_LOG( LOG_DEBUG, "DriverImpl:: AIDL sendMessage completed, status: %d\r\n", static_cast<int>(sendStatus));
    }

    CCEC_LOG( LOG_DEBUG, "Send Async Completed\r\n");
}


/*
 * Only 1 write is allowed at a time. Queue the write request and wait for response.
 */
void  DriverImpl::write(const CECFrame &frame)  noexcept(false)
{

	const uint8_t *buf = NULL;
	size_t length = 0;

	frame.getBuffer(&buf, &length);
	printFrameDetails(frame);

	if (length <= 1) {
		/*
		 * Poll frame (header only): do NOT forward to AIDL sendMessage.
		 * Some AIDL backends reject 1-byte messages as invalid size.
		 * Instead emulate ACK/NACK from logical addresses observed on
		 * inbound frames (tracked in DriverReceiveCallback).
		 *
		 * If destination is not yet in seen-LA cache, probe with a harmless
		 * 2-byte directed frame (GiveDevicePowerStatus) to infer presence
		 * from AIDL ACK/NACK and cache the destination on ACK.
		 */
		const uint8_t destination = (buf != NULL) ? (buf[0] & 0x0F) : 0xFF;
		bool addressPresent = false;
		if (destination <= 0x0E) {
			AutoLock lock_(mutex);
			addressPresent = (mSeenLogicalAddresses.count(destination) > 0);
		}

		if (addressPresent) {
			CCEC_LOG(LOG_DEBUG,
				"DriverImpl::write poll-frame destination=0x%X present in seen-LA set. Emulating ack.\r\n",
				destination);
			return;
		}

		if (destination <= 0x0E) {
			AutoLock lock_(mutex);
			if (this->status != OPENED || mAidlController == nullptr) {
				throw InvalidStateException();
			}

			std::vector<uint8_t> probe;
			probe.reserve(2);
			probe.push_back(buf ? buf[0] : 0);
			probe.push_back(0x8F); // GiveDevicePowerStatus

			SendMessageStatus probeStatus = SendMessageStatus::BUSY;
			android::binder::Status aidlStatus = mAidlController->sendMessage(probe, &probeStatus);
			if (aidlStatus.isOk() && probeStatus == SendMessageStatus::ACK_STATE_0) {
				mSeenLogicalAddresses.insert(destination);
				CCEC_LOG(LOG_DEBUG,
					"DriverImpl::write poll-frame destination=0x%X ACKed by 2-byte probe. Emulating ack.\r\n",
					destination);
				return;
			}

			CCEC_LOG(LOG_DEBUG,
				"DriverImpl::write poll-frame destination=0x%X probe NACK/failed (aidlOk=%d status=%d).\r\n",
				destination,
				aidlStatus.isOk() ? 1 : 0,
				static_cast<int>(probeStatus));
		}

		CCEC_LOG(LOG_DEBUG,
			"DriverImpl::write poll-frame destination=0x%X not in seen-LA set. Returning no-ack.\r\n",
			destination);
		throw CECNoAckException();
	}

	if (length > kAidlMaxCecFrameSize) {
		CCEC_LOG(LOG_EXP,
			"DriverImpl::write blocking unsupported CEC frame length=%zu on AIDL backend (valid range: 2..16).\\r\\n",
			length);
		throw IOException();
	}
    {AutoLock lock_(mutex);
    	if (status != OPENED) {
    		throw InvalidStateException();
    	}
		CCEC_LOG( LOG_DEBUG, "DriverImpl::write to call AIDL sendMessage\r\n");

		if (mAidlController == nullptr) {
			throw IOException();
		}
		
		// Convert buffer to vector
		std::vector<uint8_t> message(buf, buf + length);
		SendMessageStatus sendStatus;
		android::binder::Status status = mAidlController->sendMessage(message, &sendStatus);
		
		CCEC_LOG( LOG_DEBUG, ">>>>>>> >>>>> >>>> >> >> >\r\n");

		dump_buffer((unsigned char*)buf,length);

		CCEC_LOG(LOG_DEBUG, "==========================\r\n");

		if (!status.isOk()) {
			CCEC_LOG(LOG_EXP, "DriverImpl:: AIDL sendMessage failed: %s\r\n", status.toString8().c_str());
			throw IOException();
		}
		
		// Map AIDL SendMessageStatus to HAL error codes
		int sendResult = 0;  // HDMI_CEC_IO_SUCCESS
		if (sendStatus == SendMessageStatus::ACK_STATE_0) {
			sendResult = 1;  // HDMI_CEC_IO_SENT_AND_ACKD
		} else if (sendStatus == SendMessageStatus::ACK_STATE_1) {
			sendResult = 2;  // HDMI_CEC_IO_SENT_BUT_NOT_ACKD
		} else if (sendStatus == SendMessageStatus::BUSY) {
			sendResult = 3;  // HDMI_CEC_IO_SENT_FAILED
			throw IOException();
		}
		
		CCEC_LOG( LOG_DEBUG, "DriverImpl:: AIDL sendMessage DONE, result %x\r\n", sendResult);

		if (((frame.at(0) & 0x0F) != 0x0F) && sendResult == 2) {  // HDMI_CEC_IO_SENT_BUT_NOT_ACKD
			throw CECNoAckException();
		}
		   /* CEC CTS 9-3-3 -Ensure that the DUT will accept a negatively for broadcat report physical address msg and retry atleast once */
		else if (((frame.at(0) & 0x0F) == 0x0F) && (length > 1) && ((frame.at(1) & 0xFF) == REPORT_PHYSICAL_ADDRESS ) && (sendResult == 2))
		{
                   throw CECNoAckException();
		}
    }

    CCEC_LOG( LOG_DEBUG, "Send Completed\r\n");
}

int DriverImpl::getLogicalAddress(int devType)
{
    {AutoLock lock_(mutex);
	int logicalAddress = 0;
	CCEC_LOG( LOG_DEBUG, "DriverImpl::getLogicalAddress called for devType : %d \r\n", devType);

	if (mAidlService == nullptr) {
		android::sp<IHdmiCec> service = getAidlService();
		if (service == nullptr) {
			return 0;
		}
		mAidlService = service;
	}
	
	std::vector<int32_t> addresses;
	android::binder::Status status = mAidlService->getLogicalAddresses(&addresses);
	if (status.isOk() && addresses.size() > 0) {
		logicalAddress = addresses[0];  // Return first logical address
	} else {
		CCEC_LOG(LOG_WARN,
			"DriverImpl::getLogicalAddress no allocated LA from AIDL (statusOk=%d, count=%zu). Trying fallback allocation.\r\n",
			status.isOk() ? 1 : 0,
			addresses.size());

		if (mAidlController != nullptr) {
			const std::vector<int32_t> preferred = preferredLogicalAddressesForDeviceType(devType);
			for (std::vector<int32_t>::const_iterator it = preferred.begin(); it != preferred.end(); ++it) {
				std::vector<int32_t> candidate;
				candidate.push_back(*it);
				bool addResult = false;
				android::binder::Status addStatus = mAidlController->addLogicalAddresses(candidate, &addResult);
				CCEC_LOG(LOG_DEBUG,
					"DriverImpl::getLogicalAddress fallback addLogicalAddresses candidate=%d addOk=%d addResult=%d\r\n",
					candidate[0],
					addStatus.isOk() ? 1 : 0,
					addResult ? 1 : 0);

				addresses.clear();
				android::binder::Status retryStatus = mAidlService->getLogicalAddresses(&addresses);
				if (retryStatus.isOk() && addresses.size() > 0) {
					logicalAddress = addresses[0];
					break;
				}
			}
		}
	}

	CCEC_LOG( LOG_DEBUG, "DriverImpl::getLogicalAddress got logical Address : %d \r\n", logicalAddress);
	return logicalAddress;
    }
}

void DriverImpl::getPhysicalAddress(unsigned int *physicalAddress)
{
    {AutoLock lock_(mutex);
        CCEC_LOG( LOG_DEBUG, "DriverImpl::getPhysicalAddress called \r\n");

        // Note: Physical address is not directly available in AIDL interface
        // This might need to be obtained via a different method or kept as HAL call
        // For now, set to 0 as placeholder
        if (physicalAddress != nullptr) {
            *physicalAddress = 0;
        }

        CCEC_LOG( LOG_DEBUG, "DriverImpl::getPhysicalAddress got physical Address : %x \r\n", physicalAddress ? *physicalAddress : 0);
        return ;
    }
}


void DriverImpl::removeLogicalAddress(const LogicalAddress &source)
{
    {AutoLock lock_(mutex);
		if (status != OPENED) {
			throw InvalidStateException();
		}

		if (mAidlController == nullptr) {
			throw IOException();
		}
		
		logicalAddresses.remove(source);
		
		std::vector<int32_t> addresses;
		addresses.push_back(source.toInt());
		bool result = false;
		android::binder::Status status = mAidlController->removeLogicalAddresses(addresses, &result);
		if (!status.isOk() || !result) {
			CCEC_LOG(LOG_EXP, "Failed to remove logical address via AIDL: %s\r\n", status.toString8().c_str());
			throw IOException();
		}
    }
}

bool DriverImpl::addLogicalAddress(const LogicalAddress &source)
{
    {AutoLock lock_(mutex);

		if (status != OPENED) {
			throw InvalidStateException();
		}

		if (mAidlController == nullptr) {
			throw IOException();
		}
		
		std::vector<int32_t> addresses;
		addresses.push_back(source.toInt());
		bool result = false;
		android::binder::Status status = mAidlController->addLogicalAddresses(addresses, &result);
		
		if (!status.isOk()) {
			CCEC_LOG(LOG_EXP, "Failed to add logical address via AIDL: %s\r\n", status.toString8().c_str());
			throw IOException();
		}
		
		if (!result) {
			// Address unavailable
			throw AddressNotAvailableException();
		}
		
		logicalAddresses.push_back(source);
    }

    return true;
}

bool DriverImpl::isValidLogicalAddress(const LogicalAddress & source) const
{
	AutoLock lock_(mutex);
		bool found = false;
		std::list<LogicalAddress>::const_iterator it;
		for (it = logicalAddresses.begin(); it != logicalAddresses.end(); it++) {
			if(*it == source) {
				found = true;
				break;
			}
		}
	return found;
}

void DriverImpl::poll(const LogicalAddress &from, const LogicalAddress &to)
     	 	 	 	  noexcept(false)
{
	uint8_t firstByte = (((from.toInt() & 0x0F) << 4) | (to.toInt() & 0x0F));
	CCEC_LOG( LOG_DEBUG, "$$$$$$$$$$$$$$$$$$$$ POST POLL [%s] [%s]$$$$$$$$$$$$$$$$$$$$$\r\n", from.toString().c_str(), to.toString().c_str());

	{
		CECFrame frame;
		frame.append(firstByte);
		write(frame);
	}
	
#if 0
	{
		/* Send a Poll so indicate there is a device present */
		CECFrame *frame = new CECFrame();
		frame->append(firstByte);
		rQueue.offer(frame);
	}
#endif
}

DriverImpl::IncomingQueue & DriverImpl::getIncomingQueue(int nativeHandle)
{
	if (status != OPENED) {
		throw InvalidStateException();
	}

	return rQueue;
}

void  DriverImpl::printFrameDetails(const CECFrame &frame)  noexcept(false) {
	const uint8_t *buf = NULL;
	char strBuffer[50] = {0};
	size_t len = 0;
	const char *opname = "none";

	try{
		frame.getBuffer(&buf, &len);
		Header header(frame,HEADER_OFFSET);
		for (size_t i = 0; i < len; i++) {
			snprintf(strBuffer + strlen(strBuffer) , (sizeof(strBuffer) - strlen(strBuffer)) ,"%02X ",(uint8_t) *(buf + i));
		}
		if (frame.length() > OPCODE_OFFSET) {
			opname = GetOpName(OpCode(frame,OPCODE_OFFSET).opCode());
			CCEC_LOG( LOG_INFO, "%s to %s : opcode: %s :%s\n",header.from.toString().c_str(), header.to.toString().c_str(), opname, strBuffer);
		}
	}
	catch(Exception &e)
	{
		CCEC_LOG(LOG_EXP, "printFrameDetails caught %s \r\n",e.what());
	}
}

CCEC_END_NAMESPACE


/** @} */
/** @} */
