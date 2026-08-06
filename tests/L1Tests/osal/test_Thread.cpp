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
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <pthread.h>

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

/**
 * Test double for the one path that genuinely leaves this translation unit:
 * Thread::start() hands Thread::CEntry to pthread_create, so run() is invoked on
 * another thread and the caller has no join to wait on -- start() creates the
 * thread with PTHREAD_CREATE_DETACHED, and Thread::stop() and
 * Thread::getNativeHandle() are declared in the header but never defined
 * anywhere in the component, so neither can be called.
 *
 * The dispatch is therefore observed the only way it can be: the runnable itself
 * signals a condition variable, and the test waits on that with a bound.  It
 * records the thread it ran on as well, which is what distinguishes "start()
 * dispatched asynchronously" from "start() happened to call run() inline".
 */
class SignallingRunnable : public Runnable {
public:
    SignallingRunnable() : dispatched_(false), dispatchThread_() {
    }

    void run(void) override {
        std::unique_lock<std::mutex> lock(mutex_);
        dispatchThread_ = pthread_self();
        dispatched_ = true;
        condition_.notify_all();
    }

    /* Returns true once run() has executed, false if it has not within the bound. */
    bool waitForDispatch(int timeoutMs) {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                  [this]() { return dispatched_; });
    }

    pthread_t dispatchThread(void) {
        std::unique_lock<std::mutex> lock(mutex_);
        return dispatchThread_;
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool dispatched_;
    pthread_t dispatchThread_;
};

} // namespace

class ThreadTest : public ::testing::Test {
protected:
    CountingRunnable runnable;
};

TEST_F(ThreadTest, NamedConstructionDoesNotDispatch) {
    Thread thread(runnable, reinterpret_cast<const int8_t *>("worker"));

    EXPECT_EQ(0, runnable.invocationCount());
    (void)thread;
}

TEST_F(ThreadTest, RunDispatchesToRunnable) {
    Thread thread(runnable);

    thread.run();

    EXPECT_EQ(1, runnable.invocationCount());
}

TEST_F(ThreadTest, DestructionAtScopeExitPreservesDispatchResult) {
    {
        Thread thread(runnable);
        thread.run();
        EXPECT_EQ(1, runnable.invocationCount());
    }

    EXPECT_EQ(1, runnable.invocationCount());
}

TEST_F(ThreadTest, DetachThenScopeExitPreservesDispatchResult) {
    {
        Thread thread(runnable);
        thread.run();
        EXPECT_NO_THROW(thread.detach());
    }

    EXPECT_EQ(1, runnable.invocationCount());
}

TEST_F(ThreadTest, EmptyNameIsAccepted) {
    Thread thread(runnable, reinterpret_cast<const int8_t *>(""));

    thread.run();

    EXPECT_EQ(1, runnable.invocationCount());
}

TEST_F(ThreadTest, LongNameIsAccepted) {
    const std::string longName(1024U, 'n');
    Thread thread(
        runnable,
        reinterpret_cast<const int8_t *>(longName.c_str()));

    thread.run();

    EXPECT_EQ(1, runnable.invocationCount());
}

TEST_F(ThreadTest, RunDispatchIsRepeatable) {
    Thread thread(runnable, reinterpret_cast<const int8_t *>("repeat"));

    thread.run();
    thread.run();
    thread.run();

    EXPECT_EQ(3, runnable.invocationCount());
}

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

TEST_F(ThreadTest, DestructionWithoutStartIsHarmless) {
    EXPECT_EQ(0, runnable.invocationCount());
    {
        Thread thread(runnable);
        (void)thread;
    }

    EXPECT_EQ(0, runnable.invocationCount());
}

TEST_F(ThreadTest, DetachWithoutStartLeavesThreadUsable) {
    Thread thread(runnable);

    EXPECT_NO_THROW(thread.detach());
    EXPECT_EQ(0, runnable.invocationCount());

    thread.run();

    EXPECT_EQ(1, runnable.invocationCount());
}

// Traceability: #gap-mw-thread; COVERAGE_GAPS.md section 6.2 rank 34; P2.
// Symbols: CCEC_OSAL::Thread::Thread(Runnable &, const int8_t *),
// CCEC_OSAL::Thread::start, CCEC_OSAL::Thread::CEntry, CCEC_OSAL::Thread::~Thread.
//
// The remaining two symbols of the unit, and the only asynchronous behaviour it has:
// start() configures a detached pthread attribute set and passes Thread::CEntry to
// pthread_create, and CEntry casts its argument back to the Runnable and calls run().
// Waiting on the runnable's own signal rather than on a fixed sleep keeps the case
// deterministic: it returns as soon as the dispatch lands and fails rather than hangs
// if it never does.
TEST_F(ThreadTest, StartDispatchesRunnableOnAnotherThread) {
    SignallingRunnable asyncRunnable;
    const pthread_t callingThread = pthread_self();

    {
        Thread thread(asyncRunnable, reinterpret_cast<const int8_t *>("l1-start"));

        thread.start();

        // Bounded, and deliberately generous rather than tight: this runs inside a suite whose
        // Bus reader threads can be CPU-bound, and a tight bound would turn scheduling pressure
        // into a spurious failure (measured: one such failure in 25 full-suite runs at 5 s).
        // Fatal on purpose - start() creates the worker DETACHED and swallows a pthread_create
        // failure, so the only way this times out is that no worker exists, and continuing would
        // assert on a dispatch that never happened.
        ASSERT_TRUE(asyncRunnable.waitForDispatch(30000));
    }

    // Dispatch really crossed a thread boundary; start() did not degrade to an
    // inline call on the caller.
    EXPECT_EQ(0, pthread_equal(callingThread, asyncRunnable.dispatchThread()));

    // The worker was created detached, so scope exit above is a complete teardown:
    // ~Thread has no join to perform and must not block or fail.
    EXPECT_TRUE(asyncRunnable.waitForDispatch(0));
}
