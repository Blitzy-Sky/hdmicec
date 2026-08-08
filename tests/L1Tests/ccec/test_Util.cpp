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
 * The Bus reader and writer threads are the other shared state and are deliberately NOT
 * stopped: quiescing them means LibCCEC::term() then init(), and that path can lose the
 * reader's RUNNING/STOPPING transition and hang. Both threads park without CEC traffic,
 * which no case here generates, and every assertion over captured output matches a unique
 * marker or an ordered token sequence rather than the size of the capture, so output from
 * another writer cannot change a verdict. The residual data race on cec_log_level itself
 * needs a production change and is reported as a blocked gap.
 */

#include <gtest/gtest.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ccec/Util.hpp"

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

    /*
     * Write one configuration value, atomically and without following a link.
     *
     * Only a fully complete write counts. A metadata step that did not take aborts before the
     * rename, so a false here means the path still holds exactly what it held before and
     * metadataError() says why.
     */
    bool write(const char *data, size_t length) {
        metadataErrno_ = 0;
        const int outcome = atomicWrite(data, length, savedMode_, savedUid_, savedGid_);
        if (outcome != kWriteComplete) {
            error_ = describeMetadataFailure("could not write " + std::string(kLogConfigPath));
            return false;
        }
        return true;
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

    /*
     * Put the path back exactly as it was found and disarm the restoration paths.
     *
     * "Exactly" includes the mode and the owner, so a restore that reinstated the CONTENT but
     * could not reinstate the metadata returns false with an explanation. That is not pedantry:
     * this fixture's contract is that it leaves the host as it found it, and a file whose mode
     * this suite widened is a change to the machine whether or not its bytes are right. The
     * content has been put back in that case - the return value reports the shortfall, it does
     * not describe a file left holding a test value.
     */
    bool restoreAndDeactivate() {
        if (!active_) {
            return true;
        }
        metadataErrno_ = 0;
        const int outcome = restoreQuietly();
        removeSignalHandlers();
        active_ = false;

        if (outcome == kWriteComplete) {
            return true;
        }
        if (outcome == kContentWrittenMetadataIncomplete) {
            error_ = describeMetadataFailure(
                std::string("restored the contents of ") + kLogConfigPath
                + " but could not restore its mode and owner");
        } else {
            error_ = describeMetadataFailure(
                std::string("could not restore ") + kLogConfigPath);
        }
        return false;
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
        , handlersInstalled_(false)
        , restoreInProgress_(false)
        , metadataErrno_(0) {
        saved_[0] = '\0';
        tempPath_[0] = '\0';
    }

    LogConfigGuard(const LogConfigGuard &);
    LogConfigGuard &operator=(const LogConfigGuard &);

    /*
     * Turn a recorded metadata errno into a message, on the non-signal paths only.
     *
     * atomicWrite() stores the errno and formats nothing, because it may be running inside a
     * signal handler where allocation is not permitted. The formatting happens here instead,
     * where a std::string is safe, and names the mode and owner that were being restored so a
     * reader can put the file right by hand if this fixture could not.
     */
    std::string describeMetadataFailure(const std::string &prefix) const {
        std::string message = prefix;
        if (metadataErrno_ != 0) {
            message += ": ";
            message += std::strerror(metadataErrno_);
            message += " (wanted mode 0" + std::to_string(static_cast<unsigned long>(savedMode_))
                + ", uid " + std::to_string(static_cast<long>(savedUid_))
                + ", gid " + std::to_string(static_cast<long>(savedGid_)) + ")";
        }
        return message;
    }

    /*
     * Outcome of atomicWrite(), and the reason it is three-valued rather than a bool.
     *
     * A metadata step failing is not the same event as a content step failing, and the two
     * demand opposite handling depending on which caller is asking. When a TEST VALUE is being
     * written, a metadata failure must abort the whole operation: nothing is renamed into place,
     * the path keeps the bytes it had, and the caller reports a failure - the recovery material
     * is then still exactly where it was. When the ORIGINAL is being RESTORED, aborting would be
     * the worse outcome by far, because the path is holding a test value at that moment and
     * refusing to rename would leave it there. So the content is put back and the metadata
     * failure is reported separately, which is what kContentWrittenMetadataIncomplete says.
     */
    enum WriteOutcome {
        kWriteFailed = -1,                       /* nothing replaced; the path is untouched */
        kWriteComplete = 0,                      /* content and metadata both as requested */
        kContentWrittenMetadataIncomplete = 1    /* content replaced; mode and/or owner are not */
    };

    /*
     * Async-signal-safe: no allocation, and only open/write/fchmod/fchown/fstat/rename/unlink/
     * close, every one of which POSIX lists as async-signal-safe. The metadata failure it
     * records is stored in plain scalars for the same reason - the signal path must be able to
     * set them without formatting anything.
     */
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
        /*
         * Hand back the metadata the file was found with, and CHECK that it was handed back.
         * These two calls used to be discarded with a (void) cast on the reasoning that a
         * non-root process cannot always give a file away. The reasoning is sound; discarding
         * the result is not, because it makes the two indistinguishable outcomes - "this process
         * is not permitted to restore the owner" and "the file has been left with the wrong mode
         * or the wrong owner" - and the second is a machine this fixture modified and did not
         * put back.
         *
         * fstat is what turns the check from "the call returned zero" into "the file is as it
         * should be": a partial success, or a platform that reports success without applying the
         * change, is caught by comparing the descriptor's own metadata against what was asked
         * for. The descriptor is used rather than the path, so nothing that happens to the
         * directory entry between the calls can affect the answer.
         */
        /*
         * WHEN THIS BRANCH CAN FIRE, said plainly so it is not mistaken for tested code. On a
         * normal filesystem it cannot. activate() refuses the path unless it is owned by this
         * process's effective uid or this process is root, so the fchown below is always either
         * "give the file to the user who already owns it" or "root gives a file away" - both of
         * which succeed - and fchmod on a file this process owns succeeds for the same reason.
         * What is left is the case this code exists for: a filesystem or a security module that
         * refuses the change anyway - a read-only remount, an immutable attribute, a restrictive
         * label - where the alternative would be to leave the host's file with a mode or an owner
         * this suite changed and never put back. No test provokes it, because provoking it would
         * mean manufacturing a filesystem condition rather than exercising this unit, and the
         * suite's own bar rules that out. It is stated here instead of implied.
         */
        int metadataFailure = 0;
        if (fchmod(fd, mode) != 0) {
            metadataFailure = errno ? errno : EPERM;
        }
        if (uid != static_cast<uid_t>(-1) && fchown(fd, uid, gid) != 0) {
            if (metadataFailure == 0) {
                metadataFailure = errno ? errno : EPERM;
            }
        }
        if (metadataFailure == 0) {
            struct stat applied;
            if (fstat(fd, &applied) != 0) {
                metadataFailure = errno ? errno : EIO;
            } else if ((applied.st_mode & 07777) != (mode & 07777)) {
                metadataFailure = EPERM;
            } else if (uid != static_cast<uid_t>(-1)
                       && (applied.st_uid != uid || applied.st_gid != gid)) {
                metadataFailure = EPERM;
            }
        }

        if (close(fd) != 0) {
            unlink(tempPath_);
            return kWriteFailed;
        }

        /*
         * A metadata failure while writing a TEST VALUE aborts before the rename: the path keeps
         * the bytes it had, the temporary is removed, and saved_ is untouched - so everything
         * needed to recover is still in place, which is the point. During a RESTORE the rename
         * goes ahead, because leaving a test value on the machine to protect a mode bit would be
         * the wrong trade; the caller is told through the return value.
         */
        if (metadataFailure != 0 && !restoreInProgress_) {
            unlink(tempPath_);
            metadataErrno_ = metadataFailure;
            return kWriteFailed;
        }

        if (rename(tempPath_, kLogConfigPath) != 0) {
            unlink(tempPath_);
            return kWriteFailed;
        }

        if (metadataFailure != 0) {
            metadataErrno_ = metadataFailure;
            return kContentWrittenMetadataIncomplete;
        }
        return kWriteComplete;
    }

    /*
     * The restoration used by TearDown, atexit and the signal handlers alike.
     *
     * restoreInProgress_ is what tells atomicWrite() which of the two metadata policies applies.
     * It is set and cleared around the call rather than passed as an argument so that the signal
     * and atexit paths, which call this function with no context of their own, get the restore
     * policy automatically.
     */
    int restoreQuietly() {
        if (!savedExisted_) {
            return (unlink(kLogConfigPath) == 0 || errno == ENOENT) ? kWriteComplete : kWriteFailed;
        }
        restoreInProgress_ = true;
        const int outcome = atomicWrite(saved_, savedLength_, savedMode_, savedUid_, savedGid_);
        restoreInProgress_ = false;
        return outcome;
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
    /* Selects atomicWrite()'s metadata policy: abort before the rename while writing a test
     * value, complete the rename while restoring. Set only around restoreQuietly(). */
    bool restoreInProgress_;
    /* The errno of the metadata step that failed, recorded by atomicWrite() - which may be on a
     * signal path and so cannot format - and turned into a message by describeMetadataFailure().
     * volatile sig_atomic_t because a signal handler writes it. */
    volatile sig_atomic_t metadataErrno_;
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

// Table key that selects a given numeric level, so a snapshotted level can be put back
// through the only route production offers: write the file, then re-read it. LOG_INFO is
// Util.cpp's static initialiser, i.e. the level a process has before any file is read, so
// it is the documented fallback when the probe found nothing.
const char *keyForLogLevel(int level) {
    for (const LogLevelCase &levelCase : kLogLevels) {
        if (levelCase.value == level) {
            return levelCase.key;
        }
    }
    return "INFO";
}

// Whether a configuration file is present at the fixed path. lstat, so a symlink standing
// there is not reported as the file.
bool logConfigExists() {
    struct stat linkStatus;
    return lstat(kLogConfigPath, &linkStatus) == 0 && S_ISREG(linkStatus.st_mode) != 0;
}

} // namespace

class UtilTest : public ::testing::Test {
protected:
    void SetUp() override {
        /* Capture the configuration file and arm every restoration path before the first
         * write. A refusal here means the path holds something this fixture must not
         * modify, which is a hard failure rather than a licence to proceed. */
        ASSERT_TRUE(logConfig().activate()) << logConfig().lastError();

        /* Snapshot the EFFECTIVE level as well as the file. Restoring only the file would
         * leave the process at whatever verbosity the last case selected, which the later
         * LibCCEC and DriverImpl cases would silently inherit. */
        savedLogLevel_ = probeEffectiveLogLevel();

        /* Regression guard for exactly that leak: every case in this fixture must start
         * from the same effective level, so a case that failed to restore it fails here. */
        if (firstObservedLogLevel_ == kUnknownLogLevel) {
            firstObservedLogLevel_ = savedLogLevel_;
        }
        EXPECT_EQ(firstObservedLogLevel_, savedLogLevel_)
            << "the effective CEC log level changed between UtilTest cases. Either an "
            << "earlier case leaked verbosity into this one, or another process rewrote "
            << kLogConfigPath << " while this suite was running - that path is fixed by "
            << "the production reader, so two concurrent copies of this suite on one host "
            << "contend for it.";

        applyLogConfig("INFO\n");
    }

    void TearDown() override {
        /* Normalise the process-static level to the compile-time default first: when the
         * configuration file did not exist before this test, the production reader returns
         * early without touching the level, so the file being gone is not by itself enough
         * to put the level back. */
        if (logConfig().active()) {
            const bool resetFileWritten =
                writeLogConfig(std::string(keyForLogLevel(savedLogLevel_)) + "\n");
            /* lastError() is streamed so a metadata failure - a mode or owner that could not be
             * applied - names itself here rather than surfacing only as a bare false. */
            EXPECT_TRUE(resetFileWritten) << logConfig().lastError();
            if (resetFileWritten) {
                EXPECT_NO_THROW(check_cec_log_status());
                /* Read it back: writing the file is not proof the reader adopted it. */
                if (savedLogLevel_ != kUnknownLogLevel) {
                    EXPECT_EQ(savedLogLevel_, probeEffectiveLogLevel());
                }
            }
        }

        /* Then hand the file back byte-for-byte (or remove it again) and disarm the
         * atexit/signal restoration. */
        /* A false here now covers two distinct outcomes and lastError() says which: the contents
         * could not be put back at all, or the contents were restored but the file's mode and
         * owner were not. Both leave the host altered, so both fail - and both name what to fix
         * by hand. */
        EXPECT_TRUE(logConfig().restoreAndDeactivate())
            << "failed to restore " << kLogConfigPath << ": " << logConfig().lastError();

        /* Re-read the file that is now on disk so the process-static level cannot be left
         * disagreeing with it: with the original file back the level becomes whatever that
         * file says, and with no file the reader returns early and the level stays at the
         * default just written above. Either way the pair is consistent when this test
         * hands control on. */
        EXPECT_NO_THROW(check_cec_log_status());
    }

    void applyLogConfig(const std::string &contents) {
        EXPECT_TRUE(writeLogConfig(contents)) << logConfig().lastError();
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
    int savedLogLevel_ = kUnknownLogLevel;

    /* Shared by every case in the fixture: the effective level observed before the first
     * case ran, which every later case must also observe. */
    static int firstObservedLogLevel_;
};

int UtilTest::firstObservedLogLevel_ = kUnknownLogLevel;

// Traceability: #gap-mw-util - section 6.2 rank 33, P2 -
// check_cec_log_status().
TEST_F(UtilTest, MissingConfigFileLeavesLogLevelUnchanged) {
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
}

TEST_F(UtilTest, EveryRecognisedLevelMapsToItsNumericSetting) {
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
}

TEST_F(UtilTest, EmptyConfigFileLeavesLogLevelUnchanged) {
    applyLogConfig("INFO\n");
    applyLogConfig("");

    const std::string infoMarker = "UTIL_EMPTY_FILE_INFO";
    const std::string infoOutput = captureLog(LOG_INFO, infoMarker);
    EXPECT_NE(infoOutput.find(infoMarker), std::string::npos);

    const std::string debugMarker = "UTIL_EMPTY_FILE_DEBUG";
    const std::string debugOutput = captureLog(LOG_DEBUG, debugMarker);
    EXPECT_EQ(debugOutput.find(debugMarker), std::string::npos);
}

TEST_F(UtilTest, UnrecognisedLevelLeavesLogLevelUnchanged) {
    applyLogConfig("TRACE\n");
    applyLogConfig("NOT_A_LEVEL\n");

    const std::string marker = "UTIL_UNRECOGNISED_RETAINS_TRACE";
    const std::string output = captureLog(LOG_TRACE, marker);
    EXPECT_NE(output.find(marker), std::string::npos);
}

TEST_F(UtilTest, ShortPrefixDoesNotMatchRecognisedLevel) {
    applyLogConfig("INFO\n");
    applyLogConfig("DEBU\n");

    const std::string infoMarker = "UTIL_SHORT_PREFIX_INFO";
    const std::string infoOutput = captureLog(LOG_INFO, infoMarker);
    EXPECT_NE(infoOutput.find(infoMarker), std::string::npos);

    const std::string debugMarker = "UTIL_SHORT_PREFIX_DEBUG";
    const std::string debugOutput = captureLog(LOG_DEBUG, debugMarker);
    EXPECT_EQ(debugOutput.find(debugMarker), std::string::npos);
}

TEST_F(UtilTest, RecognisedLevelWithTrailingContentIsAccepted) {
    applyLogConfig("DEBUG extra trailing text\n");

    unsigned char probe[] = { 0xC3, 0x5E };
    const std::string output = captureDump(probe, 2);
    EXPECT_NE(output.find("C3 5E"), std::string::npos);
}

TEST_F(UtilTest, DumpBufferEmitsAtDebugAndTraceLevels) {
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
}

TEST_F(UtilTest, DumpBufferIsSilentBelowDebug) {
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
}

TEST_F(UtilTest, DumpBufferWithZeroLengthEmitsNothing) {
    applyLogConfig("DEBUG\n");

    unsigned char probe[] = { 0xA5, 0x5A, 0xA5 };
    const std::string output = captureDump(probe, 0);
    EXPECT_EQ(output.find("A5 5A A5"), std::string::npos);
}

TEST_F(UtilTest, DumpBufferEmitsMaximumLengthBuffer) {
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
}

TEST_F(UtilTest, LogEmitsAtOrBelowConfiguredLevel) {
    applyLogConfig("INFO\n");

    const std::string lowerMarker = "UTIL_LOG_LOWER_LEVEL";
    const std::string lowerOutput = captureLog(LOG_ERROR, lowerMarker);
    EXPECT_NE(lowerOutput.find(lowerMarker), std::string::npos);

    const std::string thresholdMarker = "UTIL_LOG_THRESHOLD_LEVEL";
    const std::string thresholdOutput = captureLog(LOG_INFO, thresholdMarker);
    EXPECT_NE(thresholdOutput.find(thresholdMarker), std::string::npos);
}

TEST_F(UtilTest, LogSuppressesLevelsAboveConfiguredLevel) {
    applyLogConfig("INFO\n");

    const std::string aboveMarker = "UTIL_LOG_ABOVE_LEVEL";
    const std::string aboveOutput = captureLog(LOG_DEBUG, aboveMarker);
    EXPECT_EQ(aboveOutput.find(aboveMarker), std::string::npos);

    const std::string sentinelMarker = "UTIL_LOG_SENTINEL_LEVEL";
    const std::string sentinelOutput = captureLog(LOG_MAX, sentinelMarker);
    EXPECT_EQ(sentinelOutput.find(sentinelMarker), std::string::npos);
}
