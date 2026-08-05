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
 * L1 unit tests for the OSAL Mutex primitive.
 *
 * TRACEABILITY
 *   Gap register anchor : #gap-mw-mutex  (COVERAGE_GAPS.md, section 6.2 rank 35, priority P2)
 *   Symbols under test  : CCEC_OSAL::Mutex::Mutex(const Mutex &)
 *                         CCEC_OSAL::Mutex::operator = (const Mutex &)
 *   The register anchors this gap on the lock() entry point, but the measured
 *   zero-hit surface is the pair of copy operations named above; the anchor and
 *   the symbol names are the stable keys, so no source line number is cited here
 *   (this pass shifts line numbers).
 *
 * WHY THE GAP EXISTED
 *   No dedicated own-suite test previously targeted the OSAL Mutex. Existing
 *   middleware tests exercised ordinary construction, locking and handle access
 *   indirectly, but they never executed the copy constructor or copy-assignment
 *   operator. This translation unit was absent from run_L1Tests_SOURCES, which is
 *   why those two operations are the focus here alongside the lock/unlock and
 *   native-handle contract that the rest of the middleware depends on.
 *
 * WHY THERE IS NO MOCK
 *   The unit is exercised against the real pthread implementation. Its uncovered
 *   paths are resource-duplication semantics, not concurrency behaviour, so no
 *   mock or driver stub is required or used here. Every case is synchronous,
 *   deterministic and self-contained: it builds its own subjects as local
 *   objects, spawns no thread, performs no sleep or timed wait, and mutates no
 *   process-global or fixture-shared state. The suite-wide test environment
 *   installed by test_main.cpp is left completely untouched.
 *
 * UNCOVERABLE PATH, ENUMERATED RATHER THAN FORCED
 *   The allocation-failure behaviour of both copy operations cannot be exercised
 *   from a test-only change. osal/Util.hpp defines the allocator as bare libc --
 *   "#define Malloc malloc" and "#define Free free" -- so there is no seam a test
 *   could interpose on. Reaching that behaviour would need either a production change
 *   or a malloc interposer / LD_PRELOAD shim / linker wrap, and such a harness
 *   construct is not required by any documented gap. The condition is therefore
 *   reported for the traceability register instead of being manufactured here.
 *
 * NAMING
 *   Test names are CamelCase, matching the sibling osal suite. Other repositories
 *   in this workspace use underscore-bearing test names, which upstream
 *   GoogleTest guidance discourages; the local convention wins and the tension is
 *   recorded rather than resolved by renaming anything.
 *
 * INVARIANT OBSERVED BY EVERY COPY CASE BELOW
 *   A Mutex is only ever copied while it is UNLOCKED. The copy constructor
 *   duplicates the underlying pthread_mutex_t byte-for-byte, so copying a held
 *   lock would duplicate owner and recursion-count state into a fresh allocation
 *   and make any subsequent use of either object undefined. Locking is therefore
 *   always done strictly before or strictly after a copy, never across one.
 */

#include <gtest/gtest.h>

#include "osal/Mutex.hpp"

using namespace CCEC_OSAL;

/*
 * A plain fixture with no SetUp()/TearDown() overrides is deliberate. The
 * override pair used by the ccec driver tests exists purely to clear GoogleMock
 * expectations, and there are no mocks in this translation unit. Keeping the
 * fixture stateless also keeps every case independent of execution order: each
 * one constructs the subjects it needs and destroys them on the way out.
 */
class MutexTest : public ::testing::Test {
};

/* ------------------------------------------------------------------------- */
/* Positive cases: the construction, locking and handle-accessor contract     */
/* ------------------------------------------------------------------------- */

// A default-constructed mutex must own a usable native handle.
TEST_F(MutexTest, DefaultConstructionAllocatesNativeHandle) {
    Mutex mutex;

    EXPECT_TRUE(mutex.getNativeHandle() != nullptr);
}

// The basic acquire/release contract, and the object remains usable afterwards.
TEST_F(MutexTest, LockThenUnlock) {
    Mutex mutex;

    EXPECT_NO_THROW({
        mutex.lock();
        mutex.unlock();
    });

    // A released lock must be immediately re-acquirable by the same thread.
    EXPECT_NO_THROW({
        mutex.lock();
        mutex.unlock();
    });
}

// The mutex is created recursive, and the header documents that the owning
// thread may take it repeatedly provided it releases it the same number of
// times. Nesting the acquisition proves that contract: a non-recursive mutex
// would not survive the second lock() from the same thread.
TEST_F(MutexTest, RecursiveLockRequiresMatchingUnlocks) {
    Mutex mutex;
    void *handleBefore = mutex.getNativeHandle();

    EXPECT_NO_THROW({
        mutex.lock();
        mutex.lock();
        mutex.unlock();
        mutex.unlock();
    });

    // Locking never reallocates, and the balanced sequence left the mutex fully
    // released rather than still held at a positive recursion count.
    EXPECT_EQ(mutex.getNativeHandle(), handleBefore);
    EXPECT_NO_THROW({
        mutex.lock();
        mutex.unlock();
    });
}

// The accessor is a pure getter: it must report the same handle every time and
// must not be perturbed by locking activity.
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

/* ------------------------------------------------------------------------- */
/* Gap closure: copy construction                                            */
/* ------------------------------------------------------------------------- */

// Copy construction allocates its own block and duplicates the source into it,
// so the copy must own a DISTINCT native handle rather than an alias of the
// original's. Pointer inequality is the only available proof, because the handle
// member is private and the accessor is the sole observation channel.
TEST_F(MutexTest, CopyConstructionAllocatesDistinctNativeHandle) {
    Mutex original;                 // unlocked at the moment of the copy
    Mutex copy(original);

    EXPECT_TRUE(original.getNativeHandle() != nullptr);
    EXPECT_TRUE(copy.getNativeHandle() != nullptr);
    EXPECT_NE(copy.getNativeHandle(), original.getNativeHandle());
}

// Independence of the copy once the original has been used: exercise the
// original first, then the copy, and confirm the two are still backed by
// different resources. This is what distinguishes a genuine duplicate from an
// alias -- an alias would have been re-entered rather than separately acquired.
TEST_F(MutexTest, CopyConstructedMutexIsIndependentlyUsable) {
    Mutex original;                 // unlocked at the moment of the copy
    Mutex copy(original);

    // Original first ...
    EXPECT_NO_THROW({
        original.lock();
        original.unlock();
    });

    // ... then the copy, each acquired and released in its own right.
    EXPECT_NO_THROW({
        copy.lock();
        copy.unlock();
    });

    EXPECT_TRUE(original.getNativeHandle() != nullptr);
    EXPECT_TRUE(copy.getNativeHandle() != nullptr);
    EXPECT_NE(copy.getNativeHandle(), original.getNativeHandle());
}

// Destroying a copy releases only the copy's own block, so the mutex it was
// copied from must survive intact and keep the handle it started with.
TEST_F(MutexTest, DestroyingCopyLeavesOriginalUsable) {
    Mutex original;
    void *originalHandle = original.getNativeHandle();

    {
        Mutex copy(original);       // unlocked at the moment of the copy
        EXPECT_NE(copy.getNativeHandle(), originalHandle);
        copy.lock();
        copy.unlock();
    }                               // copy destroyed here

    EXPECT_EQ(original.getNativeHandle(), originalHandle);
    EXPECT_NO_THROW({
        original.lock();
        original.unlock();
    });
}

/* ------------------------------------------------------------------------- */
/* Gap closure: copy assignment                                              */
/* ------------------------------------------------------------------------- */

// Copy assignment is implemented as copy-and-swap: a temporary copy of the
// source is built, the target and the temporary exchange handles, and the
// temporary then releases the handle the target used to own. The target must
// therefore end up on a freshly allocated block that is neither the one it held
// before nor the source's.
TEST_F(MutexTest, CopyAssignmentReplacesNativeHandle) {
    Mutex target;                   // both unlocked at the moment of the copy
    Mutex source;

    void *handleBeforeAssignment = target.getNativeHandle();
    target = source;

    EXPECT_TRUE(target.getNativeHandle() != nullptr);
    EXPECT_NE(target.getNativeHandle(), handleBeforeAssignment);
    EXPECT_NE(target.getNativeHandle(), source.getNativeHandle());

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

// The operator must hand back the assigned-to object itself, not a copy of it.
// Binding the result to a reference and comparing addresses asserts that
// directly.
TEST_F(MutexTest, CopyAssignmentReturnsReferenceToTarget) {
    Mutex target;
    Mutex source;

    Mutex &result = (target = source);

    EXPECT_EQ(&result, &target);
    EXPECT_EQ(result.getNativeHandle(), target.getNativeHandle());
    EXPECT_TRUE(result.getNativeHandle() != nullptr);
}

// Chaining relies on that returned reference and runs the swap twice in one
// expression. Every object involved must come out with its own live block.
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

    EXPECT_NO_THROW({
        first.lock();
        first.unlock();
        second.lock();
        second.unlock();
    });
}

// Self-assignment is the classic copy-and-swap corner case. This implementation
// carries no self-assignment guard, so the sequence really does allocate a fresh
// block, swap it in and release the previous one. That is safe because the
// temporary is a distinct allocation, and the mutex must remain fully usable.
// The assignment is routed through a reference so the self-assignment is not
// folded away by the compiler.
TEST_F(MutexTest, SelfAssignmentLeavesMutexUsable) {
    Mutex mutex;
    Mutex &alias = mutex;
    void *handleBeforeAssignment = mutex.getNativeHandle();

    EXPECT_NO_THROW({ mutex = alias; });

    EXPECT_TRUE(mutex.getNativeHandle() != nullptr);
    EXPECT_NE(mutex.getNativeHandle(), handleBeforeAssignment);
    EXPECT_NO_THROW({
        mutex.lock();
        mutex.unlock();
    });
}

/** @} */
/** @} */
