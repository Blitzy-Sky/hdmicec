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
 * Dedicated L1 tests for the CEC logging utility - its configuration reader, verbosity
 * gates and buffer formatter.
 *
 * The configuration reader selects a fixed table entry by key prefix and never parses a
 * numeric value out of the file, so malformed-number scenarios are unreachable and are
 * deliberately absent.
 *
 * Both the hardcoded configuration file and the file-static log level are shared process
 * state, and the fixture restores BOTH: first the effective level the process had on
 * entry - recovered by probing CCEC_LOG, because cec_log_level has internal linkage and no
 * accessor - and then the file byte-for-byte, or its absence. That order is required,
 * because applying a level means writing the file. Restoring only one half leaks verbosity
 * into every later LibCCEC and DriverImpl case.
 *
 * The configuration path is fixed by production check_cec_log_status(), which hardcodes
 * fopen("/tmp/cec_log_enabled"), so this suite cannot be pointed at a private temporary
 * without a production change. A fixed name in a world-writable directory is the classic
 * unsafe-temporary shape, so one guard owns every access to it: the path is classified with
 * lstat and refused unless it is absent or a regular file this process owns; reads open
 * O_RDONLY|O_NOFOLLOW|O_CLOEXEC; writes go to a fresh O_EXCL|O_NOFOLLOW temporary in the
 * same directory, with the original mode, owner and group reapplied, and are rename()d over
 * the path so a planted link is replaced rather than written through; and restoration is
 * additionally armed through atexit and the fatal/termination signals using only
 * async-signal-safe primitives, so a crash cannot leave CEC debug logging switched on for
 * the whole machine.
 *
 * cec_log_level is a plain int with internal linkage in Util.cpp: check_cec_log_status()
 * writes it, and CCEC_LOG() and dump_buffer() read it, with no lock, no atomic and no
 * accessor. The Bus reader and writer threads outlive every case in this binary - the
 * global environment in test_main.cpp brings the library up once for the whole program,
 * and quiescing the threads would mean LibCCEC::term() then init(), a path that can lose
 * the reader's RUNNING/STOPPING transition and hang - so a write issued from the test
 * thread would be concurrent with their reads. This fixture therefore never writes that
 * global in the process those threads live in. Every case body, and the level baseline it
 * starts from, runs in a forked child, where fork() left only the calling thread behind
 * and nothing else reads the global while it changes. The parent only reads it, once on
 * entry and once on exit through CCEC_LOG, and asserts the two agree - a direct check
 * that no child's writes reached the shared process.
 *
 * The child reports through its exit status: it runs the body, GoogleTest records any
 * failure in the child's own copy of the result, and the child exits non-zero when it
 * recorded one, so the parent's case fails with the child's own failure text already
 * printed. Coverage counters are per process, so the child dumps them before _exit();
 * without that, the production lines it drove would not be counted at all.
 *
 * fork() in a process that has other threads is safe only for what the child does with
 * state those threads could have been holding, and GoogleTest's own death tests make the
 * same trade on Linux by default. Both Bus threads park on a condition variable when
 * there is no CEC traffic and no case here generates any, so neither holds an allocator or
 * stdio lock when the fork happens. The parent still waits with a deadline and fails the
 * case rather than blocking forever, so a child that ever did get stuck is reported
 * instead of hanging the suite.
 *
 * What is left unfixed is in production, not here: cec_log_level should be atomic, or
 * Util.cpp should expose a seam for setting and reading it, which would remove the need
 * for a child process at all. That is a production change, so it is reported as a blocked
 * gap rather than made.
 *
 * The output assertions are unaffected by any of this: each matches a unique marker or an
 * ordered token sequence rather than the size of the capture, so output from another
 * writer reaching the redirected descriptor cannot change a verdict.
 */

#include <gtest/gtest.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "ccec/Util.hpp"

/*
 * libgcov's public reset and dump entry points.
 *
 * Every production line this fixture drives is executed in a forked child, and a child that
 * leaves through _exit() writes no coverage data at all unless it is asked to - measured:
 * without this, Util.cpp reads 64.0% (16/25) instead of 100% (25/25), because the lines the
 * children ran were discarded with their address spaces. Resetting on entry to the child
 * means it contributes the counters for what it actually ran rather than a second copy of
 * everything the parent had already accumulated.
 *
 * Declared here rather than weakly, and libgcov named in run_L1Tests_LDADD, because both
 * symbols sit in their own members of libgcov.a (_gcov_reset.o, _gcov_dump.o) which the
 * linker pulls in only for a direct reference - a weak reference leaves them unresolved even
 * in a build that passes -fprofile-arcs, which is precisely the configuration that has to
 * work. With no instrumentation present they are no-ops over an empty counter list.
 */
extern "C" {
void __gcov_reset(void);
void __gcov_dump(void);
}

namespace {

const char *kLogConfigPath = "/tmp/cec_log_enabled";

struct LogLevelCase {
    const char *key;
    int value;
};

const LogLevelCase kLogLevels[] = {
    { "FATAL", LOG_FATAL },
    { "ERROR", LOG_ERROR },
    { "WARN", LOG_WARN },
    { "EXP", LOG_EXP },
    { "NOTICE", LOG_NOTICE },
    { "INFO", LOG_INFO },
    { "DEBUG", LOG_DEBUG },
    { "TRACE", LOG_TRACE }
};

/*
 * Self-contained stdout capture, at file-descriptor level.
 *
 * The code under test logs through printf/vprintf - C stdio writing to fd 1 - so redirecting the
 * C++ std::cout streambuf would capture nothing. GoogleTest does offer CaptureStdout() and
 * GetCapturedStdout(), and they work, but they live in ::testing::internal, which upstream
 * documents as not part of the public API; this suite builds against GoogleTest 1.14.0 locally and
 * v1.15.0 in CI, and it is the only place in the middleware suite that would reach into that
 * namespace. Doing it directly costs a few lines and removes the coupling: duplicate fd 1, point it
 * at an anonymous temporary, run the body, restore, read back.
 *
 * tmpfile() is used rather than a named path: it is unlinked as it is created, so there is nothing
 * for another process to substitute.
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

    // Restores the real stdout first, so nothing written afterwards is swallowed, then returns
    // everything the body wrote.
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

template <typename Callable>
std::string captureStdout(Callable body) {
    StdoutCapture capture;
    if (!capture.isValid()) {
        // Redirection could not be established. Run the body anyway so the test still exercises
        // the code, and return nothing so the content assertions fail loudly rather than silently
        // passing against an empty string that was never compared.
        body();
        return std::string();
    }

    try {
        body();
    } catch (...) {
        (void)capture.read();
        throw;
    }

    return capture.read();
}

/*
 * Guard over the production log-configuration file.
 *
 * One primitive owns every access to the path: capture, write, remove and restore. That
 * is deliberate - four hand-rolled variants of "save the file and hope to put it back"
 * is exactly how a fixture ends up leaving a machine modified - and it is what makes the
 * no-follow, atomic-replace and signal-safe properties described in the file header hold
 * for every code path rather than for the ones somebody remembered.
 *
 * Everything the signal path needs is a fixed-size static: the handler allocates nothing.
 */
class LogConfigGuard {
public:
    static LogConfigGuard &instance() {
        static LogConfigGuard guard;
        return guard;
    }

    /*
     * Capture the current state of the path and arm the restoration paths. Returns false
     * when the path is something this fixture must not touch; lastError() then explains
     * what was found. No mutation of any kind has happened at that point.
     */
    bool activate() {
        if (active_) {
            return true;
        }
        error_.clear();
        savedLength_ = 0;
        savedExisted_ = false;
        savedMode_ = 0600;
        savedUid_ = static_cast<uid_t>(-1);
        savedGid_ = static_cast<gid_t>(-1);

        struct stat status;
        if (lstat(kLogConfigPath, &status) == 0) {
            if (!S_ISREG(status.st_mode)) {
                error_ = std::string(kLogConfigPath)
                    + " exists and is not a regular file (mode "
                    + std::to_string(static_cast<unsigned long>(status.st_mode))
                    + "); refusing to modify it, because writing through it would alter"
                      " whatever it refers to";
                return false;
            }
            if (status.st_uid != geteuid() && geteuid() != 0) {
                error_ = std::string(kLogConfigPath) + " is owned by uid "
                    + std::to_string(static_cast<unsigned long>(status.st_uid))
                    + " and this process is not that user; refusing to modify it";
                return false;
            }

            const int fd = open(kLogConfigPath, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
            if (fd < 0) {
                error_ = std::string("could not read ") + kLogConfigPath + ": "
                    + std::strerror(errno);
                return false;
            }
            ssize_t got = 0;
            size_t total = 0;
            while (total < sizeof(saved_)
                   && (got = read(fd, saved_ + total, sizeof(saved_) - total)) > 0) {
                total += static_cast<size_t>(got);
            }
            const bool readFailed = (got < 0);
            /* A file larger than the static buffer could not be reproduced byte-for-byte
             * from a signal handler, and restoring a truncated copy would be worse than not
             * touching it at all, so it is refused outright. The production configuration is
             * a single short line, so this is a "something unexpected is here" signal. */
            char overflow = '\0';
            const bool tooLong = (total == sizeof(saved_)) && (read(fd, &overflow, 1) > 0);
            close(fd);
            if (readFailed) {
                error_ = std::string("could not read ") + kLogConfigPath;
                return false;
            }
            if (tooLong) {
                error_ = std::string(kLogConfigPath) + " is larger than "
                    + std::to_string(sizeof(saved_))
                    + " bytes, so it could not be restored faithfully; refusing to modify it";
                return false;
            }
            savedLength_ = total;
            savedExisted_ = true;
            savedMode_ = status.st_mode & 07777;
            savedUid_ = status.st_uid;
            savedGid_ = status.st_gid;
        } else if (errno != ENOENT) {
            error_ = std::string("could not inspect ") + kLogConfigPath + ": "
                + std::strerror(errno);
            return false;
        }

        buildTempPath();
        registerAtExitOnce();
        installSignalHandlers();
        active_ = true;
        return true;
    }

    /* Write one configuration value, atomically and without following a link. */
    bool write(const char *data, size_t length) {
        return atomicWrite(data, length, savedMode_, savedUid_, savedGid_) == 0;
    }

    bool write(const std::string &contents) {
        return write(contents.data(), contents.size());
    }

    /* Remove the file. unlink() never follows, so a link would be removed, not its target. */
    bool remove() {
        return unlink(kLogConfigPath) == 0 || errno == ENOENT;
    }

    bool existed() const { return savedExisted_; }
    bool active() const { return active_; }
    const std::string &lastError() const { return error_; }

    /* Put the path back exactly as it was found and disarm the restoration paths. */
    bool restoreAndDeactivate() {
        if (!active_) {
            return true;
        }
        const bool restored = (restoreQuietly() == 0);
        removeSignalHandlers();
        active_ = false;
        return restored;
    }

private:
    LogConfigGuard()
        : active_(false)
        , savedExisted_(false)
        , savedLength_(0)
        , savedMode_(0600)
        , savedUid_(static_cast<uid_t>(-1))
        , savedGid_(static_cast<gid_t>(-1))
        , atExitRegistered_(false)
        , handlersInstalled_(false) {
        saved_[0] = '\0';
        tempPath_[0] = '\0';
    }

    LogConfigGuard(const LogConfigGuard &);
    LogConfigGuard &operator=(const LogConfigGuard &);

    /* Async-signal-safe: no allocation, only open/write/fchmod/fchown/rename/unlink/close. */
    int atomicWrite(const char *data, size_t length, mode_t mode, uid_t uid, gid_t gid) {
        if (tempPath_[0] == '\0') {
            return -1;
        }
        unlink(tempPath_);
        const int fd = open(tempPath_, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) {
            return -1;
        }
        size_t written = 0;
        while (written < length) {
            const ssize_t chunk = ::write(fd, data + written, length - written);
            if (chunk <= 0) {
                if (chunk < 0 && errno == EINTR) {
                    continue;
                }
                close(fd);
                unlink(tempPath_);
                return -1;
            }
            written += static_cast<size_t>(chunk);
        }
        /* Hand back the metadata the file was found with. The two calls are NOT
         * equivalent, so they are not treated equivalently:
         *
         *   fchmod - this descriptor was just created by this process with O_EXCL, so
         *            this process owns the file and fchmod to any mode must succeed. A
         *            failure here means the restored file carries permissions the host
         *            did not have before, which is exactly the silent host mutation the
         *            restoration exists to prevent, so it fails the write.
         *   fchown - genuinely best-effort. A non-root process cannot give a file away,
         *            so EPERM is the normal, expected outcome whenever the config file
         *            was owned by somebody else, and failing on it would abort every
         *            unprivileged run for a condition the test cannot influence.
         */
        if (fchmod(fd, mode) != 0) {
            close(fd);
            unlink(tempPath_);
            return -1;
        }
        if (uid != static_cast<uid_t>(-1)) {
            (void)fchown(fd, uid, gid);
        }
        if (close(fd) != 0) {
            unlink(tempPath_);
            return -1;
        }
        if (rename(tempPath_, kLogConfigPath) != 0) {
            unlink(tempPath_);
            return -1;
        }
        return 0;
    }

    /* The restoration used by TearDown, atexit and the signal handlers alike. */
    int restoreQuietly() {
        if (!savedExisted_) {
            return (unlink(kLogConfigPath) == 0 || errno == ENOENT) ? 0 : -1;
        }
        return atomicWrite(saved_, savedLength_, savedMode_, savedUid_, savedGid_);
    }

    void buildTempPath() {
        /* Built once, into a static buffer, so the signal path needs no formatting. */
        const long pid = static_cast<long>(getpid());
        std::snprintf(tempPath_, sizeof(tempPath_), "%s.test.%ld.tmp", kLogConfigPath, pid);
    }

    void registerAtExitOnce() {
        if (atExitRegistered_) {
            return;
        }
        std::atexit(&LogConfigGuard::atExitRestore);
        atExitRegistered_ = true;
    }

    static void atExitRestore() {
        LogConfigGuard &guard = instance();
        if (guard.active_) {
            (void)guard.restoreQuietly();
            guard.active_ = false;
        }
    }

    static void signalRestore(int signalNumber) {
        LogConfigGuard &guard = instance();
        if (guard.active_) {
            (void)guard.restoreQuietly();
            guard.active_ = false;
        }
        /* Chain to whatever was installed before, so a crash still produces its usual
         * diagnosis and this fixture never swallows a failure. */
        guard.removeSignalHandlers();
        raise(signalNumber);
    }

    void installSignalHandlers() {
        if (handlersInstalled_) {
            return;
        }
        struct sigaction action;
        std::memset(&action, 0, sizeof(action));
        action.sa_handler = &LogConfigGuard::signalRestore;
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0;
        for (size_t i = 0; i < kSignalCount; ++i) {
            if (sigaction(kSignals[i], &action, &previous_[i]) != 0) {
                std::memset(&previous_[i], 0, sizeof(previous_[i]));
            }
        }
        handlersInstalled_ = true;
    }

    void removeSignalHandlers() {
        if (!handlersInstalled_) {
            return;
        }
        for (size_t i = 0; i < kSignalCount; ++i) {
            (void)sigaction(kSignals[i], &previous_[i], NULL);
        }
        handlersInstalled_ = false;
    }

    static const size_t kSignalCount = 6;
    static const int kSignals[kSignalCount];

    bool active_;
    bool savedExisted_;
    size_t savedLength_;
    mode_t savedMode_;
    uid_t savedUid_;
    gid_t savedGid_;
    bool atExitRegistered_;
    bool handlersInstalled_;
    std::string error_;
    char saved_[4096];
    char tempPath_[256];
    struct sigaction previous_[kSignalCount];
};

const int LogConfigGuard::kSignals[LogConfigGuard::kSignalCount] = {
    SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGSEGV, SIGABRT
};

LogConfigGuard &logConfig() {
    return LogConfigGuard::instance();
}

bool writeLogConfig(const std::string &contents) {
    return logConfig().write(contents);
}

const int kUnknownLogLevel = -1;

// cec_log_level is file-static in Util.cpp with no accessor, so the effective level can
// only be observed: CCEC_LOG emits when its level is <= the configured level, so the
// highest level that still emits IS the configured level.
int probeEffectiveLogLevel() {
    for (int level = LOG_MAX - 1; level >= 0; --level) {
        const std::string marker = "UTIL_PROBE_LEVEL_" + std::to_string(level);
        const std::string output = captureStdout([level, &marker]() {
            CCEC_LOG(level, "%s", marker.c_str());
        });
        if (output.find(marker) != std::string::npos) {
            return level;
        }
    }
    return kUnknownLogLevel;
}

// Whether a configuration file is present at the fixed path. lstat, so a symlink standing
// there is not reported as the file.
bool logConfigExists() {
    struct stat linkStatus;
    return lstat(kLogConfigPath, &linkStatus) == 0 && S_ISREG(linkStatus.st_mode) != 0;
}

// Outcome of running a fixture body in its own process.
struct IsolatedRun {
    bool exited = false;           // child terminated by returning or _exit()
    int exitStatus = -1;           // its status, when it exited
    int terminatingSignal = 0;     // the signal that killed it, when it did not
    bool timedOut = false;         // the deadline expired and the child was killed
    std::string error;             // fork/waitpid itself failed
};

// How long the parent waits for a child before declaring it stuck. Generous: the slowest
// case here writes and re-reads the configuration file eight times and formats a 256-byte
// dump, which takes single-digit milliseconds, so this only ever fires on a real hang.
const long kChildDeadlineMs = 30000;
const long kChildPollIntervalMs = 2;

void sleepMilliseconds(long milliseconds) {
    struct timespec interval;
    interval.tv_sec = milliseconds / 1000;
    interval.tv_nsec = (milliseconds % 1000) * 1000000L;
    while (nanosleep(&interval, &interval) != 0 && errno == EINTR) {
        // Finish the remaining interval nanosleep wrote back.
    }
}

/*
 * Runs body in a forked child and reports how that child ended.
 *
 * This is the whole of the isolation: the caller's process keeps its cec_log_level
 * untouched because only the child ever calls check_cec_log_status(), and the child has no
 * Bus threads reading the global while it does.
 *
 * The child leaves through _exit() so it runs no atexit handler and prints no second
 * GoogleTest summary; its coverage counters are dumped explicitly first, because _exit()
 * would otherwise discard them.
 */
IsolatedRun runInChildProcess(const std::function<void()> &body) {
    IsolatedRun run;

    // fork() duplicates whatever is sitting in the stdio buffers, and the child leaves
    // through _exit(), so flush first or the same bytes are written twice.
    ::fflush(stdout);
    ::fflush(stderr);

    const pid_t child = ::fork();
    if (child < 0) {
        run.error = std::string("fork() failed: ") + std::strerror(errno);
        return run;
    }

    if (child == 0) {
        // Start this child's counters from zero so its dump adds only its own work to the
        // .gcda the parent will later merge into.
        __gcov_reset();

        body();
        const int childStatus = ::testing::Test::HasFailure() ? 1 : 0;

        ::fflush(stdout);
        ::fflush(stderr);
        __gcov_dump();
        ::_exit(childStatus);
    }

    long waitedMs = 0;
    for (;;) {
        int waitStatus = 0;
        const pid_t reaped = ::waitpid(child, &waitStatus, WNOHANG);
        if (reaped == child) {
            if (WIFEXITED(waitStatus)) {
                run.exited = true;
                run.exitStatus = WEXITSTATUS(waitStatus);
            } else if (WIFSIGNALED(waitStatus)) {
                run.terminatingSignal = WTERMSIG(waitStatus);
            }
            return run;
        }
        if (reaped < 0) {
            if (errno == EINTR) {
                continue;
            }
            run.error = std::string("waitpid() failed: ") + std::strerror(errno);
            return run;
        }

        if (waitedMs >= kChildDeadlineMs) {
            run.timedOut = true;
            ::kill(child, SIGKILL);
            // Blocking reap, so no zombie is left behind for the rest of the suite.
            int killedStatus = 0;
            while (::waitpid(child, &killedStatus, 0) < 0 && errno == EINTR) {
                // Retry the interrupted reap.
            }
            return run;
        }

        sleepMilliseconds(kChildPollIntervalMs);
        waitedMs += kChildPollIntervalMs;
    }
}

} // namespace

class UtilTest : public ::testing::Test {
protected:
    void SetUp() override {
        /* Capture the configuration file and arm every restoration path before the first
         * write. A refusal here means the path holds something this fixture must not
         * modify, which is a hard failure rather than a licence to proceed. The file is
         * captured in the parent on purpose: the parent outlives every child, so it is the
         * process that can be relied on to hand the file back. */
        ASSERT_TRUE(logConfig().activate()) << logConfig().lastError();

        /* Read the effective level - a read, never a write, so it is safe to do here with
         * the Bus threads running. cec_log_level has internal linkage and no accessor, so
         * CCEC_LOG is the only observation available. */
        entryLogLevel_ = probeEffectiveLogLevel();

        /* Every case in this fixture must start from the same effective level. Since no
         * case writes the level in this process any more, a mismatch here means either a
         * child's write escaped its own address space - which cannot happen - or another
         * process rewrote the configuration file mid-run. */
        if (firstObservedLogLevel_ == kUnknownLogLevel) {
            firstObservedLogLevel_ = entryLogLevel_;
        }
        EXPECT_EQ(firstObservedLogLevel_, entryLogLevel_)
            << "the effective CEC log level changed between UtilTest cases, so something "
            << "outside this fixture rewrote " << kLogConfigPath << " while the suite was "
            << "running - that path is fixed by the production reader, so two concurrent "
            << "copies of this suite on one host contend for it.";
    }

    void TearDown() override {
        /* Hand the file back byte-for-byte (or remove it again) and disarm the
         * atexit/signal restoration. Nothing normalises the process-static level, because
         * nothing in this process changed it: check_cec_log_status() is only ever called
         * inside a forked child, whose address space is gone by the time control gets
         * here. */
        EXPECT_TRUE(logConfig().restoreAndDeactivate())
            << "failed to restore " << kLogConfigPath;

        /* Prove that, rather than assume it. The level observed on the way out must be the
         * level observed on the way in; if a write ever did happen on this thread, this is
         * where it surfaces. */
        EXPECT_EQ(entryLogLevel_, probeEffectiveLogLevel())
            << "the effective CEC log level changed inside the test process, which means a "
            << "write to the non-atomic production global escaped the child-process "
            << "isolation this fixture depends on";
    }

    /*
     * Runs one case body in its own process.
     *
     * The child applies the INFO baseline every case starts from and then runs the body, so
     * the whole of the mutation - baseline included - happens where no Bus thread can be
     * reading cec_log_level. Failures recorded in the child come back as a non-zero exit
     * status, with the child's own GoogleTest failure text already on stdout above this
     * assertion.
     */
    void RunIsolated(const std::function<void()> &body) {
        ASSERT_TRUE(logConfig().active())
            << "the configuration-file guard is not armed, so a child must not be allowed "
            << "to write " << kLogConfigPath;

        const IsolatedRun run = runInChildProcess([this, &body]() {
            applyLogConfig("INFO\n");
            body();
        });

        ASSERT_TRUE(run.error.empty()) << run.error;
        ASSERT_FALSE(run.timedOut)
            << "the isolated child did not finish within " << kChildDeadlineMs
            << "ms and was killed. fork() in a process with live Bus threads can inherit a "
            << "lock one of them held, which is the one way this fixture can wedge.";
        ASSERT_TRUE(run.exited)
            << "the isolated child was terminated by signal " << run.terminatingSignal
            << " instead of exiting";
        EXPECT_EQ(0, run.exitStatus)
            << "the isolated child recorded a GoogleTest failure; its own failure output is "
            << "printed above this line";
    }

    void applyLogConfig(const std::string &contents) {
        EXPECT_TRUE(writeLogConfig(contents));
        EXPECT_NO_THROW(check_cec_log_status());
    }

    /* Removing the configuration file goes through the same guard as every other access,
     * so no test reaches the path with a call that could follow a link. */
    void removeLogConfig() {
        EXPECT_TRUE(logConfig().remove()) << "failed to remove " << kLogConfigPath;
    }

    std::string captureLog(int level, const std::string &marker) {
        return captureStdout([level, &marker]() {
            CCEC_LOG(level, "%s", marker.c_str());
        });
    }

    std::string captureDump(unsigned char *buffer, int length) {
        return captureStdout([buffer, length]() {
            dump_buffer(buffer, length);
        });
    }

private:
    /* The effective level this process had when the case started, which it must still have
     * when the case ends. */
    int entryLogLevel_ = kUnknownLogLevel;

    /* Shared by every case in the fixture: the effective level observed before the first
     * case ran, which every later case must also observe. */
    static int firstObservedLogLevel_;
};

int UtilTest::firstObservedLogLevel_ = kUnknownLogLevel;

// Traceability: #gap-mw-util - section 6.2 rank 33, P2 -
// check_cec_log_status().
TEST_F(UtilTest, MissingConfigFileLeavesLogLevelUnchanged) {
    RunIsolated([this]() {
        applyLogConfig("DEBUG\n");
        removeLogConfig();
        EXPECT_FALSE(logConfigExists());

        std::string readerOutput;
        EXPECT_NO_THROW(readerOutput = captureStdout([]() {
            check_cec_log_status();
        }));
        EXPECT_NE(readerOutput.find("cec_log_enabled"), std::string::npos);

        unsigned char probe[] = { 0xA5, 0x5A, 0xA5 };
        const std::string dumpOutput = captureDump(probe, 3);
        EXPECT_NE(dumpOutput.find("A5 5A A5"), std::string::npos);
    });
}

TEST_F(UtilTest, EveryRecognisedLevelMapsToItsNumericSetting) {
    RunIsolated([this]() {
        unsigned char dumpProbe[] = { 0xD3, 0x7B };

        for (const LogLevelCase &levelCase : kLogLevels) {
            SCOPED_TRACE(levelCase.key);
            applyLogConfig(std::string(levelCase.key) + "\n");

            const std::string thresholdMarker =
                std::string("UTIL_THRESHOLD_") + levelCase.key;
            const std::string thresholdOutput =
                captureLog(levelCase.value, thresholdMarker);
            EXPECT_NE(thresholdOutput.find(thresholdMarker), std::string::npos);

            if (levelCase.value + 1 < LOG_MAX) {
                const std::string aboveMarker =
                    std::string("UTIL_ABOVE_") + levelCase.key;
                const std::string aboveOutput =
                    captureLog(levelCase.value + 1, aboveMarker);
                EXPECT_EQ(aboveOutput.find(aboveMarker), std::string::npos);
            }

            const std::string dumpOutput = captureDump(dumpProbe, 2);
            if (levelCase.value >= LOG_DEBUG) {
                EXPECT_NE(dumpOutput.find("D3 7B"), std::string::npos);
            } else {
                EXPECT_EQ(dumpOutput.find("D3 7B"), std::string::npos);
            }
        }
    });
}

TEST_F(UtilTest, EmptyConfigFileLeavesLogLevelUnchanged) {
    RunIsolated([this]() {
        applyLogConfig("INFO\n");
        applyLogConfig("");

        const std::string infoMarker = "UTIL_EMPTY_FILE_INFO";
        const std::string infoOutput = captureLog(LOG_INFO, infoMarker);
        EXPECT_NE(infoOutput.find(infoMarker), std::string::npos);

        const std::string debugMarker = "UTIL_EMPTY_FILE_DEBUG";
        const std::string debugOutput = captureLog(LOG_DEBUG, debugMarker);
        EXPECT_EQ(debugOutput.find(debugMarker), std::string::npos);
    });
}

TEST_F(UtilTest, UnrecognisedLevelLeavesLogLevelUnchanged) {
    RunIsolated([this]() {
        applyLogConfig("TRACE\n");
        applyLogConfig("NOT_A_LEVEL\n");

        const std::string marker = "UTIL_UNRECOGNISED_RETAINS_TRACE";
        const std::string output = captureLog(LOG_TRACE, marker);
        EXPECT_NE(output.find(marker), std::string::npos);
    });
}

TEST_F(UtilTest, ShortPrefixDoesNotMatchRecognisedLevel) {
    RunIsolated([this]() {
        applyLogConfig("INFO\n");
        applyLogConfig("DEBU\n");

        const std::string infoMarker = "UTIL_SHORT_PREFIX_INFO";
        const std::string infoOutput = captureLog(LOG_INFO, infoMarker);
        EXPECT_NE(infoOutput.find(infoMarker), std::string::npos);

        const std::string debugMarker = "UTIL_SHORT_PREFIX_DEBUG";
        const std::string debugOutput = captureLog(LOG_DEBUG, debugMarker);
        EXPECT_EQ(debugOutput.find(debugMarker), std::string::npos);
    });
}

TEST_F(UtilTest, RecognisedLevelWithTrailingContentIsAccepted) {
    RunIsolated([this]() {
        applyLogConfig("DEBUG extra trailing text\n");

        unsigned char probe[] = { 0xC3, 0x5E };
        const std::string output = captureDump(probe, 2);
        EXPECT_NE(output.find("C3 5E"), std::string::npos);
    });
}

TEST_F(UtilTest, DumpBufferEmitsAtDebugAndTraceLevels) {
    RunIsolated([this]() {
        const char *enablingLevels[] = { "DEBUG", "TRACE" };
        unsigned char probe[] = { 0x00, 0x0F, 0xA6, 0xFF };

        for (const char *level : enablingLevels) {
            SCOPED_TRACE(level);
            applyLogConfig(std::string(level) + "\n");

            const std::string output = captureDump(probe, 4);
            EXPECT_NE(output.find("00"), std::string::npos);
            EXPECT_NE(output.find("0F"), std::string::npos);
            EXPECT_NE(output.find("A6"), std::string::npos);
            EXPECT_NE(output.find("FF"), std::string::npos);
        }
    });
}

TEST_F(UtilTest, DumpBufferIsSilentBelowDebug) {
    RunIsolated([this]() {
        unsigned char probe[] = { 0xA5, 0x5A, 0xA5 };

        for (const LogLevelCase &levelCase : kLogLevels) {
            if (levelCase.value >= LOG_DEBUG) {
                continue;
            }

            SCOPED_TRACE(levelCase.key);
            applyLogConfig(std::string(levelCase.key) + "\n");
            const std::string output = captureDump(probe, 3);
            EXPECT_EQ(output.find("A5 5A A5"), std::string::npos);
        }
    });
}

TEST_F(UtilTest, DumpBufferWithZeroLengthEmitsNothing) {
    RunIsolated([this]() {
        applyLogConfig("DEBUG\n");

        unsigned char probe[] = { 0xA5, 0x5A, 0xA5 };
        const std::string output = captureDump(probe, 0);
        EXPECT_EQ(output.find("A5 5A A5"), std::string::npos);
    });
}

TEST_F(UtilTest, DumpBufferEmitsMaximumLengthBuffer) {
    RunIsolated([this]() {
        applyLogConfig("DEBUG\n");

        unsigned char buffer[256];
        for (unsigned int index = 0; index < sizeof(buffer); ++index) {
            buffer[index] = static_cast<unsigned char>(index);
        }

        /*
         * Assert the whole dump as one ordered token sequence rather than asserting the size
         * of the capture. It is the stronger statement about dump_buffer - every byte, in
         * order, in the documented "%02X " form - and unlike a size comparison it stays
         * deterministic if any other writer reaches the redirected descriptor.
         */
        std::string expectedSequence;
        expectedSequence.reserve(sizeof(buffer) * 3);
        for (unsigned int value = 0; value < sizeof(buffer); ++value) {
            char expectedToken[4];
            std::snprintf(expectedToken, sizeof(expectedToken), "%02X ",
                          static_cast<unsigned int>(buffer[value]));
            expectedSequence += expectedToken;
        }
        ASSERT_EQ(sizeof(buffer) * 3, expectedSequence.size());

        const std::string output = captureDump(buffer, sizeof(buffer));
        EXPECT_NE(output.find(expectedSequence), std::string::npos)
            << "dump_buffer did not emit all " << sizeof(buffer)
            << " bytes as an ordered uppercase token sequence";
    });
}

TEST_F(UtilTest, LogEmitsAtOrBelowConfiguredLevel) {
    RunIsolated([this]() {
        applyLogConfig("INFO\n");

        const std::string lowerMarker = "UTIL_LOG_LOWER_LEVEL";
        const std::string lowerOutput = captureLog(LOG_ERROR, lowerMarker);
        EXPECT_NE(lowerOutput.find(lowerMarker), std::string::npos);

        const std::string thresholdMarker = "UTIL_LOG_THRESHOLD_LEVEL";
        const std::string thresholdOutput = captureLog(LOG_INFO, thresholdMarker);
        EXPECT_NE(thresholdOutput.find(thresholdMarker), std::string::npos);
    });
}

TEST_F(UtilTest, LogSuppressesLevelsAboveConfiguredLevel) {
    RunIsolated([this]() {
        applyLogConfig("INFO\n");

        const std::string aboveMarker = "UTIL_LOG_ABOVE_LEVEL";
        const std::string aboveOutput = captureLog(LOG_DEBUG, aboveMarker);
        EXPECT_EQ(aboveOutput.find(aboveMarker), std::string::npos);

        const std::string sentinelMarker = "UTIL_LOG_SENTINEL_LEVEL";
        const std::string sentinelOutput = captureLog(LOG_MAX, sentinelMarker);
        EXPECT_EQ(sentinelOutput.find(sentinelMarker), std::string::npos);
    });
}
