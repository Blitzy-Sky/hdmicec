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
 * @defgroup HDMI_CEC_FAKE_AIDL_SERVICE_HOST HDMI CEC Fake AIDL Service Host
 * @ingroup HDMI_CEC_FAKE_AIDL_SERVICE
 * @{
 * @par Fake Service Host Specification
 * This translation unit is a whole program: a main() that publishes the test-scope fake
 * com.rdk.hal.hdmicec service in a process of its own, serves transactions against it, and lives
 * until its parent asks it to stop.@n
 * It exists because process boundaries are not an implementation detail of binder - they are the
 * thing under test.  libbinder resolves a service name that was registered in the CALLING process
 * to the local BBinder, so interface_cast there hands back that very object: no Bp* proxy is
 * created, no transaction crosses the binder driver, and the client's threadpool is never involved.
 * A fake registered inside the test runner therefore cannot prove the transport at all, however
 * faithfully it implements the interface.  Hosting the same fake HERE, in a separate process, is
 * what makes the middleware hold a real proxy and receive its event callbacks on a binder thread.
 *
 * There is deliberately nothing else in this file.  It declares no class, wraps nothing, registers
 * nothing with anything, and adds no abstraction of any kind: the fake it hosts is already written
 * and already knows how to publish itself, so this program is a startup order, a readiness signal
 * and a shutdown wait.
 *
 */

/**
 * @file fake_hdmi_cec_aidl_service_host.cpp
 *
 * @brief Separate-process host for the test-scope fake com.rdk.hal.hdmicec AIDL HdmiCec service.
 *
 * Publishes the fake declared in fake_hdmi_cec_aidl_service.h under the production service name,
 * starts the SERVICE-side binder threadpool that serves incoming transactions, signals readiness to
 * the parent that launched it, and waits for a termination signal.@n
 * This binary is the only place the AIDL receive path is genuinely exercised: it is the counterpart
 * of the out-of-process test invocation, in which the middleware resolves a real proxy over the
 * binder driver and its listener callback arrives on a binder threadpool thread rather than on the
 * caller's own stack.  Without this program that invocation cannot exist.
 *
 * @par The lifecycle contract, in full
 * This file is the normative statement of the host side of the contract.  The parent end lives in
 * the L2 runner's harness (tests/L2Tests/test_main.cpp) and is written against the text below.
 *
 * Startup, in this order and no other:
 * -# Install the shutdown handlers, so that a termination signal arriving at any later point still
 *    produces a clean exit rather than a default-disposition kill.
 * -# Resolve the readiness file descriptor.  It is validated here, before any side effect, so that a
 *    botched handoff costs nothing; the readiness line itself is not written until step 6.
 * -# Confirm a binder driver node is present and openable.
 * -# Confirm nothing is already published under the production service name.  Something there is a
 *    HARD FAILURE and never a condition to work around: this run's outcome would otherwise depend on
 *    a process this suite does not own.
 * -# Construct the fake, start the service-side threadpool, then publish the fake.  The pool is
 *    started BEFORE publication so that no transaction can ever arrive with no thread to serve it.
 * -# Write the readiness line - only now, after publication has succeeded and the pool is running,
 *    so that a parent which has seen the line may rely on the service being both published and
 *    served.
 * -# Block until signalled, then return EXIT_SUCCESS.
 *
 * @par Environment
 * - `CEC_FAKE_AIDL_HOST_PATH` is where the parent finds this binary.  The build sets it and the L2
 *   harness reads it; this program never does, being already running by the time it matters.
 * - `CEC_FAKE_HOST_READY_FD` is the number of an inherited file descriptor - the write end of a pipe
 *   the parent holds open - on which the readiness line is delivered.  Unset means "no parent is
 *   listening", and the line goes to standard output instead so that a human can run this binary
 *   from a shell and watch it work.  Set but not a usable non-negative descriptor number is a hard
 *   failure, because a parent that asked to be signalled on a pipe and was silently answered on
 *   standard output would wait out its whole timeout with nothing to explain why.
 *
 * @par Readiness
 * The readiness signal is the exact line `FAKE_HDMI_CEC_AIDL_HOST_READY\n`, written once, with a raw
 * write that loops over partial writes and retries an interrupted one.  It is a fixed token and the
 * parent matches it verbatim.  Everything else this program prints is diagnostics on standard output
 * and no part of the signal.
 *
 * @par The parent's obligations
 * The parent creates the pipe, passes its write end to this child as an inherited descriptor, names
 * that descriptor in `CEC_FAKE_HOST_READY_FD`, and waits for the token under a BOUNDED TIMEOUT after
 * which it fails the run rather than proceeding.  The bound is not optional: a run that proceeds
 * without the token tests the legacy back-end while reporting an AIDL result.  In its teardown the
 * parent terminates this child and reaps it, so that no host outlives the suite that launched it.
 *
 * @warning `CEC_TEST_AIDL_MODE` is NOT read here.  That variable is read by the L1 and L2 harnesses
 *          and nowhere else; this host is LAUNCHED by the remote mode rather than being told about
 *          it, and a second reader of it would be a second place for the modes to drift.
 * @warning A sleep-based readiness signal is not acceptable anywhere in this contract.  A timed wait
 *          in place of the token converts a race into a flake: it passes on a fast machine, fails on
 *          a loaded one, and never once proves the service was actually published.
 * @warning This host does NOT reimplement the middleware's bounded binder preflight.  That predicate
 *          is production code in the middleware's own source directory and this binary links no
 *          libRCEC, so its protocol-version equality check and its bounded wait for binder handle 0
 *          are both absent here.  One
 *          consequence survives and is real: where a driver node exists but no service manager is
 *          running, reaching the service manager BLOCKS - it retries in one-second intervals until
 *          binder handle 0 resolves - and this program will sit there.  The parent's bounded
 *          readiness timeout is the only guard against that case, which is part of why it is
 *          mandatory.  A service manager is an unconditional runtime prerequisite wherever a binder
 *          driver is present.
 * @note The threadpool started here is the SERVICE side, serving transactions addressed to the fake.
 *       It is unrelated to the client-side pool, which the middleware's AIDL back-end starts inside
 *       its own open() call.  The fake implementation itself starts neither, which is what keeps the
 *       in-process and out-of-process cases distinguishable.
 * @warning Test scope only.  This program is a noinst_PROGRAMS target built for test targets
 *          exclusively; it is never installed and no production source list references the directory
 *          it lives in, so nothing here can reach the shipped middleware library.
 *
 * @see fake_hdmi_cec_aidl_service.h
 * @see registerFakeHdmiCecService()
 */

#include "fake_hdmi_cec_aidl_service.h"

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <com/rdk/hal/hdmicec/IHdmiCec.h>
#include <utils/String16.h>
#include <utils/StrongPointer.h>

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>

/**
 * @brief Prefix every diagnostic line this program prints carries.
 *
 * A failed out-of-process invocation is diagnosed from these lines and nothing else: the parent sees
 * a timeout, and only this output says which step the host reached before it stopped.  The prefix is
 * what separates them from the fake's own `[FakeRegistration]` and `[Fake...]` lines in an
 * interleaved capture.
 */
static const char TRACE_PREFIX[] = "[FakeHdmiCecAidlHost] ";

/**
 * @brief The one readiness token, spelled exactly once.
 *
 * The parent matches this line verbatim, so it is a fixed contract and not free to vary.  The
 * trailing newline is part of it: a parent reading a line needs the terminator to know the line is
 * whole.
 */
static const char READINESS_TOKEN[] = "FAKE_HDMI_CEC_AIDL_HOST_READY\n";

/**
 * @brief Environment variable naming the inherited descriptor the readiness line is written to.
 *
 * Unset means no parent is listening and the token goes to standard output.  Set but unusable is a
 * hard failure - see resolveReadinessFd().
 */
static const char READY_FD_VARIABLE[] = "CEC_FAKE_HOST_READY_FD";

/**
 * @brief Binder driver node checked before anything in this process touches libbinder.
 *
 * The linked libbinder aborts the whole process when it cannot open its driver, so on a host without
 * kernel binder support an unguarded service-manager call would kill this program outright: no
 * diagnostic, no exit code, nothing for the parent to report but a timeout.  Checking the node first
 * turns that into a traced failure with an exit code of its own.@n
 * This single check is NOT the middleware's bounded preflight, which additionally requires the
 * driver's protocol version to equal the one libbinder was built for and waits for binder handle 0
 * under a bound.  Neither of those is performed here, and the @warning on this file records what
 * that leaves exposed.  The node name is spelled here because it cannot be shared: the fake's own
 * copy is file-local to its implementation and the middleware's is production code this binary
 * deliberately does not link.
 */
static const char BINDER_DRIVER_PATH[] = "/dev/binder";

/**
 * @brief Exit code reported when something is already published under the production service name.
 */
static const int EXIT_STALE_REGISTRATION = 2;

/**
 * @brief Exit code reported when CEC_FAKE_HOST_READY_FD is set to something unusable.
 */
static const int EXIT_BAD_READY_FD = 3;

/**
 * @brief Exit code reported when the service manager refused to publish the fake.
 */
static const int EXIT_REGISTRATION_FAILED = 4;

/**
 * @brief Exit code reported when the readiness line could not be written.
 */
static const int EXIT_READINESS_WRITE_FAILED = 5;

/**
 * @brief Exit code reported when no usable binder driver node is present.
 */
static const int EXIT_NO_BINDER_TRANSPORT = 6;

/**
 * @brief Exit code reported when this program could not set itself up.
 *
 * Covers the failures that are neither configuration nor binder state: the self-pipe, the signal
 * handlers, constructing the fake, and a service manager that cannot be reached at all.  Every one
 * of them means this host never became able to serve, so none of them may exit zero.
 */
static const int EXIT_SETUP_FAILED = 7;

/*
 * State the signal handler touches, and therefore the only state in this file that is not local to a
 * function.  Both objects are volatile sig_atomic_t because a handler may run between any two
 * instructions of main and nothing weaker is guaranteed to be read or written indivisibly.
 *
 * EXIT_FAILURE - plain 1 - is deliberately never returned by this program.  Every failure class
 * above has a number of its own, so a parent, a CI log or a person reading an exit status can tell
 * "a stale service was already published" from "the readiness pipe was wrong" without reading the
 * output.
 */

/** @brief Signal number that asked this program to stop, or 0 while none has. */
static volatile std::sig_atomic_t g_shutdownSignalNumber = 0;

/** @brief Write end of the self-pipe, or -1 before it exists.  Written by main, read by the handler. */
static volatile std::sig_atomic_t g_shutdownPipeWriteFd = -1;

/** @brief Read end of the self-pipe, or -1 before it exists.  Never touched by the handler. */
static int g_shutdownPipeReadFd = -1;

extern "C" {

/**
 * @brief Records a termination request and wakes the waiting main thread.
 *
 * The whole of the handler: record the signal number, then write one byte to the self-pipe so that
 * the blocking read in waitForShutdownSignal() completes.  Nothing else happens here, and nothing
 * else may - this runs asynchronously between arbitrary instructions, so a stream insertion, an
 * allocation or a binder call would risk deadlocking against a lock the interrupted code already
 * holds.  Both objects it touches are volatile sig_atomic_t and write() is async-signal-safe.@n
 * errno is saved and restored around the write, because the interrupted code may be in the middle of
 * examining its own errno and this handler must not be able to change what it reads.
 *
 * @param [in] signalNumber               - Signal being delivered, recorded for the exit trace
 *
 * @post waitForShutdownSignal() returns, and g_shutdownSignalNumber names the signal.
 *
 * @warning Async-signal-safe by construction.  Adding any other call - tracing included - would
 *          break that property, which is why the exit trace is emitted by main after the wait
 *          returns rather than from here.
 *
 * @see waitForShutdownSignal(), installShutdownHandlers()
 */
static void handleShutdownSignal(int signalNumber)
{
    const int savedErrno = errno;

    g_shutdownSignalNumber = signalNumber;

    const int writeFd = g_shutdownPipeWriteFd;
    if (writeFd >= 0) {
        const unsigned char wakeByte = 1;
        ssize_t written;
        do {
            written = ::write(writeFd, &wakeByte, sizeof(wakeByte));
        } while (written < 0 && errno == EINTR);
    }

    errno = savedErrno;
}

} // extern "C"

/**
 * @brief Creates the self-pipe and installs the termination handlers.
 *
 * Establishes the shutdown path before anything else happens, so that a signal arriving at any later
 * point - during registration, or while the readiness line is being written - still produces the
 * clean exit the parent expects instead of the default disposition's abrupt kill.@n
 * SIGTERM is what the parent sends when it tears the host down.  SIGINT is handled beside it so that
 * a person running this binary by hand can stop it with the same clean path the suite uses, rather
 * than exercising an exit route the suite never takes.  Neither handler sets SA_RESTART: an
 * interrupted wait is retried explicitly, which keeps the interruption visible in one place instead
 * of relying on the kernel to hide it.
 *
 * @return bool                                   - Whether the shutdown path was established
 * @retval true                                   - Self-pipe created and both handlers installed
 * @retval false                                  - The pipe or a handler could not be installed, in
 *                                                  which case this program has no clean way to stop
 *                                                  and must not continue
 *
 * @post On success g_shutdownPipeReadFd and g_shutdownPipeWriteFd are open, and a termination signal
 *       completes the wait in waitForShutdownSignal().
 *
 * @see handleShutdownSignal(), waitForShutdownSignal()
 */
static bool installShutdownHandlers()
{
    int selfPipe[2] = { -1, -1 };
    if (::pipe(selfPipe) != 0) {
        std::cout << TRACE_PREFIX << "Could not create the shutdown self-pipe: "
                  << std::strerror(errno) << std::endl;
        return false;
    }

    g_shutdownPipeReadFd = selfPipe[0];
    g_shutdownPipeWriteFd = selfPipe[1];

    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_handler = handleShutdownSignal;
    ::sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    const int handledSignals[] = { SIGTERM, SIGINT };
    for (size_t index = 0; index < sizeof(handledSignals) / sizeof(handledSignals[0]); ++index) {
        if (::sigaction(handledSignals[index], &action, nullptr) != 0) {
            std::cout << TRACE_PREFIX << "Could not install a handler for signal "
                      << handledSignals[index] << ": " << std::strerror(errno) << std::endl;
            return false;
        }
    }

    std::cout << TRACE_PREFIX << "Shutdown path ready: self-pipe read fd " << g_shutdownPipeReadFd
              << ", write fd " << static_cast<int>(g_shutdownPipeWriteFd)
              << ", handling SIGTERM and SIGINT" << std::endl;
    return true;
}

/**
 * @brief Determines which file descriptor the readiness line is written to.
 *
 * Reads CEC_FAKE_HOST_READY_FD and resolves it to a descriptor number, or falls back to standard
 * output when no parent named one.  The variable is parsed strictly and in full: only an unadorned
 * run of decimal digits is accepted, so an empty value, leading whitespace, a sign, a trailing
 * character, a negative number and a value too large to be a descriptor are all rejected rather than
 * coerced.@n
 * The asymmetry between "unset" and "set to nonsense" is deliberate.  Unset means nobody is
 * listening on a pipe, which is exactly the case when a person runs this binary from a shell, so the
 * token goes to standard output where they can see it.  Set to nonsense means a parent intended to
 * be signalled on a pipe and the handoff was botched; answering on standard output would leave that
 * parent waiting out its entire timeout with nothing to explain why, so it fails immediately and
 * says what the value was.
 *
 * @param [out] readinessFd               - Receives the descriptor the readiness line is to be
 *                                          written to.  Untouched when this reports failure
 *
 * @return bool                                   - Whether a descriptor was resolved
 * @retval true                                   - readinessFd holds either the descriptor the parent
 *                                                  named or STDOUT_FILENO
 * @retval false                                  - The variable was present but not a usable
 *                                                  non-negative descriptor number
 *
 * @warning Validated here, before this program takes any action a parent could observe, so that a
 *          bad value costs nothing.  The readiness line itself is written much later, only once the
 *          service is published and served.
 *
 * @see writeAllRetryingOnInterrupt(), READY_FD_VARIABLE
 */
static bool resolveReadinessFd(int &readinessFd)
{
    const char *const rawValue = ::getenv(READY_FD_VARIABLE);

    if (rawValue == nullptr) {
        readinessFd = STDOUT_FILENO;
        std::cout << TRACE_PREFIX << READY_FD_VARIABLE << " is unset, so no parent is listening on a "
                     "pipe; the readiness token will go to standard output (fd " << STDOUT_FILENO
                  << ") for a standalone run" << std::endl;
        return true;
    }

    if (rawValue[0] == '\0') {
        std::cout << TRACE_PREFIX << READY_FD_VARIABLE << " is set to an empty value. Refusing to "
                     "guess: a parent that asked to be signalled on a pipe would wait out its whole "
                     "timeout if this host answered on standard output instead" << std::endl;
        return false;
    }

    /*
     * strtol() skips leading whitespace and accepts a sign, so " 7" and "+7" would otherwise parse
     * as 7. Requiring the first character to be a digit rejects both, and rejects "-1" before the
     * conversion rather than after it. A descriptor number the parent formatted correctly never
     * needs any of that latitude, and a value that does need it is a botched handoff.
     */
    const bool startsWithDigit = (rawValue[0] >= '0' && rawValue[0] <= '9');

    errno = 0;
    char *parseEnd = nullptr;
    const long parsedFd = std::strtol(rawValue, &parseEnd, 10);

    if (!startsWithDigit || errno != 0 || parseEnd == rawValue || *parseEnd != '\0' ||
        parsedFd < 0 || parsedFd > INT_MAX) {
        std::cout << TRACE_PREFIX << READY_FD_VARIABLE << " is set to \"" << rawValue
                  << "\", which is not a usable non-negative file descriptor number" << std::endl;
        return false;
    }

    readinessFd = static_cast<int>(parsedFd);
    std::cout << TRACE_PREFIX << "The readiness token will be written to inherited fd "
              << readinessFd << ", named by " << READY_FD_VARIABLE << std::endl;
    return true;
}

/**
 * @brief Writes a buffer to a descriptor in full, tolerating short and interrupted writes.
 *
 * A single write() call is allowed to transfer fewer bytes than it was given and is allowed to fail
 * outright when a signal arrives mid-call, and the readiness token is the one thing in this program
 * that must arrive whole - a parent matching a line cannot match half of one.  So this loops until
 * every byte is gone, retrying an interrupted call and advancing over a partial one.@n
 * The write is raw and unbuffered on purpose.  A token handed to a C++ stream could sit in that
 * stream's buffer while the parent waits for it, which presents as a hang and not as a bug.
 *
 * @param [in] fd                         - Descriptor to write to
 * @param [in] data                       - Buffer to write.  Must not be null
 * @param [in] length                     - Number of bytes to write
 *
 * @return bool                                   - Whether the whole buffer was written
 * @retval true                                   - All length bytes were written
 * @retval false                                  - The descriptor rejected the write, and errno
 *                                                  describes why
 *
 * @see resolveReadinessFd()
 */
static bool writeAllRetryingOnInterrupt(int fd, const char *data, size_t length)
{
    size_t written = 0;

    while (written < length) {
        const ssize_t result = ::write(fd, data + written, length - written);

        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        if (result == 0) {
            /*
             * A zero-length transfer on a positive request makes no progress, so retrying it would
             * spin forever.  Report it rather than loop.
             */
            errno = EIO;
            return false;
        }

        written += static_cast<size_t>(result);
    }

    return true;
}

/**
 * @brief Reports whether a usable binder driver node is present.
 *
 * Consulted before this process touches libbinder for the first time, because the linked libbinder
 * treats a driver it cannot open as fatal and aborts: on a host with no kernel binder support an
 * unguarded service-manager call would take this program down with no diagnostic and no exit code of
 * its own.  Checking the node first turns that into a traced failure the parent can report, and it
 * keeps this host's failure disposition the same as the fake's own registration routine, which
 * refuses for the same reason rather than aborting.
 *
 * @return bool                                   - Whether the driver node is present and openable
 * @retval true                                   - The node exists and is readable and writable
 * @retval false                                  - The node is absent or cannot be opened, so no
 *                                                  binder transport exists on this host
 *
 * @warning This is a node check and nothing more.  It does NOT establish that the driver's protocol
 *          version matches the one libbinder was built for, and it does NOT establish that a service
 *          manager is running - a node with no service manager behind it still blocks, and the
 *          parent's bounded readiness timeout is the guard for that.
 *
 * @see BINDER_DRIVER_PATH, verifyServiceNameIsFree()
 */
static bool isBinderTransportPresent()
{
    if (::access(BINDER_DRIVER_PATH, R_OK | W_OK) != 0) {
        std::cout << TRACE_PREFIX << "Binder driver " << BINDER_DRIVER_PATH
                  << " is absent or unopenable (" << std::strerror(errno)
                  << "), so this host cannot serve anything on this machine" << std::endl;
        return false;
    }

    std::cout << TRACE_PREFIX << "Binder driver " << BINDER_DRIVER_PATH << " is present and openable"
              << std::endl;
    return true;
}

/**
 * @brief Establishes that nothing is already published under the production service name.
 *
 * A stale registration left behind by another process - an earlier host that was never reaped, or a
 * real HAL on a device - would make the middleware's own lookup resolve against THAT service instead
 * of the fake this host publishes.  The invocation would still run, and its result would describe
 * something nobody chose.  So this is a hard failure and not a condition to work around: publishing
 * over the entry would hide the collision, and tolerating it would make a green result meaningless.@n
 * The query uses checkService(), which answers immediately with null when the name is free.
 * getService() would poll for seconds before concluding the same thing and can race with a service
 * registering concurrently, which is precisely the ambiguity this check exists to remove.
 *
 * @param [in] serviceName                - Production service name to test, obtained from the
 *                                          generated interface rather than spelled here
 *
 * @return int                                    - EXIT_SUCCESS, or the code this program must exit
 *                                                  with
 * @retval EXIT_SUCCESS                           - The name is free and the fake may be published
 * @retval EXIT_SETUP_FAILED                      - No service manager could be reached at all, so
 *                                                  whether the name is free is unknown
 * @retval EXIT_STALE_REGISTRATION                - Something else already holds the name
 *
 * @pre isBinderTransportPresent() has already reported true.  Calling this first risks the abort that
 *      check exists to prevent.
 *
 * @see isBinderTransportPresent(), registerFakeHdmiCecService()
 */
static int verifyServiceNameIsFree(const std::string &serviceName)
{
    const ::android::sp< ::android::IServiceManager> serviceManager =
        ::android::defaultServiceManager();

    if (serviceManager == nullptr) {
        std::cout << TRACE_PREFIX << "No service manager could be reached, so whether \""
                  << serviceName << "\" is already published cannot be established" << std::endl;
        return EXIT_SETUP_FAILED;
    }

    if (serviceManager->checkService(::android::String16(serviceName.c_str())) != nullptr) {
        std::cout << TRACE_PREFIX << "\"" << serviceName << "\" is already published by another "
                     "process. Refusing to publish over it: the invocation's back-end selection "
                     "would resolve against that service rather than this host's fake, so its "
                     "outcome would depend on a process this suite does not own. Stop that process "
                     "and run again" << std::endl;
        return EXIT_STALE_REGISTRATION;
    }

    std::cout << TRACE_PREFIX << "\"" << serviceName << "\" is free" << std::endl;
    return EXIT_SUCCESS;
}

/**
 * @brief Blocks until a termination signal is delivered.
 *
 * Waits by reading the self-pipe the handler writes to, which is a genuine blocking wait: this
 * process consumes no CPU while it serves transactions on its binder threads and holds this thread
 * still.  There is no timed loop and no polling interval, because a wait that wakes on a timer would
 * be a wait that can be wrong about when to stop.@n
 * A read interrupted by the very signal being waited for is retried, and the byte the handler wrote
 * is then there to be read.  A read that reports end of file is treated as a request to stop as
 * well: it can only mean the write end has been closed, and there is nothing left to wait for.
 *
 * @param [out] signalNumber              - Receives the signal number that ended the wait, or 0 when
 *                                          the wait ended because the self-pipe closed
 *
 * @return bool                                   - Whether the wait ended as intended
 * @retval true                                   - A termination request was received, or the pipe
 *                                                  closed, and this program should exit cleanly
 * @retval false                                  - The self-pipe could not be read, which is an
 *                                                  internal failure rather than a shutdown
 *
 * @pre installShutdownHandlers() has already reported true.
 *
 * @see handleShutdownSignal(), installShutdownHandlers()
 */
static bool waitForShutdownSignal(int &signalNumber)
{
    unsigned char wakeByte = 0;

    for (;;) {
        const ssize_t result = ::read(g_shutdownPipeReadFd, &wakeByte, sizeof(wakeByte));

        if (result >= 0) {
            /*
             * A byte means the handler ran.  Zero means end of file, which can only mean the write
             * end has been closed, and there is nothing left to wait for either way.
             */
            signalNumber = static_cast<int>(g_shutdownSignalNumber);
            return true;
        }

        if (errno == EINTR) {
            continue;
        }

        std::cout << TRACE_PREFIX << "Could not wait on the shutdown self-pipe: "
                  << std::strerror(errno) << std::endl;
        return false;
    }
}

/**
 * @brief Hosts the fake com.rdk.hal.hdmicec AIDL service until asked to stop.
 *
 * Runs the startup sequence documented on this file, in that order, and treats every step as
 * load-bearing: each failure traces what happened, returns a code of its own and writes NO readiness
 * line, so a parent never mistakes a host that failed to publish for one that is serving.  Nothing is
 * swallowed and nothing degrades quietly - a host that cannot serve must not report that it can.
 *
 * @param [in] argc                       - Argument count.  Unused: this program takes no arguments
 * @param [in] argv                       - Argument vector.  Unused, for the same reason
 *
 * @return int                                    - Process exit status
 * @retval EXIT_SUCCESS                           - Published, served, and stopped cleanly on a
 *                                                  termination signal
 * @retval EXIT_STALE_REGISTRATION                - Something was already published under the
 *                                                  production service name
 * @retval EXIT_BAD_READY_FD                      - CEC_FAKE_HOST_READY_FD was set to something that
 *                                                  is not a usable non-negative descriptor number
 * @retval EXIT_REGISTRATION_FAILED               - The service manager refused to publish the fake
 * @retval EXIT_READINESS_WRITE_FAILED            - The readiness token could not be written, so the
 *                                                  parent can never learn this host is ready
 * @retval EXIT_NO_BINDER_TRANSPORT               - No usable binder driver node exists on this host
 * @retval EXIT_SETUP_FAILED                      - The shutdown path, the fake, or the service
 *                                                  manager could not be established
 *
 * @post On every return path the process has published no readiness line unless it genuinely became
 *       ready, and holds no resource that outlives it: the binder registration is released with the
 *       process and the fake clears its own published pointer as it is destroyed.
 *
 * @warning Configuration is taken from the environment, never from the command line, so that the
 *          parent's launch is a plain exec of the path in CEC_FAKE_AIDL_HOST_PATH with no argument
 *          contract to keep in step.
 *
 * @see resolveReadinessFd(), verifyServiceNameIsFree(), registerFakeHdmiCecService(),
 *      waitForShutdownSignal()
 */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /*
     * The pid is traced first because it is what a parent's diagnostics, and a person hunting an
     * orphan, both key on.  It is streamed rather than cast: pid_t is an integer type of unspecified
     * width, so a cast would either be redundant here or lossy somewhere else.
     */
    std::cout << TRACE_PREFIX << "Starting the out-of-process fake HDMI CEC AIDL service host, pid "
              << ::getpid() << std::endl;

    if (!installShutdownHandlers()) {
        return EXIT_SETUP_FAILED;
    }

    int readinessFd = -1;
    if (!resolveReadinessFd(readinessFd)) {
        return EXIT_BAD_READY_FD;
    }

    if (!isBinderTransportPresent()) {
        return EXIT_NO_BINDER_TRANSPORT;
    }

    /*
     * The name comes from the generated interface and is never spelled here, so this host cannot
     * drift from the name the middleware actually looks up.  Only the service is published: a client
     * receives its controller from the out-parameter of open(), so the controller is never a
     * separately registered name.
     */
    const std::string &serviceName = ::com::rdk::hal::hdmicec::IHdmiCec::serviceName();

    const int nameCheck = verifyServiceNameIsFree(serviceName);
    if (nameCheck != EXIT_SUCCESS) {
        return nameCheck;
    }

    ::android::sp<FakeHdmiCecService> fake = ::android::sp<FakeHdmiCecService>::make();
    if (fake == nullptr) {
        std::cout << TRACE_PREFIX << "The fake HDMI CEC AIDL service could not be constructed"
                  << std::endl;
        return EXIT_SETUP_FAILED;
    }

    /*
     * Publish the pointer so that anything in this process which needs the hosted fake reaches the
     * one that was registered, following the same single-pointer idiom the legacy driver double in
     * this directory uses.  The strong reference above is what keeps it alive.
     */
    FakeHdmiCecService::setInstance(fake.get());
    std::cout << TRACE_PREFIX << "Constructed the fake HDMI CEC AIDL service " << (void*)fake.get()
              << std::endl;

    /*
     * The SERVICE-side threadpool, started before publication so that no transaction can arrive with
     * no thread to serve it.  startThreadPool() is idempotent and the library's own default governs
     * the thread count: setThreadPoolMaxThreadCount() is deliberately not called, because lowering a
     * maximum another component already established can abort the process.
     */
    ::android::ProcessState::self()->startThreadPool();
    std::cout << TRACE_PREFIX << "Started the service-side binder threadpool" << std::endl;

    if (!registerFakeHdmiCecService(fake)) {
        std::cout << TRACE_PREFIX << "The fake could not be published as \"" << serviceName
                  << "\", so this host has nothing to serve and is not ready" << std::endl;
        return EXIT_REGISTRATION_FAILED;
    }

    std::cout << TRACE_PREFIX << "Published the fake as \"" << serviceName << "\"" << std::endl;

    /*
     * Ready, and only now.  The token is written raw and unbuffered: handed to a C++ stream it could
     * sit in that stream's buffer while the parent waits for it, which presents as a hang rather than
     * as a bug.  The trace below the write is diagnostics and is no part of the signal.
     */
    if (!writeAllRetryingOnInterrupt(readinessFd, READINESS_TOKEN, std::strlen(READINESS_TOKEN))) {
        std::cout << TRACE_PREFIX << "Could not write the readiness token to fd " << readinessFd
                  << ": " << std::strerror(errno)
                  << ". The parent can never learn this host is ready, so it exits rather than serve "
                     "an invocation that will time out anyway" << std::endl;
        return EXIT_READINESS_WRITE_FAILED;
    }

    std::cout << TRACE_PREFIX << "Wrote the readiness token to fd " << readinessFd
              << "; waiting for SIGTERM or SIGINT" << std::endl;

    int shutdownSignal = 0;
    if (!waitForShutdownSignal(shutdownSignal)) {
        return EXIT_SETUP_FAILED;
    }

    if (shutdownSignal != 0) {
        std::cout << TRACE_PREFIX << "Received signal " << shutdownSignal << ", shutting down"
                  << std::endl;
    } else {
        std::cout << TRACE_PREFIX << "The shutdown self-pipe closed, shutting down" << std::endl;
    }

    /*
     * Release what this program took.  The published pointer is cleared so nothing can reach a fake
     * that is going away, the self-pipe descriptors are closed, and the binder registration needs no
     * withdrawal: the pinned service manager exposes no removal API and process exit releases it.
     */
    FakeHdmiCecService::setInstance(nullptr);
    ::close(g_shutdownPipeReadFd);
    ::close(static_cast<int>(g_shutdownPipeWriteFd));
    g_shutdownPipeReadFd = -1;
    g_shutdownPipeWriteFd = -1;

    std::cout << TRACE_PREFIX << "Exited cleanly" << std::endl;
    return EXIT_SUCCESS;
}

/** @} */ // End of HDMI_CEC_FAKE_AIDL_SERVICE_HOST
