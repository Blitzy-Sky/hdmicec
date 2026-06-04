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

#include "AidlHAL.h"
#include "vHAL.h"

#include "ccec/Util.hpp"

#include <binder/IServiceManager.h>
#include <com/rdk/hal/hdmicec/IHdmiCec.h>
#include <utils/String16.h>
#include <utils/String8.h>
#include <utils/Vector.h>

#include <cstdlib>
#include <cstring>

using android::String16;
using android::String8;
using android::defaultServiceManager;
using namespace com::rdk::hal::hdmicec;

static const String16 mServiceManagerName("manager");
static bool mCachedAidlServiceAvailability = false;
static bool mIsAidlServiceCached = false;

bool HDMICecHalFactory::isAidlServiceAvailable()
{
    CCEC_LOG(LOG_INFO, "HDMICecHalFactory::isAidlServiceAvailable invoked\r\n");

    if (mIsAidlServiceCached) {
        return mCachedAidlServiceAvailability;
    }

    android::sp<android::IServiceManager> serviceManager = defaultServiceManager();
    if (serviceManager == nullptr) {
        CCEC_LOG(LOG_ERROR, "HDMICecHalFactory::isAidlServiceAvailable failed: IServiceManager unavailable\r\n");
        mIsAidlServiceCached = true;
        mCachedAidlServiceAvailability = false;
        return false;
    }

    const String16 expectedServiceName(IHdmiCec::serviceName().c_str());
    android::Vector<String16> services = serviceManager->listServices();
    size_t discoveredServiceCount = 0;
    bool matched = false;

    for (size_t index = 0; index < services.size(); ++index) {
        if (services[index] != mServiceManagerName) {
            ++discoveredServiceCount;
        }
    }

    if (discoveredServiceCount == 0) {
        CCEC_LOG(LOG_INFO,
            "HDMICecHalFactory::isAidlServiceAvailable found no binder services beyond the ServiceManager entry while searching for '%s'\r\n",
            String8(expectedServiceName).string());
        mIsAidlServiceCached = true;
        mCachedAidlServiceAvailability = false;
        return false;
    }

    CCEC_LOG(LOG_INFO,
        "HDMICecHalFactory::isAidlServiceAvailable inspecting %zu registered binder services for '%s'\r\n",
        discoveredServiceCount, String8(expectedServiceName).string());

    for (size_t index = 0; index < services.size(); ++index) {
        if (services[index] == mServiceManagerName) {
            continue;
        }

        const String8 discoveredServiceName(services[index]);
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
            String8(expectedServiceName).string());
        mIsAidlServiceCached = true;
        mCachedAidlServiceAvailability = true;
        return true;
    }

    CCEC_LOG(LOG_INFO,
        "HDMICecHalFactory::isAidlServiceAvailable did not find HDMI CEC AIDL service '%s'\r\n",
        String8(expectedServiceName).string());
    mIsAidlServiceCached = true;
    mCachedAidlServiceAvailability = false;
    return false;
}

bool HDMICecHalFactory::isAidlBackendSelected()
{
    return mCachedAidlServiceAvailability;
}

std::unique_ptr<HDMICecHal> HDMICecHalFactory::Create()
{
    CCEC_LOG(LOG_INFO, "HDMICecHalFactory::Create invoked\r\n");

    if (isAidlServiceAvailable()) {
        CCEC_LOG(LOG_INFO, "HDMICecHalFactory: Aidl Service is available — using AidlHAL\r\n");
        return std::make_unique<AidlHAL>();
    }

    CCEC_LOG(LOG_INFO, "HDMICecHalFactory: Aidl Service is not available — using legacy vHAL\r\n");
    return std::make_unique<vHAL>();
}
