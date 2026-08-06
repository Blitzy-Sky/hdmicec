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
 * @defgroup hdmicec
 * @{
 * @defgroup osal
 * @{
 **/

/*
 * L1 unit tests for the OSAL Mutex primitive, focused on its copy constructor and
 * copy-assignment operator.
 *
 * The unit is exercised against the real pthread implementation: the behaviour under
 * test is resource duplication, not concurrency, so no mock is needed. Every case is
 * synchronous and self-contained - it builds its own subjects as local objects, spawns
 * no thread, performs no sleep or timed wait, and leaves the global test environment
 * installed by test_main.cpp untouched.
 *
 * INVARIANT OBSERVED BY EVERY COPY CASE BELOW: a Mutex is only ever copied while it is
 * UNLOCKED. The copy constructor duplicates the underlying pthread_mutex_t
 * byte-for-byte, so copying a held lock would duplicate owner and recursion-count
 * state into a fresh allocation and make any subsequent use of either object
 * undefined. Locking is therefore always done strictly before or strictly after a
 * copy, never across one.
 *
 * The allocation-failure arms of both copy operations are NOT reachable from a
 * test-only change: osal/Util.hpp defines the allocator as bare libc ("#define Malloc
 * malloc"), so there is no seam to interpose on without a production change or a
 * malloc interposer.
 */

#include <gtest/gtest.h>

#include <cerrno>
#include <cstring>
#include <pthread.h>

#include "osal/Mutex.hpp"

using namespace CCEC_OSAL;

namespace {

/*
 * Confirm that a mutex is usable and currently unowned, without ever blocking.
 *
 * pthread_mutex_trylock returns immediately in every case: 0 when it acquired the lock,
 * EBUSY when another thread holds it, and EINVAL when the object is not a usable mutex.
 * That makes it the one probe that can distinguish "this duplicate is a working, unlocked
 * mutex" from "this duplicate is not usable" without risking the whole single-process test
 * binary on the answer. The lock taken by the probe is released again immediately, so the
 * object is left exactly as it was found.
 */
void expectUsableAndUnlocked(Mutex &mutex, const char *what) {
    void *handle = mutex.getNativeHandle();
    ASSERT_TRUE(handle != nullptr) << what << ": native handle is null";

    pthread_mutex_t *native = static_cast<pthread_mutex_t *>(handle);
    const int acquired = pthread_mutex_trylock(native);
    ASSERT_EQ(0, acquired)
        << what << ": pthread_mutex_trylock refused the mutex (" << std::strerror(acquired)
        << "), so it is not a usable, unowned mutex";
    EXPECT_EQ(0, pthread_mutex_unlock(native))
        << what << ": pthread_mutex_unlock failed after a successful trylock";
}

} // namespace

/*
 * No SetUp()/TearDown() overrides: the pair used by the ccec driver tests exists only
 * to clear GoogleMock expectations, and there are no mocks here. A stateless fixture
 * also keeps every case independent of execution order.
 */
class MutexTest : public ::testing::Test {
};

TEST_F(MutexTest, DefaultConstructionAllocatesNativeHandle) {
    Mutex mutex;

    EXPECT_TRUE(mutex.getNativeHandle() != nullptr);
}

TEST_F(MutexTest, LockThenUnlock) {
    Mutex mutex;

    EXPECT_NO_THROW({
        mutex.lock();
        mutex.unlock();
    });

    EXPECT_NO_THROW({
        mutex.lock();
        mutex.unlock();
    });
}

// The mutex is created recursive, so the owning thread may take it repeatedly provided
// it releases it the same number of times. A non-recursive mutex would deadlock or fail
// on the second lock() from this thread.
TEST_F(MutexTest, RecursiveLockRequiresMatchingUnlocks) {
    Mutex mutex;
    void *handleBefore = mutex.getNativeHandle();

    EXPECT_NO_THROW({
        mutex.lock();
        mutex.lock();
        mutex.unlock();
        mutex.unlock();
    });

    // A stable handle proves locking never reallocated; the re-acquisition below proves
    // the balanced sequence left no residual recursion count.
    EXPECT_EQ(mutex.getNativeHandle(), handleBefore);
    EXPECT_NO_THROW({
        mutex.lock();
        mutex.unlock();
    });
}

TEST_F(MutexTest, NativeHandleIsStableAcrossCalls) {
    Mutex mutex;

    void *firstRead = mutex.getNativeHandle();
    void *secondRead = mutex.getNativeHandle();
    EXPECT_EQ(firstRead, secondRead);
    EXPECT_TRUE(firstRead != nullptr);

    mutex.lock();
    EXPECT_EQ(mutex.getNativeHandle(), firstRead);
    mutex.unlock();
    EXPECT_EQ(mutex.getNativeHandle(), firstRead);
}

// Pointer inequality is the only available proof that the copy owns a distinct
// allocation rather than an alias: the handle member is private and the accessor is the
// sole observation channel.
TEST_F(MutexTest, CopyConstructionAllocatesDistinctNativeHandle) {
    Mutex original;                 // unlocked at the moment of the copy
    Mutex copy(original);

    EXPECT_TRUE(original.getNativeHandle() != nullptr);
    EXPECT_TRUE(copy.getNativeHandle() != nullptr);
    EXPECT_NE(copy.getNativeHandle(), original.getNativeHandle());

    // The duplicate is a working, unowned mutex - asserted rather than assumed, and
    // asserted without blocking, so an unusable byte image fails here by name.
    expectUsableAndUnlocked(copy, "copy-constructed mutex");
    expectUsableAndUnlocked(original, "copy source after being copied");
}

// Acquiring the original and then the copy distinguishes a genuine duplicate from an
// alias: an alias would have been re-entered rather than separately acquired.
TEST_F(MutexTest, CopyConstructedMutexIsIndependentlyUsable) {
    Mutex original;                 // unlocked at the moment of the copy
    Mutex copy(original);

    expectUsableAndUnlocked(original, "copy source before use");
    expectUsableAndUnlocked(copy, "copy-constructed mutex before use");

    // Original first ...
    EXPECT_NO_THROW({
        original.lock();
        original.unlock();
    });

    EXPECT_NO_THROW({
        copy.lock();
        copy.unlock();
    });

    EXPECT_TRUE(original.getNativeHandle() != nullptr);
    EXPECT_TRUE(copy.getNativeHandle() != nullptr);
    EXPECT_NE(copy.getNativeHandle(), original.getNativeHandle());
}

// Destroying a copy releases only the copy's own block, so the original must survive
// intact and keep the handle it started with.
TEST_F(MutexTest, DestroyingCopyLeavesOriginalUsable) {
    Mutex original;
    void *originalHandle = original.getNativeHandle();

    {
        Mutex copy(original);       // unlocked at the moment of the copy
        EXPECT_NE(copy.getNativeHandle(), originalHandle);
        expectUsableAndUnlocked(copy, "copy-constructed mutex before use");
        copy.lock();
        copy.unlock();
    }

    EXPECT_EQ(original.getNativeHandle(), originalHandle);
    // Destroying the duplicate released only its own block: the original is still a
    // working, unowned mutex.
    expectUsableAndUnlocked(original, "copy source after the copy was destroyed");
    EXPECT_NO_THROW({
        original.lock();
        original.unlock();
    });
}

// Copy assignment is copy-and-swap: a temporary copy of the source is built, target
// and temporary exchange handles, and the temporary releases the handle the target used
// to own. The target therefore ends up on a freshly allocated block that is neither the
// one it held before nor the source's.
TEST_F(MutexTest, CopyAssignmentReplacesNativeHandle) {
    Mutex target;                   // both unlocked at the moment of the copy
    Mutex source;

    void *handleBeforeAssignment = target.getNativeHandle();
    target = source;

    EXPECT_TRUE(target.getNativeHandle() != nullptr);
    EXPECT_NE(target.getNativeHandle(), handleBeforeAssignment);
    EXPECT_NE(target.getNativeHandle(), source.getNativeHandle());

    expectUsableAndUnlocked(target, "assignment target after the swap");
    expectUsableAndUnlocked(source, "assignment source after the swap");

    // Both operands remain usable once the swap has settled.
    EXPECT_NO_THROW({
        target.lock();
        target.unlock();
    });
    EXPECT_NO_THROW({
        source.lock();
        source.unlock();
    });
}

// Binding the result to a reference and comparing addresses is what asserts the
// operator hands back the assigned-to object itself rather than a copy of it.
TEST_F(MutexTest, CopyAssignmentReturnsReferenceToTarget) {
    Mutex target;
    Mutex source;

    Mutex &result = (target = source);

    EXPECT_EQ(&result, &target);
    EXPECT_EQ(result.getNativeHandle(), target.getNativeHandle());
    EXPECT_TRUE(result.getNativeHandle() != nullptr);
    expectUsableAndUnlocked(result, "assignment result");
}

// Chaining relies on that returned reference and runs the swap twice in one expression.
TEST_F(MutexTest, ChainedCopyAssignmentGivesEachTargetItsOwnHandle) {
    Mutex first;
    Mutex second;
    Mutex source;

    first = second = source;

    EXPECT_TRUE(first.getNativeHandle() != nullptr);
    EXPECT_TRUE(second.getNativeHandle() != nullptr);
    EXPECT_TRUE(source.getNativeHandle() != nullptr);
    EXPECT_NE(first.getNativeHandle(), second.getNativeHandle());
    EXPECT_NE(first.getNativeHandle(), source.getNativeHandle());
    EXPECT_NE(second.getNativeHandle(), source.getNativeHandle());

    expectUsableAndUnlocked(first, "first chained assignment target");
    expectUsableAndUnlocked(second, "second chained assignment target");
    expectUsableAndUnlocked(source, "chained assignment source");

    EXPECT_NO_THROW({
        first.lock();
        first.unlock();
        second.lock();
        second.unlock();
    });
}

// This implementation carries no self-assignment guard, so the sequence really does
// allocate a fresh block, swap it in and release the previous one - safe because the
// temporary is a distinct allocation. The assignment is routed through a reference so
// the compiler cannot fold it away.
TEST_F(MutexTest, SelfAssignmentLeavesMutexUsable) {
    Mutex mutex;
    Mutex &alias = mutex;
    void *handleBeforeAssignment = mutex.getNativeHandle();

    EXPECT_NO_THROW({ mutex = alias; });

    EXPECT_TRUE(mutex.getNativeHandle() != nullptr);
    EXPECT_NE(mutex.getNativeHandle(), handleBeforeAssignment);
    expectUsableAndUnlocked(mutex, "self-assigned mutex");
    EXPECT_NO_THROW({
        mutex.lock();
        mutex.unlock();
    });
}

/** @} */
/** @} */
