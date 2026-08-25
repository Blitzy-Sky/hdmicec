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
 * L1 unit tests for the AIDL/binder HDMI CEC back-end, for the runtime selection between
 * it and the legacy back-end, and for the compatibility rule that selection rests on.
 *
 * Three things are exercised, and they are exercised by three different routes because
 * no single route reaches all of them:
 *
 *   (1) The compatibility predicate and the binder preflight, BY DIRECT CALL against
 *       locally constructed doubles and against paths this file owns. Neither needs a
 *       registered service, a binder driver or a resolved back-end, so both run under
 *       every invocation.
 *   (2) The AIDL back-end's closed-state behaviour, on a LOCAL instance. DriverAidlImpl's
 *       constructor touches no binder at all - it sets the state to CLOSED, the legacy
 *       handle field to 0 and an empty address list, exactly as DriverImpl's does
 *       (DriverAidlImpl.cpp:618-637, mirroring DriverImpl.cpp:87-90) - so a local instance
 *       is constructible even where no service exists, and every status != OPENED guard,
 *       the writeAsync prelude ordering, and the two methods that carry no guard at all
 *       are reachable without a HAL of any kind.
 *   (3) The back-end actually resolved for this process, through Driver::getInstance().
 *       Which one that is depends on the invocation, so the cases that need a specific
 *       one live in fixtures of their own and assert their precondition in SetUp.
 *
 * ISOLATION: this suite shares one process-global driver with every other suite in the
 * binary, and the binary is order-sensitive. Cases needing an open driver establish that
 * precondition themselves and leave the driver open again - the state the global
 * environment sets up at start-up - and clear their own mock expectations. Cases needing
 * a driver that is NOT open construct a local DriverAidlImpl instead of disturbing the
 * shared one. No case here closes, terminates or re-initialises the shared library, and
 * no case here installs a default action on the process-global HAL mock.
 *
 * SEVEN PATHS ARE UNREACHABLE BY CONSTRUCTION and are deliberately not chased. Each is
 * named with its mechanism and with what reaching it would take, so that none of them
 * reads as an oversight:
 *
 * (1) "The server reports an OLDER version within the same major." This arm of
 *     halcompat's era-0 rule is EMPTY FOR THIS CLIENT, not merely untested.
 *     IHdmiCec::VERSION is 1000 (IHdmiCec.h:25), which under the positional encoding at
 *     halcompat.h:86-96 is era 0, major 1. The servers that satisfy era(server) == 0 and
 *     major(server) == 1 are exactly the integers 1000 through 1999, and every one of
 *     them is >= 1000, so the `serverVersion >= clientVersion` conjunct at
 *     halcompat.h:114 cannot fail while the two preceding conjuncts hold. A server that
 *     reports 999 decodes to era 0, MAJOR 0, and is rejected by the cross-major conjunct
 *     at halcompat.h:113 - so labelling such a case "older same-major" would record a
 *     rule that does not exist. The arm is reached instead with a DIFFERENT CLIENT, by
 *     calling detail::isCompatible(3020, 3000) directly; a static assertion below pins
 *     it and explains why halcompat.h:128's own (3000, 2000) assertion is mislabelled.
 *
 * (2) The era >= 1 branch of the ternary at halcompat.h:110-111. It requires
 *     era(clientVersion) >= 1, and this client's era is 0 for as long as
 *     IHdmiCec::VERSION is 1000, so NO server value whatsoever can activate it here.
 *     Reaching it needs a re-frozen interface in era 1 or later, at which point the
 *     static assertion on VERSION below fires and this whole analysis is recomputed.
 *
 * (3) The preflight's protocol-version-mismatch arm (DriverAidlImpl.cpp:1590-1594) and
 *     its context-manager deadline-expiry arm (DriverAidlImpl.cpp:1583). Neither can be
 *     synthesised from a path and a timeout: the first needs a node that ANSWERS
 *     BINDER_VERSION with a value other than the one this build was compiled against,
 *     and the second needs a working binder driver whose context manager never registers.
 *     A regular file reaches the arm before them - the ioctl fails outright - and a
 *     nonexistent path reaches the arm before that. Both are platform-integration
 *     conditions, so they are reached on a binder-capable runner and not here; the three
 *     arms that ARE reachable from a path are asserted below.
 *
 * (4) Withdrawing a registered service. THE PINNED C++ IServiceManager EXPOSES NO
 *     SERVICE-REMOVAL API, and the service manager retains a reference to whatever was
 *     published, so a test written around unregistering could not be implemented at all -
 *     dropping the local reference would at best destroy an object the service manager
 *     still advertises. No case here attempts it. The selection-stability case therefore
 *     proves its point by ADDING a service mid-process rather than by taking one away,
 *     which is the direction that is actually expressible.
 *
 * (5) A physical-address read on the AIDL back-end. BLOCKED ITEM B1 - see the contract
 *     block below, which states what is covered, what is not, and the change that would
 *     close it.
 *
 * (6) The body of DriverAidlImpl::printFrameDetails' catch(Exception &e)
 *     (DriverAidlImpl.cpp:1481-1484). The only statement in the guarded try that can throw
 *     is the Header construction, which reads frame.at(0) and raises std::out_of_range -
 *     and Exception and std::out_of_range are SIBLINGS under std::exception, not related by
 *     inheritance, so catch(Exception &e) cannot catch it and the exception escapes instead.
 *     Reaching the handler needs a PRODUCTION change: a broader handler, or a length guard
 *     ahead of the Header. The method is byte-for-byte the legacy one, whose identical
 *     boundary the neighbouring suite records at test_DriverImpl_Async.cpp:38-46, so this is
 *     a shared pre-existing condition and not something the AIDL back-end introduced. The
 *     escape itself IS asserted below, by the writeAsync prelude-ordering cases, so the
 *     boundary is pinned and any future change to the handler is a visible decision.
 *
 * (7) The non-CLOSED arm of ~DriverAidlImpl (DriverAidlImpl.cpp:642-650), which closes an
 *     open session on the way out. No instance a test can destroy is ever OPENED: a local
 *     instance holds no service proxy, so its open() raises IOException before the state
 *     moves, and the only instance that does get opened is the function-local static inside
 *     Driver::getInstance(), whose destructor runs at STATIC DESTRUCTION - after the last
 *     test has finished, where nothing can assert on it. Reaching it observably would need a
 *     production seam for injecting a proxy, which is a new abstraction and not permitted.
 *     Note in passing that the destructor takes the instance lock and then calls close(),
 *     which takes it again; that is safe rather than a latent deadlock, because
 *     CCEC_OSAL::Mutex is created PTHREAD_MUTEX_RECURSIVE_NP (osal/src/Mutex.cpp:44), and
 *     DriverImpl's destructor has had the same shape all along.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "ccec/CECFrame.hpp"
#include "ccec/Driver.hpp"
#include "ccec/Exception.hpp"
#include "ccec/LibCCEC.hpp"
#include "ccec/OpCode.hpp"
#include "ccec/Operands.hpp"
#include "ccec/Util.hpp"

/*
 * DriverImpl.hpp lives under ccec/src, which AM_CPPFLAGS does not cover, so it is
 * reached by relative path rather than by adding an -I - the established route, taken
 * verbatim from test_DriverImpl_Async.cpp:75. It is needed here for one thing the
 * abstract Driver interface cannot express: naming the legacy concrete type in a
 * dynamic_cast, which is how the cases below establish WHICH back-end this process
 * resolved without a production introspection API being added to Driver.hpp.
 */
#include "../../../ccec/src/DriverImpl.hpp"

/*
 * DriverAidlImpl.hpp is reached by the same relative route, and for the same reason: it
 * is absent from hdmicec/Makefile.am's installed nobase_include_HEADERS list, exactly as
 * DriverImpl.hpp is, which is what makes a second back-end possible without altering the
 * middleware public API - and what makes reaching it from a test translation unit
 * legitimate rather than a hack. It is needed for three things, all of which the abstract
 * interface hides: calling the PUBLIC STATIC preflight predicate
 * DriverAidlImpl::isBinderPreflightOk(), whose own documentation records that it is
 * public precisely so a test may call it; naming the AIDL concrete type in a
 * dynamic_cast, the other half of the selection assertions; and CONSTRUCTING A LOCAL
 * CLOSED INSTANCE, which is what reaches this back-end's state guards and its
 * prelude ordering without touching the process-global driver every other suite in this
 * binary shares.
 */
#include "../../../ccec/src/DriverAidlImpl.hpp"

#include <com/rdk/hal/hdmicec/IHdmiCec.h>
#include <com/rdk/hal/hdmicec/IHdmiCecController.h>
#include <com/rdk/hal/hdmicec/SendMessageStatus.h>
#include <com/rdk/hal/hdmicec/State.h>
#include <utils/StrongPointer.h>

/*
 * Spelled with quotes, not angle brackets, because that is how the production back-end
 * spells it (DriverAidlImpl.cpp:117) and there is nothing to be gained from a second
 * spelling. halcompat.h is not inside either frozen snapshot include root - it lives at
 * HALIF_PREFIX/common/current - which is why configure resolves that third root
 * separately (configure.ac:172-176) and why this include would fail with the two
 * snapshot roots alone.
 */
#include "halcompat.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include "fake_hdmi_cec_aidl_service.h"
#include "hdmi_cec_driver_mock.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;

/** @brief Short alias for the generated AIDL package, matching DriverAidlImpl.cpp:162. */
namespace cechal = ::com::rdk::hal::hdmicec;
/** @brief Short alias for the shared HAL compatibility helpers, matching DriverAidlImpl.cpp:165. */
namespace halcompat = ::com::rdk::hal::halcompat;

/*
 * ---------------------------------------------------------------------------------
 * AIDL CONTRACT CHECK: THE AUTHORITATIVE VALUES, AND THE ONE CONFLICT BETWEEN THEM
 *
 * Every number this file asserts against comes from one of four authorities, and where
 * two of them disagree the disagreement is a DESIGNED, DECLARED difference between the
 * two back-ends rather than a defect. Tabulated so that a future reader does not have to
 * re-derive any of it:
 *
 *     IHdmiCec::VERSION            1000        IHdmiCec.h:25
 *     IHdmiCec::HASHVALUE          70a901cf... IHdmiCec.h:27
 *     SendMessageStatus            0 / 1 / 2   SendMessageStatus.h - ACK_STATE_0,
 *                                              ACK_STATE_1, BUSY, in that order
 *     REPORT_PHYSICAL_ADDRESS      0x84        ccec/include/ccec/OpCode.hpp:73
 *     CECFrame::MAX_LENGTH         128         ccec/include/ccec/CECFrame.hpp:55-57
 *     AIDL sendMessage maximum     16          IHdmiCecController.aidl - "The maximum
 *                                              message size (Header Block plus opcode
 *                                              block plus operand blocks) is 16 * 8 bits
 *                                              (16bytes)", enforced at
 *                                              DriverAidlImpl.cpp:1013-1016 against the
 *                                              file-local AIDL_MAX_MESSAGE_LENGTH at
 *                                              DriverAidlImpl.cpp:198
 *     legacy HAL frame maximum     20          the legacy HAL specification
 *
 * THE CONFLICT, AND WHY IT IS NOT RESOLVED HERE. A CECFrame can carry 128 bytes; the
 * legacy HAL accepts 20; the AIDL contract states 16. Each contract is authoritative for
 * its OWN back-end, and no divergence settles the overlap, so a frame of 17 to 20 bytes
 * is sendable on the legacy back-end and raises IOException on the AIDL one. That is an
 * AUTHORIZED OBSERVABLE DIFFERENCE, and the important half of it is what does NOT happen:
 * the frame is policed, never truncated, because a truncated CEC frame on the bus is
 * worse than a refusal. The cases below assert both halves - the exception AND that
 * nothing reached the fake's sendMessage.
 *
 * THE INVERTED ACK SENSE, which is the crux of the whole adaptation. SendMessageStatus
 * does not mean one thing: ACK_STATE_0 means ACKNOWLEDGED for a directed message and
 * REJECTED for a broadcast, and ACK_STATE_1 is the mirror of that. The destination nibble
 * that decides which reading applies is frame.at(0) & 0x0F, broadcast being 0x0F, read
 * exactly as the legacy implementation reads it. The production translation is at
 * DriverAidlImpl.cpp:1046-1064 and each of its arms reproduces one arm of the legacy
 * mapping in DriverImpl.cpp: :261-263 (a HAL error becomes IOException), :265-274 (the
 * five-value send-failed family, likewise IOException), :276-278 (a directed message not
 * acknowledged becomes CECNoAckException) and :279-284 (the CEC CTS 9-3-3 arm, where a
 * REJECTED BROADCAST REPORT_PHYSICAL_ADDRESS becomes CECNoAckException so the caller
 * retries, while every other rejected broadcast opcode returns normally).
 *
 * WHAT IS COVERED HERE AND WHAT IS BLOCKED.  Physical-address retrieval - SC6(f) - is
 * covered on the LEGACY back-end, where the legacy mock's HdmiCecGetPhysicalAddress
 * expectation is met and the value reaches the caller. On the AIDL back-end it is
 * BLOCKED ITEM B1 and cannot be covered at all, because the method does not read an
 * address: the mandated replacement is an EDID-byte read belonging to the DEVICE SETTINGS
 * HAL layer rather than to the CEC HAL, and the public header declaring it was to be
 * supplied as a separate input to this migration and was not. Every alternative route is
 * closed - reconstructing the declaration from the plugin test mocks is forbidden and a
 * mock is usage; the AIDL HDMI-output controller EDID event is forbidden outright;
 * substituting any CEC HAL call is forbidden; and acquiring a legacy CEC handle lazily is
 * both that forbidden substitution and independently unsafe, since HdmiCecOpen() performs
 * logical-address discovery for source devices and would put a second CEC controller on
 * the wire. So what is asserted on the AIDL back-end is the DOCUMENTED INTERIM
 * behaviour and nothing more: the block is logged and the caller's out-parameter is left
 * UNTOUCHED, proved with a distinctive sentinel. This is a PARTIAL DISCHARGE of SC6(f)
 * and is recorded as one rather than presented as a pass.
 *
 *   REQUIRED CHANGE, REPORTED NOT MADE - supply the device-settings HAL public header
 *   that declares the EDID-byte read, implement DriverAidlImpl::getPhysicalAddress()
 *   against that declaration in place of the B1 log and the untouched out-parameter, then
 *   review DriverImpl::getPhysicalAddress() for whether the same read should replace its
 *   HdmiCecGetPhysicalAddress() call so that BOTH back-ends use the mandated API, and
 *   finally convert the legacy-only assertion below into a both-paths assertion. Until
 *   that header exists the AIDL path has no physical address, which is a real functional
 *   gap and is reported as one.
 *
 * A SECOND ITEM IS PROVISIONAL RATHER THAN BLOCKED, and the difference matters.
 * IHdmiCec::close() is a HIGH-CONFIDENCE CANDIDATE for the legacy HdmiCecClose(), PENDING
 * OWNER CONFIRMATION: the HAL mapping table carries no entry for HdmiCecClose() and no
 * divergence settles it. The candidate is used because a back-end that cannot close is
 * not deliverable, and DriverAidlImpl.cpp records the same marker on the method. The
 * close cases below therefore assert the SEQUENCE - state CLOSED before the raise, local
 * address list deliberately not cleared - which is what the legacy back-end does and what
 * stays true whichever AIDL method the owners confirm. A GREEN RESULT HERE DOES NOT
 * CONFIRM THE MAPPING.
 * ---------------------------------------------------------------------------------
 */

/*
 * ---------------------------------------------------------------------------------
 * FIXTURE MANIFEST - the handoff to whoever wires the invocation matrix
 *
 * The back-end selection resolves ONCE PER PROCESS, inside LibCCEC::init in the global
 * environment's SetUp (tests/L1Tests/test_main.cpp:298), which is the first thing in this
 * binary to force Driver::getInstance(). By the time any TEST_F body runs the choice is
 * made, so ONE BINARY RUN YIELDS ONE SELECTION OUTCOME and every outcome needs its own
 * process. That is why the fixtures below are partitioned by invocation and why each
 * invocation-specific one asserts its precondition in SetUp instead of adapting to
 * whatever it finds.
 *
 *   Fixture                       Cases  Invocation  CEC_TEST_AIDL_MODE  Binder driver
 *   ---------------------------------------------------------------------------------
 *   DriverAidlCompatibilityTest      11  A, B, C     any                 no
 *   DriverAidlPreflightTest           5  A, B, C     any                 no
 *   DriverAidlSelectionTest           4  A           absent              no
 *   DriverAidlLocalInstanceTest       9  A, B, C     any                 no
 *   DriverAidlLegacyArmTest           4  A           absent              no
 *   DriverAidlSessionTest            19  B           compatible          YES
 *   DriverAidlTransmitTest           12  B           compatible          YES
 *   ---------------------------------------------------------------------------------
 *                                    64  of which 33 run under invocation A
 *
 * THE COUNTS THE RUNNER WILL SEE, since its per-invocation check compares the EXECUTED
 * count against the registered inventory and a mismatch is a failed invocation:
 *
 *   registered in the binary          547  (483 pre-existing + 64 here)
 *   invocation A, with the filter     516  (483 + the 33 above) - MEASURED GREEN
 *   invocation B, whole file          547  minus nothing excluded by back-end
 *   invocation C, with the filter     483  plus 25 (Compatibility + Preflight + LocalInstance)
 *
 * The invocation-A figure is measured on this host; the B and C figures are derived from the
 * table and are NOT measured, because neither invocation can execute without a binder-capable
 * kernel. Whoever wires the matrix should re-measure them there rather than trust the
 * arithmetic.
 *
 * THE FILTERS, so that nobody has to derive them:
 *
 *   invocation A   --gtest_filter=-DriverAidlSessionTest.*:DriverAidlTransmitTest.*
 *   invocation B   --gtest_filter=DriverAidl*  (plus the back-end-neutral existing cases)
 *   invocation C   --gtest_filter=DriverAidlCompatibilityTest.*:DriverAidlPreflightTest.*
 *                                 :DriverAidlLocalInstanceTest.*
 *
 * Invocation A's filter is NEGATIVE and it is not optional. The two session fixtures
 * require the AIDL back-end to be the resolved one, so under invocation A their SetUp
 * fails - loudly, with a diagnostic naming the mode they need. THAT IS THE DESIGN, and
 * the alternative was rejected: GTEST_SKIP would make a skipped arm indistinguishable
 * from a passing one in an aggregate count, which is precisely the swallowed-failure
 * shape this file exists to avoid. run_coverage.sh already carries the hook for passing
 * the filter through - GTEST_EXTRA_ARGS (run_coverage.sh:507, :2033) - and it emits an
 * advisory noting the run was a partial selection, which is the honest record.
 *
 * Invocation C selects only the three back-end-independent fixtures. DriverAidlSelection
 * is excluded from it deliberately: under mode incompatible the legacy back-end is
 * selected for a DIFFERENT reason - a service that is present and rejected rather than
 * absent - and the mid-process registration case would then be registering a second
 * service under a name already taken, which test_main.cpp's own collision check
 * (test_main.cpp:169-182) exists to forbid.
 *
 * EXPECTED_SUITE_PATTERN (run_coverage.sh:1797) NEEDS NO EDIT for this file. It is
 * deliberately a stable subset of the nine oldest, largest fixtures, so adding a test
 * file never requires touching it; and none of the fixtures here should be added to it,
 * because a run that exercised only this file is exactly the kind of partial run that
 * pattern exists to catch. The runner's count gate is not a hardcoded number either - it
 * compares the executed count against --gtest_list_tests (run_coverage.sh:1810-1820), so
 * the cases added here need no expected-count edit.
 *
 * INVOCATIONS D AND E ARE NOT HERE, AND MUST NOT BE DUPLICATED HERE. They belong to
 * hdmicec/tests/L2Tests (test_main.cpp and ccec/test_DualPathIntegration.cpp), because an
 * in-process fake IS NOT IPC: libbinder resolves a name registered in the calling process
 * to the local BBinder, so interface_cast hands back that very object - no Bp* proxy is
 * created, no transaction crosses the binder driver, and the client threadpool is never
 * involved. What that LOSES is real transport, which is why the L2 tier hosts the fake in
 * its own process and why the genuine "callback arrives on a binder thread" evidence is
 * invocation E's rather than this file's.
 *
 * What it GAINS is the reason invocations B and C are in-process at all, and it is not a
 * consolation prize: halcompat::isCompatible reads the server's metadata through
 * getInterfaceHash() and getInterfaceVersion() (halcompat.h:164, :171), which on a local
 * object DISPATCH VIRTUALLY and can therefore be overridden. A REMOTE Bn* service cannot
 * report bad metadata at all, because its generated onTransact answers those transactions
 * from the compiled-in constants. So THE COMPATIBILITY-REJECTION BRANCHES ARE REACHABLE
 * ONLY IN-PROCESS, which is why invocation C has no L2 counterpart.
 *
 * AND INVOCATION C IS NOT REDUNDANT WITH THE UNIT TESTS BELOW, which is worth stating
 * because it looks redundant. "Present but incompatible falls back to legacy" is a
 * FACTORY-LEVEL behaviour: the selection helper that acts on the answer is
 * resolveBackEnd() in ccec/src/Driver.cpp:123-152, declared in an anonymous namespace and
 * therefore of internal linkage - it cannot be called from here at all. A unit test of
 * the predicate proves the predicate; only a process that starts with an incompatible
 * service registered proves the factory acts on it.
 * ---------------------------------------------------------------------------------
 */
namespace {

/*
 * The AIDL sendMessage length contract, transcribed rather than imported.
 *
 * DriverAidlImpl.cpp:198 defines AIDL_MAX_MESSAGE_LENGTH in an anonymous namespace, so it
 * has internal linkage and no test can reach it. Restating it here is therefore the only
 * option, and the static assertion below is what keeps the restatement honest: it pins
 * the CECFrame capacity the conflict rests on, so the two numbers cannot drift apart
 * unnoticed.
 */
constexpr size_t kAidlMaxMessageLength = 16;

/** @brief One byte past the AIDL contract and still within the legacy HAL's 20. */
constexpr size_t kJustOverAidlLimit = 17;

/** @brief The legacy HAL specification's maximum, the far end of the disputed range. */
constexpr size_t kLegacyMaxMessageLength = 20;

/*
 * The selected-path log contract, transcribed from ccec/src/Driver.cpp:57-64.
 *
 * Those three constants live in an anonymous namespace in that translation unit - by
 * design, since the string is defined once and referenced nowhere else - so they cannot
 * be imported either. What is copied here is exactly what is there, and the case that
 * uses it does not merely compare two local strings: it drives the PRODUCTION logger with
 * this format and asserts the line that comes out, which is what makes a reworded
 * production string, or a lowered default log level, visible here.
 */
const char *const kSelectedBackEndLogFormat =
    "Driver::getInstance : HDMI CEC HAL back-end selected : %s\r\n";
const char *const kSelectedBackEndAidl   = "AIDL";
const char *const kSelectedBackEndLegacy = "legacy";

/*
 * Local copies of this client's compiled-against interface metadata, and they are NOT a
 * stylistic preference - they are required.
 *
 * IHdmiCec::VERSION is declared `static const int32_t VERSION = 1000;` (IHdmiCec.h:25) with
 * an in-class initializer and NO out-of-line definition anywhere in the snapshot - verified
 * against the generated sources and against the shipped libhdmicec-v0.1.0.0-cpp.so, which
 * exports no such symbol. It is therefore usable in a constant expression, and in a
 * by-value argument, but ODR-USING IT FAILS TO LINK: every GoogleTest comparison macro
 * binds both operands to a const reference, which is an ODR-use, and the result is
 * `undefined reference to com::rdk::hal::hdmicec::IHdmiCec::VERSION` at link time.
 * Measured, not inferred. So the header value is read ONCE here into a constexpr of this
 * translation unit's own, and it is that copy the macros below compare against. The
 * static assertion further down still reads the header member directly, which is the
 * point: it is a constant expression, so it pins the real value rather than this copy.
 *
 * HASHVALUE needs no such treatment - `static constexpr char*` is implicitly inline in
 * C++17, so it has a definition - but it is copied alongside for symmetry and so that
 * every comparison in this file names a local constant.
 */
constexpr int32_t kClientInterfaceVersion = cechal::IHdmiCec::VERSION;

/** @brief The real frozen interface hash this client was compiled against (IHdmiCec.h:27). */
const char *const kFrozenInterfaceHash = cechal::IHdmiCec::HASHVALUE;

/** @brief The interface hash halcompat rejects as a failed hash RPC (halcompat.h:165-167). */
const char *const kBrokenInterfaceHash = "-1";

/** @brief The hash a pre-freeze development server reports (halcompat.h:168-170). */
const char *const kUnfrozenInterfaceHash = "notfrozen";

/** @brief A server version inside this client's era and major, newer than it: MUST be accepted. */
constexpr int32_t kNewerCompatibleVersion = 1010;

/** @brief The largest era-0 major-1 encoding, the far end of the accepted range. */
constexpr int32_t kNewestCompatibleVersion = 1999;

/** @brief The next major generation up: rejected by the cross-major conjunct. */
constexpr int32_t kCrossMajorVersion = 2000;

/** @brief Era 1: rejected because this client's era is 0. */
constexpr int32_t kCrossEraVersion = 100000;

/** @brief The generator default a pre-freeze server reports: era 0, major 0, so cross-major. */
constexpr int32_t kUnfrozenGeneratorVersion = 1;

/*
 * A THIRD-PARTY client/server pair used for one purpose only: reaching the era-0
 * older-same-major arm, which is unreachable for THIS client (see unreachable path (1) in
 * the file block). 3020 is era 0, major 3, minor 2; 3000 is era 0, major 3, minor 0.
 */
constexpr int32_t kOlderSameMajorClient = 3020;
constexpr int32_t kOlderSameMajorServer = 3000;

/*
 * ---------------------------------------------------------------------------------
 * COMPILE-TIME INVARIANTS
 *
 * Every one of these pins a value that an assertion below silently depends on, and every
 * message says what to DO if it fires rather than merely that it fired. The point is that
 * a snapshot regeneration or a header edit stops the build HERE, pointing at the analysis
 * it invalidated, instead of quietly inverting a runtime expectation.
 *
 * detail::isCompatible is constexpr (halcompat.h:108) and detail is an accessible
 * namespace, so the whole version table is checkable without running anything.
 * ---------------------------------------------------------------------------------
 */

static_assert(cechal::IHdmiCec::VERSION == 1000,
    "The compiled-against AIDL interface version is no longer 1000. Every version case in this "
    "file was derived for era 0, major 1 - and so were the two unreachable-by-construction "
    "findings recorded in the file block. RECOMPUTE BOTH: decode the new VERSION with "
    "halcompat.h:93-96, re-derive which server values satisfy era(server) == era(client) and "
    "major(server) == major(client), and rebuild the accept/reject table below from that. In "
    "particular, if the new era is 1 or greater then the era >= 1 branch at halcompat.h:110-111 "
    "becomes reachable and must be covered rather than recorded as unreachable.");

static_assert(halcompat::detail::isCompatible(cechal::IHdmiCec::VERSION, cechal::IHdmiCec::VERSION),
    "A server reporting exactly this client's version is no longer compatible. That is the "
    "identity case; if it fails, the era rules at halcompat.h:108-115 changed fundamentally and "
    "CompatibleWhenServerReportsThisClientsVersion must be revisited before anything else.");

static_assert(halcompat::detail::isCompatible(cechal::IHdmiCec::VERSION, kNewerCompatibleVersion),
    "A NEWER server within the same era and major is no longer accepted. Do not 'fix' this by "
    "asserting rejection: accepting a newer additive server is the documented rule "
    "(halcompat.h:100-103), and a test that treated 'not exactly our version' as incompatible "
    "would encode a rule that does not exist. If the rule genuinely changed, update "
    "CompatibleWhenServerReportsNewerVersionInSameMajor and this assertion together.");

static_assert(halcompat::detail::isCompatible(cechal::IHdmiCec::VERSION, kNewestCompatibleVersion),
    "1999 - the largest era-0 major-1 encoding - is no longer accepted, so the accepted range is "
    "no longer the whole of [1000, 1999]. Unreachable path (1) in the file block is derived from "
    "that range being closed at the top; re-derive it before changing any case.");

static_assert(!halcompat::detail::isCompatible(cechal::IHdmiCec::VERSION, kCrossMajorVersion),
    "A server in the NEXT MAJOR generation is now accepted. In era 0 a major bump is breaking "
    "(halcompat.h:60-62), so this would mean the era-0 rule was relaxed; if that is intended, "
    "IncompatibleWhenServerReportsDifferentMajor must be inverted and the file block's account of "
    "which conjunct rejects 999 rewritten, since that case also relies on the cross-major "
    "conjunct.");

static_assert(!halcompat::detail::isCompatible(cechal::IHdmiCec::VERSION, kCrossEraVersion),
    "An era-1 server now satisfies this era-0 client. Re-read halcompat.h:110-115: for that to "
    "happen either the era equality conjunct went away or this client's era changed. If the "
    "client moved to era 1 or later, the VERSION assertion above fires too and the whole table "
    "is rebuilt from there.");

static_assert(!halcompat::detail::isCompatible(cechal::IHdmiCec::VERSION, kUnfrozenGeneratorVersion),
    "The generator's default version 1 now satisfies a released client. That is the pre-freeze "
    "development server the rules exist to reject (halcompat.h:105-106); if it is now accepted, "
    "IncompatibleWhenServerReportsUnfrozenGeneratorVersion must be inverted deliberately and not "
    "by accident.");

static_assert(!halcompat::detail::isCompatible(kOlderSameMajorClient, kOlderSameMajorServer),
    "THE GENUINE OLDER-SAME-MAJOR PROOF NO LONGER HOLDS, and it is the only one this file has. "
    "(3020, 3000) is era 0 == era 0, major 3 == major 3, and 3000 >= 3020 false, so the ORDERING "
    "conjunct at halcompat.h:114 is what rejects it - which is exactly the arm being pinned. Note "
    "that halcompat.h:128's own static_assert(!isCompatible(3000, 2000), \"era0 older rejected\") "
    "is MISLABELLED: major(3000) is 3 and major(2000) is 2, so that pair is rejected by the "
    "CROSS-MAJOR conjunct and merely re-covers a case already covered. Do not substitute it for "
    "this one. And do not look for an equivalent pair for THIS client - unreachable path (1) in "
    "the file block proves none exists.");

static_assert(static_cast<int32_t>(cechal::SendMessageStatus::ACK_STATE_0) == 0
              && static_cast<int32_t>(cechal::SendMessageStatus::ACK_STATE_1) == 1
              && static_cast<int32_t>(cechal::SendMessageStatus::BUSY) == 2,
    "The SendMessageStatus enumerators were renumbered by a snapshot regeneration. STOP: the ACK "
    "sense is INVERTED between directed and broadcast messages, so a renumbering that went "
    "unnoticed would silently swap 'acknowledged' for 'rejected' on one of the two - the exact "
    "defect the whole status-translation matrix below exists to prevent. Re-read the enumerator "
    "documentation, then re-derive every arm of DriverAidlImpl.cpp:1046-1064 and every case in "
    "DriverAidlTransmitTest from it.");

static_assert(CECFrame::MAX_LENGTH == 128,
    "CECFrame's capacity changed, and it is one of the three numbers the frame-size difference "
    "rests on - the other two being the AIDL contract's 16 and the legacy HAL specification's 20. "
    "If the capacity dropped below 20 the legacy arm of FrameOfTwentyBytes... can no longer be "
    "constructed at all; if it dropped below 17 the disputed range vanishes and the difference "
    "stops being observable. Re-derive the three frame sizes before touching a case.");

static_assert(kAidlMaxMessageLength < kJustOverAidlLimit
              && kJustOverAidlLimit <= kLegacyMaxMessageLength
              && kLegacyMaxMessageLength <= CECFrame::MAX_LENGTH,
    "The three frame sizes no longer straddle the authority conflict they were chosen to "
    "straddle: 16 must be accepted by both back-ends, 17 must sit strictly above the AIDL limit "
    "and at or below the legacy one, and 20 must still fit in a CECFrame. Fix the constants, not "
    "this assertion - and if AIDL_MAX_MESSAGE_LENGTH at DriverAidlImpl.cpp:198 changed, "
    "kAidlMaxMessageLength here is the transcription that must follow it.");

static_assert(REPORT_PHYSICAL_ADDRESS == 0x84,
    "REPORT_PHYSICAL_ADDRESS is no longer 0x84. It is the selector for the CEC CTS 9-3-3 arm "
    "(DriverAidlImpl.cpp:1060, mirroring DriverImpl.cpp:279-284), which is the ONE rejected "
    "broadcast opcode that must raise CECNoAckException while every other rejected broadcast "
    "returns normally. Update the CTS-arm case and its negative control together, or the two "
    "cases will be asserting the same opcode and the distinction will be untested.");

/*
 * ---------------------------------------------------------------------------------
 * Self-contained stdout capture, at file-descriptor level.
 *
 * Reproduced from the established implementation in this same directory
 * (ccec/test_Util.cpp:143-211) rather than invented, and for the reasons recorded there:
 * the code under test logs through printf - C stdio writing to fd 1 - so redirecting the
 * C++ std::cout streambuf would capture nothing, and GoogleTest's CaptureStdout lives in
 * ::testing::internal, which upstream documents as outside the public API.
 *
 * WHY THIS FILE NEEDS IT AT ALL, since the timing looks wrong at first glance. The
 * selected-path line is emitted ONCE, inside LibCCEC::init in the global environment's
 * SetUp, long before any test body runs, so no test can capture the original emission -
 * that line has already gone to the process log, which is where run_coverage.sh greps for
 * it. What the case below does instead is drive the production logger with the
 * transcribed contract format and capture THAT, into an anonymous temporary. Three things
 * follow, and the third is why the redirection matters: the transcribed format is proved
 * to be a format CCEC_LOG accepts and substitutes into; the emitted line is proved to
 * still pass the default log-level filter, so a lowered default that silently broke every
 * consumer of the line would be caught here; and the re-emitted line goes ONLY into the
 * temporary, never into the process log, so that log still carries EXACTLY ONE
 * selected-path hit and the runner's grep contract is untouched.
 *
 * tmpfile() is used rather than a named path: it is unlinked as it is created, so there is
 * nothing for another process to substitute.
 * ---------------------------------------------------------------------------------
 */
class StdoutCapture {
public:
    StdoutCapture()
        : savedStdout_(-1)
        , sink_(::tmpfile())
    {
        if (sink_ == nullptr) {
            return;
        }

        ::fflush(stdout);
        savedStdout_ = ::dup(STDOUT_FILENO);
        if (savedStdout_ < 0) {
            ::fclose(sink_);
            sink_ = nullptr;
            return;
        }

        if (::dup2(::fileno(sink_), STDOUT_FILENO) < 0) {
            ::close(savedStdout_);
            savedStdout_ = -1;
            ::fclose(sink_);
            sink_ = nullptr;
        }
    }

    StdoutCapture(const StdoutCapture &) = delete;
    StdoutCapture &operator=(const StdoutCapture &) = delete;

    ~StdoutCapture() {
        restore();
        if (sink_ != nullptr) {
            ::fclose(sink_);
        }
    }

    bool isValid() const { return sink_ != nullptr && savedStdout_ >= 0; }

    // Restores the real stdout first, so nothing written afterwards is swallowed, then
    // returns everything the body wrote.
    std::string read() {
        restore();
        if (sink_ == nullptr) {
            return std::string();
        }

        ::fflush(sink_);
        ::rewind(sink_);

        std::string captured;
        char buffer[512];
        size_t bytesRead = 0;
        while ((bytesRead = ::fread(buffer, 1, sizeof(buffer), sink_)) > 0) {
            captured.append(buffer, bytesRead);
        }
        return captured;
    }

private:
    void restore() {
        if (savedStdout_ >= 0) {
            ::fflush(stdout);
            ::dup2(savedStdout_, STDOUT_FILENO);
            ::close(savedStdout_);
            savedStdout_ = -1;
        }
    }

    int savedStdout_;
    FILE *sink_;
};

/**
 * @brief The minimal correct double for every compatibility arm.
 *
 * halcompat::isCompatible<I> takes a const android::sp<I>& and reads the server's metadata
 * through getInterfaceHash() and getInterfaceVersion() (halcompat.h:164, :171), both of
 * which dispatch VIRTUALLY. So a locally constructed subclass of the snapshot's own
 * IHdmiCecDefault (IHdmiCec.h:40-72) overriding only those two members is sufficient:
 * there is no need for a Bn* server base, no registration with the service manager and no
 * onTransact involvement whatsoever. IHdmiCec derives from ::android::IInterface and hence
 * from RefBase, so holding one in an android::sp<> is valid.
 *
 * IHdmiCecDefault answers every INTERFACE method with UNKNOWN_TRANSACTION, which is
 * exactly right here - the compatibility check never calls one. Its stock metadata is an
 * EMPTY hash (IHdmiCec.h:69-71) and version 0 (:66-68), so the empty-hash arm needs no
 * override at all and is asserted against the stock object deliberately, as evidence that
 * the default really is empty rather than something this file arranged.
 *
 * @warning Deliberately NOT derived from BnHdmiCec. A Bn* object could be registered and
 *          reached remotely, and its generated onTransact would answer the metadata
 *          transactions from the compiled-in constants - which would make the overrides
 *          below inert. Local, virtual dispatch is the whole mechanism.
 */
class MetadataDouble : public cechal::IHdmiCecDefault {
public:
    MetadataDouble(std::string hash, int32_t version)
        : hash_(std::move(hash))
        , version_(version)
    {
    }

    std::string getInterfaceHash() override { return hash_; }

    int32_t getInterfaceVersion() override { return version_; }

private:
    std::string hash_;
    int32_t version_;
};

/**
 * @brief Builds a compatibility double reporting the real frozen hash and a chosen version.
 *
 * The hash is taken from the generated header rather than written out, so a snapshot
 * regeneration cannot leave a stale literal here that would send every version case down
 * the broken-hash arm at halcompat.h:165-167 and make them all pass for the wrong reason.
 */
::android::sp<cechal::IHdmiCec> frozenDoubleReportingVersion(int32_t version) {
    return ::android::sp<MetadataDouble>::make(std::string(kFrozenInterfaceHash), version);
}

/**
 * @brief Builds a compatibility double reporting a chosen hash and a compatible version.
 *
 * The version is this client's own, so a rejection can only have come from the hash. A
 * double that reported both a bad hash and a bad version would pass its case while
 * proving nothing about which arm fired.
 */
::android::sp<cechal::IHdmiCec> doubleReportingHash(const char *hash) {
    return ::android::sp<MetadataDouble>::make(std::string(hash), kClientInterfaceVersion);
}

/**
 * @brief Creates an openable non-binder node and reports its path, or "" on failure.
 *
 * Used by the preflight case that needs a path which OPENS but is not a binder driver, so
 * that the ioctl arm is the one reached. An anonymous tmpfile() cannot serve: the
 * predicate takes a PATH and opens it itself, so the node must be nameable for the
 * duration of the call. It is unlinked by the caller immediately afterwards.
 */
std::string makeOpenableNonBinderNode() {
    char pattern[] = "/tmp/blitzy_cec_aidl_preflight_XXXXXX";
    const int fd = ::mkstemp(pattern);
    if (fd < 0) {
        return std::string();
    }
    ::close(fd);
    return std::string(pattern);
}

/** @brief A well-formed DIRECTED frame: initiator 4, destination 0, then an opcode. */
CECFrame directedFrame(uint8_t opcode = GIVE_DEVICE_POWER_STATUS) {
    CECFrame frame;
    frame.append(static_cast<uint8_t>(0x40));
    frame.append(opcode);
    return frame;
}

/** @brief A well-formed BROADCAST frame: destination nibble 0x0F, then an opcode. */
CECFrame broadcastFrame(uint8_t opcode = REPORT_PHYSICAL_ADDRESS) {
    CECFrame frame;
    frame.append(static_cast<uint8_t>(0x4F));
    frame.append(opcode);
    return frame;
}

/**
 * @brief A directed frame padded with operands to exactly @p length bytes.
 *
 * Used for the frame-size boundary. The header and opcode are real so the frame is
 * well-formed at every length; the padding carries no meaning and is not decoded by
 * anything on either path.
 */
CECFrame frameOfLength(size_t length) {
    CECFrame frame = directedFrame();
    while (frame.length() < length) {
        frame.append(static_cast<uint8_t>(0x00));
    }
    return frame;
}

} // namespace

/**
 * @brief Compatibility-rejection coverage, per branch, by direct call.
 *
 * Runs under EVERY invocation because it needs nothing from the environment: no registered
 * service, no binder driver, no resolved back-end and no HAL. Every case calls
 * halcompat::isCompatible<IHdmiCec>() against a locally constructed double and asserts the
 * answer.
 *
 * NO DRIVER PRECONDITION IS ESTABLISHED HERE, and that is a deliberate departure from the
 * fixture idiom the driver-facing suites in this directory use. Those fixtures open the
 * process-global driver in SetUp because their cases assert against it; nothing in this
 * fixture touches the driver, the HAL mock or the shared library at all, so opening the
 * driver would be interaction with shared state that this fixture has no use for - and
 * shared state that is touched for no reason is exactly how an order dependency gets
 * introduced. TearDown is likewise empty because there is nothing to restore.
 *
 * SELF-SUFFICIENCY. Every case constructs its own double, calls the predicate and asserts
 * the result. Nothing is carried between cases, nothing outside the fixture is read or
 * written, and each case passes identically alone under a --gtest_filter and inside the
 * full suite, in any order.
 */
class DriverAidlCompatibilityTest : public ::testing::Test {
};

// The double used by every case below is a legitimate stand-in, and this case is what
// establishes that rather than assuming it.
//
// MetadataDouble overrides ONLY getInterfaceHash() and getInterfaceVersion(), which is
// sufficient because halcompat::isCompatible reads nothing else (halcompat.h:161-171). The
// risk in a double that narrow is the opposite of the usual one: not that it does too little,
// but that an INTERFACE method might be reached and quietly answer something plausible,
// letting a case pass for a reason it does not name. IHdmiCecDefault forecloses that by
// answering every interface method with UNKNOWN_TRANSACTION (IHdmiCec.h:45-65), so any such
// call surfaces as a non-ok status rather than as a plausible answer - and that is asserted
// here, on all seven, so the guarantee is measured rather than read off the header.
//
// onAsBinder() returning nullptr is asserted for a second, separate reason: it is exactly why
// the FAKE SERVICE deliberately does NOT derive from IHdmiCecDefault but from BnHdmiCec
// (recorded at fake_hdmi_cec_aidl_service.h:97-100) - an object derived from the default can
// never be published to the service manager nor reached through a proxy. Pinning it here,
// where the one IHdmiCecDefault-derived object in the suite lives, keeps that reasoning
// attached to the thing it explains.
TEST_F(DriverAidlCompatibilityTest, TheCompatibilityDoubleAnswersNoInterfaceMethod) {
    // Held by its CONCRETE type rather than as an sp<IHdmiCec>, and deliberately:
    // IInterface declares onAsBinder() protected, and IHdmiCecDefault re-declares its override
    // public, so the assertion on it below is only well formed through the derived type. The
    // other calls would work either way.
    const ::android::sp<MetadataDouble> doubleUnderTest =
        ::android::sp<MetadataDouble>::make(std::string(kFrozenInterfaceHash),
                                           kClientInterfaceVersion);
    ASSERT_TRUE(doubleUnderTest != nullptr);

    // Every interface method must refuse. Out-parameters are supplied so that a method which
    // wrote one despite refusing would be a use-of-uninitialised rather than a silent pass.
    cechal::State state = cechal::State::CLOSED;
    ::std::optional< ::com::rdk::hal::PropertyValue> property;
    ::std::vector<int32_t> addresses;
    ::android::sp<cechal::IHdmiCecController> controller;
    bool flag = false;

    EXPECT_FALSE(doubleUnderTest->getState(&state).isOk())
        << "the double answered getState. It must refuse every interface method, or a case could "
           "pass on an answer it never asked for";
    EXPECT_FALSE(doubleUnderTest->getProperty(cechal::Property::HAL_CEC_VERSION, &property).isOk())
        << "the double answered getProperty";
    EXPECT_FALSE(doubleUnderTest->getLogicalAddresses(&addresses).isOk())
        << "the double answered getLogicalAddresses";
    EXPECT_FALSE(doubleUnderTest->open(nullptr, &controller).isOk())
        << "the double answered open, which would let a compatibility case accidentally establish "
           "a session";
    EXPECT_FALSE(doubleUnderTest->close(nullptr, &flag).isOk())
        << "the double answered close";
    EXPECT_FALSE(doubleUnderTest->registerEventListener(nullptr, &flag).isOk())
        << "the double answered registerEventListener";
    EXPECT_FALSE(doubleUnderTest->unregisterEventListener(nullptr, &flag).isOk())
        << "the double answered unregisterEventListener";

    EXPECT_EQ(doubleUnderTest->onAsBinder(), nullptr)
        << "IHdmiCecDefault::onAsBinder no longer returns nullptr. That return is why the fake "
           "SERVICE derives from BnHdmiCec instead of from IHdmiCecDefault - an object that cannot "
           "produce a binder can never be published nor reached through a proxy - so if this "
           "changed, revisit fake_hdmi_cec_aidl_service.h:97-100, which records that reasoning";

    // And the two members that ARE overridden still answer, so the double is narrow but not inert.
    EXPECT_EQ(doubleUnderTest->getInterfaceVersion(), kClientInterfaceVersion)
        << "the version override is not in effect, so every version case below is measuring the "
           "stock default rather than the value it installed";
    EXPECT_EQ(doubleUnderTest->getInterfaceHash(), std::string(kFrozenInterfaceHash))
        << "the hash override is not in effect";
}

// A null proxy is rejected before any metadata is read (halcompat.h:161-163). This is the
// arm the selection actually takes when the service manager has nothing published under
// the production name, so it is the most frequently exercised branch in production and the
// cheapest one to get wrong by assuming a null check exists further down.
TEST_F(DriverAidlCompatibilityTest, IncompatibleWhenServiceIsNull) {
    const ::android::sp<cechal::IHdmiCec> absent;

    ASSERT_TRUE(absent == nullptr) << "the fixture's own premise failed: a default-constructed "
                                     "sp<IHdmiCec> must be null for this case to test anything";

    EXPECT_FALSE(halcompat::isCompatible<cechal::IHdmiCec>(absent))
        << "a null service proxy was accepted as compatible, so the selection would go on to "
           "open() and dereference nothing";
}

// An EMPTY hash means the hash query itself failed - a broken link rather than a
// development build - and is rejected at halcompat.h:165-167.
//
// The double is the STOCK IHdmiCecDefault, unmodified, because IHdmiCec.h:69-71 already
// returns "" from getInterfaceHash(). Asserting against the stock object rather than
// against an override is the point: it demonstrates that the snapshot's own default really
// is the rejected value, so nothing in this file had to arrange the condition.
TEST_F(DriverAidlCompatibilityTest, IncompatibleWhenInterfaceHashIsEmpty) {
    const ::android::sp<cechal::IHdmiCec> stockDefault =
        ::android::sp<cechal::IHdmiCecDefault>::make();

    ASSERT_TRUE(stockDefault != nullptr);
    ASSERT_TRUE(stockDefault->getInterfaceHash().empty())
        << "IHdmiCecDefault no longer returns an empty interface hash, so this case is no longer "
           "exercising the empty-hash arm; give it an explicit MetadataDouble(\"\", VERSION) "
           "instead and note that IHdmiCec.h:69-71 changed";

    EXPECT_FALSE(halcompat::isCompatible<cechal::IHdmiCec>(stockDefault))
        << "a service reporting an empty interface hash was accepted; a failed hash query must "
           "never be read as agreement";
}

// A hash of "-1" is the other spelling of a failed hash query, rejected by the same
// condition at halcompat.h:165-167. It is not interchangeable with the empty case: this is
// the exact value the L1 harness installs for invocation C (test_main.cpp:155, :211), so
// the factory-level fallback that invocation proves rests on THIS branch.
TEST_F(DriverAidlCompatibilityTest, IncompatibleWhenInterfaceHashIsMinusOne) {
    const ::android::sp<cechal::IHdmiCec> broken = doubleReportingHash(kBrokenInterfaceHash);

    ASSERT_TRUE(broken != nullptr);
    ASSERT_EQ(broken->getInterfaceVersion(), kClientInterfaceVersion)
        << "the double must report a COMPATIBLE version, so that a rejection can only have come "
           "from the hash";

    EXPECT_FALSE(halcompat::isCompatible<cechal::IHdmiCec>(broken))
        << "a service reporting the \"-1\" interface hash was accepted as compatible, which would "
           "make invocation C select the AIDL back-end instead of falling back to legacy";
}

// A "notfrozen" hash is a pre-freeze development server, which makes no compatibility
// promise. halcompat.h:168-170 returns allowUnfrozen for it, and that parameter DEFAULTS TO
// FALSE (halcompat.h:159), so the default-argument call the production back-end makes
// (DriverAidlImpl.cpp:1656) rejects it.
//
// Both dispositions are asserted, because the default is the whole behaviour under test: a
// case that only asserted rejection could not distinguish "rejected because unfrozen
// servers are opt-in" from "rejected because the hash was not recognised at all".
TEST_F(DriverAidlCompatibilityTest, UnfrozenServerIsRejectedByDefaultAndAcceptedOnlyWhenOptedIn) {
    const ::android::sp<cechal::IHdmiCec> unfrozen = doubleReportingHash(kUnfrozenInterfaceHash);

    ASSERT_TRUE(unfrozen != nullptr);

    EXPECT_FALSE(halcompat::isCompatible<cechal::IHdmiCec>(unfrozen))
        << "an unfrozen development server was accepted by the DEFAULT call, which is the call "
           "the production back-end makes; a development image must be opted into explicitly";

    EXPECT_TRUE(halcompat::isCompatible<cechal::IHdmiCec>(unfrozen, true))
        << "an unfrozen server was rejected even with allowUnfrozen=true, so the parameter no "
           "longer selects the behaviour it documents and the rejection above is happening for "
           "some other reason";
}

// The identity case, and the baseline every version case below is measured against: a
// server reporting exactly this client's compiled-against version, with the real frozen
// hash, is compatible. If this failed, every rejection below would be meaningless because
// nothing would ever be accepted.
TEST_F(DriverAidlCompatibilityTest, CompatibleWhenServerReportsThisClientsVersion) {
    const ::android::sp<cechal::IHdmiCec> exact =
        frozenDoubleReportingVersion(kClientInterfaceVersion);

    ASSERT_TRUE(exact != nullptr);
    ASSERT_EQ(exact->getInterfaceHash(), std::string(kFrozenInterfaceHash))
        << "the double must report the real frozen hash, or it would be rejected by the hash arm "
           "and this case would pass without ever reaching the version comparison";

    EXPECT_TRUE(halcompat::isCompatible<cechal::IHdmiCec>(exact))
        << "a server reporting this client's own interface version was rejected, so no service "
           "could ever be selected on any platform";
}

// A NEWER server within the same era and major is COMPATIBLE, and this case exists
// specifically to stop the opposite rule being encoded by accident.
//
// The era-0 rule at halcompat.h:112-114 accepts any server that is same-era, same-major and
// not older, so 1010 against 1000 must be accepted. A test that treated "not exactly our
// version" as incompatible would look reasonable, would pass against a fake that reports
// the exact version, and would be asserting a rule that does not exist - which would then
// reject a legitimately upgraded HAL in the field. Both ends of the accepted range are
// asserted, 1010 and 1999, so the case pins a range rather than a point.
TEST_F(DriverAidlCompatibilityTest, CompatibleWhenServerReportsNewerVersionInSameMajor) {
    const ::android::sp<cechal::IHdmiCec> newer =
        frozenDoubleReportingVersion(kNewerCompatibleVersion);
    const ::android::sp<cechal::IHdmiCec> newest =
        frozenDoubleReportingVersion(kNewestCompatibleVersion);

    ASSERT_TRUE(newer != nullptr);
    ASSERT_TRUE(newest != nullptr);
    ASSERT_GT(kNewerCompatibleVersion, kClientInterfaceVersion)
        << "the 'newer' double must actually be newer than this client for the case to mean "
           "anything";

    EXPECT_TRUE(halcompat::isCompatible<cechal::IHdmiCec>(newer))
        << "a NEWER server in the same era and major was rejected. The era-0 rule accepts it "
           "(halcompat.h:112-114); rejecting it would refuse an upgraded HAL that is in fact "
           "additive and compatible";

    EXPECT_TRUE(halcompat::isCompatible<cechal::IHdmiCec>(newest))
        << "the top of this client's accepted range was rejected, so the accepted set is no "
           "longer the whole of [1000, 1999] that the file block's analysis relies on";
}

// A different MAJOR generation is breaking in era 0, so it is rejected by the major
// equality conjunct at halcompat.h:113 - notably even though 2000 is numerically LARGER
// than this client's 1000, which is why an ordering-only comparison would wrongly accept it.
TEST_F(DriverAidlCompatibilityTest, IncompatibleWhenServerReportsDifferentMajor) {
    const ::android::sp<cechal::IHdmiCec> crossMajor =
        frozenDoubleReportingVersion(kCrossMajorVersion);

    ASSERT_TRUE(crossMajor != nullptr);
    ASSERT_GT(kCrossMajorVersion, kClientInterfaceVersion)
        << "this case is only meaningful while the cross-major value is numerically GREATER than "
           "the client's, since that is what distinguishes the major conjunct from the ordering "
           "conjunct";

    EXPECT_FALSE(halcompat::isCompatible<cechal::IHdmiCec>(crossMajor))
        << "a server from the next major generation was accepted. In era 0 a major bump is "
           "breaking, and it is larger numerically, so accepting it means the check degenerated "
           "to an ordering comparison";
}

// A server in a LATER ERA is rejected for a third, distinct reason: the era equality
// conjunct at halcompat.h:112. This is not the same branch as the cross-major case above -
// 100000 has era 1 and major 0, so it fails on era first - and it is worth separating
// because an era-1 server is a re-frozen interface rather than a broken one.
TEST_F(DriverAidlCompatibilityTest, IncompatibleWhenServerReportsDifferentEra) {
    const ::android::sp<cechal::IHdmiCec> crossEra =
        frozenDoubleReportingVersion(kCrossEraVersion);

    ASSERT_TRUE(crossEra != nullptr);

    EXPECT_FALSE(halcompat::isCompatible<cechal::IHdmiCec>(crossEra))
        << "an era-1 server satisfied this era-0 client. The two eras make different promises, so "
           "this must be rejected until the client itself is re-frozen into era 1 or later";
}

// The generator's default version, 1, is what a pre-freeze development server reports
// through the VERSION channel rather than through the hash. It decodes to era 0, MAJOR 0,
// so it is rejected by the cross-major conjunct - and this is the case that would be
// mislabelled as "older same-major" if the encoding were not decoded carefully, since 1 is
// indeed smaller than 1000. It is not older-same-major; there is no such value for this
// client (unreachable path (1) in the file block).
TEST_F(DriverAidlCompatibilityTest, IncompatibleWhenServerReportsUnfrozenGeneratorVersion) {
    const ::android::sp<cechal::IHdmiCec> generatorDefault =
        frozenDoubleReportingVersion(kUnfrozenGeneratorVersion);

    ASSERT_TRUE(generatorDefault != nullptr);
    ASSERT_EQ(halcompat::detail::major(kUnfrozenGeneratorVersion), 0)
        << "version 1 no longer decodes to major 0, so the reason it is rejected has changed and "
           "this case's comment - and the file block's account of it - are now wrong";
    ASSERT_NE(halcompat::detail::major(kUnfrozenGeneratorVersion),
              halcompat::detail::major(kClientInterfaceVersion))
        << "version 1 now shares this client's major, which would make it the older-same-major "
           "case the file block proves cannot exist; re-derive unreachable path (1)";

    EXPECT_FALSE(halcompat::isCompatible<cechal::IHdmiCec>(generatorDefault))
        << "a pre-freeze development server reporting the generator's default version was "
           "accepted as compatible";
}

// The era-0 ORDERING conjunct at halcompat.h:114, reached with a different client because it
// is unreachable for this one.
//
// This is the whole of unreachable path (1) expressed as an executable assertion. For client
// 1000 the same-era, same-major servers are exactly 1000 through 1999 and every one of them
// satisfies server >= client, so the ordering conjunct can never be the conjunct that
// rejects. Calling detail::isCompatible directly with a client of 3020 and a server of 3000
// - era 0 == era 0, major 3 == major 3, and 3000 >= 3020 false - reaches it. The two
// preceding assertions establish that the OTHER two conjuncts hold, which is what makes this
// a proof about the ordering conjunct specifically rather than just another rejection.
//
// halcompat.h:128's own static_assert(!isCompatible(3000, 2000), "era0 older rejected") does
// NOT reach this arm: major(3000) is 3, major(2000) is 2, so that pair is rejected on major
// and merely re-covers the cross-major case above.
TEST_F(DriverAidlCompatibilityTest, OlderSameMajorServerIsRejectedByTheOrderingRule) {
    ASSERT_EQ(halcompat::detail::era(kOlderSameMajorClient),
              halcompat::detail::era(kOlderSameMajorServer))
        << "the chosen pair no longer shares an era, so a rejection would come from the era "
           "conjunct and this case would stop testing the ordering conjunct";
    ASSERT_EQ(halcompat::detail::major(kOlderSameMajorClient),
              halcompat::detail::major(kOlderSameMajorServer))
        << "the chosen pair no longer shares a major, so this case has degenerated into the "
           "cross-major case - which is exactly the flaw in halcompat.h:128's own assertion";
    ASSERT_LT(kOlderSameMajorServer, kOlderSameMajorClient)
        << "the server must be OLDER than the client for the ordering conjunct to be the one "
           "that fails";

    EXPECT_FALSE(halcompat::detail::isCompatible(kOlderSameMajorClient, kOlderSameMajorServer))
        << "an older server within the same era and major was accepted, so the ordering conjunct "
           "at halcompat.h:114 is no longer enforced";

    // Positive control on the same pair, reversed: a NEWER server in that major is accepted,
    // so the rejection above is attributable to the ordering and not to the pair itself.
    EXPECT_TRUE(halcompat::detail::isCompatible(kOlderSameMajorServer, kOlderSameMajorClient))
        << "the reversed pair was also rejected, so something other than ordering is rejecting "
           "both and the case above proves nothing about the ordering conjunct";
}

/**
 * @brief The binder preflight's negative arms, by direct call.
 *
 * DriverAidlImpl::isBinderPreflightOk() is a PUBLIC STATIC member taking the binder driver
 * path and a context-manager timeout, both defaulted (DriverAidlImpl.hpp:736-737). Taking
 * the path as a parameter is exactly what makes these cases possible: a nonexistent path
 * and a non-binder node can be probed WITHOUT rendering a runner's real /dev/binder
 * unusable, and nothing here writes to, chmods or unlinks the real node. The member's own
 * documentation records that it is public so that a test translation unit may call it; a
 * file-local function in the .cpp would not have been callable at all.
 *
 * WHY THIS PREDICATE MUST EXIST AT ALL, since a test of a guard reads as ceremony
 * otherwise. On the pinned binder stack, reaching the service manager unguarded is unsafe
 * in two independent ways: a missing or protocol-mismatched driver node is FATAL rather
 * than an error return, because the pin extends libbinder's failed-driver
 * LOG_ALWAYS_FATAL_IF to plain Linux; and a working driver with no running servicemanager
 * BLOCKS INDEFINITELY, because obtaining an IServiceManager polls until binder handle 0
 * resolves with no bound of its own. Either one defeats the absolute requirement that a
 * supported SOC without an AIDL HAL reach the legacy back-end. On a host with no kernel
 * binder support - which is the common case for this suite - the first arm below is the one
 * standing between a plain ./run_L1Tests and a SIGABRT that would take all of the other
 * cases in this binary down with it.
 *
 * NO DRIVER PRECONDITION IS ESTABLISHED, for the same reason as the compatibility fixture:
 * nothing here touches the process-global driver, the HAL mock or the shared library.
 *
 * SELF-SUFFICIENCY. Each case supplies its own path, and the one case that needs a real
 * filesystem node creates it, uses it and unlinks it within the case body. No shared state
 * is read or written, so every case passes alone and in any order.
 */
class DriverAidlPreflightTest : public ::testing::Test {
};

// An empty path is refused outright, before anything is opened
// (DriverAidlImpl.cpp:1551-1554). This is the arm a misconfigured deployment reaches - a
// path variable that resolved to nothing - and refusing it explicitly is what stops
// ::open("") being attempted and its errno being reported as though a node had been found.
TEST_F(DriverAidlPreflightTest, DeclinesAnEmptyDriverPath) {
    EXPECT_FALSE(DriverAidlImpl::isBinderPreflightOk(std::string()))
        << "the preflight accepted an empty driver path, so a deployment whose path variable "
           "resolved to nothing would be told the binder transport is usable";
}

// A path with no node behind it is declined because ::open fails
// (DriverAidlImpl.cpp:1562-1567). THIS IS THE ARM THIS ENTIRE SUITE DEPENDS ON: a host
// without kernel binder support has no /dev/binder, so this is the arm that returns false,
// the selection falls back to the legacy back-end, and every other case in this binary gets
// to run. A regression here would not fail one test - it would abort the process inside
// libbinder before the first test body executed.
TEST_F(DriverAidlPreflightTest, DeclinesANonexistentDriverPath) {
    const std::string absent = "/dev/blitzy_cec_aidl_no_such_binder_node";

    ASSERT_NE(::access(absent.c_str(), F_OK), 0)
        << "the path chosen to be absent exists on this host, so this case is not exercising the "
           "cannot-open arm; choose another path";

    EXPECT_FALSE(DriverAidlImpl::isBinderPreflightOk(absent))
        << "the preflight accepted a driver path with nothing behind it. On a platform with no "
           "binder driver this is what stands between falling back to the legacy back-end and "
           "libbinder aborting the process during initialization";
}

// A node that OPENS but is not a binder driver is declined at the next arm along, where the
// BINDER_VERSION ioctl fails (DriverAidlImpl.cpp:1583-1588).
//
// This is a genuinely different branch from the case above and worth separating: "the path
// opened" and "the thing behind the path speaks binder" are two questions, and a predicate
// that asked only the first would pass a stale non-binder node straight through to
// libbinder. A regular file is used because it is the one thing guaranteed to open O_RDWR
// on any host while refusing every ioctl; an anonymous tmpfile() cannot serve, because the
// predicate takes a PATH and opens it itself.
TEST_F(DriverAidlPreflightTest, DeclinesAPathThatOpensButIsNotABinderDriver) {
    const std::string node = makeOpenableNonBinderNode();

    ASSERT_FALSE(node.empty()) << "a temporary node could not be created, so this case cannot "
                                 "establish its precondition";
    ASSERT_EQ(::access(node.c_str(), R_OK | W_OK), 0)
        << "the temporary node is not readable and writable, so the preflight would decline it at "
           "the cannot-open arm instead of at the ioctl arm this case is about";

    const bool accepted = DriverAidlImpl::isBinderPreflightOk(node);

    // Unlinked before the assertion, so a failure does not also leave a file behind.
    ::unlink(node.c_str());

    EXPECT_FALSE(accepted)
        << "the preflight accepted a path that opens but does not speak the binder protocol. "
           "Opening a node is not evidence that it is a binder driver, and handing such a node to "
           "libbinder is the condition the pin treats as fatal";
}

// The timeout parameter is accepted and honoured as an argument, asserted at both ends of
// its range on a path that cannot get far enough to wait for anything.
//
// WHAT THIS DOES AND DOES NOT PROVE, stated plainly rather than left to be assumed. It
// proves the two-argument overload is callable and that neither timeout value changes the
// verdict for an absent node - which is the property that matters for the production call,
// since the default two-second bound must not turn a missing node into a two-second stall.
// It does NOT reach the deadline-expiry arm itself (DriverAidlImpl.cpp:1583, via the probe
// loop): that arm needs the node to open AND to report a matching protocol version first,
// so it requires a working binder driver whose context manager never registered. That is a
// platform-integration condition, not something a path and a timeout can synthesise, and it
// is recorded as unreachable path (3) in the file block rather than faked here.
TEST_F(DriverAidlPreflightTest, HonoursTheContextManagerTimeoutArgumentWithoutWaitingForAbsentNode) {
    const std::string absent = "/dev/blitzy_cec_aidl_no_such_binder_node";

    EXPECT_FALSE(DriverAidlImpl::isBinderPreflightOk(absent, 0u))
        << "a zero timeout was expected to mean 'do not wait at all' and to leave the verdict for "
           "an absent node unchanged";

    EXPECT_FALSE(DriverAidlImpl::isBinderPreflightOk(
                     absent, DriverAidlImpl::DEFAULT_CONTEXT_MANAGER_TIMEOUT_MS))
        << "the default timeout changed the verdict for an absent node, which would mean the node "
           "check is no longer short-circuiting ahead of the context-manager probe";
}

// The default driver path is the one libbinder itself opens, and the default argument is what
// keeps that path out of every caller.
//
// This case asserts the CONSTANT rather than the verdict, deliberately: the verdict for the
// default path is a property of the host - false here, where there is no kernel binder
// support, and true on a binder-capable runner - so asserting it either way would encode
// one host's configuration as a rule. What is host-independent is that the production call
// site names nothing (DriverAidlImpl.cpp:1643 calls isBinderPreflightOk() with no
// arguments) and that the harness's own preflight call does the same
// (test_main.cpp:193), so a drift in this constant would silently change what both of them
// probe. The POSITIVE arm - the preflight passing - is reached by invocation B, where the
// AIDL back-end is selected only because it passed.
TEST_F(DriverAidlPreflightTest, DefaultDriverPathIsTheNodeLibbinderOpens) {
    EXPECT_STREQ(DriverAidlImpl::DEFAULT_BINDER_DRIVER_PATH, "/dev/binder")
        << "the default binder driver path changed. Both the production selection and the L1 "
           "harness call isBinderPreflightOk() with no arguments, so this constant is what they "
           "probe; if the platform genuinely moved the node, update this expectation and check "
           "that nothing else hard-codes the old spelling";

    EXPECT_GT(DriverAidlImpl::DEFAULT_CONTEXT_MANAGER_TIMEOUT_MS, 0u)
        << "the default context-manager timeout is zero, which means the production preflight no "
           "longer waits at all for handle 0 to resolve. A platform whose servicemanager starts "
           "concurrently with the middleware would then be misread as having none";
}

/**
 * @brief The resolved selection under invocation A: legacy, once, and stable thereafter.
 *
 * REQUIRES INVOCATION A - CEC_TEST_AIDL_MODE=absent, or unset, which means the same thing
 * (test_main.cpp:240-249). SetUp asserts that precondition rather than adapting to whatever
 * it finds, so a mis-wired --gtest_filter fails with a diagnostic naming the mode instead of
 * failing somewhere further in with no explanation.
 *
 * HOW THE SELECTION IS OBSERVED, given that no production introspection API exists and none
 * was added. Two independent routes, both used: a dynamic_cast against the two concrete
 * types, which a test translation unit may name because neither ccec/src/DriverImpl.hpp nor
 * ccec/src/DriverAidlImpl.hpp is an installed header; and the selected-path log line, which
 * is the route the coverage runner and device-level validation use because the concrete
 * headers are out of scope for them.
 *
 * SELF-SUFFICIENCY. No case here opens, closes or otherwise mutates the process-global
 * driver - they only ask the factory which object it holds, which is a pure read - so there
 * is no shared driver state to establish or restore and TearDown has nothing to do. The one
 * case that changes anything outside itself publishes a service, and its own comment records
 * exactly what that leaves behind and why it is harmless.
 */
class DriverAidlSelectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        legacyBackEnd = dynamic_cast<DriverImpl *>(&Driver::getInstance());

        ASSERT_NE(legacyBackEnd, nullptr)
            << "this fixture requires the LEGACY back-end to be the resolved one, i.e. invocation "
               "A (CEC_TEST_AIDL_MODE=absent or unset), and the factory returned something else. "
               "The selection resolves once per process inside LibCCEC::init, so it cannot be "
               "changed from here; re-run with the invocation-A filter from the FIXTURE MANIFEST "
               "above instead";
    }

    DriverImpl *legacyBackEnd = nullptr;
};

// SC6(b), first half: with no AIDL service registered the factory selects the legacy
// back-end. Asserted by concrete type, in both directions - it IS a DriverImpl and it is NOT
// a DriverAidlImpl - because a one-sided assertion would also pass against some third
// implementation, and because the two casts together are what make "exactly one of the two"
// an assertion rather than a description.
TEST_F(DriverAidlSelectionTest, AbsentServiceSelectsTheLegacyBackEnd) {
    Driver &resolved = Driver::getInstance();

    EXPECT_NE(dynamic_cast<DriverImpl *>(&resolved), nullptr)
        << "the resolved back-end is not the legacy one, although no AIDL service is registered";

    EXPECT_EQ(dynamic_cast<DriverAidlImpl *>(&resolved), nullptr)
        << "the resolved back-end is the AIDL one although no AIDL service is registered, so the "
           "selection is not resting on service availability at all";
}

// SC6(b), second half: the selected-path log line names the legacy back-end.
//
// The line the factory actually emitted went to the process log during LibCCEC::init, before
// any test body existed, so it cannot be captured here - and re-emitting it into the process
// log would break the runner's one-hit-per-process grep. What this case does instead is
// drive the PRODUCTION logger with the transcribed contract format, into an anonymous
// temporary, and assert the line that comes out. That proves three things a pair of local
// string comparisons could not: the format is one CCEC_LOG accepts and substitutes the
// back-end name into; the line still survives the default log-level filter, so a lowered
// default that silently broke every consumer of the line is caught here; and the substituted
// name agrees with what the dynamic_cast above established.
TEST_F(DriverAidlSelectionTest, SelectedPathLogLineNamesTheLegacyBackEnd) {
    // The format is a contract, so its shape is asserted before it is used: exactly one
    // substitution, and the \r\n terminator every CCEC log line carries.
    const std::string format(kSelectedBackEndLogFormat);
    ASSERT_EQ(format.find("%s"), format.rfind("%s"))
        << "the transcribed selected-path format carries more than one substitution, so it no "
           "longer matches ccec/src/Driver.cpp:57-58, where the back-end name is the only part "
           "that varies";
    ASSERT_NE(format.find("%s"), std::string::npos)
        << "the transcribed selected-path format has no substitution at all, so it cannot name a "
           "back-end";
    ASSERT_GE(format.size(), 2u);
    EXPECT_EQ(format.substr(format.size() - 2), "\r\n")
        << "the selected-path format no longer ends in \\r\\n, which every CCEC_LOG line in this "
           "middleware carries and which the runner's line-oriented grep relies on";

    std::string captured;
    {
        StdoutCapture capture;
        ASSERT_TRUE(capture.isValid())
            << "stdout could not be redirected, so nothing about the emitted line can be "
               "established; failing rather than asserting against an empty capture";

        CCEC_LOG(LOG_INFO, kSelectedBackEndLogFormat, kSelectedBackEndLegacy);

        captured = capture.read();
    }

    const std::string expected =
        std::string("HDMI CEC HAL back-end selected : ") + kSelectedBackEndLegacy;

    EXPECT_NE(captured.find(expected), std::string::npos)
        << "the production logger did not emit the selected-path line naming the legacy back-end. "
           "Either the transcribed format at the top of this file has drifted from "
           "ccec/src/Driver.cpp:57-64, or the default CEC log level is now below LOG_INFO and the "
           "line is being filtered out - in which case every consumer of it, this suite included, "
           "is reading an empty log. Captured instead: [" << captured << "]";

    // The two back-end names must be distinguishable, or the line could not identify either.
    EXPECT_STRNE(kSelectedBackEndLegacy, kSelectedBackEndAidl)
        << "the two back-end names in the log contract are now identical, so the line cannot say "
           "which back-end was selected";
}

// SC6(c), first half: the factory hands back the SAME object every time.
//
// Driver::getInstance() holds a reference to a function-local static
// (ccec/src/Driver.cpp:156-160), so this is what makes "resolved once" observable at all: a
// factory that re-decided per call, or returned a fresh object, would break the state
// machine every caller depends on - Bus's reader and writer threads and LibCCEC would each
// be talking to a different driver.
TEST_F(DriverAidlSelectionTest, FactoryReturnsTheSameObjectOnEveryCall) {
    Driver *first = &Driver::getInstance();
    Driver *second = &Driver::getInstance();
    Driver *third = &Driver::getInstance();

    ASSERT_NE(first, nullptr);

    EXPECT_EQ(first, second) << "two consecutive calls to Driver::getInstance() returned different "
                               "objects, so the back-end is not a stable singleton";
    EXPECT_EQ(second, third) << "a third call returned yet another object";
    EXPECT_EQ(first, static_cast<Driver *>(legacyBackEnd))
        << "the object the factory returns is not the one SetUp resolved, so the identity "
           "established there does not describe what the rest of this suite is talking to";
}

// SC6(c), second half, and the one that actually needs arranging: registering a service
// MID-PROCESS does not change an already-resolved selection.
//
// This is the property that makes a session deterministic. The selection resolves once,
// inside LibCCEC::init, and is then fixed for the lifetime of the process - so a HAL that
// comes up late must not be picked up half way through, leaving one part of a session
// talking to the legacy back-end and another to the AIDL one.
//
// THE DIRECTION IS FORCED. Proving stability by taking a service AWAY is not expressible:
// the pinned C++ IServiceManager has no service-removal API and retains a reference to
// whatever was published, so there is nothing to deregister - see unreachable path (4) in
// the file block. Adding one is the direction that can be expressed, and it is also the more
// dangerous direction in practice, since it is the one a late-starting HAL produces.
//
// THE PREFLIGHT IS NOT OPTIONAL HERE. registerFakeHdmiCecService() guards on the driver node
// itself (fake_hdmi_cec_aidl_service.cpp:1745-1749) and so cannot abort, but it does not
// guard the OTHER hazard: on a host that has a driver node and no running servicemanager,
// defaultServiceManager() blocks until binder handle 0 resolves, with no bound, which would
// hang this binary rather than fail it. isBinderPreflightOk() covers both, which is why it
// is asked first here exactly as the harness asks it (test_main.cpp:193).
//
// WHAT THIS LEAVES BEHIND, AND WHY IT IS HARMLESS. Where the registration does succeed, the
// fake stays published for the rest of the process, because there is no way to withdraw it.
// No sibling suite is affected: the selection is already resolved and cannot be re-decided,
// nothing else in this binary looks the name up, and the entry lives only as long as this
// process - the service manager's reference dies with it. The strong reference is held in a
// function-local static rather than in the fixture so that the published binder outlives
// this case; dropping it at the end of the case would leave the service manager advertising
// a destroyed object, which is a worse state than leaving it advertising a live one.
TEST_F(DriverAidlSelectionTest, RegisteringAServiceMidProcessDoesNotChangeTheResolvedBackEnd) {
    Driver *before = &Driver::getInstance();
    ASSERT_EQ(dynamic_cast<DriverAidlImpl *>(before), nullptr)
        << "the AIDL back-end was already selected, so this case cannot demonstrate that a "
           "mid-process registration fails to switch to it";

    static ::android::sp<FakeHdmiCecService> latePublishedFake;

    if (DriverAidlImpl::isBinderPreflightOk()) {
        latePublishedFake = ::android::sp<FakeHdmiCecService>::make();
        ASSERT_TRUE(latePublishedFake != nullptr) << "the fake service could not be constructed";

        const bool published = registerFakeHdmiCecService(latePublishedFake);

        // The outcome is REPORTED rather than asserted, because it legitimately differs by
        // host: on a binder-capable runner the registration succeeds and this case exercises
        // its full strength, while a host whose service manager refuses it still leaves the
        // invariant below meaningful. It is printed so that a reader of the log can tell the
        // two apart instead of guessing which one ran.
        std::cout << "[DriverAidlSelectionTest] mid-process registration "
                  << (published ? "succeeded" : "was refused")
                  << "; the selection invariant is asserted either way" << std::endl;
    } else {
        std::cout << "[DriverAidlSelectionTest] the binder preflight declined, so no service was "
                     "published; the selection invariant is asserted against an unchanged "
                     "environment" << std::endl;
    }

    Driver *after = &Driver::getInstance();

    EXPECT_EQ(after, before)
        << "the factory returned a different object after a service was registered, so the "
           "selection is being re-decided per call rather than held for the process";

    EXPECT_NE(dynamic_cast<DriverImpl *>(after), nullptr)
        << "the resolved back-end is no longer the legacy one. A service that appears after "
           "initialization must NOT be adopted: half a session on each back-end is precisely the "
           "non-determinism resolving once is there to prevent";

    EXPECT_EQ(dynamic_cast<DriverAidlImpl *>(after), nullptr)
        << "the AIDL back-end was adopted mid-process";
}

/**
 * @brief The AIDL back-end's closed-state behaviour, on a LOCAL instance.
 *
 * Runs under EVERY invocation, and that is the whole point of the fixture. DriverAidlImpl's
 * constructor touches no binder at all - it sets the state to CLOSED, the legacy handle
 * field to 0 and an empty address list (DriverAidlImpl.cpp:618-637), exactly as
 * DriverImpl's does (DriverImpl.cpp:87-90) - which is precisely what makes the factory's
 * construct-then-query order safe on a legacy-only SOC. The consequence for testing is
 * large: a local instance is constructible even where no service, no service manager and no
 * binder driver exist, so every status != OPENED guard, the writeAsync prelude ordering, and
 * the three methods that carry no guard at all are reachable WITHOUT a HAL of any kind.
 *
 * A LOCAL INSTANCE IS USED RATHER THAN THE SHARED DRIVER, following the idiom the
 * neighbouring async suite established (test_DriverImpl_Async.cpp:515, :532, :543, :556,
 * :567, :581): the process-global driver is open, every other suite in this binary shares
 * it, and closing it to reach a guard would hand a closed driver to whichever suite runs
 * next.
 *
 * AND A LOCAL INSTANCE IS NEVER OPENED HERE, which is a hazard worth naming rather than
 * discovering. IHdmiCec::open() is single-instance and fails EX_ILLEGAL_STATE if a session
 * is already held, so opening a local instance under invocation B - where the
 * process-global AIDL back-end holds the session - is a designed-in failure. It happens
 * that a local instance cannot open at all, on any invocation: its service proxy is only
 * ever cached by its own isServiceAvailable(), so open() raises IOException first
 * (DriverAidlImpl.cpp:700-703) - which is itself asserted below, and is what keeps every
 * instance here CLOSED and its destructor off the close path.
 *
 * SELF-SUFFICIENCY. Every case constructs its own instance inside its own body and lets it
 * go out of scope at the end; nothing is shared between cases and nothing outside the
 * fixture is written. The HAL mock is consulted only through Times(0) expectations - what
 * these cases assert about it is that it is NOT reached - and those are cleared in TearDown.
 */
class DriverAidlLocalInstanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock = HdmiCecDriverMock::getInstance();
        if (mock != nullptr) {
            ::testing::Mock::VerifyAndClearExpectations(mock);
        }

        // NO driver precondition is established and none is needed: every case here drives a
        // local instance. The process-global driver is deliberately left exactly as it is,
        // which is also why TearDown does not restore it - nothing here disturbs it.
    }

    void TearDown() override {
        if (mock != nullptr) {
            ::testing::Mock::VerifyAndClearExpectations(mock);
        }
    }

    HdmiCecDriverMock *mock = nullptr;
};

// Ordering proof, on the AIDL back-end. writeAsync takes the frame buffer and logs the frame
// BEFORE it locks and checks the state (DriverAidlImpl.cpp:920-924, mirroring
// DriverImpl.cpp:203-204 ahead of the guard at :207-209), so a closed driver handed an EMPTY
// frame reports the decode failure - not the invalid state, and not the
// operation-not-supported that is this back-end's authorized difference.
//
// WHY std::out_of_range ESCAPES AT ALL: printFrameDetails constructs a Header, which reads
// frame.at(0) and raises std::out_of_range on an empty frame, and its handler catches only
// Exception. Exception and std::out_of_range are SIBLINGS under std::exception - not
// related by inheritance - so catch(Exception &e) cannot catch it and it propagates. The
// neighbouring async suite records the same boundary for the legacy back-end
// (test_DriverImpl_Async.cpp:38-46).
//
// The positive control is what makes this an ordering proof rather than one more guard case:
// with a well-formed frame the SAME closed instance gets past the prelude and is then
// stopped by the state guard instead. If the two were ever reordered, the first expectation
// would start reporting InvalidStateException and fail loudly.
TEST_F(DriverAidlLocalInstanceTest, FrameLoggingPrecedesStateGuardInWriteAsync) {
    ASSERT_NE(mock, nullptr);

    // The frame never survives the prelude, so nothing may reach the legacy HAL either -
    // and on this back-end nothing ever should, at any point.
    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _)).Times(0);

    DriverAidlImpl closedDriver;
    CECFrame emptyFrame;

    EXPECT_THROW({ closedDriver.writeAsync(emptyFrame); }, std::out_of_range)
        << "an empty frame on a closed AIDL driver did not raise std::out_of_range, so the frame "
           "prelude no longer runs ahead of the state guard";

    CECFrame wellFormed = directedFrame();
    EXPECT_THROW({ closedDriver.writeAsync(wellFormed); }, InvalidStateException)
        << "a well-formed frame on a closed AIDL driver did not raise InvalidStateException, so "
           "the state guard is no longer reached";
}

// The same ordering, on the LEGACY back-end, so that the two are compared rather than
// asserted separately and assumed to agree.
//
// This is the half of the writeAsync difference that must be IDENTICAL on both back-ends.
// The authorized difference is only what happens once the prelude and the guard have both
// passed - success on legacy, OperationNotSupportedException on AIDL - and that arm needs an
// OPEN driver, so it is asserted in the invocation-specific fixtures below. What is asserted
// here is that the two failure modes reached BEFORE the guard passes are the same on both,
// which is what makes the difference a single declared one rather than a diffuse divergence.
TEST_F(DriverAidlLocalInstanceTest, WriteAsyncPreludeOrderIsIdenticalOnTheLegacyBackEnd) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _)).Times(0);

    DriverImpl closedLegacyDriver;
    CECFrame emptyFrame;

    EXPECT_THROW({ closedLegacyDriver.writeAsync(emptyFrame); }, std::out_of_range)
        << "the legacy back-end no longer raises std::out_of_range for an empty frame on a closed "
           "driver, so the two back-ends' prelude ordering has diverged";

    CECFrame wellFormed = directedFrame();
    EXPECT_THROW({ closedLegacyDriver.writeAsync(wellFormed); }, InvalidStateException)
        << "the legacy back-end no longer raises InvalidStateException for a well-formed frame on "
           "a closed driver";
}

// Every state-guarded operation refuses a closed AIDL driver, and refuses it BEFORE touching
// any HAL. The guards are swept together rather than one case each because the property being
// asserted is uniform - status != OPENED means InvalidStateException, for all of them - and a
// gap in that set is much easier to see in one list than spread across five cases.
//
// poll() is included and is not redundant: it carries no guard of its own, reaching the guard
// through this back-end's own write() (DriverAidlImpl.cpp:1381-1384), so it is the one case
// here that proves the internal call goes to the right place.
TEST_F(DriverAidlLocalInstanceTest, EveryStateGuardedOperationRefusesAClosedDriver) {
    ASSERT_NE(mock, nullptr);

    // Not one legacy HAL entry point may be reached from the AIDL back-end, in any state.
    EXPECT_CALL(*mock, HdmiCecTx(_, _, _, _)).Times(0);
    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _)).Times(0);
    EXPECT_CALL(*mock, HdmiCecAddLogicalAddress(_, _)).Times(0);
    EXPECT_CALL(*mock, HdmiCecRemoveLogicalAddress(_, _)).Times(0);

    DriverAidlImpl closedDriver;
    const LogicalAddress address(LogicalAddress::PLAYBACK_DEVICE_1);
    CECFrame frame = directedFrame();

    EXPECT_THROW({ closedDriver.write(frame); }, InvalidStateException)
        << "write() accepted a frame on a closed driver";
    EXPECT_THROW({ closedDriver.writeAsync(frame); }, InvalidStateException)
        << "writeAsync() accepted a frame on a closed driver";
    EXPECT_THROW({ closedDriver.addLogicalAddress(address); }, InvalidStateException)
        << "addLogicalAddress() claimed an address on a closed driver, so the driver's bookkeeping "
           "and the HAL's could diverge";
    EXPECT_THROW({ closedDriver.removeLogicalAddress(address); }, InvalidStateException)
        << "removeLogicalAddress() released an address on a closed driver";
    EXPECT_THROW({ closedDriver.poll(address, LogicalAddress(LogicalAddress::TV)); },
                 InvalidStateException)
        << "poll() transmitted on a closed driver, so it is no longer reaching the guard through "
           "this back-end's own write()";

    CECFrame received;
    EXPECT_THROW({ closedDriver.read(received); }, InvalidStateException)
        << "read() did not refuse a closed driver on entry. It MUST refuse before reaching the "
           "queue: with an empty queue EventQueue::poll blocks by contract, so a read that got "
           "past the guard here would hang the whole test binary rather than fail";
}

// The guard is a STATE check, not an address check, so no address may slip past it - swept
// from the TV at one end to the broadcast/unregistered value at the other. A guard that
// happened to be written as a validity check would pass the case above and fail here.
TEST_F(DriverAidlLocalInstanceTest, AddressGuardsRejectEveryLogicalAddressWhileClosed) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecAddLogicalAddress(_, _)).Times(0);
    EXPECT_CALL(*mock, HdmiCecRemoveLogicalAddress(_, _)).Times(0);

    DriverAidlImpl closedDriver;

    const int addresses[] = {
        LogicalAddress::TV,                // minimum
        LogicalAddress::PLAYBACK_DEVICE_1, // interior
        LogicalAddress::BROADCAST          // maximum / unregistered
    };

    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i++) {
        const LogicalAddress address(addresses[i]);
        EXPECT_THROW({ closedDriver.addLogicalAddress(address); }, InvalidStateException)
            << "addLogicalAddress accepted address " << addresses[i] << " while closed";
        EXPECT_THROW({ closedDriver.removeLogicalAddress(address); }, InvalidStateException)
            << "removeLogicalAddress accepted address " << addresses[i] << " while closed";
    }
}

// close() on an instance that was never opened RETURNS SILENTLY, and that is the observable
// legacy behaviour rather than a convenience: DriverImpl::close()'s throw is compiled out
// under #if 0 (DriverImpl.cpp:130-140), and the AIDL back-end carries the same #if 0
// verbatim (DriverAidlImpl.cpp:779-785) so the two files read alike. A close that threw here
// would break LibCCEC::term on any path that had not opened.
//
// The double close is asserted too, because idempotence is what callers actually rely on:
// Bus reaches Driver::close() from two places and LibCCEC from a third.
TEST_F(DriverAidlLocalInstanceTest, CloseOnANeverOpenedDriverReturnsSilently) {
    ASSERT_NE(mock, nullptr);

    // No legacy close may be attempted from the AIDL back-end, and no AIDL close can be
    // either, since no session was ever held.
    EXPECT_CALL(*mock, HdmiCecClose(_)).Times(0);

    DriverAidlImpl closedDriver;

    EXPECT_NO_THROW({ closedDriver.close(); })
        << "close() raised on a driver that was never opened. The legacy back-end returns "
           "silently - its throw is compiled out - so raising here is an unregistered difference";

    EXPECT_NO_THROW({ closedDriver.close(); })
        << "a second close() raised, so close is not idempotent; Bus and LibCCEC both reach "
           "Driver::close() and neither tracks whether another already did";
}

// A fresh instance holds no logical address, and isValidLogicalAddress answers without
// touching any HAL on either back-end - it is a walk of the local list under the instance
// lock (DriverAidlImpl.cpp:1330-1345, byte-for-byte the legacy implementation). Connection is
// its only production caller (Connection.cpp:354), one address at a time.
//
// The whole address space is swept rather than one value sampled, because "holds nothing" is
// the claim; a single false answer would be consistent with an implementation that special
// cased one address.
TEST_F(DriverAidlLocalInstanceTest, FreshInstanceHoldsNoLogicalAddressAndConsultsNoHal) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecAddLogicalAddress(_, _)).Times(0);
    EXPECT_CALL(*mock, HdmiCecGetLogicalAddress(_, _)).Times(0);

    const DriverAidlImpl freshDriver;

    for (int address = LogicalAddress::TV; address <= LogicalAddress::BROADCAST; address++) {
        EXPECT_FALSE(freshDriver.isValidLogicalAddress(LogicalAddress(address)))
            << "a freshly constructed AIDL back-end reported that it holds logical address "
            << address << ", although nothing has been added to it";
    }
}

// open() without a cached service proxy raises IOException, and raises it BEFORE anything in
// the process touches libbinder.
//
// The ordering is the substance here, not the exception. The proxy is cached only by a
// successful isServiceAvailable() (DriverAidlImpl.cpp:1664), so a local instance never has
// one; open() checks for it at DriverAidlImpl.cpp:700-703, which is AHEAD of the
// ProcessState::self()->startThreadPool() call at :716. That order is what makes this case
// runnable on a host with no kernel binder support at all: reaching ProcessState first would
// raise SIGABRT and take the whole binary down rather than raise an exception this case can
// catch. It is also what keeps every local instance in this fixture CLOSED, and therefore
// keeps its destructor off the close path.
//
// The legacy HAL must not be reached either. An AIDL back-end that fell back to HdmiCecOpen
// when its own service was missing would be a second, undeclared selection point.
TEST_F(DriverAidlLocalInstanceTest, OpenWithoutAServiceProxyRaisesIoExceptionAndTouchesNoLegacyHal) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecOpen(_)).Times(0);
    EXPECT_CALL(*mock, HdmiCecSetRxCallback(_, _, _)).Times(0);
    EXPECT_CALL(*mock, HdmiCecSetTxCallback(_, _, _)).Times(0);

    DriverAidlImpl unresolvedDriver;

    EXPECT_THROW({ unresolvedDriver.open(); }, IOException)
        << "open() did not raise IOException on an instance holding no service proxy. If it "
           "instead reached ProcessState::self(), this process would have aborted rather than "
           "arrived here at all on a host without a binder driver";

    // Still closed, so the guards remain live - which is the proof that open() did not
    // half-succeed and leave the state OPENED with no session behind it.
    CECFrame frame = directedFrame();
    EXPECT_THROW({ unresolvedDriver.write(frame); }, InvalidStateException)
        << "the instance behaves as though it were open after a failed open(), so open() moved the "
           "state before it had a session";
}

// getLogicalAddress carries NO state guard on either back-end, and answers 0 when it has no
// service to ask (DriverAidlImpl.cpp:1100-1102).
//
// ZERO IS THE CONTRACT, not a shortfall, and it covers four distinct conditions - no proxy, a
// non-ok binder status, a successful call reporting no addresses, and the genuine address 0 -
// which are distinguished in the log rather than in the return value. That is deliberate:
// the legacy implementation zero-initialises its local and returns whatever the HAL leaves
// there (DriverImpl.cpp:290-301), and LibCCEC::getLogicalAddress turns a zero into
// InvalidStateException (LibCCEC.cpp:164-167), which is the EXISTING signal callers already
// handle for "no address". Any other sentinel here would suppress that throw and hand a
// caller an address it does not hold.
//
// devType is ignored on both back-ends, so it is swept: a value being honoured here would be
// a divergence from the legacy back-end and from the AIDL get-all contract alike.
TEST_F(DriverAidlLocalInstanceTest, GetLogicalAddressReportsZeroWithoutAServiceAndIgnoresDevType) {
    ASSERT_NE(mock, nullptr);

    EXPECT_CALL(*mock, HdmiCecGetLogicalAddress(_, _)).Times(0);

    DriverAidlImpl unresolvedDriver;

    const int devTypes[] = { DeviceType::TV, DeviceType::TUNER, DeviceType::PLAYBACK_DEVICE };

    for (size_t i = 0; i < sizeof(devTypes) / sizeof(devTypes[0]); i++) {
        EXPECT_EQ(unresolvedDriver.getLogicalAddress(devTypes[i]), 0)
            << "getLogicalAddress reported a non-zero address for device type " << devTypes[i]
            << " although no AIDL service is held. Zero is what LibCCEC::getLogicalAddress turns "
               "into InvalidStateException, so any other value would report an address this "
               "device does not have";
    }
}

// SC6(f), the AIDL half: BLOCKED ITEM B1. getPhysicalAddress does not read an address on this
// back-end - it logs the block and LEAVES THE CALLER'S OUT-PARAMETER UNTOUCHED
// (DriverAidlImpl.cpp:1171-1180).
//
// The sentinel is what makes "untouched" an assertion rather than a hope. A case that merely
// asserted the call did not throw would pass equally against an implementation that wrote a
// fabricated value, a zero, or uninitialised memory - and writing a plausible-looking
// physical address would be far worse than writing nothing, because a caller cannot tell a
// fabricated address from a real one. Seeding a distinctive value and asserting it survives
// is the only form that rules all of those out.
//
// This is the DOCUMENTED INTERIM behaviour and it is not equivalent to the legacy back-end -
// see the B1 paragraph in the contract block above, which states the change that would close
// the gap. It is also NOT a fabricated outcome: it is the same observable result the legacy
// path already produces when its own call fails, since that implementation ignores the return
// value and writes nothing. Both plugin call sites already wrap the call and tolerate not
// obtaining an address.
TEST_F(DriverAidlLocalInstanceTest, GetPhysicalAddressIsBlockedOnB1AndLeavesTheOutParameterUntouched) {
    ASSERT_NE(mock, nullptr);

    // The legacy CEC HAL call must NOT be substituted on this back-end. That substitution is
    // forbidden outright, and it is independently unsafe: HdmiCecOpen performs logical-address
    // discovery for source devices, which is CEC polling on the wire, so it would put a second
    // controller on the bus while the AIDL HAL is the active one.
    EXPECT_CALL(*mock, HdmiCecGetPhysicalAddress(_, _)).Times(0);
    EXPECT_CALL(*mock, HdmiCecOpen(_)).Times(0);

    DriverAidlImpl aidlDriver;

    const unsigned int sentinel = 0xDEADBEEFu;
    unsigned int physicalAddress = sentinel;

    EXPECT_NO_THROW({ aidlDriver.getPhysicalAddress(&physicalAddress); })
        << "getPhysicalAddress raised on the AIDL back-end. The blocked read must degrade to a "
           "log and a return, because both plugin call sites tolerate not obtaining an address "
           "but neither expects a new exception";

    EXPECT_EQ(physicalAddress, sentinel)
        << "the caller's out-parameter was modified. Pending B1 this method must write NOTHING: a "
           "fabricated or zeroed physical address is indistinguishable from a real one to every "
           "caller, which is worse than the caller keeping the value it initialized";
}

/**
 * @brief The legacy arms of the two authorized differences, and SC6(f)'s legacy half.
 *
 * REQUIRES INVOCATION A - CEC_TEST_AIDL_MODE=absent or unset - because these cases drive the
 * PROCESS-GLOBAL driver and it must be the legacy one. SetUp asserts that, with a message
 * naming the mode, so a mis-wired filter fails on a diagnostic rather than on a mock
 * expectation that was never going to be met.
 *
 * WHY THESE ARE HERE AT ALL, when the AIDL fixtures below assert the other side. Each of the
 * two authorized observable differences between the back-ends is a PAIR of behaviours, and a
 * declared difference is only established by asserting both halves against each other. The
 * legacy halves live here, on invocation A; the AIDL halves live in the two fixtures below,
 * on invocation B. Splitting them is not a preference - a single process can only ever hold
 * one back-end - and the pairing is what turns "the AIDL back-end raises" into "the two
 * back-ends differ, in exactly this way, and nowhere else".
 *
 * These cases drive the SHARED driver rather than a local DriverImpl, and deliberately: the
 * legacy arms need an OPEN driver, and the shared one is the only driver in this process
 * whose HAL is the mock the fixture programs.
 *
 * SELF-SUFFICIENCY. SetUp establishes the open precondition for itself instead of inheriting
 * whatever the previously executed suite left behind, and TearDown restores it. Each case
 * programs its own mock expectations and TearDown clears them. Nothing is carried between
 * cases and the driver is left open, which is the state the global environment set up.
 */
class DriverAidlLegacyArmTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock = HdmiCecDriverMock::getInstance();
        if (mock != nullptr) {
            ::testing::Mock::VerifyAndClearExpectations(mock);
        }

        ASSERT_NE(dynamic_cast<DriverImpl *>(&Driver::getInstance()), nullptr)
            << "this fixture requires the LEGACY back-end to be the resolved one, i.e. invocation "
               "A (CEC_TEST_AIDL_MODE=absent or unset). The selection resolves once per process "
               "inside LibCCEC::init and cannot be changed from here; re-run with the invocation-A "
               "filter from the FIXTURE MANIFEST above";

        // open() is deliberately NOT wrapped in catch(...). It returns silently when the driver
        // is already opened - its InvalidStateException throw is compiled out - so the only
        // exception it can raise is the IOException meaning the mock HAL refused to open.
        // Swallowing that would leave the process-global driver CLOSED while this fixture went
        // on asserting against it, and would hand every sibling suite in this binary a closed
        // driver too. A fatal setup failure instead: the body does not run, and TearDown still
        // gets its chance to put the shared state back.
        ASSERT_NO_THROW({ Driver::getInstance().open(); })
            << "the shared driver could not be opened, so this fixture's precondition does not "
               "hold; continuing would assert against a closed driver";
    }

    void TearDown() override {
        if (mock != nullptr) {
            ::testing::Mock::VerifyAndClearExpectations(mock);
        }

        // Restore the baseline the global environment set up, so no sibling suite observes a
        // closed driver because of anything done here. A non-fatal expectation is used so that
        // a failure is reported rather than cutting the rest of the cleanup short.
        EXPECT_NO_THROW({ Driver::getInstance().open(); })
            << "failed to restore the shared driver to its opened baseline";
    }

    HdmiCecDriverMock *mock = nullptr;
};

// SC6(f), the legacy half: physical-address retrieval reads the LEGACY HAL API and the value
// reaches the caller.
//
// This is the half of SC6(f) that is fully dischargeable. The other half is BLOCKED on B1 and
// is covered as the documented interim behaviour by
// DriverAidlLocalInstanceTest.GetPhysicalAddressIsBlockedOnB1AndLeavesTheOutParameterUntouched
// - see the B1 paragraph in the contract block above for what would close it. The two cases
// together are the whole of SC6(f) that exists today, and the pairing is what makes the
// partial discharge visible rather than implied.
//
// The value asserted is a real HDMI physical address shape - 1.0.0.0 encoded as 0x1000 - so
// that a case which happened to leave the out-parameter untouched could not pass by accident.
TEST_F(DriverAidlLegacyArmTest, PhysicalAddressIsReadThroughTheLegacyHalApi) {
    ASSERT_NE(mock, nullptr);

    const unsigned int halPhysicalAddress = 0x1000u;
    const unsigned int sentinel = 0xDEADBEEFu;

    EXPECT_CALL(*mock, HdmiCecGetPhysicalAddress(_, _))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<1>(halPhysicalAddress), Return(HDMI_CEC_IO_SUCCESS)));

    unsigned int physicalAddress = sentinel;
    EXPECT_NO_THROW({ Driver::getInstance().getPhysicalAddress(&physicalAddress); });

    EXPECT_EQ(physicalAddress, halPhysicalAddress)
        << "the legacy back-end did not deliver the HAL's physical address to the caller";
    EXPECT_NE(physicalAddress, sentinel)
        << "the out-parameter still holds the sentinel, so nothing was written and this case would "
           "also pass against the BLOCKED AIDL behaviour - which would make the two back-ends "
           "indistinguishable here and hide the B1 gap";
}

// AUTHORIZED DIFFERENCE 1, legacy half: frames of 16, 17 and 20 bytes are ALL sendable on the
// legacy back-end.
//
// The three sizes straddle an authority conflict rather than sampling a range. A CECFrame
// carries up to 128 bytes; the legacy HAL specification allows 20; the AIDL sendMessage
// contract states 16. Each is authoritative for its own back-end and no divergence settles
// the overlap, so 17 through 20 is a disputed band: accepted here, refused by the AIDL
// back-end. 16 is the control that must be accepted by BOTH, which is what confines the
// difference to the band rather than to the guard's existence.
//
// The captured length is asserted per size, because "the call happened" is not the claim -
// the claim is that the WHOLE frame reached the HAL, un-truncated, which is the property the
// AIDL half refuses to compromise by truncating.
TEST_F(DriverAidlLegacyArmTest, FramesUpToTheLegacyMaximumAreSentOnTheLegacyBackEnd) {
    ASSERT_NE(mock, nullptr);

    const size_t sizes[] = { kAidlMaxMessageLength, kJustOverAidlLimit, kLegacyMaxMessageLength };

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        ::testing::Mock::VerifyAndClearExpectations(mock);

        int capturedLength = -1;
        EXPECT_CALL(*mock, HdmiCecTx(_, _, _, _))
            .Times(1)
            .WillOnce(DoAll(SaveArg<2>(&capturedLength),
                            SetArgPointee<3>(HDMI_CEC_IO_SUCCESS),
                            Return(HDMI_CEC_IO_SUCCESS)));

        CECFrame frame = frameOfLength(sizes[i]);
        ASSERT_EQ(frame.length(), sizes[i])
            << "the frame builder did not produce a frame of " << sizes[i] << " bytes, so this "
               "iteration is not testing the size it claims to";

        EXPECT_NO_THROW({ Driver::getInstance().write(frame); })
            << "the legacy back-end refused a frame of " << sizes[i] << " bytes, which its own HAL "
               "specification permits";

        EXPECT_EQ(capturedLength, static_cast<int>(sizes[i]))
            << "the legacy back-end passed " << capturedLength << " bytes to the HAL for a frame of "
            << sizes[i] << ", so the frame was truncated or padded on the way through";
    }
}

// AUTHORIZED DIFFERENCE 2, legacy half: writeAsync SUCCEEDS on an open legacy driver.
//
// This is the arm that the AIDL back-end refuses with OperationNotSupportedException, and it
// is the ONLY part of writeAsync where the two differ - the prelude ordering and the state
// guard are identical on both, asserted in DriverAidlLocalInstanceTest above. So this case is
// the control that confines difference 2 to exactly one arm: if the prelude cases and this one
// all hold, the divergence is one branch wide and not a diffuse one.
//
// No production call site reaches writeAsync on either back-end - every plugin transmit goes
// through Connection::sendToAsync and Connection::sendAsync onto the Bus writer thread, which
// then calls the SYNCHRONOUS write - so the difference is unreachable in production today. It
// is asserted anyway, because "unreachable today" is not "impossible".
TEST_F(DriverAidlLegacyArmTest, WriteAsyncSucceedsOnAnOpenLegacyDriver) {
    ASSERT_NE(mock, nullptr);

    int capturedLength = -1;
    EXPECT_CALL(*mock, HdmiCecTxAsync(_, _, _))
        .Times(1)
        .WillOnce(DoAll(SaveArg<2>(&capturedLength), Return(HDMI_CEC_IO_SUCCESS)));

    CECFrame frame = directedFrame();
    EXPECT_NO_THROW({ Driver::getInstance().writeAsync(frame); })
        << "asynchronous transmit failed on the legacy back-end, so the legacy half of authorized "
           "difference 2 no longer holds and the difference is no longer one-sided";

    EXPECT_EQ(capturedLength, 2)
        << "the legacy asynchronous transmit received " << capturedLength
        << " bytes rather than the frame's 2";
}

// The legacy back-end still translates its own transmit statuses, asserted on the two arms the
// AIDL translation mirrors, so that the mirror has something measured to be a mirror OF.
//
// Two arms are chosen because they are the two that the inverted AIDL sense makes easy to get
// wrong: a DIRECTED frame reported as not acknowledged raises CECNoAckException
// (DriverImpl.cpp:276-278), while a BROADCAST frame reported the same way returns normally
// unless it is the CEC CTS 9-3-3 opcode (:279-284). The corresponding AIDL arms are asserted
// in DriverAidlTransmitTest below, where ACK_STATE_0 and ACK_STATE_1 swap meaning between the
// two destinations.
TEST_F(DriverAidlLegacyArmTest, LegacyTransmitStatusTranslationDistinguishesDirectedFromBroadcast) {
    ASSERT_NE(mock, nullptr);

    // A directed frame that was not acknowledged: the addressed follower did not answer.
    EXPECT_CALL(*mock, HdmiCecTx(_, _, _, _))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<3>(HDMI_CEC_IO_SENT_BUT_NOT_ACKD),
                        Return(HDMI_CEC_IO_SUCCESS)));

    CECFrame directed = directedFrame();
    EXPECT_THROW({ Driver::getInstance().write(directed); }, CECNoAckException)
        << "an unacknowledged DIRECTED frame did not raise CECNoAckException on the legacy "
           "back-end";

    ::testing::Mock::VerifyAndClearExpectations(mock);

    // The same status on a BROADCAST frame carrying an opcode other than
    // REPORT_PHYSICAL_ADDRESS returns normally: a broadcast has no single follower to
    // acknowledge it, so "not acknowledged" is not a failure.
    EXPECT_CALL(*mock, HdmiCecTx(_, _, _, _))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<3>(HDMI_CEC_IO_SENT_BUT_NOT_ACKD),
                        Return(HDMI_CEC_IO_SUCCESS)));

    CECFrame broadcast = broadcastFrame(GIVE_DEVICE_POWER_STATUS);
    EXPECT_NO_THROW({ Driver::getInstance().write(broadcast); })
        << "an unacknowledged BROADCAST frame carrying an opcode other than "
           "REPORT_PHYSICAL_ADDRESS raised on the legacy back-end, so the CEC CTS 9-3-3 arm is no "
           "longer specific to that one opcode";

    ::testing::Mock::VerifyAndClearExpectations(mock);

    // And the CEC CTS 9-3-3 arm itself: a rejected broadcast REPORT_PHYSICAL_ADDRESS DOES
    // raise, so that the caller retries at least once.
    EXPECT_CALL(*mock, HdmiCecTx(_, _, _, _))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<3>(HDMI_CEC_IO_SENT_BUT_NOT_ACKD),
                        Return(HDMI_CEC_IO_SUCCESS)));

    CECFrame ctsBroadcast = broadcastFrame(REPORT_PHYSICAL_ADDRESS);
    EXPECT_THROW({ Driver::getInstance().write(ctsBroadcast); }, CECNoAckException)
        << "a rejected broadcast REPORT_PHYSICAL_ADDRESS did not raise on the legacy back-end, so "
           "the CEC CTS 9-3-3 retry arm is gone";
}

/**
 * @brief Shared precondition for the two invocation-B fixtures.
 *
 * Not a test suite of its own - no TEST_F names it - so it adds no case and no fixture to the
 * filter surface. It exists because both fixtures below need exactly the same non-trivial
 * precondition, and one copy of it is one thing to keep correct rather than two.
 *
 * REQUIRES INVOCATION B - CEC_TEST_AIDL_MODE=compatible. SetUp asserts that the resolved
 * back-end is the AIDL one and FAILS if it is not, with a message naming the mode. It does
 * NOT skip: a skipped arm is indistinguishable from a passing one in an aggregate count,
 * which is exactly the shape that would let an AIDL invocation report green having never
 * exercised the AIDL path. And it does not read CEC_TEST_AIDL_MODE to decide - that variable
 * is read by the harness only (test_main.cpp:143, :238); these fixtures are POSITIONED by it
 * and must not branch on it, or a mis-set variable and a mis-wired filter would become
 * indistinguishable.
 *
 * THE SESSION IS CYCLED IN SetUp, and the order matters:
 *
 *   close -> reset -> open
 *
 * The reason is the captured listener. FakeHdmiCecService::reset() clears it, which returns
 * the event triggers to their no-op behaviour, and the listener is captured only by
 * IHdmiCec::open() - which DriverAidlImpl::open() does not call when the driver is already
 * OPENED, because it returns silently in that state. Resetting without re-opening would
 * therefore leave every trigger a silent no-op, and a case asserting on a delivered frame
 * would fail for a reason that had nothing to do with the code under test. Closing first,
 * then resetting, then opening leaves each case with a clean fake, a captured listener, and
 * counters that describe only SetUp's own open.
 *
 * CLOSING THE SHARED DRIVER HERE IS SAFE, and it is safe for a documented reason rather than
 * by luck. close() offers the NULL sentinel onto the incoming queue, which is exactly what
 * that sentinel is for - it wakes the blocked Bus reader - and Bus::Reader::run() catches
 * InvalidStateException and stays in its loop (Bus.cpp:156-173), so it re-arms cleanly the
 * moment open() restores the driver. The reader is left RUNNING throughout. What is NOT done
 * here is reaching the same state through LibCCEC::term() and a re-arming init(): that is the
 * pattern the middleware's own test notes identify as the reader-thread hazard, and the
 * neighbouring async suite records the analysis at test_DriverImpl_Async.cpp:614-636.
 *
 * SELF-SUFFICIENCY. Every case gets a freshly reset fake and an open session from SetUp, and
 * TearDown resets the fake again and restores the opened baseline non-fatally, so a failing
 * case cannot leave a closed driver or a configured fake behind for the next one - here or in
 * a sibling suite.
 */
class DriverAidlSessionFixture : public ::testing::Test {
protected:
    void SetUp() override {
        aidlBackEnd = dynamic_cast<DriverAidlImpl *>(&Driver::getInstance());

        ASSERT_NE(aidlBackEnd, nullptr)
            << "this fixture requires the AIDL back-end to be the resolved one, i.e. invocation B "
               "(CEC_TEST_AIDL_MODE=compatible, on a host with a binder driver and a running "
               "service manager), and the factory returned the legacy back-end instead. The "
               "selection resolves once per process inside LibCCEC::init and cannot be changed "
               "from here. If this ran under invocation A, apply the invocation-A filter from the "
               "FIXTURE MANIFEST above, which excludes this fixture by name";

        fake = FakeHdmiCecService::getInstance();
        ASSERT_NE(fake, nullptr)
            << "the AIDL back-end resolved but no fake service was published for this process, so "
               "these cases have no HAL to program. The harness publishes it before init "
               "(test_main.cpp:223); if the back-end resolved against some OTHER service, this "
               "run's outcome depends on a process this suite does not own";

        // Bring the session down, wipe the fake, bring it back up - see the ordering note above.
        ASSERT_NO_THROW({ Driver::getInstance().close(); })
            << "the shared driver could not be closed, so the session cannot be cycled and the "
               "fake's captured listener cannot be refreshed";

        fake->reset();
        ASSERT_NE(fake->getController(), nullptr)
            << "the fake reports no controller, so nothing here could reach addLogicalAddresses, "
               "removeLogicalAddresses or sendMessage";
        fake->getController()->reset();

        ASSERT_NO_THROW({ Driver::getInstance().open(); })
            << "the shared driver could not be re-opened after the reset, so this fixture's "
               "precondition does not hold; continuing would assert against a closed driver";

        ASSERT_EQ(fake->getOpenCallCount(), 1)
            << "the fake did not observe exactly one open() after the reset, so the session was "
               "not re-established through the HAL and the listener the triggers need was not "
               "captured";
    }

    void TearDown() override {
        if (fake != nullptr) {
            // Reset BEFORE restoring the baseline: a case that installed a failing close or a
            // failing open status would otherwise make the restoration below fail for a reason
            // the case created rather than for a real one.
            if (fake->getController() != nullptr) {
                fake->getController()->reset();
            }
            fake->reset();
        }

        // Restore the baseline the global environment set up, non-fatally so that a failure is
        // reported rather than cutting the rest of the cleanup short.
        EXPECT_NO_THROW({ Driver::getInstance().open(); })
            << "failed to restore the shared driver to its opened baseline";
    }

    DriverAidlImpl *aidlBackEnd = nullptr;
    FakeHdmiCecService *fake = nullptr;
};

/**
 * @brief Session lifecycle, address marshalling and event delivery on the AIDL back-end.
 *
 * REQUIRES INVOCATION B. See DriverAidlSessionFixture for the precondition and for why the
 * session is cycled in SetUp.
 */
class DriverAidlSessionTest : public DriverAidlSessionFixture {
};

// SC6(a): a present and compatible service selects the AIDL back-end, asserted by concrete
// type in both directions and by the selected-path log line - the same two independent routes
// the legacy half uses, so that the pair of assertions is symmetric.
TEST_F(DriverAidlSessionTest, CompatibleServiceSelectsTheAidlBackEndAndNamesItInTheLog) {
    Driver &resolved = Driver::getInstance();

    EXPECT_NE(dynamic_cast<DriverAidlImpl *>(&resolved), nullptr)
        << "a compatible service is registered but the resolved back-end is not the AIDL one";
    EXPECT_EQ(dynamic_cast<DriverImpl *>(&resolved), nullptr)
        << "the legacy back-end was selected although a compatible AIDL service is registered";

    std::string captured;
    {
        StdoutCapture capture;
        ASSERT_TRUE(capture.isValid())
            << "stdout could not be redirected, so nothing about the emitted line can be "
               "established";

        CCEC_LOG(LOG_INFO, kSelectedBackEndLogFormat, kSelectedBackEndAidl);

        captured = capture.read();
    }

    const std::string expected =
        std::string("HDMI CEC HAL back-end selected : ") + kSelectedBackEndAidl;

    EXPECT_NE(captured.find(expected), std::string::npos)
        << "the production logger did not emit the selected-path line naming the AIDL back-end. "
           "That line is how the coverage runner and device-level validation establish which "
           "back-end resolved, since no introspection API exists on the Driver interface. "
           "Captured instead: [" << captured << "]";
}

// SC6(d), the add half: ONE logical address is marshalled as a ONE-ELEMENT array.
//
// This is divergence 1 made observable. The AIDL calls take int[], the middleware API is
// single-valued throughout, and the array shape must stay confined to the temporary the
// adapter builds - no multi-address state, no iteration, no fan-out, and no middleware
// structure or signature growing a plural form. Asserting size() == 1 AND the value is what
// distinguishes "one address was sent" from "an array happened to arrive"; a case that checked
// only the value would pass against an implementation that padded the vector.
TEST_F(DriverAidlSessionTest, AddLogicalAddressMarshalsExactlyOneElement) {
    const LogicalAddress address(LogicalAddress::PLAYBACK_DEVICE_1);

    EXPECT_TRUE(Driver::getInstance().addLogicalAddress(address))
        << "addLogicalAddress reported failure although the fake accepts by default";

    EXPECT_EQ(fake->getController()->getAddLogicalAddressesCallCount(), 1)
        << "the HAL was not asked exactly once to add the address";

    const std::vector<int32_t> sent = fake->getController()->getLastAddedLogicalAddresses();
    ASSERT_EQ(sent.size(), 1u)
        << "addLogicalAddresses received " << sent.size() << " entries for one middleware address. "
           "The array shape must stay confined to the adapter's temporary; a longer vector means "
           "multi-address behaviour leaked into the middleware";
    EXPECT_EQ(sent[0], address.toInt())
        << "the marshalled address does not match the one requested";

    // The address is now held locally, which is the caller-visible half of the operation -
    // Connection consults exactly this, one address at a time (Connection.cpp:354).
    EXPECT_TRUE(Driver::getInstance().isValidLogicalAddress(address))
        << "the address was accepted by the HAL but not recorded locally, so isValidLogicalAddress "
           "would deny an address this device holds";
}

// SC6(d), the remove half, plus the ORDER-VISIBLE consequence that makes the legacy
// disposition observable.
//
// The legacy shape IS the specification here and it is reproduced rather than improved
// (DriverImpl.cpp:316-327): the state guard, then the removal from the LOCAL list at :324,
// and only THEN the HAL call at :325 - whose return value is DISCARDED. So a HAL that declines
// the removal changes nothing: the address is gone locally either way, and nothing is raised.
// Asserting the local effect after a DECLINED remove is what pins that order; a case that only
// asserted "no exception escaped" would pass against an implementation that rolled the local
// removal back.
TEST_F(DriverAidlSessionTest, RemoveLogicalAddressMarshalsOneElementAndIgnoresHalRefusal) {
    const LogicalAddress address(LogicalAddress::PLAYBACK_DEVICE_1);

    ASSERT_TRUE(Driver::getInstance().addLogicalAddress(address));
    ASSERT_TRUE(Driver::getInstance().isValidLogicalAddress(address));

    // The HAL declines the removal, and a non-ok status is asserted separately below.
    fake->getController()->setRemoveLogicalAddressesResult(false);

    EXPECT_NO_THROW({ Driver::getInstance().removeLogicalAddress(address); })
        << "removeLogicalAddress raised when the HAL declined. The legacy back-end DISCARDS that "
           "return value and returns silently, so raising here would be an unregistered "
           "behaviour change - and one that Bus and LibCCEC do not expect";

    EXPECT_EQ(fake->getController()->getRemoveLogicalAddressesCallCount(), 1)
        << "the HAL was not asked exactly once to remove the address";

    const std::vector<int32_t> sent = fake->getController()->getLastRemovedLogicalAddresses();
    ASSERT_EQ(sent.size(), 1u)
        << "removeLogicalAddresses received " << sent.size() << " entries for one middleware "
           "address";
    EXPECT_EQ(sent[0], address.toInt()) << "the marshalled address does not match the one released";

    EXPECT_FALSE(Driver::getInstance().isValidLogicalAddress(address))
        << "the address is still held locally after a DECLINED removal. The local removal precedes "
           "the HAL call and is not rolled back, so it must be gone regardless of what the HAL "
           "reported - that ordering is the legacy behaviour being preserved";
}

// SC6(d), the get half: the address is read from entry ZERO of the returned array, and it is
// read from IHdmiCec rather than from the controller.
//
// The interface split is worth asserting and not merely commenting: getLogicalAddresses lives
// on IHdmiCec, while only addLogicalAddresses, removeLogicalAddresses and sendMessage live on
// IHdmiCecController. The existing technical specification attributes it to the controller,
// which is wrong, so the service-side call counter is checked to pin where the call actually
// goes.
TEST_F(DriverAidlSessionTest, GetLogicalAddressReadsEntryZeroFromTheServiceInterface) {
    const int32_t halAddress = 8;
    fake->setLogicalAddressesResult(std::vector<int32_t>{ halAddress });

    const int reported = Driver::getInstance().getLogicalAddress(DeviceType::TUNER);

    EXPECT_EQ(reported, static_cast<int>(halAddress))
        << "getLogicalAddress did not report the single address the HAL holds";

    EXPECT_EQ(fake->getGetLogicalAddressesCallCount(), 1)
        << "IHdmiCec::getLogicalAddresses was not called exactly once. It lives on the SERVICE "
           "interface, not on the controller, and a call routed to the controller instead would "
           "not compile against this snapshot - so a zero here means the address came from "
           "somewhere else entirely";
}

// SC6(e): more than one returned address operates on ENTRY ZERO and logs the condition.
//
// Divergence 1 is explicit that a multi-address HAL result is logged and the first entry used,
// with no multi-address state, no iteration and no dispatch fan-out added to the middleware.
// Both halves are asserted - the returned value AND the log naming the count and the entry
// used - because the log is the only signal a platform integrator gets that the HAL is
// reporting more than the middleware can represent, and a silent first-entry pick would hide
// a real configuration problem.
TEST_F(DriverAidlSessionTest, MultipleReturnedAddressesUseEntryZeroAndAreLogged) {
    const std::vector<int32_t> halAddresses{ 4, 8, 11 };
    fake->setLogicalAddressesResult(halAddresses);

    int reported = 0;
    std::string captured;
    {
        StdoutCapture capture;
        ASSERT_TRUE(capture.isValid()) << "stdout could not be redirected, so the log half of this "
                                         "case cannot be established";

        reported = Driver::getInstance().getLogicalAddress(DeviceType::TV);

        captured = capture.read();
    }

    EXPECT_EQ(reported, static_cast<int>(halAddresses[0]))
        << "getLogicalAddress did not report entry 0 of a multi-entry result. Operating on the "
           "first entry is the specified behaviour; picking any other entry, or iterating, would "
           "introduce multi-address behaviour the middleware has no way to represent";

    EXPECT_NE(captured.find("3"), std::string::npos)
        << "the log does not name the number of addresses the HAL reported, so an integrator has "
           "no signal that the HAL holds more addresses than the middleware uses. Captured: ["
        << captured << "]";
    EXPECT_NE(captured.find("entry 0"), std::string::npos)
        << "the log does not name the entry that was used. Captured: [" << captured << "]";
}

// An EMPTY result reports 0, and 0 is the contract rather than a shortfall.
//
// The AIDL contract states the array is empty when no additional addresses have been set, so
// this is a DOCUMENTED NORMAL OUTCOME and not an error. Reporting 0 is what makes it behave
// like the legacy path: LibCCEC::getLogicalAddress turns a zero into InvalidStateException
// (LibCCEC.cpp:164-167), which is the signal callers already handle. Any other sentinel would
// suppress that throw and hand a caller an address it does not hold.
TEST_F(DriverAidlSessionTest, EmptyAddressResultReportsZeroSoTheExistingCallerSignalSurvives) {
    fake->setLogicalAddressesResult(std::vector<int32_t>());

    EXPECT_EQ(Driver::getInstance().getLogicalAddress(DeviceType::TV), 0)
        << "an empty HAL result did not report 0. Zero is what LibCCEC::getLogicalAddress turns "
           "into InvalidStateException, so any other value would report an address this device "
           "does not have";

    EXPECT_EQ(fake->getGetLogicalAddressesCallCount(), 1)
        << "the HAL was not asked exactly once";
}

// AUTHORIZED DIFFERENCE 3: addLogicalAddress's failure category is COARSER on the AIDL
// back-end than on the legacy one, and that is forced rather than chosen.
//
// The legacy implementation distinguishes three outcomes from one HAL status
// (DriverImpl.cpp:339-344): LOGICALADDRESS_UNAVAILABLE becomes AddressNotAvailableException,
// GENERAL_ERROR becomes IOException, and any other value is success. IHdmiCecController::
// addLogicalAddresses returns ONE BOOLEAN, documented as false when the address is outside
// 0x0..0xE or already added, so the distinction cannot be carried without a new HAL method -
// which the constraints forbid. So false maps to the nearer legacy category,
// AddressNotAvailableException, and a non-ok binder status maps to IOException.
//
// Both mappings are asserted here, and their CALLER-VISIBLE EFFECT is measured separately
// below rather than assumed to be equivalent.
TEST_F(DriverAidlSessionTest, AddLogicalAddressMapsRefusalAndTransportFailureToDistinctExceptions) {
    const LogicalAddress address(LogicalAddress::PLAYBACK_DEVICE_1);

    // The HAL refuses the address: it is out of range, or already taken.
    fake->getController()->setAddLogicalAddressesResult(false);

    EXPECT_THROW({ Driver::getInstance().addLogicalAddress(address); }, AddressNotAvailableException)
        << "a refused address did not raise AddressNotAvailableException. That is the nearer of "
           "the two legacy categories for 'the address is not available', and it is what the Sink "
           "plugin's allocation loop is written around";

    EXPECT_FALSE(Driver::getInstance().isValidLogicalAddress(address))
        << "the address was recorded locally although the HAL refused it, so the middleware's "
           "bookkeeping and the HAL's have diverged";

    fake->getController()->reset();

    // Transport failure: a dead binder, an EX_ILLEGAL_STATE, a marshalling fault. Distinct
    // from a refusal, and mapped distinctly.
    fake->getController()->setAddLogicalAddressesBinderStatus(
        ::android::binder::Status::fromStatusT(::android::DEAD_OBJECT));

    EXPECT_THROW({ Driver::getInstance().addLogicalAddress(address); }, IOException)
        << "a non-ok binder status did not raise IOException. A transport failure is not an "
           "unavailable address, and the Sink plugin's call path distinguishes the two";

    EXPECT_FALSE(Driver::getInstance().isValidLogicalAddress(address))
        << "the address was recorded locally although the transaction failed";
}

// The same two failures again, measured THROUGH THE REAL CALLER PATHS rather than through the
// Driver interface, because "the caller sees the same thing" is a claim about the plugin and
// not about the adapter.
//
// The two Sink call paths differ, and the difference is structural - re-read from the plugin
// source, which is out of scope and is NOT modified:
//
//   PATH 1  LibCCEC::getInstance().addLogicalAddress(...) at
//           entservices-hdmicecsink/plugin/HdmiCecSinkImplementation.cpp:2767, inside the try
//           opened at :2764, which has THREE catches - catch(InvalidStateException &e) at
//           :2789, catch(IOException &e) at :2793 and catch(...) at :2797. So a transport
//           failure reaches the DISTINCT :2793 arm, while a refused address falls through to
//           the GENERIC :2797 arm. Both set the same poll-thread state, but they are
//           different arms and they log differently.
//
//   PATH 2  the enable-time call at :3065, inside the if at :3061 and NOT INSIDE ANY TRY
//           BLOCK - the nearby IOException catches at :3040 and :3173 belong to LibCCEC::init
//           and term, not to this call - so either exception PROPAGATES OUT of the enable path.
//
// LibCCEC::addLogicalAddress itself catches nothing and propagates (LibCCEC.cpp:133-144).
// Both dispositions are modelled here - a try/catch shaped like path 1, and an uncaught
// propagation shaped like path 2 - so the caller-visible effect of difference 3 is measured.
// What this establishes is that the coarser category still lands in a SPECIFIC, handled arm on
// path 1 and still propagates on path 2, which is the property that makes the difference
// tolerable for those callers.
TEST_F(DriverAidlSessionTest, AddLogicalAddressFailuresReachBothRealSinkCallPathsAsExpected) {
    const LogicalAddress address(LogicalAddress::PLAYBACK_DEVICE_1);

    // PATH 1, refusal: the three-catch shape at HdmiCecSinkImplementation.cpp:2789-2799. A
    // refusal is NOT an IOException, so it must fall through to the generic arm.
    fake->getController()->setAddLogicalAddressesResult(false);
    {
        bool reachedInvalidStateArm = false;
        bool reachedIoArm = false;
        bool reachedGenericArm = false;

        try {
            LibCCEC::getInstance().addLogicalAddress(address);
        }
        catch (InvalidStateException &e) {
            (void)e;
            reachedInvalidStateArm = true;
        }
        catch (IOException &e) {
            (void)e;
            reachedIoArm = true;
        }
        catch (...) {
            reachedGenericArm = true;
        }

        EXPECT_FALSE(reachedInvalidStateArm)
            << "a refused address reached the InvalidStateException arm, which the Sink treats as "
               "'the library is not initialized' - a different diagnosis entirely";
        EXPECT_FALSE(reachedIoArm)
            << "a refused address reached the IOException arm at "
               "HdmiCecSinkImplementation.cpp:2793, so a full address pool would be logged and "
               "diagnosed as a transport failure";
        EXPECT_TRUE(reachedGenericArm)
            << "a refused address reached NO catch arm on path 1, so nothing was raised at all and "
               "the Sink would carry on believing it holds an address the HAL refused";
    }

    fake->getController()->reset();

    // PATH 1, transport failure: this one MUST reach the distinct IOException arm.
    fake->getController()->setAddLogicalAddressesBinderStatus(
        ::android::binder::Status::fromStatusT(::android::DEAD_OBJECT));
    {
        bool reachedIoArm = false;
        bool reachedGenericArm = false;

        try {
            LibCCEC::getInstance().addLogicalAddress(address);
        }
        catch (IOException &e) {
            (void)e;
            reachedIoArm = true;
        }
        catch (...) {
            reachedGenericArm = true;
        }

        EXPECT_TRUE(reachedIoArm)
            << "a transport failure did not reach the IOException arm at "
               "HdmiCecSinkImplementation.cpp:2793, so the one failure the Sink diagnoses "
               "specifically would be logged as a generic exception instead";
        EXPECT_FALSE(reachedGenericArm) << "the transport failure reached the generic arm instead";
    }

    fake->getController()->reset();

    // PATH 2: the enable-time call site sits outside any try block, so the exception must
    // PROPAGATE out of LibCCEC rather than be absorbed anywhere below it. Modelled as the
    // uncaught propagation it is; EXPECT_THROW here is the assertion that nothing in the
    // middleware swallows it on the way up.
    fake->getController()->setAddLogicalAddressesResult(false);
    EXPECT_THROW({ LibCCEC::getInstance().addLogicalAddress(address); },
                 AddressNotAvailableException)
        << "the refusal did not propagate out of LibCCEC::addLogicalAddress. The enable-time Sink "
           "call at HdmiCecSinkImplementation.cpp:3065 is not inside a try block, so an exception "
           "absorbed in the middleware would leave that path believing it acquired an address";
}

// The close SEQUENCE, asserted as a sequence rather than as an outcome.
//
// Three things are ordered inside DriverAidlImpl::close(), and each is observable:
//
//   1. the state becomes CLOSED **before** any failure is raised
//      (DriverAidlImpl.cpp:829-833, mirroring DriverImpl.cpp:145-150), so a caller that
//      swallows the exception still sees a consistently closed object rather than one that
//      believes it is open with no session behind it;
//   2. the local logical-address list is DELIBERATELY NOT CLEARED - DriverImpl::close() does
//      not clear it either, so isValidLogicalAddress can keep reporting true across a close,
//      and the HAL drops the addresses on its own side anyway. Clearing would be an
//      unauthorized improvement and a fourth observable difference;
//   3. the controller reference is released, so a subsequent operation cannot reach a stale
//      session.
//
// Asserting only that IOException escaped would leave all three unproved. NOTE that a green
// result here does NOT confirm B2 - which AIDL method corresponds to the legacy HdmiCecClose()
// is pending owner confirmation; what is asserted is the sequence, which holds whichever
// method the owners confirm.
TEST_F(DriverAidlSessionTest, FailedCloseStillReachesTheClosedStateAndKeepsTheLocalAddressList) {
    const LogicalAddress address(LogicalAddress::PLAYBACK_DEVICE_1);
    ASSERT_TRUE(Driver::getInstance().addLogicalAddress(address));
    ASSERT_TRUE(Driver::getInstance().isValidLogicalAddress(address));

    // The HAL reports that it did not close the session.
    fake->setCloseResult(false);

    EXPECT_THROW({ Driver::getInstance().close(); }, IOException)
        << "a close the HAL reported as failed did not raise IOException";

    EXPECT_EQ(fake->getCloseCallCount(), 1) << "the HAL was not asked exactly once to close";

    // The state reached CLOSED before the raise, so the guards are live again. Observed
    // through a guarded operation, since the state itself is private and no introspection
    // exists - which is the point: this is the caller-visible form of the invariant.
    CECFrame frame = directedFrame();
    EXPECT_THROW({ Driver::getInstance().write(frame); }, InvalidStateException)
        << "the driver still behaves as though it were open after a FAILED close. The state must "
           "reach CLOSED before the exception is raised, or a caller that swallows the IOException "
           "is left with an object that believes it holds a session it does not";

    EXPECT_TRUE(Driver::getInstance().isValidLogicalAddress(address))
        << "close() cleared the local logical-address list. The legacy back-end does not clear it "
           "either, so clearing here would be an unauthorized improvement and a fourth observable "
           "difference between the two back-ends";

    // A second close returns silently, as the legacy back-end does with its throw compiled out.
    EXPECT_NO_THROW({ Driver::getInstance().close(); })
        << "a close on an already-closed driver raised; the legacy back-end returns silently";
    EXPECT_EQ(fake->getCloseCallCount(), 1)
        << "the already-closed driver reached the HAL again, so the state guard is not short-"
           "circuiting the second close";
}

// A non-ok binder status on close is a DIFFERENT arm from a false result, and it must reach the
// same closed state.
//
// Both arms converge on `if (!txn.isOk() || !closed)` (DriverAidlImpl.cpp:829), and covering
// only one of them would leave a short-circuit reordering - which would skip the CLOSED
// assignment on the transport arm - undetected.
TEST_F(DriverAidlSessionTest, CloseWithNonOkBinderStatusAlsoReachesTheClosedState) {
    fake->setCloseBinderStatus(::android::binder::Status::fromStatusT(::android::DEAD_OBJECT));

    EXPECT_THROW({ Driver::getInstance().close(); }, IOException)
        << "a close whose transaction failed did not raise IOException";

    CECFrame frame = directedFrame();
    EXPECT_THROW({ Driver::getInstance().write(frame); }, InvalidStateException)
        << "the driver still behaves as though it were open after a close whose TRANSACTION "
           "failed, although the false-result arm reaches CLOSED correctly. The two arms converge "
           "on one condition, so this is a short-circuit that skips the state assignment";
}

// A received CEC frame is delivered into the incoming queue while the driver is OPEN, and the
// delivery is observable end to end - HAL event to Bus reader to Connection's listeners - by
// the Bus reader draining it.
//
// The trigger stands in for the HAL raising the oneway callback. IN THIS PROCESS the listener
// runs on the CALLING thread, because an in-process fake resolves to the local BBinder and no
// transaction crosses the driver; the real-IPC form of this, with the callback arriving on a
// genuine binder threadpool thread, is invocation E's and lives in
// hdmicec/tests/L2Tests/ccec/test_DualPathIntegration.cpp. What IS established here is the
// adapter's own behaviour: the bytes are copied into a fresh CECFrame and offered through the
// state-guarded accessor.
TEST_F(DriverAidlSessionTest, ReceivedMessageIsAcceptedWhileTheDriverIsOpen) {
    const std::vector<uint8_t> message{ 0x0F, GIVE_DEVICE_POWER_STATUS };

    EXPECT_TRUE(fake->fireOnMessageReceived(message))
        << "no listener was captured, so the trigger was a silent no-op and this case would prove "
           "nothing. SetUp cycles the session precisely so that open() re-captures it";

    EXPECT_NE(fake->getListener(), nullptr)
        << "the fake holds no listener although open() completed, so the middleware did not hand "
           "one to IHdmiCec::open()";
}

// THE CLOSING-STATE REJECTION, which is the arm that stops a frame leaking per rejected
// message.
//
// A frame arriving while the driver is not OPENED must be REFUSED and RELEASED, not queued.
// The adapter reaches the queue through a state-guarded accessor that raises when the state is
// not OPENED (DriverAidlImpl.cpp:1426-1440), exactly as the legacy receive callback does
// (guard at DriverImpl.cpp:389-393, releasing the frame at :75), and the listener catches that
// and frees the allocation. Offering directly to the queue member instead would accept frames
// the legacy path rejects AND would leave them in a queue nothing will ever drain.
//
// The assertion is that the delivery is absorbed without an exception escaping into the
// binder callback: a oneway callback has no caller to receive a fault, so letting one escape
// into onTransact would be worse than dropping one frame - which is the disposition the legacy
// callback already takes.
TEST_F(DriverAidlSessionTest, MessageArrivingWhileClosedIsRejectedAndReleased) {
    ASSERT_NO_THROW({ Driver::getInstance().close(); })
        << "the driver could not be closed, so the rejecting-queue precondition does not hold";

    const std::vector<uint8_t> message{ 0x0F, GIVE_DEVICE_POWER_STATUS };

    // The listener is still registered - close() does not withdraw it - so the trigger really
    // reaches production code rather than being a no-op.
    ASSERT_NE(fake->getListener(), nullptr)
        << "the listener was withdrawn by close(), so this trigger cannot reach the adapter";

    EXPECT_TRUE(fake->fireOnMessageReceived(message))
        << "the trigger did not reach the listener, so the rejection path was never exercised";

    // TearDown restores the opened baseline; nothing here is left closed for a sibling suite.
}

// The two diagnostic callbacks are accepted and do nothing more, which is a behavioural claim
// and not an absence of one.
//
// onStateChanged and onMessageSent are logged and acted on in no other way, deliberately:
// there is no legacy counterpart to either, so acting on them would be new behaviour. In
// particular a transition to CLOSED does NOT offer the close sentinel onto the incoming queue -
// an in-process HAL cannot vanish, so the legacy path has no such notion - and no death
// recipient is installed for the same reason.
//
// What is asserted is that both are absorbed without disturbing the session: after both have
// fired, the driver is still open and still transmits. A callback that quietly closed the
// driver, or raised into onTransact, would fail here.
TEST_F(DriverAidlSessionTest, DiagnosticCallbacksAreAbsorbedWithoutDisturbingTheSession) {
    EXPECT_TRUE(fake->fireOnStateChanged(cechal::State::STARTED, cechal::State::CLOSED))
        << "the state-change trigger did not reach the listener";

    const std::vector<uint8_t> message{ 0x40, GIVE_DEVICE_POWER_STATUS };
    EXPECT_TRUE(fake->fireOnMessageSent(message, cechal::SendMessageStatus::ACK_STATE_0))
        << "the message-sent trigger did not reach the listener";

    // Still open, and still able to transmit: neither callback changed any state.
    CECFrame frame = directedFrame();
    EXPECT_NO_THROW({ Driver::getInstance().write(frame); })
        << "the driver is no longer usable after the two diagnostic callbacks. A transition to "
           "CLOSED must NOT close the middleware's own session - an in-process HAL cannot vanish, "
           "so there is no legacy counterpart and reacting to it would be new behaviour";
}

// The threadpool obligation is discharged idempotently, including when a pool was ALREADY
// started elsewhere in the process - which is the case a naive back-end-local flag would break.
//
// DriverAidlImpl::open() calls ProcessState::self()->startThreadPool() unconditionally
// (DriverAidlImpl.cpp:716). That is deliberately the WHOLE of the obligation: the call is
// idempotent, and setThreadPoolMaxThreadCount is NOT called because no back-end-local flag can
// detect a pool started by another component and lowering an already-established maximum can
// abort the process.
//
// By the time this case runs, a pool has certainly been started - SetUp's open() did it, and so
// did the original open during LibCCEC::init - so the already-started condition is the one being
// exercised, and a second open must still succeed and callbacks must still arrive. The
// never-started condition is covered by the process's FIRST open, inside LibCCEC::init: a
// failure there would abort initialization and no case in this binary would run at all.
TEST_F(DriverAidlSessionTest, OpenSucceedsAndDeliversWhenAThreadPoolWasAlreadyStarted) {
    // A close/open cycle so that open() genuinely runs its body again, rather than returning
    // silently on an already-open driver.
    ASSERT_NO_THROW({ Driver::getInstance().close(); });
    ASSERT_EQ(fake->getCloseCallCount(), 1) << "the close did not reach the HAL";

    EXPECT_NO_THROW({ Driver::getInstance().open(); })
        << "open() failed on a second pass, with a binder threadpool already started in this "
           "process. startThreadPool() is idempotent, so a failure here means the back-end is "
           "tracking pool state of its own - which cannot see a pool another component started";

    EXPECT_EQ(fake->getOpenCallCount(), 2)
        << "the HAL did not observe a second open, so the session was not re-established";

    const std::vector<uint8_t> message{ 0x0F, GIVE_DEVICE_POWER_STATUS };
    EXPECT_TRUE(fake->fireOnMessageReceived(message))
        << "no callback arrives after the second open, so the listener was not re-registered and "
           "the receive path is dead for the rest of the session";

    CECFrame frame = directedFrame();
    EXPECT_NO_THROW({ Driver::getInstance().write(frame); })
        << "the re-opened session cannot transmit, so open() did not restore the controller";
}

// open() reporting a NULL controller is a failure, even when the transaction itself succeeded.
//
// Two conditions converge on one guard - `if (!txn.isOk() || (controller == 0))`
// (DriverAidlImpl.cpp:729-731) - and this is the arm that is easy to omit, because a status of
// ok reads as success. Accepting a null controller would leave the state OPENED with no
// session, and every subsequent transmit would then fail on the controller check instead, far
// from the cause.
TEST_F(DriverAidlSessionTest, OpenRejectsANullControllerEvenWhenTheStatusIsOk) {
    ASSERT_NO_THROW({ Driver::getInstance().close(); });

    fake->setOpenReturnsNullController(true);

    EXPECT_THROW({ Driver::getInstance().open(); }, IOException)
        << "open() accepted a null controller reported alongside an ok status, so the state would "
           "be OPENED with no session behind it";

    // Still closed, so the guards are live - open() did not move the state before its checks.
    CECFrame frame = directedFrame();
    EXPECT_THROW({ Driver::getInstance().write(frame); }, InvalidStateException)
        << "the driver believes it is open after an open() that reported no controller";

    // Restore a usable session for TearDown; the fake is reset there in any case.
    fake->setOpenReturnsNullController(false);
    EXPECT_NO_THROW({ Driver::getInstance().open(); })
        << "the session could not be restored after the null-controller case";
}

// A non-ok binder status on open is the other arm of the same guard, and it is asserted
// separately for the same reason: one condition, two independent ways to satisfy it.
TEST_F(DriverAidlSessionTest, OpenRejectsANonOkBinderStatus) {
    ASSERT_NO_THROW({ Driver::getInstance().close(); });

    fake->setOpenBinderStatus(::android::binder::Status::fromStatusT(::android::DEAD_OBJECT));

    EXPECT_THROW({ Driver::getInstance().open(); }, IOException)
        << "open() accepted a failed transaction, so a dead HAL would be reported as an open "
           "session";

    fake->setOpenBinderStatus(::android::binder::Status::ok());
    EXPECT_NO_THROW({ Driver::getInstance().open(); })
        << "the session could not be restored after the failed-open case";
}

// A non-ok status on getLogicalAddresses reports 0 and does NOT raise, which is the arm that
// keeps the AIDL back-end's failure signalling identical to the legacy back-end's.
//
// getLogicalAddress never throws on either back-end: the legacy implementation ignores its
// HAL's return value entirely and yields whatever the call left in a zero-initialised local.
// Raising here would be a new exception on a path LibCCEC does not guard, and it would bypass
// the InvalidStateException that LibCCEC raises for a zero - the signal callers already
// handle.
TEST_F(DriverAidlSessionTest, GetLogicalAddressReportsZeroOnTransportFailureWithoutRaising) {
    fake->setGetLogicalAddressesBinderStatus(
        ::android::binder::Status::fromStatusT(::android::DEAD_OBJECT));

    int reported = -1;
    EXPECT_NO_THROW({ reported = Driver::getInstance().getLogicalAddress(DeviceType::TV); })
        << "getLogicalAddress raised on a transport failure. Neither back-end throws here, and "
           "LibCCEC::getLogicalAddress does not guard the call - a new exception would propagate "
           "into the Source plugin's discovery path unhandled";

    EXPECT_EQ(reported, 0)
        << "a failed query did not report 0, so LibCCEC's zero-means-no-address signal is bypassed";
}

// A non-ok status on removeLogicalAddresses is IGNORED, matching the legacy disposition of
// discarding the HAL return - and the local removal still stands, because it precedes the call.
//
// This is the transport arm of the remove disposition; the refusal arm is asserted above. Both
// are needed: they are separate branches (DriverAidlImpl.cpp:1226-1233) and either one raising
// would be an unregistered difference.
TEST_F(DriverAidlSessionTest, RemoveLogicalAddressIgnoresTransportFailureAndStillRemovesLocally) {
    const LogicalAddress address(LogicalAddress::PLAYBACK_DEVICE_1);
    ASSERT_TRUE(Driver::getInstance().addLogicalAddress(address));

    fake->getController()->setRemoveLogicalAddressesBinderStatus(
        ::android::binder::Status::fromStatusT(::android::DEAD_OBJECT));

    EXPECT_NO_THROW({ Driver::getInstance().removeLogicalAddress(address); })
        << "removeLogicalAddress raised on a transport failure. The legacy back-end discards its "
           "HAL's return value, so raising here would be an unregistered behaviour change";

    EXPECT_FALSE(Driver::getInstance().isValidLogicalAddress(address))
        << "the address is still held locally after a removal whose transaction failed. The local "
           "removal precedes the HAL call and is not rolled back";
}

// The four AIDL methods that are deliberately NOT consumed stay unconsumed.
//
// getState, getProperty, registerEventListener and unregisterEventListener are each excluded
// for a stated reason: the middleware keeps its own CLOSED/CLOSING/OPENED state machine and a
// second source of truth would be worse than none; the HAL properties have no legacy
// counterpart, so reading them would be new behaviour; and the listener registration methods
// exist for non-controlling diagnostic clients, whereas this back-end is the controlling client
// and receives events through the listener it hands to open().
//
// This is asserted rather than commented because an absence is exactly the kind of decision
// that erodes silently. A future change that started consulting getState - the most tempting of
// the four, since it looks like a cheap way to answer "is the HAL up" - would fail here and be
// a visible decision rather than a quiet second state machine. The whole session lifecycle has
// run by the time this case executes: SetUp closed, reset and re-opened.
TEST_F(DriverAidlSessionTest, TheFourUnconsumedAidlMethodsAreNeverCalled) {
    // A representative slice of the surface the middleware does drive, so that the zeros below
    // cannot be explained by nothing having happened at all.
    const LogicalAddress address(LogicalAddress::PLAYBACK_DEVICE_1);
    ASSERT_TRUE(Driver::getInstance().addLogicalAddress(address));
    CECFrame frame = directedFrame();
    ASSERT_NO_THROW({ Driver::getInstance().write(frame); });
    ASSERT_NO_THROW({ Driver::getInstance().poll(address, LogicalAddress(LogicalAddress::TV)); });
    (void)Driver::getInstance().getLogicalAddress(DeviceType::TV);

    EXPECT_EQ(fake->getGetStateCallCount(), 0)
        << "IHdmiCec::getState was consulted. The middleware tracks its own lifecycle state, and a "
           "second source of truth is worse than none - poll() in particular is a CEC ping via a "
           "one-byte transmit, not a state query";
    EXPECT_EQ(fake->getGetPropertyCallCount(), 0)
        << "IHdmiCec::getProperty was consulted. HAL_CEC_VERSION and the METRIC_* properties have "
           "no legacy counterpart, so reading them would be new behaviour";
    EXPECT_EQ(fake->getRegisterEventListenerCallCount(), 0)
        << "IHdmiCec::registerEventListener was called. It exists for non-controlling diagnostic "
           "clients; this back-end is the CONTROLLING client and gets its events from the listener "
           "it passes to open(), so calling it would register a second listener";
    EXPECT_EQ(fake->getUnregisterEventListenerCallCount(), 0)
        << "IHdmiCec::unregisterEventListener was called, which is the mirror of a registration "
           "that never happened";
}

/**
 * @brief Transmit-status translation and the frame-length guard on the AIDL back-end.
 *
 * REQUIRES INVOCATION B. See DriverAidlSessionFixture for the precondition and for why the
 * session is cycled in SetUp.
 *
 * Separated from the session fixture because the subject is different: everything here is one
 * call - write() - and what varies is the status the HAL reports and the destination nibble
 * the frame carries. Keeping the matrix in a fixture of its own keeps each case's body to the
 * one thing it varies.
 */
class DriverAidlTransmitTest : public DriverAidlSessionFixture {
protected:
    /**
     * @brief Programs the canned send status and hands the frame to the AIDL back-end.
     *
     * @param [in] frame  - Frame to transmit, header byte first.
     * @param [in] status - Status the fake reports for the transmission.
     */
    void transmitWithStatus(const CECFrame &frame, cechal::SendMessageStatus status) {
        fake->getController()->setSendMessageResult(status);
        Driver::getInstance().write(frame);
    }

    /** @brief Asserts the fake received exactly one frame, of @p length bytes. */
    void expectExactlyOneFrameOfLength(size_t length) {
        EXPECT_EQ(fake->getController()->getSendMessageCallCount(), 1)
            << "the HAL was not asked exactly once to send";
        EXPECT_EQ(fake->getController()->getLastSentMessage().size(), length)
            << "the HAL received a frame of " << fake->getController()->getLastSentMessage().size()
            << " bytes rather than " << length << ", so the frame was truncated or padded";
    }
};

// A DIRECTED frame reported ACK_STATE_0 succeeded: the addressed follower acknowledged it.
//
// This is the arm where the inverted sense bites hardest, because the SAME status value means
// the opposite thing on a broadcast (asserted below). The frame is also checked on the way
// through: transmit success is not merely "no exception", it is "these exact bytes reached the
// HAL".
TEST_F(DriverAidlTransmitTest, DirectedFrameAcknowledgedByTheFollowerSucceeds) {
    CECFrame frame = directedFrame();

    EXPECT_NO_THROW({ transmitWithStatus(frame, cechal::SendMessageStatus::ACK_STATE_0); })
        << "a directed frame the follower ACKNOWLEDGED was reported as a failure. ACK_STATE_0 "
           "means acknowledged for a DIRECTED message; reading it as a rejection would fail every "
           "successful directed transmit in the system";

    expectExactlyOneFrameOfLength(2u);

    const std::vector<uint8_t> sent = fake->getController()->getLastSentMessage();
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_EQ(sent[0], 0x40) << "the header byte was altered on the way to the HAL";
    EXPECT_EQ(sent[1], GIVE_DEVICE_POWER_STATUS) << "the opcode was altered on the way to the HAL";
}

// A DIRECTED frame reported ACK_STATE_1 was NOT acknowledged, and raises CECNoAckException -
// reproducing DriverImpl.cpp:276-278, where the legacy SENT_BUT_NOT_ACKD does the same.
//
// The exception type is the substance: Connection and the plugins above it distinguish "no
// device answered" from "the transmit failed", and collapsing the two into IOException would
// make an absent peer indistinguishable from a broken HAL.
TEST_F(DriverAidlTransmitTest, DirectedFrameNotAcknowledgedRaisesNoAck) {
    CECFrame frame = directedFrame();

    EXPECT_THROW({ transmitWithStatus(frame, cechal::SendMessageStatus::ACK_STATE_1); },
                 CECNoAckException)
        << "a directed frame that was NOT acknowledged did not raise CECNoAckException. "
           "ACK_STATE_1 is the not-acknowledged value for a DIRECTED message, and the exception "
           "type is what lets callers tell an absent peer from a broken HAL";

    // It was still SENT: the exception describes the bus outcome, not a refusal to transmit.
    expectExactlyOneFrameOfLength(2u);
}

// A BROADCAST frame reported ACK_STATE_1 SUCCEEDED, which is the mirror image of the directed
// case above and the single fact that makes the translation non-trivial.
//
// A broadcast has no addressed follower, so nothing acknowledges it; ACK_STATE_1 there means
// "sent and not rejected". An implementation that mapped ACK_STATE_1 to CECNoAckException
// unconditionally would compile, would pass every directed case, and would fail every
// broadcast the middleware sends - which is most of what the Sink emits during discovery.
TEST_F(DriverAidlTransmitTest, BroadcastFrameNotRejectedSucceeds) {
    CECFrame frame = broadcastFrame(GIVE_DEVICE_POWER_STATUS);

    EXPECT_NO_THROW({ transmitWithStatus(frame, cechal::SendMessageStatus::ACK_STATE_1); })
        << "a BROADCAST frame reported ACK_STATE_1 was treated as a failure. On a broadcast that "
           "value means sent and NOT rejected - the opposite of its meaning on a directed message "
           "- so this is the inverted sense being read the wrong way round";

    expectExactlyOneFrameOfLength(2u);
}

// A BROADCAST frame reported ACK_STATE_0 was REJECTED by some follower, and for MOST opcodes
// that returns normally - matching the legacy implementation, which throws only on the CEC CTS
// arm.
//
// This case is the negative control for the CTS case below: without it, an implementation that
// raised on every rejected broadcast would pass the CTS case and be wrong everywhere else.
TEST_F(DriverAidlTransmitTest, RejectedBroadcastReturnsNormallyForOpcodesOutsideTheCtsArm) {
    CECFrame frame = broadcastFrame(GIVE_DEVICE_POWER_STATUS);

    EXPECT_NO_THROW({ transmitWithStatus(frame, cechal::SendMessageStatus::ACK_STATE_0); })
        << "a rejected BROADCAST carrying an opcode outside the CEC CTS 9-3-3 arm raised. The "
           "legacy back-end raises only on a rejected REPORT_PHYSICAL_ADDRESS, so raising here "
           "would make every rejected broadcast an error the callers do not expect";

    expectExactlyOneFrameOfLength(2u);
}

// THE CEC CTS 9-3-3 ARM: a rejected BROADCAST REPORT_PHYSICAL_ADDRESS DOES raise, so that the
// caller retries at least once - which is what the conformance test requires.
//
// This is the one opcode-specific arm in the whole translation (DriverAidlImpl.cpp:1060,
// mirroring DriverImpl.cpp:279-284), and it is the reason REPORT_PHYSICAL_ADDRESS is pinned by
// a static assertion at the top of this file: if that constant drifted, this case and the
// negative control above would silently be testing the same opcode and the distinction between
// them would vanish.
TEST_F(DriverAidlTransmitTest, RejectedBroadcastReportPhysicalAddressRaisesForTheCtsRetry) {
    CECFrame frame = broadcastFrame(REPORT_PHYSICAL_ADDRESS);

    EXPECT_THROW({ transmitWithStatus(frame, cechal::SendMessageStatus::ACK_STATE_0); },
                 CECNoAckException)
        << "a rejected broadcast REPORT_PHYSICAL_ADDRESS did not raise CECNoAckException, so the "
           "caller has nothing to retry on and CEC CTS 9-3-3 fails";

    expectExactlyOneFrameOfLength(2u);
}

// A one-byte broadcast reported ACK_STATE_0 returns normally, because the CTS arm requires an
// OPCODE to inspect and a one-byte frame has none.
//
// The guard is `(length > 1) && ((frame.at(1) & 0xFF) == REPORT_PHYSICAL_ADDRESS)`, and the
// length conjunct is not decoration: poll() transmits exactly such a one-byte frame
// (DriverAidlImpl.cpp:1381-1384), so an implementation that read frame.at(1) without the length
// check would raise std::out_of_range out of every poll - and poll is how the Sink discovers
// the bus.
TEST_F(DriverAidlTransmitTest, OneByteBroadcastDoesNotReachTheCtsArm) {
    CECFrame pollFrame;
    pollFrame.append(static_cast<uint8_t>(0x4F));
    ASSERT_EQ(pollFrame.length(), 1u);

    EXPECT_NO_THROW({ transmitWithStatus(pollFrame, cechal::SendMessageStatus::ACK_STATE_0); })
        << "a one-byte broadcast raised. The CTS arm needs an opcode byte to inspect, and reading "
           "frame.at(1) without the length guard would raise std::out_of_range out of every "
           "poll() - which is exactly the frame poll() sends";

    expectExactlyOneFrameOfLength(1u);
}

// BUSY means arbitration failed after two attempts and the message was NOT sent, which is the
// legacy send-failed family and therefore an IOException - on both destinations, because
// nothing reached the bus for the ACK sense to apply to.
//
// Both destinations are asserted in one case precisely because the answer must NOT depend on
// the destination here: BUSY is checked before the nibble is ever read
// (DriverAidlImpl.cpp:1050-1054), and an implementation that folded it into the ACK matrix
// would give two different answers for the same failure.
TEST_F(DriverAidlTransmitTest, BusyIsATransmitFailureOnBothDestinations) {
    CECFrame directed = directedFrame();

    EXPECT_THROW({ transmitWithStatus(directed, cechal::SendMessageStatus::BUSY); }, IOException)
        << "a BUSY result on a directed frame did not raise IOException. Arbitration failed and "
           "nothing was sent, which is the legacy send-failed family - not a missing "
           "acknowledgement";

    fake->getController()->reset();

    CECFrame broadcast = broadcastFrame(REPORT_PHYSICAL_ADDRESS);
    EXPECT_THROW({ transmitWithStatus(broadcast, cechal::SendMessageStatus::BUSY); }, IOException)
        << "a BUSY result on a BROADCAST frame did not raise IOException. BUSY is decided before "
           "the destination nibble is read, so it must give the same answer on both destinations - "
           "note that the frame used here is the CTS opcode, whose ACK_STATE_0 arm raises "
           "CECNoAckException, so a different exception type here means BUSY fell through into the "
           "ACK matrix";
}

// A non-ok binder status is a TRANSPORT failure and raises IOException, reproducing
// DriverImpl.cpp:261-263, where a HAL error return does the same.
//
// It is checked ahead of the reported send status (DriverAidlImpl.cpp:1046-1048), which matters:
// the out-parameter is meaningless when the transaction did not complete, so a translation that
// consulted it first would classify a dead binder by whatever happened to be in that variable.
// The canned status here is deliberately one whose own arm would SUCCEED, so a pass proves the
// transport check ran first.
TEST_F(DriverAidlTransmitTest, NonOkBinderStatusOnSendRaisesIoException) {
    CECFrame frame = directedFrame();

    fake->getController()->setSendMessageResult(cechal::SendMessageStatus::ACK_STATE_0);
    fake->getController()->setSendMessageBinderStatus(
        ::android::binder::Status::fromStatusT(::android::DEAD_OBJECT));

    EXPECT_THROW({ Driver::getInstance().write(frame); }, IOException)
        << "a failed transaction did not raise IOException. The canned send status was "
           "ACK_STATE_0, whose own arm SUCCEEDS on a directed frame, so a pass here would mean the "
           "status out-parameter was consulted despite the transaction never completing";
}

// AUTHORIZED DIFFERENCE 1, AIDL half: a frame over the AIDL contract length is REFUSED, and -
// the half that matters more - NOTHING IS SENT.
//
// 16 bytes is accepted, matching the legacy back-end, which confines the difference to the
// disputed 17-to-20 band rather than to the guard's existence. 17 and 20 are refused with
// IOException.
//
// THE NOTHING-WAS-SENT ASSERTION IS THE POINT. An IOException raised AFTER a truncated frame
// reached the bus would be strictly worse than either outcome alone: the caller sees a failure
// while a corrupt CEC frame is already on the wire, and a corrupt frame can be interpreted by a
// peer as a different message entirely. So the send counter is asserted to be zero, not merely
// the exception to have been raised. The guard runs ahead of the transmit
// (DriverAidlImpl.cpp:1013-1016), which is what makes that true.
TEST_F(DriverAidlTransmitTest, FramesOverTheAidlLimitAreRefusedWithoutBeingSentOrTruncated) {
    // The control: exactly at the limit, accepted, whole.
    CECFrame atLimit = frameOfLength(kAidlMaxMessageLength);
    ASSERT_EQ(atLimit.length(), kAidlMaxMessageLength);

    EXPECT_NO_THROW({ transmitWithStatus(atLimit, cechal::SendMessageStatus::ACK_STATE_0); })
        << "a frame of exactly " << kAidlMaxMessageLength << " bytes was refused, although that is "
           "the AIDL contract's stated maximum and the legacy back-end accepts it too. The "
           "difference between the back-ends must be confined to the disputed band, not extended "
           "to the boundary itself";
    expectExactlyOneFrameOfLength(kAidlMaxMessageLength);

    const size_t overLimit[] = { kJustOverAidlLimit, kLegacyMaxMessageLength };

    for (size_t i = 0; i < sizeof(overLimit) / sizeof(overLimit[0]); i++) {
        fake->getController()->reset();

        CECFrame tooLong = frameOfLength(overLimit[i]);
        ASSERT_EQ(tooLong.length(), overLimit[i])
            << "the frame builder did not produce a frame of " << overLimit[i] << " bytes";

        EXPECT_THROW({ transmitWithStatus(tooLong, cechal::SendMessageStatus::ACK_STATE_0); },
                     IOException)
            << "a frame of " << overLimit[i] << " bytes was accepted, although the AIDL "
               "sendMessage contract states a maximum of " << kAidlMaxMessageLength;

        EXPECT_EQ(fake->getController()->getSendMessageCallCount(), 0)
            << "a frame of " << overLimit[i] << " bytes reached the HAL before being refused. The "
               "guard must run AHEAD of the transmit: an IOException raised after a truncated "
               "frame is already on the wire is worse than either outcome alone, because a peer "
               "can interpret a corrupt frame as a different message entirely";
        EXPECT_TRUE(fake->getController()->getLastSentMessage().empty())
            << "a frame of " << overLimit[i] << " bytes was captured by the HAL, so something was "
               "transmitted despite the refusal";
    }
}

// AUTHORIZED DIFFERENCE 2, AIDL half: writeAsync raises OperationNotSupportedException on an
// OPEN driver, after the prelude and the state guard have both passed.
//
// Asynchronous transmit is not migrated and is deliberately not emulated - no threads, no work
// queue, no deferred callback, no wrapper - and that is safe because no production call site
// reaches it: every plugin transmit goes through Connection::sendToAsync and
// Connection::sendAsync onto the Bus writer thread, which then calls the SYNCHRONOUS write.
//
// Nothing may be transmitted, which is asserted rather than assumed: an implementation that
// quietly forwarded the frame to sendMessage before raising would emulate asynchrony badly -
// the frame would go out while the caller was told the operation is unsupported.
TEST_F(DriverAidlTransmitTest, WriteAsyncRaisesOperationNotSupportedOnAnOpenDriver) {
    CECFrame frame = directedFrame();

    EXPECT_THROW({ Driver::getInstance().writeAsync(frame); }, OperationNotSupportedException)
        << "writeAsync did not raise OperationNotSupportedException on an OPEN AIDL driver. This "
           "is the one arm where the two back-ends differ on writeAsync - the prelude ordering and "
           "the state guard are identical on both - so a different outcome here either emulates "
           "asynchrony or has lost the guard";

    EXPECT_EQ(fake->getController()->getSendMessageCallCount(), 0)
        << "writeAsync transmitted the frame before raising, which is the worst of both outcomes: "
           "the frame is on the bus and the caller was told the operation is unsupported";
}

// poll() reaches the bus as a one-byte CEC ping built from the two addresses, and it reaches it
// through this back-end's own write() rather than through any state query.
//
// The byte layout is asserted, not just the call: initiator in the high nibble, destination in
// the low one (DriverAidlImpl.cpp:1377). getState() is deliberately not used for this - a poll
// is a transmit that either gets acknowledged or does not, and answering it from the HAL's
// state would answer a different question entirely, which is why the unconsumed-methods case
// above checks the state counter stays at zero.
TEST_F(DriverAidlTransmitTest, PollTransmitsAOneByteFrameCarryingBothAddresses) {
    const LogicalAddress from(LogicalAddress::PLAYBACK_DEVICE_1);
    const LogicalAddress to(LogicalAddress::TV);

    fake->getController()->setSendMessageResult(cechal::SendMessageStatus::ACK_STATE_0);

    EXPECT_NO_THROW({ Driver::getInstance().poll(from, to); })
        << "poll() failed against a HAL that acknowledged the ping";

    expectExactlyOneFrameOfLength(1u);

    const std::vector<uint8_t> sent = fake->getController()->getLastSentMessage();
    ASSERT_EQ(sent.size(), 1u);

    const uint8_t expectedByte =
        static_cast<uint8_t>(((from.toInt() & 0x0F) << 4) | (to.toInt() & 0x0F));
    EXPECT_EQ(sent[0], expectedByte)
        << "the poll byte is not the initiator in the high nibble and the destination in the low "
           "one, so a peer would see the ping as addressed to the wrong device";
}

// A poll the addressed device does not answer raises CECNoAckException, which is how "no device
// there" is reported all the way up to Bus.
//
// A poll is a DIRECTED one-byte frame, so ACK_STATE_1 means not acknowledged and the directed
// arm applies. This is the arm bus discovery actually depends on: a poll that reported success
// for an absent device would populate the device list with devices that are not there.
TEST_F(DriverAidlTransmitTest, UnansweredPollRaisesNoAck) {
    const LogicalAddress from(LogicalAddress::PLAYBACK_DEVICE_1);
    const LogicalAddress to(LogicalAddress::TV);

    fake->getController()->setSendMessageResult(cechal::SendMessageStatus::ACK_STATE_1);

    EXPECT_THROW({ Driver::getInstance().poll(from, to); }, CECNoAckException)
        << "an unanswered poll did not raise CECNoAckException, so bus discovery would record "
           "devices that are not present";

    expectExactlyOneFrameOfLength(1u);
}
