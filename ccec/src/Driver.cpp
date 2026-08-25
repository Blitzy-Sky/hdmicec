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


#include "ccec/Driver.hpp"
#include "ccec/Util.hpp"
#include "DriverImpl.hpp"
#include "DriverAidlImpl.hpp"

CCEC_BEGIN_NAMESPACE

namespace {

/**
 * @brief Format of the single line that names the selected HDMI CEC HAL back-end
 *
 * One format string with one substitution - the back-end name - so that the emitted line
 * is identical across every selection arm apart from that name.@n
 * This is a TEST AND TOOLING CONTRACT rather than a diagnostic convenience, which is why
 * it is defined once, here, and referenced nowhere else. The functional suites match on
 * it to establish which back-end resolved, the coverage runner greps it once per
 * invocation and treats its absence as a failed invocation, and device-level validation
 * relies on it wherever the concrete back-end headers are out of scope. It is also the
 * only way the selection is observable: no introspection API is added to the Driver
 * interface, deliberately, because that would grow the middleware public surface.
 *
 * @warning Rewording this string breaks every consumer named above. The back-end name is
 *          the only part that varies.
 *
 * @see SELECTED_BACK_END_AIDL
 * @see SELECTED_BACK_END_LEGACY
 */
const char *const SELECTED_BACK_END_LOG_FORMAT =
	"Driver::getInstance : HDMI CEC HAL back-end selected : %s\r\n";

/** @brief Name substituted into SELECTED_BACK_END_LOG_FORMAT for the AIDL back-end. */
const char *const SELECTED_BACK_END_AIDL   = "AIDL";

/** @brief Name substituted into SELECTED_BACK_END_LOG_FORMAT for the legacy back-end. */
const char *const SELECTED_BACK_END_LEGACY = "legacy";

/**
 * @brief Resolves, exactly once, which of the two HDMI CEC HAL back-ends this process uses
 *
 * This is the single selection point of the middleware. The two back-ends implement the
 * same Driver interface over different transports - DriverImpl over the legacy in-process
 * C ABI, DriverAidlImpl over the out-of-process `com.rdk.hal.hdmicec` AIDL HAL - and both
 * are compiled into the library in every build, for every SOC vendor. Which one is used is
 * decided here, at run time, on service availability alone.@n
 * The order of the two steps is load bearing and is not a matter of taste. BOTH back-ends
 * are constructed first, as function-local statics, and only then is the AIDL one asked
 * whether its service came up. Gating construction on availability would invert that
 * order, and it would also be unsafe: the presence query is the first thing in the process
 * that may touch libbinder, and on the pinned binder stack an unguarded lookup on a
 * platform with no binder driver aborts rather than returning an error. DriverAidlImpl's
 * constructor deliberately touches no binder at all, so constructing it is safe on a
 * legacy-only SOC, and its isServiceAvailable() is documented never to abort, never to
 * block indefinitely and never to propagate an exception.
 *
 * Three arms exist, each logged, because "absent" and "present but not usable" are
 * different platform conditions and validation gates them separately:
 * -# the service is present and compatible, so the AIDL back-end is selected;
 * -# the binder transport is reachable but no compatible service resolved - either none is
 *    registered or the one that is cannot be spoken to compatibly - so the legacy back-end
 *    is selected;
 * -# the binder transport itself is unavailable, so the legacy back-end is selected.
 *
 * The distinction between arms 2 and 3 is drawn from the same bounded preflight the
 * presence query runs as its own first stage, which is side-effect free and safe to
 * repeat. It is re-run only after the query has already answered no, so the selected
 * AIDL path pays for it exactly once. Which specific compatibility rule rejected a
 * service - an empty or "-1" interface hash, an unfrozen development server, or an
 * interface version outside the compiled-against era and major - is reported by
 * DriverAidlImpl::isServiceAvailable() itself, and is deliberately not restated here,
 * because this function cannot observe it and must not invent it.
 *
 * @return Driver& - The selected back-end. Both candidates have static storage duration,
 *                   so the returned reference stays valid for the lifetime of the process.
 *
 * @pre None. Safe on a platform with no binder driver, no service manager and no AIDL HAL.
 * @post Exactly one selected-path line has been emitted, and the choice is fixed for the
 *       lifetime of the process.
 * @warning Called only from the one-time initializer of Driver::getInstance(), which the
 *          language serializes, so no lock is taken here and none is needed. In particular
 *          Driver::instanceMutex is not used: it is declared but never defined anywhere in
 *          the tree, and referencing it would leave the library with an undefined symbol.
 * @warning Neither construction nor the query throws, so this function cannot leave the
 *          enclosing static uninitialized and cannot be re-entered on a later call.
 * @warning Neither back-end's constructor nor its presence query may call
 *          Driver::getInstance(). Doing so would re-enter the very initializer that is
 *          still running, which is undefined behaviour rather than a recoverable error.
 *          Both back-ends reach the incoming frame queue through their own accessor
 *          precisely so that this cannot happen.
 *
 * @see Driver::getInstance()
 * @see DriverAidlImpl::isServiceAvailable()
 * @see DriverAidlImpl::isBinderPreflightOk()
 */
Driver &resolveBackEnd(void)
{
	/*
	 * Construct-then-query. Both back-ends are always built and always constructible;
	 * nothing above gates whether the AIDL one exists. Declaring the legacy back-end
	 * first also makes it the last destroyed, which matches its role as the fallback.
	 */
	static DriverImpl     legacyBackEnd;
	static DriverAidlImpl aidlBackEnd;

	if (aidlBackEnd.isServiceAvailable()) {
		CCEC_LOG(LOG_INFO, SELECTED_BACK_END_LOG_FORMAT, SELECTED_BACK_END_AIDL);
		return aidlBackEnd;
	}

	/*
	 * Why the AIDL back-end was not selected, at the coarsest granularity this function
	 * can honestly observe. Kept deliberately unlike the selected-path wording above so
	 * that grepping for the selected-path line still yields exactly one hit per process.
	 */
	if (DriverAidlImpl::isBinderPreflightOk()) {
		CCEC_LOG(LOG_INFO, "Driver::getInstance : AIDL HDMI CEC service is not usable : the binder transport is reachable but no compatible service resolved\r\n");
	}
	else {
		CCEC_LOG(LOG_INFO, "Driver::getInstance : AIDL HDMI CEC service is not usable : the binder transport is unavailable on this platform\r\n");
	}

	CCEC_LOG(LOG_INFO, SELECTED_BACK_END_LOG_FORMAT, SELECTED_BACK_END_LEGACY);
	return legacyBackEnd;
}

} // anonymous namespace

Driver &Driver::getInstance()
{
	static Driver &instance = resolveBackEnd();
	return instance;
}

CCEC_END_NAMESPACE


/** @} */
/** @} */
