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
 * L1 unit tests for the OSAL Mutex primitive.
 *
 * No test translation unit previously compiled against osal/src/Mutex.cpp, so the
 * copy constructor and the copy-assignment operator had never been executed. Those
 * two operations own every uncovered line in the unit, which is why they are the
 * focus here alongside the basic lock/unlock and native-handle contract.
 *
 * The unit is exercised against the real pthread implementation: its uncovered
 * paths are resource-duplication semantics, not concurrency behaviour, so no mock
 * is required. Every case is self-contained and mutates no shared state.
 */

#include <gtest/gtest.h>
#include <pthread.h>
#include <thread>

#include "osal/Mutex.hpp"

using namespace CCEC_OSAL;

class MutexTest : public ::testing::Test {
};

// A default-constructed mutex must own a usable native handle.
TEST_F(MutexTest, DefaultConstructionAllocatesNativeHandle) {
    Mutex mutex;
    EXPECT_NE(mutex.getNativeHandle(), nullptr);
}

TEST_F(MutexTest, LockThenUnlock) {
    Mutex mutex;
    EXPECT_NO_THROW({
        mutex.lock();
        mutex.unlock();
    });
}

// The mutex is documented as recursive: the owning thread may take it repeatedly
// provided it releases it the same number of times.
TEST_F(MutexTest, RecursiveLockRequiresMatchingUnlocks) {
    Mutex mutex;
    mutex.lock();
    mutex.lock();
    mutex.unlock();
    mutex.unlock();

    // Still usable, and now free for another thread to take.
    bool takenByOtherThread = false;
    std::thread other([&mutex, &takenByOtherThread]() {
        mutex.lock();
        takenByOtherThread = true;
        mutex.unlock();
    });
    other.join();
    EXPECT_TRUE(takenByOtherThread);
}

TEST_F(MutexTest, AutoLockReleasesOnScopeExit) {
    Mutex mutex;
    {
        AutoLock guard(mutex);
        EXPECT_EQ(&guard.mutex, &mutex);
    }

    // If AutoLock failed to release, this second acquisition from another thread
    // would block forever.
    bool reacquired = false;
    std::thread other([&mutex, &reacquired]() {
        mutex.lock();
        reacquired = true;
        mutex.unlock();
    });
    other.join();
    EXPECT_TRUE(reacquired);
}

// Copy construction duplicates the underlying attribute block: the copy must own a
// DISTINCT native handle, not an alias of the original's.
TEST_F(MutexTest, CopyConstructionAllocatesDistinctNativeHandle) {
    Mutex original;
    Mutex copy(original);

    ASSERT_NE(copy.getNativeHandle(), nullptr);
    EXPECT_NE(copy.getNativeHandle(), original.getNativeHandle());
}

TEST_F(MutexTest, CopyConstructedMutexIsIndependentlyUsable) {
    Mutex original;
    Mutex copy(original);

    EXPECT_NO_THROW({
        copy.lock();
        copy.unlock();
    });

    // The original is unaffected by activity on the copy.
    EXPECT_NO_THROW({
        original.lock();
        original.unlock();
    });
}

// Copy assignment is implemented as copy-and-swap, so the target must end up with a
// freshly allocated handle and the previously held one must be released.
TEST_F(MutexTest, CopyAssignmentReplacesNativeHandle) {
    Mutex target;
    Mutex source;

    void *handleBeforeAssignment = target.getNativeHandle();
    target = source;

    ASSERT_NE(target.getNativeHandle(), nullptr);
    EXPECT_NE(target.getNativeHandle(), handleBeforeAssignment);
    EXPECT_NE(target.getNativeHandle(), source.getNativeHandle());

    EXPECT_NO_THROW({
        target.lock();
        target.unlock();
    });
}

TEST_F(MutexTest, SelfAssignmentLeavesMutexUsable) {
    Mutex mutex;
    Mutex &alias = mutex;

    EXPECT_NO_THROW({ mutex = alias; });

    ASSERT_NE(mutex.getNativeHandle(), nullptr);
    EXPECT_NO_THROW({
        mutex.lock();
        mutex.unlock();
    });
}

// Chained assignment relies on operator= returning *this.
TEST_F(MutexTest, CopyAssignmentReturnsSelfReference) {
    Mutex first;
    Mutex second;
    Mutex source;

    first = second = source;

    EXPECT_NE(first.getNativeHandle(), nullptr);
    EXPECT_NE(second.getNativeHandle(), nullptr);
    EXPECT_NE(first.getNativeHandle(), second.getNativeHandle());
}

// Destroying a copy must not invalidate the mutex it was copied from.
TEST_F(MutexTest, DestroyingCopyLeavesOriginalUsable) {
    Mutex original;
    {
        Mutex copy(original);
        copy.lock();
        copy.unlock();
    }

    EXPECT_NE(original.getNativeHandle(), nullptr);
    EXPECT_NO_THROW({
        original.lock();
        original.unlock();
    });
}
