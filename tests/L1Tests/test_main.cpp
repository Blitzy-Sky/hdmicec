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
 * L1 TEST HARNESS BOOTSTRAP: the process-global test environment, the legacy HAL
 * double, and the one place that decides which HDMI CEC HAL back-end this process
 * exercises.
 *
 * THE BACK-END SELECTION RESOLVES ONCE PER PROCESS, AND IT RESOLVES HERE.
 * The LibCCEC::init call in SetUp below is the first thing in this binary that forces
 * Driver::getInstance(). Its helper (ccec/src/Driver.cpp:123-152) constructs BOTH
 * back-ends, asks the AIDL one whether its service came up, and emits exactly one
 * selected-path line naming the winner - the line every functional suite, the coverage
 * runner and device-level validation match on, because no introspection API was added
 * to the Driver interface. That choice is then FIXED for the lifetime of the process.
 *
 * Two consequences, and they are the whole reason this file is shaped the way it is:
 * anything that is to influence the selection MUST happen BEFORE that init call, and
 * NOTHING after it can change the outcome. Registering a fake service after init
 * leaves the selection on the legacy back-end and produces a green run that proves
 * nothing at all about the AIDL path. So the mode handling below sits ahead of init by
 * construction, not merely "early in SetUp".
 *
 * CEC_TEST_AIDL_MODE selects what this process finds when it looks the service up. It
 * is read HERE and in tests/L2Tests/test_main.cpp, and NOWHERE ELSE: no production
 * source reads it, and none may.
 *
 *   absent        Register nothing. The lookup finds no service and the legacy
 *                 back-end is selected. An unset or empty variable means exactly this,
 *                 so a plain ./run_L1Tests behaves precisely as it did before the AIDL
 *                 back-end existed, and this mode touches libbinder not at all.
 *                                                                     [invocation A]
 *   compatible    Register an in-process fake reporting its real, frozen metadata.
 *                 The AIDL back-end is selected.                      [invocation B]
 *   incompatible  Register an in-process fake whose interface hash is "-1". The
 *                 service is PRESENT but rejected as incompatible, so the legacy
 *                 back-end is selected and the rejection is logged. Presence alone is
 *                 not sufficient, and this is the mode that proves it. [invocation C]
 *   remote        NOT IMPLEMENTED HERE. It means "launch the out-of-process fake
 *                 host", which is run_L2Tests' job, not this binary's. Seen here it is
 *                 a hard failure naming that runner.
 *
 * An unrecognised value is a HARD FAILURE and never a quiet fall back to absent. A
 * typo that silently downgraded the run to the legacy path would report a green result
 * for an AIDL invocation that never happened, which is the exact class of defect this
 * harness exists to make impossible.
 *
 * WHY AN IN-PROCESS FAKE, AND WHAT IT CAN AND CANNOT PROVE. libbinder resolves a name
 * registered in the calling process to the local BBinder, so interface_cast hands back
 * that very object: no Bp* proxy is created, no transaction crosses the binder driver,
 * and the client threadpool is never involved. That is a real limitation, and it is why
 * a separate L2 tier exists to host the fake in its own process. It is ALSO the only
 * thing that makes the compatibility-rejection branches reachable at all:
 * halcompat::isCompatible (rdk-halif-aidl/common/current/halcompat.h:157-172) rejects
 * an empty or "-1" interface hash at halcompat.h:165-167, and only an object whose
 * getInterfaceHash() dispatches virtually can report such a value. A remote fake
 * cannot, because its generated onTransact answers the metadata transactions from the
 * compiled-in constants. Hence mode incompatible lives here and has no L2 counterpart.
 *
 * THIS FILE DOES NOT START THE BINDER CLIENT THREADPOOL. DriverAidlImpl::open() owns
 * that, and it runs inside the init call below. Starting one here would duplicate an
 * ownership the production back-end already holds.
 */

#include <gtest/gtest.h>
#include <iostream>
#include "hdmi_cec_driver_mock.h"
#include "ccec/LibCCEC.hpp"

/*
 * The fake AIDL service, included unqualified because AM_CPPFLAGS already carries
 * -I$(top_srcdir)/mocks/hdmicec - the same route "hdmi_cec_driver_mock.h" above
 * travels. It supplies the fake, its metadata overrides and the registration entry
 * point that publishes it under the production service name.
 */
#include "fake_hdmi_cec_aidl_service.h"

/*
 * DriverAidlImpl.hpp lives under ccec/src, which AM_CPPFLAGS does not cover, so it is
 * reached by relative path rather than by adding an -I - the same arrangement the ccec/
 * suites use for DriverImpl.hpp, one directory level shallower from here.
 *
 * It is needed for exactly one symbol, DriverAidlImpl::isBinderPreflightOk(), and that
 * one symbol is not a convenience. Before this harness may ask the service manager
 * whether something is already published under the production name it has to reach the
 * service manager, and doing so unguarded is unsafe in two independent ways on the
 * pinned binder stack: with no driver node libbinder ABORTS the process rather than
 * returning an error, and with a driver node but no running servicemanager it BLOCKS
 * INDEFINITELY waiting for binder handle 0. A plain existence check on the driver node
 * would cover only the first. isBinderPreflightOk() covers both - node openable,
 * protocol version equal, handle 0 resolved within a bounded timeout - and its own
 * documentation records that it is public precisely so a test translation unit may call
 * it. Using it here also keeps the driver-node path out of this file, so there is no
 * second spelling of it to drift.
 */
#include "../../ccec/src/DriverAidlImpl.hpp"

#include <binder/IServiceManager.h>
#include <utils/String16.h>
#include <cstdlib>
#include <string>

// Create mock instance before main
static HdmiCecDriverMock* g_driverMock = nullptr;

/*
 * The fake AIDL service this process published, if any, following the same
 * single-pointer idiom as g_driverMock above.
 *
 * It is held for the lifetime of the process and deliberately never released in
 * TearDown. The pinned C++ IServiceManager exposes NO service-removal API, so the
 * registration cannot be withdrawn once made; the service manager keeps a reference to
 * the published binder, and the middleware may still hold one too. Dropping this
 * reference would therefore not unpublish anything - it would at best destroy an object
 * the service manager still advertises. There is nothing to deregister, and no code
 * below attempts it.
 */
static ::android::sp<FakeHdmiCecService> g_fakeAidlService;

namespace {

/*
 * The harness variable and its four values, spelled exactly once each. These spellings
 * are a fixed contract shared with tests/L2Tests/test_main.cpp, run_coverage.sh, both
 * CI workflows and the test documentation - they are not free to change here.
 */
const char *const AIDL_MODE_VARIABLE     = "CEC_TEST_AIDL_MODE";
const char *const AIDL_MODE_ABSENT       = "absent";
const char *const AIDL_MODE_COMPATIBLE   = "compatible";
const char *const AIDL_MODE_INCOMPATIBLE = "incompatible";
const char *const AIDL_MODE_REMOTE       = "remote";

/*
 * The interface hash halcompat::isCompatible rejects at halcompat.h:165-167, where the
 * accompanying comment reads "hash RPC failed - not a dev build, a broken link". It is
 * the value mode incompatible installs, and reporting it is the only way to drive a
 * PRESENT service down the incompatible arm of the selection.
 */
const char *const BROKEN_INTERFACE_HASH = "-1";

/*
 * Fails the run when something is already published under the production service name.
 *
 * A stale registration left behind by another process would make this run's back-end
 * selection resolve against that service instead of the fake this harness publishes,
 * so the run's outcome would depend on what happened to be running on the machine.
 * That is a hard failure and not a condition to work around: overwriting the entry
 * would hide the collision, and tolerating it would make a green result meaningless.
 *
 * @pre The binder preflight has already passed, so reaching the service manager here is
 *      safe. This function must never be called before it.
 */
void failIfServiceAlreadyPublished(const std::string &serviceName) {
    const ::android::sp< ::android::IServiceManager> serviceManager =
        ::android::defaultServiceManager();

    ASSERT_TRUE(serviceManager != nullptr)
        << "the binder preflight passed but no service manager could be reached, so this "
           "run cannot establish whether \"" << serviceName << "\" is already published";

    ASSERT_TRUE(serviceManager->checkService(::android::String16(serviceName.c_str())) == nullptr)
        << "\"" << serviceName << "\" is already published by another process. This run's "
           "back-end selection would resolve against that service rather than this "
           "harness's fake, so its outcome would depend on a process this suite does not "
           "own; stop that process and run again";
}

/*
 * Publishes the fake AIDL service, configured for the requested mode.
 *
 * The fake's defaults are the real frozen interface version and hash compiled into the
 * snapshot, so mode compatible needs no configuration at all - registering it IS the
 * compatible case. Mode incompatible differs by exactly one call, the hash override,
 * which is what makes the two modes' divergence auditable at a glance.
 */
void publishFakeForMode(const std::string &mode) {
    ASSERT_TRUE(DriverAidlImpl::isBinderPreflightOk())
        << AIDL_MODE_VARIABLE << "=" << mode << " requires a usable binder transport, and this "
           "host does not have one - the driver node is absent or unopenable, its protocol "
           "version differs, or no service manager answered within the bounded timeout. "
           "Failing rather than registering nothing: silently continuing would select the "
           "legacy back-end and report a green result for an AIDL invocation that never ran";

    /*
     * The name comes from the generated interface, never from a literal, so this harness
     * cannot drift from the name the middleware actually looks up.
     */
    const std::string &serviceName = ::com::rdk::hal::hdmicec::IHdmiCec::serviceName();
    ASSERT_NO_FATAL_FAILURE(failIfServiceAlreadyPublished(serviceName));

    ::android::sp<FakeHdmiCecService> fake = ::android::sp<FakeHdmiCecService>::make();
    ASSERT_TRUE(fake != nullptr) << "the fake AIDL service could not be constructed";

    if (mode == AIDL_MODE_INCOMPATIBLE) {
        fake->setInterfaceHash(BROKEN_INTERFACE_HASH);
    }

    ASSERT_TRUE(registerFakeHdmiCecService(fake))
        << "the fake AIDL service could not be published as \"" << serviceName << "\", so "
           "mode " << mode << " cannot be exercised at all";

    /*
     * Publish the pointer the test bodies reach the fake through, and keep the strong
     * reference alive. SetUp runs before any TEST_F body, so this ordering is what lets a
     * case configure or observe a fake that was registered long before it ran.
     */
    FakeHdmiCecService::setInstance(fake.get());
    g_fakeAidlService = fake;

    std::cout << "[CecTestEnvironment] Published the fake AIDL service as \"" << serviceName
              << "\" for " << AIDL_MODE_VARIABLE << "=" << mode << std::endl;
}

/*
 * Reads CEC_TEST_AIDL_MODE and does whatever it asks, before the selection resolves.
 *
 * Every path that does not need libbinder avoids it entirely, which is what keeps the
 * default invocation runnable on a host with no kernel binder support: absent returns
 * without touching it, and both rejection paths fail before reaching it.
 */
void applyAidlModeBeforeInit() {
    const char *const requested = ::getenv(AIDL_MODE_VARIABLE);

    // Unset and empty both mean absent, so the pre-existing behaviour is the default.
    const std::string mode =
        (requested != nullptr && requested[0] != '\0') ? requested : AIDL_MODE_ABSENT;

    if (mode == AIDL_MODE_ABSENT) {
        std::cout << "[CecTestEnvironment] " << AIDL_MODE_VARIABLE << "=" << mode
                  << ": registering no AIDL service, so the legacy back-end is expected"
                  << std::endl;
        return;
    }

    if (mode == AIDL_MODE_COMPATIBLE || mode == AIDL_MODE_INCOMPATIBLE) {
        ASSERT_NO_FATAL_FAILURE(publishFakeForMode(mode));
        return;
    }

    if (mode == AIDL_MODE_REMOTE) {
        FAIL() << AIDL_MODE_VARIABLE << "=" << mode << " is not implemented by run_L1Tests. It "
                  "means \"launch the out-of-process fake service host\", which only run_L2Tests "
                  "does; an in-process registration cannot produce a proxy, a driver transaction "
                  "or a callback on a binder thread. Run run_L2Tests for this mode";
        return;
    }

    FAIL() << AIDL_MODE_VARIABLE << " is set to \"" << mode << "\", which is not a recognised "
              "mode. The four are " << AIDL_MODE_ABSENT << ", " << AIDL_MODE_COMPATIBLE << ", "
           << AIDL_MODE_INCOMPATIBLE << " and " << AIDL_MODE_REMOTE << ". Refusing to fall back "
              "to " << AIDL_MODE_ABSENT << ", because a typo must not quietly downgrade the run "
              "to the legacy back-end and report it as a pass";
}

} // namespace

// Global test environment to set up mocks
class CecTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        // Create and install the driver mock
        g_driverMock = new HdmiCecDriverMock();
        HdmiCecDriverMock::setInstance(g_driverMock);
        
        // Decide what the service lookup inside init() below is going to find. This runs
        // before init() because init() is the one-way door described at the top of this
        // file, and a failure here stops the run rather than letting init() resolve a
        // selection the requested mode did not ask for.
        ASSERT_NO_FATAL_FAILURE(applyAidlModeBeforeInit());
        
        // Initialize the Bus so it's ready for tests
        //
        // NO LONGER SWALLOWED, and the claim the swallow used to carry - "Ignore if
        // already initialized" - was false. LibCCEC::init does throw InvalidStateException
        // when called a second time, but this environment's SetUp runs exactly once per
        // process, so a correct run never reaches that condition and nothing here can
        // legitimately be ignored. Every exception init can actually raise is a real
        // failure: Driver::getInstance().open() being refused by the HAL, or Bus::start()
        // failing. Swallowing those reported a green suite for a process that had never
        // initialized, with every driver-dependent case then asserting against an unopened
        // stack - which made every other green result in this binary meaningless.
        ASSERT_NO_THROW({ LibCCEC::getInstance().init("CEC_TEST"); })
            << "the CEC library could not be initialized, so not one suite in this binary "
               "has its precondition; continuing would assert against an uninitialized stack";
    }
    
    void TearDown() override {
        // Clean up
        //
        // Reported rather than swallowed, but NON-FATALLY and deliberately so. TearDown
        // runs after the suite, so every result this binary produced has already been
        // recorded; aborting here would obscure results that were legitimately earned, and
        // a fatal assertion would also cut the three cleanup statements below short. A
        // non-fatal expectation makes a failing term() visible while leaving the rest of
        // the cleanup to run to completion.
        //
        // One failure here is expected and honest rather than spurious: when init() failed
        // in SetUp the library was never initialized, so term() raises InvalidStateException
        // and is reported as a second failure alongside the first. That is the correct
        // outcome - it says the process never came up - and it is not worth suppressing.
        //
        // Nothing unpublishes the fake AIDL service, if one was published. The pinned C++
        // IServiceManager has no service-removal API, so there is nothing to deregister;
        // g_fakeAidlService above records why its reference is held to the end instead.
        EXPECT_NO_THROW({ LibCCEC::getInstance().term(); })
            << "the CEC library could not be terminated cleanly; the remaining cleanup below "
               "still runs, but this process did not shut the CEC stack down properly";
        
        HdmiCecDriverMock::setInstance(nullptr);
        delete g_driverMock;
        g_driverMock = nullptr;
    }
};

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new CecTestEnvironment);
    return RUN_ALL_TESTS();
}
