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

#include <gtest/gtest.h>
#include "osal/Runnable.hpp"
#include "osal/Thread.hpp"
#include <string>

using namespace CCEC_OSAL;

namespace {

/**
 * Minimal synchronous test double for Runnable.
 *
 * Thread::run() forwards directly to the supplied Runnable, so an ordinary
 * counter provides a deterministic observation without worker threads, timing
 * assumptions, mocks, or heap deletion through an interface without a virtual
 * destructor.
 */
class CountingRunnable : public Runnable {
public:
    CountingRunnable() : invocationCount_(0) {
    }

    void run(void) override {
        ++invocationCount_;
    }

    int invocationCount(void) const {
        return invocationCount_;
    }

private:
    int invocationCount_;
};

} // namespace

class ThreadTest : public ::testing::Test {
protected:
    CountingRunnable runnable;
};

// Traceability: #gap-mw-thread; COVERAGE_GAPS.md section 6.2 rank 34; P2.
// Symbols: CCEC_OSAL::Thread::Thread(Runnable &, const int8_t *).
TEST_F(ThreadTest, NamedConstructionDoesNotDispatch) {
    Thread thread(runnable, reinterpret_cast<const int8_t *>("worker"));

    EXPECT_EQ(0, runnable.invocationCount());
    (void)thread;
}

// Traceability: #gap-mw-thread; COVERAGE_GAPS.md section 6.2 rank 34; P2.
// Symbols: CCEC_OSAL::Thread::Thread(Runnable &), CCEC_OSAL::Thread::run.
TEST_F(ThreadTest, RunDispatchesToRunnable) {
    Thread thread(runnable);

    thread.run();

    EXPECT_EQ(1, runnable.invocationCount());
}

// Traceability: #gap-mw-thread; COVERAGE_GAPS.md section 6.2 rank 34; P2.
// Symbols: CCEC_OSAL::Thread::Thread(Runnable &), CCEC_OSAL::Thread::run,
// CCEC_OSAL::Thread::~Thread.
TEST_F(ThreadTest, DestructionAtScopeExitPreservesDispatchResult) {
    {
        Thread thread(runnable);
        thread.run();
        EXPECT_EQ(1, runnable.invocationCount());
    }

    EXPECT_EQ(1, runnable.invocationCount());
}

// Traceability: #gap-mw-thread; COVERAGE_GAPS.md section 6.2 rank 34; P2.
// Symbols: CCEC_OSAL::Thread::Thread(Runnable &), CCEC_OSAL::Thread::run,
// CCEC_OSAL::Thread::detach, CCEC_OSAL::Thread::~Thread.
TEST_F(ThreadTest, DetachThenScopeExitPreservesDispatchResult) {
    {
        Thread thread(runnable);
        thread.run();
        EXPECT_NO_THROW(thread.detach());
    }

    EXPECT_EQ(1, runnable.invocationCount());
}

// Traceability: #gap-mw-thread; COVERAGE_GAPS.md section 6.2 rank 34; P2.
// Symbols: CCEC_OSAL::Thread::Thread(Runnable &, const int8_t *),
// CCEC_OSAL::Thread::run.
TEST_F(ThreadTest, EmptyNameIsAccepted) {
    Thread thread(runnable, reinterpret_cast<const int8_t *>(""));

    thread.run();

    EXPECT_EQ(1, runnable.invocationCount());
}

// Traceability: #gap-mw-thread; COVERAGE_GAPS.md section 6.2 rank 34; P2.
// Symbols: CCEC_OSAL::Thread::Thread(Runnable &, const int8_t *),
// CCEC_OSAL::Thread::run.
TEST_F(ThreadTest, LongNameIsAccepted) {
    const std::string longName(1024U, 'n');
    Thread thread(
        runnable,
        reinterpret_cast<const int8_t *>(longName.c_str()));

    thread.run();

    EXPECT_EQ(1, runnable.invocationCount());
}

// Traceability: #gap-mw-thread; COVERAGE_GAPS.md section 6.2 rank 34; P2.
// Symbols: CCEC_OSAL::Thread::Thread(Runnable &, const int8_t *),
// CCEC_OSAL::Thread::run.
TEST_F(ThreadTest, RunDispatchIsRepeatable) {
    Thread thread(runnable, reinterpret_cast<const int8_t *>("repeat"));

    thread.run();
    thread.run();
    thread.run();

    EXPECT_EQ(3, runnable.invocationCount());
}

// Traceability: #gap-mw-thread; COVERAGE_GAPS.md section 6.2 rank 34; P2.
// Symbols: CCEC_OSAL::Thread::Thread(Runnable &),
// CCEC_OSAL::Thread::Thread(Runnable &, const int8_t *),
// CCEC_OSAL::Thread::run.
TEST_F(ThreadTest, NamedAndUnnamedConstructorsDispatchEquivalently) {
    CountingRunnable namedRunnable;
    Thread unnamedThread(runnable);
    Thread namedThread(
        namedRunnable,
        reinterpret_cast<const int8_t *>("named"));

    unnamedThread.run();
    namedThread.run();

    EXPECT_EQ(1, runnable.invocationCount());
    EXPECT_EQ(1, namedRunnable.invocationCount());
}

// Traceability: #gap-mw-thread; COVERAGE_GAPS.md section 6.2 rank 34; P2.
// Symbols: CCEC_OSAL::Thread::Thread(Runnable &),
// CCEC_OSAL::Thread::~Thread.
TEST_F(ThreadTest, DestructionWithoutStartIsHarmless) {
    EXPECT_EQ(0, runnable.invocationCount());
    {
        Thread thread(runnable);
        (void)thread;
    }

    EXPECT_EQ(0, runnable.invocationCount());
}

// Traceability: #gap-mw-thread; COVERAGE_GAPS.md section 6.2 rank 34; P2.
// Symbols: CCEC_OSAL::Thread::Thread(Runnable &),
// CCEC_OSAL::Thread::detach, CCEC_OSAL::Thread::run.
TEST_F(ThreadTest, DetachWithoutStartLeavesThreadUsable) {
    Thread thread(runnable);

    EXPECT_NO_THROW(thread.detach());
    EXPECT_EQ(0, runnable.invocationCount());

    thread.run();

    EXPECT_EQ(1, runnable.invocationCount());
}
