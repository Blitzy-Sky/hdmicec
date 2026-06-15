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
#include "HDMICecHalFactory.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/android/binder.h>
#include <binder/IServiceManager.h>
#include <com/rdk/hal/hdmicec/IHdmiCec.h>
#include <utils/String16.h>
#include <utils/String8.h>
#include <utils/Vector.h>

#include "HDMICecAidlHAL.h"
#include "HDMICecRdkVHAL.h"
#include "ccec/Util.hpp"

using namespace com::rdk::hal::hdmicec;

static const android::String16 mServiceManagerName("manager");
HDMICecHalFactory::BackendType HDMICecHalFactory::mBackendType = HDMICecHalFactory::BackendType::UNKNOWN;

namespace {

// Build the minimal Parcel payload expected by IServiceManager::PING_TRANSACTION.
size_t buildServiceManagerPingParcel(uint8_t* out, size_t capacity) {
    constexpr char16_t kIface[] = u"android.os.IServiceManager";
    constexpr int32_t kStrictModePolicy = 0;
    constexpr int32_t kIfaceLen = static_cast<int32_t>((sizeof(kIface) / sizeof(kIface[0])) - 1);

    size_t offset = 0;

    auto writeI32 = [&](int32_t value) -> bool {
        if (offset + sizeof(value) > capacity) {
            return false;
        }
        std::memcpy(out + offset, &value, sizeof(value));
        offset += sizeof(value);
        return true;
    };

    if (!writeI32(kStrictModePolicy) || !writeI32(kIfaceLen)) {
        return 0;
    }

    const size_t ifaceBytes = sizeof(kIface);  // includes UTF-16 null terminator
    if (offset + ifaceBytes > capacity) {
        return 0;
    }
    std::memcpy(out + offset, kIface, ifaceBytes);
    offset += ifaceBytes;

    // Binder parcels are 4-byte aligned.
    const size_t aligned = (offset + 3U) & ~3U;
    if (aligned > capacity) {
        return 0;
    }
    std::memset(out + offset, 0, aligned - offset);
    return aligned;
}

static bool isServiceManagerAvailable() {
    #define BINDER_DEV "/dev/binder"
    #define MAP_SIZE (128 * 1024)
    #define PING_TRANSACTION 0

    struct FdGuard {
        int fd = -1;
        ~FdGuard() {
            if (fd >= 0) {
                close(fd);
            }
        }
    };

    struct MapGuard {
        void* addr = MAP_FAILED;
        ~MapGuard() {
            if (addr != MAP_FAILED) {
                munmap(addr, MAP_SIZE);
            }
        }
    };

    FdGuard fdGuard;
    MapGuard mapGuard;

    fdGuard.fd = open(BINDER_DEV, O_RDWR);
    if (fdGuard.fd < 0) {
        CCEC_LOG(LOG_INFO, "Failed to open binder device: %s\n", strerror(errno));
        return false;
    }

    // Check binder protocol version
    struct binder_version ver;
    if (ioctl(fdGuard.fd, BINDER_VERSION, &ver) == -1) {
        CCEC_LOG(LOG_INFO, "Failed to get binder version: %s\n", strerror(errno));
        return false;
    }
    CCEC_LOG(LOG_INFO, "Binder protocol version: %d\n ", ver.protocol_version);

    // Map binder buffer
    mapGuard.addr = mmap(NULL, MAP_SIZE, PROT_READ, MAP_PRIVATE, fdGuard.fd, 0);
    if (mapGuard.addr == MAP_FAILED) {
        CCEC_LOG(LOG_INFO, "Failed to map binder buffer: %s\n", strerror(errno));
        return false;
    }

    // Prepare PING transaction
    uint8_t writebuf[256];
    uint8_t readbuf[256];
    uint8_t parcelbuf[128];

    struct binder_write_read bwr;
    memset(&bwr, 0, sizeof(bwr));

    struct {
        uint32_t cmd;
        struct binder_transaction_data txn;
    } __attribute__((packed)) msg;

    memset(&msg, 0, sizeof(msg));

    msg.cmd = BC_TRANSACTION;

    msg.txn.target.handle = 0;    // servicemanager
    msg.txn.code = PING_TRANSACTION; // ping
    msg.txn.flags = 0;
    msg.txn.data_size = buildServiceManagerPingParcel(parcelbuf, sizeof(parcelbuf));
    if (msg.txn.data_size == 0) {
        CCEC_LOG(LOG_WARN, "Failed to build service manager ping parcel.\n");
        return false;
    }
    msg.txn.offsets_size = 0;
    msg.txn.data.ptr.buffer = reinterpret_cast<binder_uintptr_t>(parcelbuf);
    msg.txn.data.ptr.offsets = 0;

    memcpy(writebuf, &msg, sizeof(msg));

    bwr.write_size = sizeof(msg);
    bwr.write_buffer = (uintptr_t)writebuf;
    bwr.read_size = sizeof(readbuf);
    bwr.read_buffer = (uintptr_t)readbuf;

    // Send ping
    if (ioctl(fdGuard.fd, BINDER_WRITE_READ, &bwr) < 0) {
        CCEC_LOG(LOG_INFO, "Failed to send ping: %s\n", strerror(errno));
        return false;
    }

    // Process response
    uint32_t* ptr = (uint32_t*)readbuf;
    size_t consumed = bwr.read_consumed;

    while (consumed > 0) {
        uint32_t cmd = *ptr++;
        consumed -= sizeof(uint32_t);

        switch (cmd) {
            case BR_NOOP:
                break;

            case BR_REPLY:
                return true;

            default:
                CCEC_LOG(LOG_INFO, "Other cmd: 0x%x\n", cmd);
                break;
        }
    }

    return false;
}

}  // namespace

bool HDMICecHalFactory::isAidlServiceAvailable()
{
    CCEC_LOG(LOG_INFO, "HDMICecHalFactory::isAidlServiceAvailable invoked\r\n");

    if (mBackendType == HDMICecHalFactory::BackendType::AIDL) {
        return true;
    } else if (mBackendType == HDMICecHalFactory::BackendType::LEGACY) {
        return false;
    }

    if (!isServiceManagerAvailable()) {
        CCEC_LOG(LOG_INFO, "Binder driver not available; assuming legacy HDMI CEC HAL\r\n");
        mBackendType = HDMICecHalFactory::BackendType::LEGACY;
        return false;
    }

    android::sp<android::IServiceManager> serviceManager = android::defaultServiceManager();
    if (serviceManager == nullptr) {
        CCEC_LOG(LOG_ERROR, "HDMICecHalFactory::isAidlServiceAvailable failed: IServiceManager unavailable\r\n");
        mBackendType = HDMICecHalFactory::BackendType::LEGACY;
        return false;
    }

    CCEC_LOG(LOG_INFO, "Successfully obtained IServiceManager\r\n");

    const android::String16 expectedServiceName(IHdmiCec::serviceName().c_str());
    android::Vector<android::String16> services = serviceManager->listServices();
    size_t discoveredServiceCount = 0;
    bool matched = false;

    for (size_t index = 0; index < services.size(); ++index) {
        if (services[index] != mServiceManagerName) {
            ++discoveredServiceCount;
        }
    }

    CCEC_LOG(LOG_INFO, "HDMICecHalFactory::isAidlServiceAvailable discovered %zu binder services\r\n", discoveredServiceCount);
    if (discoveredServiceCount == 0) {
        CCEC_LOG(LOG_INFO,
            "HDMICecHalFactory::isAidlServiceAvailable found no binder services beyond the ServiceManager entry while searching for '%s'\r\n",
            android::String8(expectedServiceName).string());
        mBackendType = HDMICecHalFactory::BackendType::LEGACY;
        return false;
    }

    CCEC_LOG(LOG_INFO,
        "HDMICecHalFactory::isAidlServiceAvailable inspecting %zu registered binder services for '%s'\r\n",
        discoveredServiceCount, android::String8(expectedServiceName).string());

    for (size_t index = 0; index < services.size(); ++index) {
        if (services[index] == mServiceManagerName) {
            continue;
        }

        const android::String8 discoveredServiceName(services[index]);
        if (services[index] == expectedServiceName) {
            matched = true;
        }

        CCEC_LOG(LOG_INFO,
            "HDMICecHalFactory::isAidlServiceAvailable discovered binder service[%zu]='%s'\r\n",
            index,
            discoveredServiceName.string());
    }

    if (matched) {
        CCEC_LOG(LOG_INFO,
            "HDMICecHalFactory::isAidlServiceAvailable found HDMI CEC AIDL service '%s'\r\n",
            android::String8(expectedServiceName).string());
        mBackendType = HDMICecHalFactory::BackendType::AIDL;
        return true;
    }

    CCEC_LOG(LOG_INFO,
        "HDMICecHalFactory::isAidlServiceAvailable did not find HDMI CEC AIDL service '%s'\r\n",
        android::String8(expectedServiceName).string());
    mBackendType = HDMICecHalFactory::BackendType::LEGACY;
    return false;
}

std::unique_ptr<IHDMICecHal> HDMICecHalFactory::Create()
{
    CCEC_LOG(LOG_INFO, "HDMICecHalFactory::Create invoked\r\n");

    if (isAidlServiceAvailable()) {
        CCEC_LOG(LOG_INFO, "HDMICecHalFactory: Aidl Service is available — using HDMICecAidlHAL\r\n");
        return std::make_unique<HDMICecAidlHAL>();
    }

    CCEC_LOG(LOG_INFO, "HDMICecHalFactory: Aidl Service is not available — using legacy HDMICecRdkVHAL\r\n");
    return std::make_unique<HDMICecRdkVHAL>();
}

