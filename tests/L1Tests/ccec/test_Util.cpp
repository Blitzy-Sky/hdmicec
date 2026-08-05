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
 * Dedicated L1 tests for the CEC logging utility.
 *
 * Util.cpp already receives incidental coverage through CECFrame's HexDump cases.
 * These tests directly cover its configuration reader, verbosity gates and buffer
 * formatter without claiming to provide the unit's first coverage.
 *
 * The configuration reader selects a fixed table entry by key prefix; numeric
 * values are not parsed from the file. Consequently, malformed numeric-value
 * scenarios are unreachable and intentionally excluded from this suite.
 *
 * Both the hardcoded configuration file and the file-static log level are shared
 * process state. The fixture restores the log level to INFO and then restores the
 * file byte-for-byte so later LibCCEC and DriverImpl tests cannot inherit verbosity.
 */

#include <gtest/gtest.h>

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>

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

template <typename Callable>
std::string captureStdout(Callable body) {
    ::testing::internal::CaptureStdout();
    try {
        body();
    } catch (...) {
        (void)::testing::internal::GetCapturedStdout();
        throw;
    }
    return ::testing::internal::GetCapturedStdout();
}

bool writeLogConfig(const std::string &contents) {
    std::ofstream out(kLogConfigPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    out.close();
    return out.good();
}

bool fileExists(const char *path) {
    struct stat fileStatus;
    return stat(path, &fileStatus) == 0;
}

} // namespace

class UtilTest : public ::testing::Test {
protected:
    void SetUp() override {
        savedFileExisted_ = fileExists(kLogConfigPath);
        if (savedFileExisted_) {
            std::ifstream in(kLogConfigPath, std::ios::binary);
            EXPECT_TRUE(in.is_open());
            if (in.is_open()) {
                savedFileContents_.assign(std::istreambuf_iterator<char>(in),
                                          std::istreambuf_iterator<char>());
            }
        }

        EXPECT_TRUE(writeLogConfig("INFO\n"));
        EXPECT_NO_THROW(check_cec_log_status());
    }

    void TearDown() override {
        const bool resetFileWritten = writeLogConfig("INFO\n");
        EXPECT_TRUE(resetFileWritten);
        if (resetFileWritten) {
            EXPECT_NO_THROW(check_cec_log_status());
        }

        if (savedFileExisted_) {
            EXPECT_TRUE(writeLogConfig(savedFileContents_));
        } else {
            errno = 0;
            const int removeResult = std::remove(kLogConfigPath);
            EXPECT_TRUE(removeResult == 0 || errno == ENOENT);
        }
    }

    void applyLogConfig(const std::string &contents) {
        EXPECT_TRUE(writeLogConfig(contents));
        EXPECT_NO_THROW(check_cec_log_status());
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
    bool savedFileExisted_ = false;
    std::string savedFileContents_;
};

// Traceability: #gap-mw-util - section 6.2 rank 33, P2 -
// check_cec_log_status().
TEST_F(UtilTest, MissingConfigFileLeavesLogLevelUnchanged) {
    applyLogConfig("DEBUG\n");
    EXPECT_EQ(0, std::remove(kLogConfigPath));

    std::string readerOutput;
    EXPECT_NO_THROW(readerOutput = captureStdout([]() {
        check_cec_log_status();
    }));
    EXPECT_NE(readerOutput.find("cec_log_enabled"), std::string::npos);

    unsigned char probe[] = { 0xA5, 0x5A, 0xA5 };
    const std::string dumpOutput = captureDump(probe, 3);
    EXPECT_NE(dumpOutput.find("A5 5A A5"), std::string::npos);
}

// Traceability: #gap-mw-util - section 6.2 rank 33, P2 -
// check_cec_log_status(), CCEC_LOG() and dump_buffer().
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

// Traceability: #gap-mw-util - section 6.2 rank 33, P2 -
// check_cec_log_status().
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

// Traceability: #gap-mw-util - section 6.2 rank 33, P2 -
// check_cec_log_status().
TEST_F(UtilTest, UnrecognisedLevelLeavesLogLevelUnchanged) {
    applyLogConfig("TRACE\n");
    applyLogConfig("NOT_A_LEVEL\n");

    const std::string marker = "UTIL_UNRECOGNISED_RETAINS_TRACE";
    const std::string output = captureLog(LOG_TRACE, marker);
    EXPECT_NE(output.find(marker), std::string::npos);
}

// Traceability: #gap-mw-util - section 6.2 rank 33, P2 -
// check_cec_log_status().
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

// Traceability: #gap-mw-util - section 6.2 rank 33, P2 -
// check_cec_log_status() and dump_buffer().
TEST_F(UtilTest, RecognisedLevelWithTrailingContentIsAccepted) {
    applyLogConfig("DEBUG extra trailing text\n");

    unsigned char probe[] = { 0xC3, 0x5E };
    const std::string output = captureDump(probe, 2);
    EXPECT_NE(output.find("C3 5E"), std::string::npos);
}

// Traceability: #gap-mw-util - section 6.2 rank 33, P2 -
// dump_buffer().
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

// Traceability: #gap-mw-util - section 6.2 rank 33, P2 -
// dump_buffer().
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

// Traceability: #gap-mw-util - section 6.2 rank 33, P2 -
// dump_buffer().
TEST_F(UtilTest, DumpBufferWithZeroLengthEmitsNothing) {
    applyLogConfig("DEBUG\n");

    unsigned char probe[] = { 0xA5, 0x5A, 0xA5 };
    const std::string output = captureDump(probe, 0);
    EXPECT_EQ(output.find("A5 5A A5"), std::string::npos);
}

// Traceability: #gap-mw-util - section 6.2 rank 33, P2 -
// dump_buffer().
TEST_F(UtilTest, DumpBufferEmitsMaximumLengthBuffer) {
    applyLogConfig("DEBUG\n");

    unsigned char buffer[256];
    for (unsigned int index = 0; index < sizeof(buffer); ++index) {
        buffer[index] = static_cast<unsigned char>(index);
    }

    const std::string output = captureDump(buffer, sizeof(buffer));
    EXPECT_EQ(sizeof(buffer) * 3, output.size());

    for (unsigned int value = 0; value < sizeof(buffer); ++value) {
        char expectedToken[4];
        std::snprintf(expectedToken, sizeof(expectedToken), "%02X ",
                      static_cast<unsigned int>(buffer[value]));
        EXPECT_NE(output.find(expectedToken), std::string::npos)
            << "missing uppercase token for value " << value;
    }
}

// Traceability: #gap-mw-util - section 6.2 rank 33, P2 - CCEC_LOG().
TEST_F(UtilTest, LogEmitsAtOrBelowConfiguredLevel) {
    applyLogConfig("INFO\n");

    const std::string lowerMarker = "UTIL_LOG_LOWER_LEVEL";
    const std::string lowerOutput = captureLog(LOG_ERROR, lowerMarker);
    EXPECT_NE(lowerOutput.find(lowerMarker), std::string::npos);

    const std::string thresholdMarker = "UTIL_LOG_THRESHOLD_LEVEL";
    const std::string thresholdOutput = captureLog(LOG_INFO, thresholdMarker);
    EXPECT_NE(thresholdOutput.find(thresholdMarker), std::string::npos);
}

// Traceability: #gap-mw-util - section 6.2 rank 33, P2 - CCEC_LOG().
TEST_F(UtilTest, LogSuppressesLevelsAboveConfiguredLevel) {
    applyLogConfig("INFO\n");

    const std::string aboveMarker = "UTIL_LOG_ABOVE_LEVEL";
    const std::string aboveOutput = captureLog(LOG_DEBUG, aboveMarker);
    EXPECT_EQ(aboveOutput.find(aboveMarker), std::string::npos);

    const std::string sentinelMarker = "UTIL_LOG_SENTINEL_LEVEL";
    const std::string sentinelOutput = captureLog(LOG_MAX, sentinelMarker);
    EXPECT_EQ(sentinelOutput.find(sentinelMarker), std::string::npos);
}
