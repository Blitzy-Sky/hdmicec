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

#include <cstdlib>
#include <cstring>

bool HDMICecHalFactory::isAidlServiceAvailable()
{
    CCEC_LOG(LOG_INFO, "HDMICecHalFactory::isAidlServiceAvailable invoked\r\n");

    const char *env = std::getenv("HDMICEC_USE_AIDL_HAL");
    if (env == nullptr) {
        CCEC_LOG(LOG_INFO, "HDMICecHalFactory::isAidlServiceAvailable env var is not set\r\n");
        return false;
    }

    const bool available = (std::strcmp(env, "true") == 0);
    CCEC_LOG(LOG_INFO, "HDMICecHalFactory::isAidlServiceAvailable env var value='%s', available=%d\r\n", env, available);
    return available;
}

std::unique_ptr<HDMICecHal> HDMICecHalFactory::Create()
{
    CCEC_LOG(LOG_INFO, "HDMICecHalFactory::Create invoked\r\n");

    if (isAidlServiceAvailable()) {
        CCEC_LOG(LOG_INFO, "HDMICecHalFactory: HDMICEC_USE_AIDL_HAL=true — using AidlHAL\r\n");
        return std::make_unique<AidlHAL>();
    }

    CCEC_LOG(LOG_INFO, "HDMICecHalFactory: HDMICEC_USE_AIDL_HAL not set or false — using legacy vHAL\r\n");
    return std::make_unique<vHAL>();
}

