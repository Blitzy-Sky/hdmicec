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
 * L2 TEST HARNESS BOOTSTRAP: the process-global test environment for the integration
 * tier, and THE PARENT HALF OF A TWO-PROCESS LIFECYCLE whose child half is
 * mocks/hdmicec/fake_hdmi_cec_aidl_service_host.cpp.
 *
 * WHAT THIS TIER IS FOR, AND WHY IT NEEDS A SECOND PROCESS AT ALL.
 * libbinder resolves a service name that was registered in the CALLING process to the
 * local BBinder, so interface_cast there hands back that very object: no Bp* proxy is
 * created, no transaction crosses the binder driver, and the client threadpool is never
 * involved. An in-process fake therefore cannot prove the transport, however faithfully
 * it implements the interface - which is exactly what the L1 tier's in-process modes
 * are, and exactly why they are not enough. Hosting the same fake in a SEPARATE process
 * is what makes the middleware hold a real proxy and receive its event callbacks on a
 * binder threadpool thread. That separation is the whole substance of this tier, and it
 * is enforced by the build: the runner compiles the legacy mock, the host binary
 * compiles the fake, and neither target carries the other's source.
 *
 * THE BACK-END SELECTION RESOLVES ONCE PER PROCESS, AND IT RESOLVES BELOW.
 * The LibCCEC::init call in SetUp is the first thing in this binary that forces
 * Driver::getInstance(), whose helper in ccec/src/Driver.cpp constructs BOTH back-ends,
 * asks the AIDL one whether its service came up, and emits exactly one selected-path
 * line naming the winner. That choice is then FIXED for the lifetime of the process.
 *
 * ORDERING IS LOAD-BEARING, AND IT IS THE FIRST OF THE TWO THINGS THAT MAKE THIS TIER
 * MEAN ANYTHING. Anything that is to influence the selection MUST happen BEFORE that
 * init call, and NOTHING after it can change the outcome. Launching the fake service
 * host AFTER init leaves the already-resolved selection on the legacy back-end: the
 * suite then exercises the legacy path from beginning to end, every case passes, and
 * the run is reported as AIDL evidence while never once having spoken binder. That is a
 * GREEN RUN THAT PROVES NOTHING, and it is the specific failure this tier exists to
 * rule out. So the launch and the readiness wait sit ahead of init by construction
 * rather than merely early in SetUp, and the sequence is: install the legacy mock ->
 * launch the host and wait for it to report ready -> only then init.
 *
 * A PIPE AND NOT A SLEEP, WHICH IS THE SECOND OF THE TWO. The host signals readiness by
 * writing one fixed line to an inherited descriptor this process holds the read end of,
 * and this process blocks on that descriptor under a bounded timeout. A timed wait in
 * place of the token would convert a race into a flake: too short on a loaded machine
 * or inside an emulated guest, wasteful when it is not, and never once proof that the
 * service was actually published. The repository already models the correct disposition
 * for exactly this problem - tests/L1Tests/ccec/test_Connection.cpp's
 * DecodingFrameListener::WaitForNotification blocks on a condition variable with
 * wait_for rather than sleeping, for the reason its own comment gives - and this is the
 * same principle reached with a different primitive, because the event being waited for
 * originates in another process.
 *
 * The bound is not a nicety either. The host writes NO readiness line on any failure
 * path, so this wait is the ONLY detector of a host that failed to register, of a host
 * that could not reach a binder transport, and of a host that is blocked indefinitely
 * inside libbinder waiting for binder handle 0 because no service manager is running.
 * On timeout, on end of file without the token, and on a token that does not match, the
 * run FAILS - it is never allowed to continue and describe a legacy result as an AIDL
 * one.
 *
 * A STALE REGISTRATION IS FATAL AND NOT A CONDITION TO WORK AROUND. Something already
 * published under the production service name would make the middleware's own lookup
 * resolve against THAT service instead of the fake this harness hosts; the invocation
 * would still run, and its result would describe something nobody chose. THE CHECK IS
 * THE HOST'S, DELIBERATELY, AND THIS PROCESS DOES NOT REPEAT IT: the host queries the
 * name with checkService() and exits with a code of its own, writing no readiness line,
 * which this harness's bounded wait then reports. Re-checking here would mean this
 * process reaching the service manager itself, and that is unsafe in two independent
 * ways on the pinned binder stack - with no driver node libbinder ABORTS the process
 * rather than returning an error, and with a driver node but no running service manager
 * it BLOCKS INDEFINITELY - neither of which may be risked in a runner that must also
 * execute the legacy invocation on a host with no binder support at all. Which brings
 * the point that governs the whole file: on the legacy invocation this harness touches
 * libbinder NOT AT ALL, and it links no binder symbol to make that impossible to get
 * wrong. It launches a binary; it does not host a fake.
 *
 * CEC_TEST_AIDL_MODE selects what this process finds when it looks the service up. It is
 * read HERE and in tests/L1Tests/test_main.cpp, and NOWHERE ELSE: no production source
 * reads it, and none may.
 *
 *   absent        Launch nothing. The lookup finds no service and the legacy back-end is
 *                 selected, driven through the in-process legacy mock. An unset or empty
 *                 variable means exactly this, so a plain ./run_L2Tests exercises the
 *                 legacy round trip and touches libbinder not at all.
 *                                                                     [invocation D]
 *   remote        Launch the out-of-process fake service host, wait for its readiness
 *                 token, and only then initialize. The middleware resolves a real proxy
 *                 over the binder driver and its listener callback arrives on a binder
 *                 threadpool thread.                                  [invocation E]
 *   compatible    IN-PROCESS modes, and NOT IMPLEMENTED HERE. They mean "register a fake
 *   incompatible  inside this very process", which is run_L1Tests' job: an in-process
 *                 registration produces no proxy, no driver transaction and no callback
 *                 on a binder thread, so it cannot be what this tier runs. Seen here
 *                 either one is a HARD FAILURE naming that runner, never a quiet
 *                 downgrade to absent - a misconfigured invocation that silently ran the
 *                 legacy path and reported green is the same defect as launching the
 *                 host too late, arrived at from a different direction.
 *
 * An unrecognised value is a HARD FAILURE for the same reason. Unset is the one value
 * treated permissively, because absent is genuinely the tier's default and a bare
 * ./run_L2Tests is a legitimate way to run the legacy arm; a TYPO is not, and is
 * refused.
 *
 * THIS FILE DOES NOT START THE BINDER CLIENT THREADPOOL. DriverAidlImpl::open() owns
 * that, and it runs inside the init call below. The host starts its own SERVICE-side
 * pool, which is a different pool serving a different direction. Starting one here would
 * duplicate an ownership the production back-end already holds.
 *
 * NOTHING HERE IS SWALLOWED. The L1 template this file is derived from used to wrap init
 * in catch (...) and discard the exception; that swallow is deliberately absent. A
 * failed initialization reported as a green suite is worse than a red one, because every
 * driver-dependent case then asserts against an unopened stack and every other result in
 * the binary becomes meaningless.
 */

#include <gtest/gtest.h>
#include <iostream>
#include "hdmi_cec_driver_mock.h"
#include "ccec/LibCCEC.hpp"

/*
 * POSIX process, descriptor and signal primitives for the host lifecycle. There is
 * deliberately no binder or AIDL header among them: this harness launches a binary and
 * reads a pipe, so it needs no libbinder symbol and must not acquire one - see the
 * stale-registration paragraph in the block above.
 */
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Create mock instance before main
static HdmiCecDriverMock* g_driverMock = nullptr;

/*
 * The out-of-process fake service host, if this invocation launched one.
 *
 * Both are module scope because SetUp acquires them and TearDown releases them, and both
 * carry an explicit "nothing here" value so that teardown after a partial setup - the
 * legacy invocation, which launches no host, or a launch that failed halfway - can tell
 * that there is nothing to signal and nothing to reap. TearDown runs even when SetUp
 * failed fatally, which is measured GoogleTest behaviour rather than an assumption, so
 * that distinction is exercised on every failing run and not merely in theory.
 */
static pid_t g_hostPid = -1;

/*
 * Read end of the readiness pipe, held OPEN for the lifetime of the host rather than
 * closed once the token arrives, because it has a second job: end of file on it means
 * the host has closed its write end, which happens when and only when the host process
 * exits. That makes it a death channel, and it is what lets TearDown wait for the host
 * to go away without polling on a timer - see terminateAndReapFakeServiceHost().
 */
static int g_hostReadinessReadFd = -1;

namespace {

/*
 * The harness variable and its four values, spelled exactly once each. These spellings
 * are a fixed contract shared with tests/L1Tests/test_main.cpp, run_coverage.sh, both CI
 * workflows and the test documentation - they are not free to change here.
 */
const char *const AIDL_MODE_VARIABLE     = "CEC_TEST_AIDL_MODE";
const char *const AIDL_MODE_ABSENT       = "absent";
const char *const AIDL_MODE_COMPATIBLE   = "compatible";
const char *const AIDL_MODE_INCOMPATIBLE = "incompatible";
const char *const AIDL_MODE_REMOTE       = "remote";

/*
 * Where this harness finds the host binary. The build sets it; this harness reads it.
 * The host itself never does, being already running by the time it would matter.
 */
const char *const HOST_PATH_VARIABLE = "CEC_FAKE_AIDL_HOST_PATH";

/*
 * The variable through which the host is told which inherited descriptor to signal on.
 * It carries the NUMBER of the write end of the pipe created below - this harness owns
 * the pipe and names the descriptor; the host parses the value strictly and refuses
 * anything that is not an unadorned non-negative decimal, so it is formatted here as
 * plain digits with no sign, no padding and no surrounding space.
 */
const char *const HOST_READY_FD_VARIABLE = "CEC_FAKE_HOST_READY_FD";

/*
 * The one readiness token, matched VERBATIM including its trailing newline.
 *
 * It is a fixed contract owned by mocks/hdmicec/fake_hdmi_cec_aidl_service_host.cpp and
 * copied here rather than invented: that file writes this exact line, once, with a raw
 * unbuffered write, and only after publication has succeeded and its service-side
 * threadpool is running. The newline is part of the token because a reader matching a
 * line needs the terminator to know the line is whole - a partial write that delivered
 * the name without it must not be mistaken for readiness. Everything else the host
 * prints goes to standard output and is no part of this signal, so the pipe carries the
 * token and nothing else.
 */
const char *const HOST_READINESS_TOKEN = "FAKE_HDMI_CEC_AIDL_HOST_READY\n";

/**
 * @brief How long to wait for the host's readiness token before failing the run.
 *
 * Thirty seconds, and the value is reasoned rather than picked. It has to cover a cold
 * start of a second process that opens the binder driver, reaches the service manager,
 * publishes a service and starts a threadpool - and reaching the service manager retries
 * at ONE-SECOND granularity until binder handle 0 resolves, so a bound of a few hundred
 * milliseconds could expire inside a single healthy retry. The AIDL invocation also runs
 * inside an emulated guest on a loaded CI machine, where every one of those steps is
 * slower than on a developer's host by a margin that is not worth predicting. Thirty
 * seconds is far above any plausible healthy startup and far below any CI job timeout,
 * which is the property that matters: a genuine failure is reported in well under a
 * minute with a diagnostic, instead of hanging until something outside this suite kills
 * it and leaves nobody any wiser.
 */
const int HOST_READINESS_TIMEOUT_MS = 30000;

/**
 * @brief How long to wait for a signalled host to exit before escalating to SIGKILL.
 *
 * Five seconds. The host's shutdown path is a self-pipe write, a few closes and a
 * return, so a host that is going to exit does so in milliseconds; a host that has not
 * gone after five seconds is wedged somewhere this harness cannot reach - blocked inside
 * libbinder, most plausibly - and waiting longer would only trade a diagnosable failure
 * for a hung suite. The escalation exists so that a wedged host cannot outlive the run
 * that started it, which would leave the production service name occupied and make the
 * NEXT run fail on a stale registration.
 */
const int HOST_SHUTDOWN_TIMEOUT_MS = 5000;

/**
 * @brief Upper bound on bytes accepted from the readiness pipe before giving up.
 *
 * The token is thirty bytes and the pipe carries nothing else, so anything approaching
 * this cap means the descriptor is not what this harness thinks it is. Bounding the
 * accumulation keeps a misdirected stream from growing a buffer without limit while the
 * timeout has not yet expired.
 */
const std::size_t HOST_READINESS_MAX_BYTES = 4096;

/** @brief Prefix every diagnostic line this harness prints carries. */
const char *const TRACE_PREFIX = "[CecL2TestEnvironment] ";

/*
 * Exit codes the CHILD reports when it fails before it ever becomes the host.
 *
 * They sit deliberately outside the range the host binary uses for its own documented
 * failures, so that a reader of an exit status can tell "the host ran and refused" from
 * "the host was never reached at all" without consulting either file. The host's codes
 * are file-local to its translation unit and are NOT duplicated here for the same
 * reason: a second copy of that table would be a second thing to keep in step, and it is
 * not needed, because this harness reports the raw status it observed and the host's own
 * trace on standard output names the step it reached.
 */
const int CHILD_EXIT_PRE_EXEC_FAILED = 120;
const int CHILD_EXIT_EXEC_FAILED     = 121;

/**
 * @brief How the wait for the host's readiness token ended.
 *
 * Every failing value is distinguished rather than collapsed into one "not ready",
 * because the three failures have three different causes and a reader of a CI log has
 * only this to go on: a timeout says the host is still running and has not published, an
 * end of file says the host exited before publishing, and a mismatch says something
 * other than the host is on the far end of that descriptor.
 */
enum class ReadinessOutcome {
    Ready,              /**< The token arrived, matched verbatim, and the host is serving. */
    TimedOut,           /**< The bound expired with no token; the host is still alive.     */
    ClosedWithoutToken, /**< The write end closed with no token; the host exited.           */
    TokenMismatch,      /**< A complete line arrived and it was not the token.              */
    PipeError           /**< The descriptor itself failed, which is neither of the above.   */
};

/**
 * @brief Writes a buffer to a descriptor in full, tolerating short and interrupted writes.
 *
 * Used only by the child between fork() and exec(), where a diagnostic is the sole way a
 * pre-exec failure can say anything at all. It calls nothing that allocates and nothing
 * that takes a lock, which is the constraint that applies in that window; the messages it
 * is given are string literals with compile-time lengths, so not even strlen() is
 * involved. A failed write is discarded because there is nothing left to report it to -
 * the caller's very next act is _exit(), and the exit status carries the outcome.
 *
 * @param [in] fd                         - Descriptor to write to
 * @param [in] data                       - Buffer to write. Must not be null
 * @param [in] length                     - Number of bytes to write
 *
 * @return None
 *
 * @warning Deliberately silent on failure. Do not add reporting here; anything richer
 *          would breach the no-allocation constraint this function exists to honour.
 */
void writeRawFully(int fd, const char *data, std::size_t length)
{
    std::size_t written = 0;

    while (written < length) {
        const ssize_t result = ::write(fd, data + written, length - written);

        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }

        if (result == 0) {
            /* No progress on a positive request; retrying would spin. */
            return;
        }

        written += static_cast<std::size_t>(result);
    }
}

/**
 * @brief Renders a wait status as a phrase naming how a process ended.
 *
 * The exit status is the load-bearing half of a host failure diagnostic, because the host
 * gives each failure class a code of its own and writes no readiness line for any of
 * them. It is reported as the raw number rather than translated into the host's own
 * vocabulary: those codes are file-local to the host's translation unit, and a second
 * copy of that table here would be one more thing to keep in step for no gain, since the
 * host also traces the step it reached to standard output, which this runner inherits.
 *
 * @param [in] status                     - Status filled in by waitpid()
 *
 * @return std::string                            - Human-readable description of the end
 *                                                  of the process
 *
 * @see reapHostBlocking()
 */
std::string describeWaitStatus(int status)
{
    if (WIFEXITED(status)) {
        return "exited with status " + std::to_string(WEXITSTATUS(status));
    }

    if (WIFSIGNALED(status)) {
        return "was killed by signal " + std::to_string(WTERMSIG(status));
    }

    return "changed state in an unexpected way (raw wait status " + std::to_string(status) + ")";
}

/**
 * @brief Reaps the host process, blocking until it has been collected.
 *
 * Called only once the host is known to be on its way out - either because end of file on
 * the readiness pipe says it has closed its descriptors, or because it has been sent a
 * signal it cannot block - so the block here is short by construction rather than by
 * hope. Reaping is not optional: an unreaped child becomes a zombie that outlives the
 * suite, and the whole point of owning the host's lifecycle is that nothing survives the
 * run that started it.
 *
 * @param [out] description               - Receives how the process ended, or why it could
 *                                          not be collected
 *
 * @return bool                                   - Whether the process was collected
 * @retval true                                   - Reaped; description names how it ended
 * @retval false                                  - waitpid() failed; description says why
 *
 * @pre g_hostPid names a live or exited child of this process.
 *
 * @see describeWaitStatus(), terminateAndReapFakeServiceHost()
 */
bool reapHostBlocking(std::string &description)
{
    int status = 0;
    pid_t reaped = -1;

    do {
        reaped = ::waitpid(g_hostPid, &status, 0);
    } while (reaped < 0 && errno == EINTR);

    if (reaped < 0) {
        description = std::string("could not be reaped: ") + std::strerror(errno);
        return false;
    }

    description = describeWaitStatus(status);
    return true;
}

/**
 * @brief Launches the fake service host as a child process with a readiness pipe.
 *
 * Creates the pipe, hands its write end to a forked child as an inherited descriptor,
 * names that descriptor in the child's environment and execs the host binary. On success
 * g_hostPid and g_hostReadinessReadFd are set and the caller must wait for the readiness
 * token before doing anything that resolves the back-end selection. On failure nothing is
 * left behind: every error path closes whatever it had opened, so a failed launch cannot
 * leak a descriptor into a run that then reports a different failure.
 *
 * @par Why argv and the environment are built before the fork
 * Everything the child needs is assembled here, in the parent, so that the child's path
 * from fork() to execve() calls nothing that allocates and nothing that takes a lock.
 * setenv() in the child would have been shorter and is the usual shape, but it may
 * reallocate the environment block, and the safe set of operations after a fork does not
 * include allocation. Copying the block and appending one entry costs nothing here and
 * removes the question entirely.
 *
 * @par Descriptor inheritance, stated explicitly because it is easy to get backwards
 * The pipe is created with O_CLOEXEC on BOTH ends, and the child then clears the flag on
 * the write end alone. So the read end is never inherited - it is closed in the child and
 * would be closed again by exec - while the write end survives into the host image, which
 * is the descriptor the host writes its token to. The parent closes its own copy of the
 * write end immediately after the fork, which is what makes end of file on the read end
 * mean "the host closed its write end" rather than "the parent is still holding one open".
 * Getting that one close wrong turns a dead host into a hang.
 *
 * @param [in] hostPath                   - Filesystem path of the host binary, taken from
 *                                          CEC_FAKE_AIDL_HOST_PATH
 * @param [out] failureDetail             - Receives a diagnostic when this reports failure.
 *                                          Untouched on success
 *
 * @return bool                                   - Whether the host was launched
 * @retval true                                   - Forked and exec'd; g_hostPid and
 *                                                  g_hostReadinessReadFd are set
 * @retval false                                  - The binary is unusable, or the pipe or
 *                                                  the fork failed; nothing was left open
 *
 * @post On success a child exists and MUST be reaped, whether or not it ever reports ready.
 *
 * @warning A successful return means the child was started, NOT that the service was
 *          published. Only the readiness token establishes that, which is why
 *          awaitHostReadiness() is not optional and its bound is not optional either.
 *
 * @see awaitHostReadiness(), terminateAndReapFakeServiceHost()
 */
bool startFakeServiceHost(const std::string &hostPath, std::string &failureDetail)
{
    /*
     * Checked here, before the fork, purely so the common misconfiguration - a path that
     * is empty, misspelled, or names something that was never built - produces a sentence
     * naming the path instead of an exec failure inside a child whose only channel is an
     * exit status. The child still handles its own exec failure below, because this check
     * cannot be conclusive: the file could stop being executable between the two, and a
     * test harness that pretends otherwise would be wrong in the one case that matters.
     */
    if (::access(hostPath.c_str(), X_OK) != 0) {
        failureDetail = "\"" + hostPath + "\", named by " + HOST_PATH_VARIABLE +
                        ", is not an executable file (" + std::strerror(errno) +
                        "). The build sets this variable to the fake_hdmi_cec_aidl_host "
                        "binary it built alongside this runner; a stale, unbuilt or "
                        "hand-edited value is the usual cause";
        return false;
    }

    /*
     * O_CLOEXEC on both ends at creation, atomically. pipe() followed by two fcntl() calls
     * would leave a window in which a concurrently forked process inherits a descriptor
     * neither end intended to share, and while this harness is single-threaded at this
     * point, relying on that is a property of today's call site rather than of this
     * function.
     */
    int readinessPipe[2] = { -1, -1 };
    if (::pipe2(readinessPipe, O_CLOEXEC) != 0) {
        failureDetail = std::string("the readiness pipe could not be created: ") +
                        std::strerror(errno);
        return false;
    }

    const int readFd  = readinessPipe[0];
    const int writeFd = readinessPipe[1];

    /*
     * argv is the path and nothing else: the host takes no arguments and reads its whole
     * configuration from the environment, so there is no argument contract for the two
     * files to keep in step.
     */
    std::string hostPathForChild = hostPath;
    char *const childArgv[] = { const_cast<char *>(hostPathForChild.c_str()), nullptr };

    /*
     * The child's environment: this process's own, minus any inherited value of the
     * readiness variable, plus the one this launch is naming. Dropping the inherited value
     * matters - a stale descriptor number from an outer harness would otherwise be
     * ambiguous, and the host, quite correctly, refuses to guess between two.
     *
     * The strings are all appended BEFORE any pointer into them is taken. Growing the
     * vector afterwards would move the std::string objects, and a short string keeps its
     * characters inside the object, so the pointers would dangle.
     */
    const std::string readyFdPrefix     = std::string(HOST_READY_FD_VARIABLE) + "=";
    const std::string readyFdAssignment = readyFdPrefix + std::to_string(writeFd);

    std::vector<std::string> childEnvironmentEntries;
    for (char **cursor = ::environ; cursor != nullptr && *cursor != nullptr; ++cursor) {
        if (std::strncmp(*cursor, readyFdPrefix.c_str(), readyFdPrefix.size()) == 0) {
            continue;
        }
        childEnvironmentEntries.push_back(*cursor);
    }
    childEnvironmentEntries.push_back(readyFdAssignment);

    std::vector<char *> childEnvironment;
    childEnvironment.reserve(childEnvironmentEntries.size() + 1);
    for (std::string &entry : childEnvironmentEntries) {
        childEnvironment.push_back(const_cast<char *>(entry.c_str()));
    }
    childEnvironment.push_back(nullptr);

    const pid_t child = ::fork();

    if (child < 0) {
        failureDetail = std::string("the host process could not be forked: ") +
                        std::strerror(errno);
        ::close(readFd);
        ::close(writeFd);
        return false;
    }

    if (child == 0) {
        /*
         * CHILD. Nothing from here to execve() allocates or locks. Both messages below are
         * string literals whose length is known at compile time, so even strlen() is out
         * of the picture, and _exit() is used rather than exit() so that no copy of the
         * parent's stdio buffers is flushed a second time.
         */
        ::close(readFd);

        if (::fcntl(writeFd, F_SETFD, 0) != 0) {
            static const char preExecMessage[] =
                "[FakeHdmiCecAidlHost:child] the readiness descriptor could not be made "
                "inheritable; the host would have had nothing to signal on\n";
            writeRawFully(STDERR_FILENO, preExecMessage, sizeof(preExecMessage) - 1);
            ::_exit(CHILD_EXIT_PRE_EXEC_FAILED);
        }

        ::execve(childArgv[0], childArgv, childEnvironment.data());

        /*
         * Only reachable when execve() failed, since a successful one does not return.
         * The parent sees this as end of file on the readiness pipe together with this
         * exit status, which its diagnostic distinguishes from a host that ran and
         * refused.
         */
        static const char execMessage[] =
            "[FakeHdmiCecAidlHost:child] the fake service host binary could not be "
            "executed; the path named by CEC_FAKE_AIDL_HOST_PATH is not runnable\n";
        writeRawFully(STDERR_FILENO, execMessage, sizeof(execMessage) - 1);
        ::_exit(CHILD_EXIT_EXEC_FAILED);
    }

    /*
     * PARENT. Closing this copy of the write end is what gives the read end a meaningful
     * end of file: while the parent holds one open, a dead child produces no EOF and the
     * wait below would sit out its whole timeout diagnosing the wrong thing.
     */
    ::close(writeFd);

    g_hostPid              = child;
    g_hostReadinessReadFd  = readFd;

    std::cout << TRACE_PREFIX << "Launched the out-of-process fake HDMI CEC AIDL service host \""
              << hostPath << "\" as pid " << static_cast<long>(child) << ", signalling readiness on "
              << HOST_READY_FD_VARIABLE << "=" << writeFd << std::endl;
    return true;
}


/**
 * @brief Renders received bytes as a single short, printable phrase for a diagnostic.
 *
 * Whatever arrived on the readiness pipe when the expected token did not is reported back
 * to whoever has to fix it, and it is reported as one bounded line: the trailing newline
 * is dropped so the surrounding sentence stays intact, and an over-long value is elided
 * rather than pasted whole into a CI log.
 *
 * @param [in] received                   - Bytes observed on the readiness pipe
 *
 * @return std::string                            - A single-line, length-bounded rendering
 *
 * @see awaitHostReadiness()
 */
std::string renderForDiagnostic(const std::string &received)
{
    std::string rendered = received;

    while (!rendered.empty() && (rendered.back() == '\n' || rendered.back() == '\r')) {
        rendered.pop_back();
    }

    const std::size_t limit = 120;
    if (rendered.size() > limit) {
        rendered = rendered.substr(0, limit) + "... (truncated)";
    }

    return rendered;
}

/**
 * @brief Blocks until the host reports ready, or the bound expires.
 *
 * A genuine blocking wait on the readiness pipe, bounded against a monotonic deadline.
 * There is no sleep and no polling interval anywhere in it: poll() is given the time
 * remaining until the deadline and returns the moment a byte arrives, so a healthy host is
 * noticed immediately and an unhealthy one is given the full bound and no more. The
 * deadline is taken from steady_clock rather than accumulated from per-iteration
 * timeouts, so an interrupted poll() resumes against the ORIGINAL deadline and cannot
 * extend the bound by being interrupted repeatedly - and steady_clock in particular
 * because a wall-clock adjustment mid-run must not shorten or lengthen it.
 *
 * @param [out] observed                  - Receives what actually arrived: the complete
 *                                          first line on a match or a mismatch, whatever
 *                                          partial bytes had accumulated on a timeout or
 *                                          an end of file, or the failing call's errno text
 *
 * @return ReadinessOutcome                       - How the wait ended
 * @retval ReadinessOutcome::Ready                - The token arrived and matched verbatim
 * @retval ReadinessOutcome::TimedOut             - The bound expired; the host is alive but
 *                                                  has not published
 * @retval ReadinessOutcome::ClosedWithoutToken   - The write end closed with no token; the
 *                                                  host exited, and its exit status says why
 * @retval ReadinessOutcome::TokenMismatch        - A complete line arrived that was not the
 *                                                  token
 * @retval ReadinessOutcome::PipeError            - poll() or read() failed on the descriptor
 *
 * @pre startFakeServiceHost() has reported success, so g_hostReadinessReadFd is open and
 *      this process has closed its own copy of the write end.
 *
 * @warning Reporting anything other than Ready means the AIDL invocation CANNOT be run.
 *          The caller must fail the run rather than continue: continuing would leave the
 *          selection on the legacy back-end and describe the result as an AIDL one.
 *
 * @see startFakeServiceHost(), HOST_READINESS_TIMEOUT_MS, HOST_READINESS_TOKEN
 */
ReadinessOutcome awaitHostReadiness(std::string &observed)
{
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(HOST_READINESS_TIMEOUT_MS);

    std::string received;

    for (;;) {
        const std::chrono::milliseconds remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());

        if (remaining.count() <= 0) {
            observed = received;
            return ReadinessOutcome::TimedOut;
        }

        struct pollfd watched;
        watched.fd      = g_hostReadinessReadFd;
        watched.events  = POLLIN;
        watched.revents = 0;

        const int ready = ::poll(&watched, 1, static_cast<int>(remaining.count()));

        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            observed = std::string("poll() on the readiness pipe failed: ") +
                       std::strerror(errno);
            return ReadinessOutcome::PipeError;
        }

        if (ready == 0) {
            observed = received;
            return ReadinessOutcome::TimedOut;
        }

        /*
         * POLLHUP is reported whether or not it was requested, and it can arrive together
         * with buffered data, so the outcome is decided by what read() returns rather than
         * by the event bits: pending bytes are consumed first and end of file is only
         * concluded from a zero-length read.
         */
        char chunk[128];
        const ssize_t got = ::read(g_hostReadinessReadFd, chunk, sizeof(chunk));

        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            observed = std::string("read() on the readiness pipe failed: ") +
                       std::strerror(errno);
            return ReadinessOutcome::PipeError;
        }

        if (got == 0) {
            observed = received;
            return ReadinessOutcome::ClosedWithoutToken;
        }

        received.append(chunk, static_cast<std::size_t>(got));

        /*
         * Matched only once a whole line is in hand. A partial read that delivered the
         * token's name without its newline is NOT readiness, and treating it as such would
         * accept a truncated write as a published service.
         */
        const std::size_t terminator = received.find('\n');
        if (terminator != std::string::npos) {
            const std::string firstLine = received.substr(0, terminator + 1);
            observed = firstLine;
            return (firstLine == HOST_READINESS_TOKEN) ? ReadinessOutcome::Ready
                                                       : ReadinessOutcome::TokenMismatch;
        }

        if (received.size() >= HOST_READINESS_MAX_BYTES) {
            observed = received;
            return ReadinessOutcome::TokenMismatch;
        }
    }
}

/**
 * @brief Waits for the host to exit, detected as end of file on the readiness pipe.
 *
 * Bounded, and free of any timer: while the host is alive it holds the write end of the
 * readiness pipe open, so end of file on the read end happens when and only when the host
 * process has gone. Waiting on that is a real wait on the real event, where polling
 * waitpid() with WNOHANG around a sleep would be a guess about how long death takes -
 * slow to notice a prompt exit and pure delay when the host is wedged.
 *
 * @param [in] timeoutMs                  - Longest time to wait, in milliseconds
 *
 * @return bool                                   - Whether the host was observed to exit
 * @retval true                                   - End of file arrived; the host has exited
 *                                                  and a blocking reap will return at once
 * @retval false                                  - The bound expired, or the descriptor
 *                                                  failed, or none was ever opened
 *
 * @see terminateAndReapFakeServiceHost()
 */
bool waitForHostExitViaPipeEof(int timeoutMs)
{
    if (g_hostReadinessReadFd < 0) {
        return false;
    }

    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    for (;;) {
        const std::chrono::milliseconds remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());

        if (remaining.count() <= 0) {
            return false;
        }

        struct pollfd watched;
        watched.fd      = g_hostReadinessReadFd;
        watched.events  = POLLIN;
        watched.revents = 0;

        const int ready = ::poll(&watched, 1, static_cast<int>(remaining.count()));

        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        if (ready == 0) {
            return false;
        }

        char drain[128];
        const ssize_t got = ::read(g_hostReadinessReadFd, drain, sizeof(drain));

        if (got == 0) {
            return true;
        }

        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        /*
         * Bytes rather than end of file: the host wrote something after its token. It is no
         * part of any contract, so it is drained and the wait continues.
         */
    }
}

/**
 * @brief Terminates the fake service host, waits for it to go, and reaps it.
 *
 * Idempotent and safe after a partial setup, which is not decoration: this runs from the
 * readiness-failure path in SetUp and again from TearDown, and TearDown executes even when
 * SetUp failed fatally. Clearing g_hostPid on the way out is what makes the second call a
 * no-op instead of a signal sent to a pid this process no longer owns - a pid the operating
 * system is free to have reused.
 *
 * SIGTERM first, because that is the signal the host waits for and its handler runs a clean
 * shutdown. SIGKILL only after the bounded wait expires, so that a host wedged somewhere
 * this harness cannot reach still cannot outlive the run - an orphan holding the production
 * service name would make the NEXT run fail on a stale registration, turning one failure
 * into a series of them.
 *
 * @return std::string                            - How the host ended, for a caller that is
 *                                                  building a diagnostic. Empty when no host
 *                                                  was ever launched
 *
 * @post g_hostPid is -1 and g_hostReadinessReadFd is closed, so no descriptor and no child
 *       outlive this call.
 *
 * @see reapHostBlocking(), waitForHostExitViaPipeEof()
 */
std::string terminateAndReapFakeServiceHost()
{
    if (g_hostPid <= 0) {
        /*
         * Nothing was launched - the legacy invocation, or a launch that failed before it
         * forked - or this has already run. Either way there is nothing to signal; only a
         * descriptor might remain, and only if a launch failed after creating the pipe.
         */
        if (g_hostReadinessReadFd >= 0) {
            ::close(g_hostReadinessReadFd);
            g_hostReadinessReadFd = -1;
        }
        return std::string();
    }

    const long hostPid = static_cast<long>(g_hostPid);

    std::cout << TRACE_PREFIX << "Terminating the fake HDMI CEC AIDL service host pid " << hostPid
              << " with SIGTERM" << std::endl;

    if (::kill(g_hostPid, SIGTERM) != 0) {
        /*
         * Almost always ESRCH, meaning the host has already exited - which is exactly the
         * case on the readiness-failure path. It still has to be reaped, so this is traced
         * and not treated as an error.
         */
        std::cout << TRACE_PREFIX << "SIGTERM could not be delivered to pid " << hostPid << " ("
                  << std::strerror(errno) << "); it has most likely already exited, and it is "
                  "reaped below regardless" << std::endl;
    }

    if (!waitForHostExitViaPipeEof(HOST_SHUTDOWN_TIMEOUT_MS)) {
        std::cout << TRACE_PREFIX << "Pid " << hostPid << " had not exited "
                  << HOST_SHUTDOWN_TIMEOUT_MS << " ms after SIGTERM; escalating to SIGKILL so it "
                  "cannot outlive this run and occupy the production service name for the next one"
                  << std::endl;
        ::kill(g_hostPid, SIGKILL);
    }

    std::string outcome;
    const bool reaped = reapHostBlocking(outcome);

    std::cout << TRACE_PREFIX << "The fake HDMI CEC AIDL service host pid " << hostPid << " "
              << outcome << std::endl;

    g_hostPid = -1;

    if (g_hostReadinessReadFd >= 0) {
        ::close(g_hostReadinessReadFd);
        g_hostReadinessReadFd = -1;
    }

    /*
     * Reported rather than swallowed, and non-fatally: a host this harness could not
     * collect is a leaked process and worth saying so, but it is not a reason to cut short
     * the rest of a teardown.
     */
    EXPECT_TRUE(reaped) << "the fake HDMI CEC AIDL service host pid " << hostPid << " " << outcome
                        << ", so it may survive this run as an orphan holding the production "
                           "service name";

    return outcome;
}

/**
 * @brief Launches the fake service host and blocks until it reports ready.
 *
 * The whole of the remote mode's preparation, and it must complete BEFORE anything resolves
 * the back-end selection. Every failure below fails the run: a launch that did not happen,
 * or happened and never became ready, means the AIDL invocation cannot be performed at all,
 * and the alternative to failing is a suite that silently exercises the legacy back-end and
 * reports the result as AIDL evidence.
 *
 * @return None
 *
 * @post On return without a failure the fake service is published, its service-side
 *       threadpool is running, and the selection has not yet been resolved.
 *
 * @warning Raises a fatal GoogleTest failure on every unhappy path, so callers must invoke
 *          it through ASSERT_NO_FATAL_FAILURE to stop rather than continue.
 *
 * @see startFakeServiceHost(), awaitHostReadiness(), terminateAndReapFakeServiceHost()
 */
void launchHostAndWaitUntilReady()
{
    const char *const rawHostPath = ::getenv(HOST_PATH_VARIABLE);

    ASSERT_TRUE(rawHostPath != nullptr && rawHostPath[0] != '\0')
        << AIDL_MODE_VARIABLE << "=" << AIDL_MODE_REMOTE << " requires " << HOST_PATH_VARIABLE
        << " to name the fake service host binary, and it is unset or empty. The build sets it "
           "to the fake_hdmi_cec_aidl_host binary built alongside this runner; with nothing to "
           "launch there is no service to find, and continuing would select the legacy back-end "
           "and report a green result for an AIDL invocation that never ran";

    std::string failureDetail;
    ASSERT_TRUE(startFakeServiceHost(rawHostPath, failureDetail))
        << "the fake HDMI CEC AIDL service host could not be launched: " << failureDetail;

    std::string observed;
    const ReadinessOutcome outcome = awaitHostReadiness(observed);

    if (outcome == ReadinessOutcome::Ready) {
        std::cout << TRACE_PREFIX << "The fake HDMI CEC AIDL service host reported ready; the "
                  "service is published and served, and the back-end selection has NOT yet been "
                  "resolved" << std::endl;
        return;
    }

    /*
     * Reaped BEFORE the failure is raised, for two reasons: a fatal GoogleTest failure
     * returns from this function immediately, so anything after it would not run, and the
     * reap is what supplies the host's exit status - the single most informative thing
     * about why no token arrived.
     */
    const std::string hostEnd = terminateAndReapFakeServiceHost();

    if (outcome == ReadinessOutcome::ClosedWithoutToken) {
        FAIL() << "the fake HDMI CEC AIDL service host exited without reporting ready, so no "
                  "service was ever published. It " << hostEnd << ". The host writes no readiness "
                  "line on ANY failure path - a stale registration under the production service "
                  "name, an absent binder driver node, a service manager that refused the "
                  "registration - and traces the step it reached to standard output, prefixed "
                  "[FakeHdmiCecAidlHost]; that trace names the cause"
               << (observed.empty() ? std::string()
                                    : (". Partial output before it closed: \"" +
                                       renderForDiagnostic(observed) + "\""));
        return;
    }

    if (outcome == ReadinessOutcome::TimedOut) {
        FAIL() << "the fake HDMI CEC AIDL service host did not report ready within "
               << HOST_READINESS_TIMEOUT_MS << " ms, and it was still running when the bound "
                  "expired, so it is blocked rather than broken. It " << hostEnd << ". The likely "
                  "cause is a binder driver node with no service manager behind it: reaching the "
                  "service manager retries indefinitely, which the host cannot bound and this "
                  "wait therefore has to. A running servicemanager is an unconditional runtime "
                  "prerequisite wherever a binder driver is present"
               << (observed.empty() ? std::string()
                                    : (". Partial output received: \"" +
                                       renderForDiagnostic(observed) + "\""));
        return;
    }

    if (outcome == ReadinessOutcome::TokenMismatch) {
        FAIL() << "the readiness pipe delivered \"" << renderForDiagnostic(observed)
               << "\" where the fake HDMI CEC AIDL service host's readiness token was expected. "
                  "The token is matched verbatim on purpose, so this is not a near miss to be "
                  "accepted: either the descriptor named by " << HOST_READY_FD_VARIABLE
               << " is not the pipe this harness created, or the binary at " << HOST_PATH_VARIABLE
               << " is not the fake service host. It " << hostEnd;
        return;
    }

    FAIL() << "the readiness pipe could not be read, so whether the fake HDMI CEC AIDL service "
              "host became ready cannot be established: " << observed << ". It " << hostEnd;
}

/**
 * @brief Reads CEC_TEST_AIDL_MODE and does what it asks, before the selection resolves.
 *
 * The one place this tier's two invocations diverge. The legacy mode returns having touched
 * neither libbinder nor a second process, which is what keeps a plain ./run_L2Tests runnable
 * on a host with no kernel binder support at all; the remote mode does the whole launch and
 * handshake here, ahead of init, because here is the only place it still can.
 *
 * @return None
 *
 * @warning An unrecognised value is a fatal failure and NEVER a quiet fall back to the
 *          legacy mode. Unset is the single permissive case, because a bare ./run_L2Tests
 *          legitimately means "run the legacy arm"; a typo does not, and a typo that
 *          silently downgraded the run would report a green result for an invocation that
 *          never happened.
 * @warning Raises fatal GoogleTest failures, so it must be called through
 *          ASSERT_NO_FATAL_FAILURE.
 *
 * @see launchHostAndWaitUntilReady()
 */
void applyAidlModeBeforeInit()
{
    const char *const requested = ::getenv(AIDL_MODE_VARIABLE);

    // Unset and empty both mean absent, so the legacy arm is what a bare run does.
    const std::string mode =
        (requested != nullptr && requested[0] != '\0') ? requested : AIDL_MODE_ABSENT;

    if (mode == AIDL_MODE_ABSENT) {
        std::cout << TRACE_PREFIX << AIDL_MODE_VARIABLE << "=" << mode
                  << ": launching no fake service host, so the legacy back-end is expected and "
                     "this process touches libbinder not at all" << std::endl;
        return;
    }

    if (mode == AIDL_MODE_REMOTE) {
        ASSERT_NO_FATAL_FAILURE(launchHostAndWaitUntilReady());
        return;
    }

    if (mode == AIDL_MODE_COMPATIBLE || mode == AIDL_MODE_INCOMPATIBLE) {
        FAIL() << AIDL_MODE_VARIABLE << "=" << mode << " is not implemented by run_L2Tests. It "
                  "means \"register a fake service INSIDE THIS PROCESS\", which only run_L1Tests "
                  "does: libbinder resolves a locally registered name to the local BBinder, so "
                  "such a registration produces no proxy, no transaction across the driver and no "
                  "callback on a binder thread - none of which this tier exists to test. Refusing "
                  "rather than treating it as " << AIDL_MODE_ABSENT << ", because a misconfigured "
                  "invocation that quietly ran the legacy arm would be reported as a pass. Use "
                  "run_L1Tests for this mode, or " << AIDL_MODE_REMOTE << " for the "
                  "out-of-process equivalent";
        return;
    }

    FAIL() << AIDL_MODE_VARIABLE << " is set to \"" << mode << "\", which is not a recognised "
              "mode. run_L2Tests implements " << AIDL_MODE_ABSENT << " and " << AIDL_MODE_REMOTE
           << "; " << AIDL_MODE_COMPATIBLE << " and " << AIDL_MODE_INCOMPATIBLE << " belong to "
              "run_L1Tests. Refusing to fall back to " << AIDL_MODE_ABSENT << ", because a typo "
              "must not quietly downgrade the run to the legacy back-end and report it as a pass";
}

} // namespace

// Global test environment to set up mocks
class CecL2TestEnvironment : public ::testing::Environment {
public:
    /**
     * @brief Brings the process to the state every L2 case assumes, in a load-bearing order.
     *
     * Installs the legacy HAL double, then does whatever the requested mode asks - which on
     * the remote mode means launching the fake service host and blocking until it reports
     * ready - and only then initializes the CEC library.
     *
     * @return None
     *
     * @post On return without a failure the CEC library is initialized, the back-end
     *       selection is resolved and fixed for the lifetime of the process, and the
     *       requested back-end is the one that was selected.
     *
     * @warning THE ORDER OF THE THREE STEPS BELOW IS THE POINT OF THIS TIER. The init call
     *          is the first thing in this binary that forces Driver::getInstance(), which
     *          resolves the selection once and for all; a service that becomes reachable
     *          after it is a service the middleware never looks for. Moving the mode handling
     *          below init would not fail - it would pass, on the legacy back-end, and report
     *          the run as AIDL evidence.
     * @warning A fatal failure here skips every case in the binary and exits non-zero, and
     *          TearDown still runs, which is why teardown tolerates a partial setup.
     *
     * @see applyAidlModeBeforeInit()
     */
    void SetUp() override {
        // Create and install the driver mock
        g_driverMock = new HdmiCecDriverMock();
        HdmiCecDriverMock::setInstance(g_driverMock);

        // Decide what the service lookup inside init() below is going to find - and, on the
        // remote mode, have a real service published and served by a real second process
        // before it looks. This runs first because init() is the one-way door.
        ASSERT_NO_FATAL_FAILURE(applyAidlModeBeforeInit());

        /*
         * The ordering property this tier rests on, made observable in the run's own log
         * rather than left to be inferred from the source: this line cannot be printed until
         * the readiness wait above has returned, and init() cannot run until it has been
         * printed. A log in which it precedes the host's readiness trace is a bug.
         */
        std::cout << TRACE_PREFIX << "Initializing the CEC library; this is the call that "
                     "resolves the back-end selection for the lifetime of the process"
                  << std::endl;

        /*
         * NOT SWALLOWED. The L1 template this file derives from wrapped this in catch (...)
         * and discarded whatever came out, under the claim that a second initialization
         * could be ignored - but this environment's SetUp runs exactly once per process, so
         * that condition cannot arise and nothing here can legitimately be ignored. What
         * init() does raise is real: Driver::getInstance().open() refused by the selected
         * HAL, or Bus::start() failing. Swallowing either reported a green suite for a
         * process that never initialized, with every case then asserting against an unopened
         * stack.
         */
        ASSERT_NO_THROW({ LibCCEC::getInstance().init("CEC_TEST"); })
            << "the CEC library could not be initialized, so not one case in this binary has "
               "its precondition; continuing would assert against an uninitialized stack";
    }

    /**
     * @brief Shuts the CEC stack down, releases the HAL double and reaps the host.
     *
     * @return None
     *
     * @post No child process and no descriptor opened by this harness outlives the run.
     *
     * @warning THE ORDER MATTERS HERE TOO, in the opposite direction. term() reaches
     *          Driver::close(), which on the AIDL back-end is a TRANSACTION TO THE HOST, so
     *          the host must still be serving when it runs. Reaping first would turn an
     *          orderly shutdown into a failed close against a process that had already gone.
     * @warning Runs even when SetUp failed, so every step tolerates never having happened:
     *          term() raises when the library was never initialized, and the host reap is a
     *          no-op when no host was launched or one was already reaped on the failure path.
     *          Failures are reported NON-fatally, so that a first problem cannot hide the
     *          cleanup that follows it.
     *
     * @see terminateAndReapFakeServiceHost()
     */
    void TearDown() override {
        /*
         * Reported rather than swallowed, but non-fatally and deliberately so. TearDown runs
         * after the suite, so every result has already been recorded; a fatal assertion here
         * would both obscure results that were legitimately earned and cut short the cleanup
         * below. One failure here is honest rather than spurious: when SetUp failed, the
         * library was never initialized, so term() raises and is reported alongside the
         * original failure - which correctly says the process never came up.
         */
        EXPECT_NO_THROW({ LibCCEC::getInstance().term(); })
            << "the CEC library could not be terminated cleanly; the remaining cleanup below "
               "still runs, but this process did not shut the CEC stack down properly";

        HdmiCecDriverMock::setInstance(nullptr);
        delete g_driverMock;
        g_driverMock = nullptr;

        /*
         * Last, because term() above needed the host alive to answer its close. Idempotent,
         * so a host already reaped by the readiness-failure path costs nothing here.
         */
        terminateAndReapFakeServiceHost();
    }
};

/**
 * @brief Entry point for the L2 integration runner.
 *
 * @param [in] argc                       - Argument count, consumed by GoogleTest
 * @param [in] argv                       - Argument vector, consumed by GoogleTest
 *
 * @return int                                    - Zero when every case passed, non-zero
 *                                                  otherwise, including when the global
 *                                                  environment failed to come up
 *
 * @warning Configuration comes from the environment - CEC_TEST_AIDL_MODE and
 *          CEC_FAKE_AIDL_HOST_PATH - and never from the command line, so that every flag on
 *          this binary remains GoogleTest's own.
 */
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new CecL2TestEnvironment);
    return RUN_ALL_TESTS();
}

