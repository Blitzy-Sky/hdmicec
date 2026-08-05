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
 * L1 unit tests for the CEC logging utility.
 *
 * No test translation unit previously compiled against ccec/src/Util.cpp. Only the
 * early-return arm of check_cec_log_status() had ever run - the arm taken when the
 * log-level configuration file cannot be opened. The success path (read a line,
 * match it against the log-level table, convert the value, close the handle) and the
 * hexadecimal buffer dump that executes only at debug verbosity were both untouched.
 *
 * check_cec_log_status() reads a process-global path and mutates a process-global
 * log level, so the fixture captures both and restores them in TearDown - never an
 * unconditional delete. The log level is restored to LOG_INFO, which is the value
 * the translation unit's static initialiser establishes at program start.
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <unistd.h>

#include "ccec/Util.hpp"

namespace {

const char *kLogConfigPath = "/tmp/cec_log_enabled";

// Redirect file descriptor 1 for the duration of `body` and return what was
// written. The unit under test logs with printf(), so the capture has to happen at
// the descriptor level rather than on a C++ stream.
std::string captureStdout(const std::function<void()> &body) {
    char templatePath[] = "/tmp/cec_util_stdoutXXXXXX";
    int captureFd = mkstemp(templatePath);
    if (captureFd < 0) {
        body();
        return std::string();
    }

    fflush(stdout);
    int savedStdout = dup(STDOUT_FILENO);
    dup2(captureFd, STDOUT_FILENO);

    body();

    fflush(stdout);
    dup2(savedStdout, STDOUT_FILENO);
    close(savedStdout);
    lseek(captureFd, 0, SEEK_SET);

    std::string captured;
    char buffer[512];
    ssize_t bytesRead;
    while ((bytesRead = read(captureFd, buffer, sizeof(buffer))) > 0) {
        captured.append(buffer, static_cast<size_t>(bytesRead));
    }

    close(captureFd);
    unlink(templatePath);
    return captured;
}

void writeLogConfig(const std::string &contents) {
    std::ofstream out(kLogConfigPath, std::ios::trunc);
    out << contents;
    out.close();
}

// dump_buffer() only emits when the configured level is at or above LOG_DEBUG, so
// its output is the observable proxy for the level check_cec_log_status() applied.
bool debugLoggingIsActive() {
    // A deliberately distinctive probe pattern so a concurrent log line from another
    // component cannot be mistaken for the dump's own output.
    unsigned char probe[] = { 0xA5, 0x5A, 0xA5 };
    std::string output = captureStdout([&probe]() { dump_buffer(probe, 3); });
    return output.find("A5 5A A5") != std::string::npos;
}

} // namespace

class UtilTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Capture the pre-existing configuration file so it can be put back exactly
        // as it was, whether or not it existed.
        std::ifstream in(kLogConfigPath);
        savedFileExisted = in.good();
        if (savedFileExisted) {
            savedFileContents.assign((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
        }
        in.close();
    }

    void TearDown() override {
        // Restore the process-global log level to the translation unit's initial
        // value (LOG_INFO) before restoring the file itself.
        writeLogConfig("INFO\n");
        check_cec_log_status();

        if (savedFileExisted) {
            writeLogConfig(savedFileContents);
        } else {
            unlink(kLogConfigPath);
        }
    }

    bool savedFileExisted = false;
    std::string savedFileContents;
};

// The previously covered arm: the configuration file is absent, so the reader logs
// and returns without touching the level. Kept as a regression guard.
TEST_F(UtilTest, MissingConfigFileLeavesLogLevelUnchanged) {
    unlink(kLogConfigPath);

    EXPECT_NO_THROW({ check_cec_log_status(); });
    // The default level is LOG_INFO, which is below LOG_DEBUG.
    EXPECT_FALSE(debugLoggingIsActive());
}

// The success path: a recognised key is read, matched against the table and
// converted, and the file handle is closed.
TEST_F(UtilTest, DebugLevelInConfigFileRaisesLogLevel) {
    writeLogConfig("DEBUG\n");

    EXPECT_NO_THROW({ check_cec_log_status(); });
    EXPECT_TRUE(debugLoggingIsActive());
}

TEST_F(UtilTest, TraceLevelInConfigFileRaisesLogLevelAboveDebug) {
    writeLogConfig("TRACE\n");

    EXPECT_NO_THROW({ check_cec_log_status(); });
    EXPECT_TRUE(debugLoggingIsActive());
}

// Every remaining entry in the log-level table must also be matched and applied.
// All of these sit below LOG_DEBUG, so the buffer dump must stay silent.
TEST_F(UtilTest, LevelsBelowDebugSuppressBufferDump) {
    const char *levelsBelowDebug[] = { "FATAL", "ERROR", "WARN", "EXP", "NOTICE", "INFO" };

    for (const char *level : levelsBelowDebug) {
        writeLogConfig(std::string(level) + "\n");
        check_cec_log_status();
        EXPECT_FALSE(debugLoggingIsActive()) << "level " << level << " must not enable the dump";
    }
}

// An unrecognised key matches nothing in the table, so the loop runs to completion
// and the level is left exactly as it was.
TEST_F(UtilTest, UnrecognisedLevelLeavesLogLevelUnchanged) {
    writeLogConfig("DEBUG\n");
    check_cec_log_status();
    ASSERT_TRUE(debugLoggingIsActive());

    writeLogConfig("NOT_A_LEVEL\n");
    EXPECT_NO_THROW({ check_cec_log_status(); });
    // Unchanged means still DEBUG, not reset to the default.
    EXPECT_TRUE(debugLoggingIsActive());
}

// An empty file opens successfully but yields no line, so the table is never
// consulted and the handle is still closed.
TEST_F(UtilTest, EmptyConfigFileLeavesLogLevelUnchanged) {
    writeLogConfig("");

    EXPECT_NO_THROW({ check_cec_log_status(); });
    EXPECT_FALSE(debugLoggingIsActive());
}

// The comparison is a prefix match of a fixed length, so trailing content after a
// recognised key is tolerated.
TEST_F(UtilTest, RecognisedLevelWithTrailingContentIsAccepted) {
    writeLogConfig("DEBUG extra trailing text\n");

    EXPECT_NO_THROW({ check_cec_log_status(); });
    EXPECT_TRUE(debugLoggingIsActive());
}

TEST_F(UtilTest, RepeatedReadsAreIdempotent) {
    writeLogConfig("DEBUG\n");

    check_cec_log_status();
    check_cec_log_status();
    check_cec_log_status();

    EXPECT_TRUE(debugLoggingIsActive());
}

TEST_F(UtilTest, DumpBufferEmitsEveryByteAsUppercaseHexAtDebugLevel) {
    writeLogConfig("DEBUG\n");
    check_cec_log_status();

    unsigned char frame[] = { 0x00, 0x0F, 0x36, 0xFF };
    std::string output = captureStdout([&frame]() { dump_buffer(frame, 4); });

    EXPECT_NE(output.find("00"), std::string::npos);
    EXPECT_NE(output.find("0F"), std::string::npos);
    EXPECT_NE(output.find("36"), std::string::npos);
    EXPECT_NE(output.find("FF"), std::string::npos);
}

// A zero length must produce no output even at debug verbosity: the guard is passed
// but the loop body never runs.
TEST_F(UtilTest, DumpBufferWithZeroLengthEmitsNothing) {
    writeLogConfig("DEBUG\n");
    check_cec_log_status();

    unsigned char frame[] = { 0xA5, 0x5A, 0xA5 };
    std::string output = captureStdout([&frame]() { dump_buffer(frame, 0); });

    EXPECT_EQ(output.find("A5 5A A5"), std::string::npos);
}

// CCEC_LOG filters on both the requested level and the configured level.
TEST_F(UtilTest, LogEmitsAtOrBelowConfiguredLevel) {
    writeLogConfig("DEBUG\n");
    check_cec_log_status();

    std::string emitted = captureStdout([]() { CCEC_LOG(LOG_ERROR, "cec-util-test-emitted\r\n"); });
    EXPECT_NE(emitted.find("cec-util-test-emitted"), std::string::npos);
}

TEST_F(UtilTest, LogSuppressesLevelsAboveConfiguredLevel) {
    writeLogConfig("FATAL\n");
    check_cec_log_status();

    std::string suppressed = captureStdout([]() { CCEC_LOG(LOG_TRACE, "cec-util-test-suppressed\r\n"); });
    EXPECT_EQ(suppressed.find("cec-util-test-suppressed"), std::string::npos);
}

// LOG_MAX is the sentinel above the highest real level and must never be emitted,
// whatever the configured level is.
TEST_F(UtilTest, LogRejectsSentinelLevel) {
    writeLogConfig("TRACE\n");
    check_cec_log_status();

    std::string suppressed = captureStdout([]() { CCEC_LOG(LOG_MAX, "cec-util-test-sentinel\r\n"); });
    EXPECT_EQ(suppressed.find("cec-util-test-sentinel"), std::string::npos);
}
