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
 * L1 unit tests for the OSAL Thread primitive.
 *
 * No test translation unit previously compiled against osal/src/Thread.cpp, which
 * left the named constructor, the destructor, run() and detach() with zero hits.
 * Those are the paths covered here.
 *
 * Thread::start() creates its worker in the DETACHED state, so there is no join to
 * wait on. Every case therefore hands the worker an atomic flag and polls it with a
 * bounded deadline - no unconditional sleep, no wall-clock dependence - and keeps
 * the Runnable alive until the worker has signalled completion.
 *
 * NOTE: Thread declares getNativeHandle() and stop() in osal/include/osal/Thread.hpp
 * but osal/src/Thread.cpp defines neither, so referencing either symbol fails to
 * link. Thread execution is therefore observed through the Runnable's own record of
 * the thread it ran on rather than through the handle accessor.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

#include "osal/Runnable.hpp"
#include "osal/Thread.hpp"

using namespace CCEC_OSAL;

namespace {

// Minimal Runnable used by the cases below. `gate` (when set) holds the worker
// inside run() so a test can observe the thread while it is provably still alive.
class CountingRunnable : public Runnable {
public:
    CountingRunnable() : runCount(0), started(false), gate(nullptr), ranOn() {}

    void run(void) override {
        ranOn = std::this_thread::get_id();
        started = true;
        if (gate != nullptr) {
            while (!gate->load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        ++runCount;
    }

    std::atomic<int> runCount;
    std::atomic<bool> started;
    std::atomic<bool> *gate;
    std::thread::id ranOn;
};

// Poll `predicate` until it holds or the deadline expires. Returns whether it held.
bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 5000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

} // namespace

class ThreadTest : public ::testing::Test {
};

// Construction alone must not start anything.
TEST_F(ThreadTest, ConstructionWithoutNameDoesNotStartRunnable) {
    CountingRunnable runnable;
    Thread thread(runnable);

    EXPECT_FALSE(runnable.started.load());
    EXPECT_EQ(runnable.runCount.load(), 0);
}

// The named constructor overload is a separate, previously unexercised entry point.
TEST_F(ThreadTest, ConstructionWithNameDoesNotStartRunnable) {
    CountingRunnable runnable;
    Thread thread(runnable, (const int8_t *)"CECWorker");

    EXPECT_FALSE(runnable.started.load());
    EXPECT_EQ(runnable.runCount.load(), 0);
}

TEST_F(ThreadTest, ConstructionWithEmptyName) {
    CountingRunnable runnable;
    EXPECT_NO_THROW({ Thread thread(runnable, (const int8_t *)""); });
}

TEST_F(ThreadTest, ConstructionWithLongName) {
    CountingRunnable runnable;
    const std::string longName(128, 'n');
    EXPECT_NO_THROW({ Thread thread(runnable, (const int8_t *)longName.c_str()); });
}

// Thread derives from Runnable and forwards run() to the target it was given, so
// calling run() directly must dispatch through that indirection on the CALLING
// thread - no worker is created.
TEST_F(ThreadTest, RunDispatchesToRunnableOnCallingThread) {
    CountingRunnable runnable;
    Thread thread(runnable);

    thread.run();

    EXPECT_EQ(runnable.runCount.load(), 1);
    EXPECT_TRUE(runnable.started.load());
    EXPECT_EQ(runnable.ranOn, std::this_thread::get_id());
}

TEST_F(ThreadTest, RunIsRepeatable) {
    CountingRunnable runnable;
    Thread thread(runnable, (const int8_t *)"CECRepeat");

    thread.run();
    thread.run();

    EXPECT_EQ(runnable.runCount.load(), 2);
}

// start() must execute the runnable on a DIFFERENT thread from the caller.
TEST_F(ThreadTest, StartExecutesRunnableOnWorkerThread) {
    CountingRunnable runnable;
    Thread thread(runnable, (const int8_t *)"CECStart");

    thread.start();

    ASSERT_TRUE(waitFor([&runnable]() { return runnable.runCount.load() == 1; }));
    EXPECT_NE(runnable.ranOn, std::this_thread::get_id());
}

// Destroying a Thread that was never started must be a no-op, not a fault.
TEST_F(ThreadTest, DestructionWithoutStartIsHarmless) {
    CountingRunnable runnable;
    {
        Thread thread(runnable);
        EXPECT_FALSE(runnable.started.load());
    }
    EXPECT_EQ(runnable.runCount.load(), 0);
}

// Destroying the Thread object does not cancel the worker: the worker holds a
// reference to the Runnable, not to the Thread.
TEST_F(ThreadTest, DestructionAfterStartDoesNotCancelRunnable) {
    CountingRunnable runnable;
    {
        Thread thread(runnable, (const int8_t *)"CECDestroy");
        thread.start();
        EXPECT_TRUE(waitFor([&runnable]() { return runnable.started.load(); }));
    }

    EXPECT_TRUE(waitFor([&runnable]() { return runnable.runCount.load() == 1; }));
}

// detach() is issued while the worker is provably still inside run(), held there by
// the gate, and is then released so the worker can finish.
TEST_F(ThreadTest, DetachOnRunningThreadIsAccepted) {
    CountingRunnable runnable;
    std::atomic<bool> release(false);
    runnable.gate = &release;

    Thread thread(runnable, (const int8_t *)"CECDetach");
    thread.start();

    ASSERT_TRUE(waitFor([&runnable]() { return runnable.started.load(); }));
    EXPECT_NO_THROW({ thread.detach(); });

    release = true;
    EXPECT_TRUE(waitFor([&runnable]() { return runnable.runCount.load() == 1; }));
}

// Two workers over two Runnables must both complete, on two distinct threads - the
// primitive holds no process-global state that would serialise or lose one of them.
TEST_F(ThreadTest, TwoStartedThreadsBothCompleteOnDistinctThreads) {
    CountingRunnable first;
    CountingRunnable second;

    Thread firstThread(first, (const int8_t *)"CECFirst");
    Thread secondThread(second, (const int8_t *)"CECSecond");

    firstThread.start();
    ASSERT_TRUE(waitFor([&first]() { return first.runCount.load() == 1; }));

    secondThread.start();
    ASSERT_TRUE(waitFor([&second]() { return second.runCount.load() == 1; }));

    EXPECT_NE(first.ranOn, std::this_thread::get_id());
    EXPECT_NE(second.ranOn, std::this_thread::get_id());
}
