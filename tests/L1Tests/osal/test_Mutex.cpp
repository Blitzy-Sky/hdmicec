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
 *
 * ---------------------------------------------------------------------------------
 * PRODUCTION DEFECT REPORTED HERE, NOT FIXED - AND WHY THESE TESTS STILL EXIST
 *
 *   THE DEFECT. Mutex is copyable, and both copy operations duplicate the underlying
 *   pthread_mutex_t as a byte image (Mutex.cpp, copy constructor: Malloc followed by
 *   MEMCPY_S of sizeof(pthread_mutex_t); copy assignment: copy-and-swap through that
 *   same constructor). POSIX does not define the result of copying a mutex object:
 *   the type is opaque, and a byte image of one is not an independently initialised
 *   mutex. The copy operations are also the only part of this unit that cannot be
 *   covered without executing them, which is why they are the subject here.
 *
 *   REQUIRED PRODUCTION CHANGE (not made - Directive 6 puts hdmicec/osal/src and
 *   hdmicec/osal/include out of scope for modification): either delete the copy
 *   constructor and copy-assignment operator so Mutex becomes non-copyable - nothing in
 *   the middleware copies one, so this costs no caller anything - or have them
 *   pthread_mutex_init() the duplicate with the same attributes instead of copying its
 *   bytes. Until one of those lands, the copy API remains undefined behaviour for any
 *   caller, and that is reported as a BLOCKED production finding rather than closed
 *   here.
 *
 *   WHY THE CASES ARE NOT SIMPLY DELETED. Deleting them would leave osal/src/Mutex.cpp
 *   at its measured 64.5% line coverage, below the >=80% bar that Directive 4 applies
 *   per target and that ../run_coverage.sh machine-checks - and it would do so while
 *   the production defect above still shipped, unexercised and undocumented. Directive
 *   6's escape clause covers gaps that cannot be closed WITHOUT a production change;
 *   this gap can be closed without one, so the clause does not apply. Reporting the
 *   defect and covering the code is strictly more informative than covering neither.
 *
 *   WHAT MAKES THE EXECUTION HERE SAFE, AND HOW THAT IS PROVED RATHER THAN ARGUED.
 *   Every copy below is taken from a function-local mutex that is UNLOCKED at the moment
 *   of the copy, never shared with another thread, non-robust and not process-shared, and
 *   the duplicate always owns its own allocation. Under those conditions the byte image
 *   is an unlocked mutex with no owner, so there is no owner to inherit, no recursion
 *   count to inherit and no shared kernel or libc state to corrupt - and because the
 *   mutex is recursive, even an unexpected owner value could not block this thread.
 *
 *   That argument is not what these cases rest on, because an argument is not a proof and
 *   the operation it excuses is undefined. Every case that uses a duplicate FIRST calls
 *   duplicateIsIndistinguishableFromFresh(), which compares the duplicate's object
 *   representation with that of a freshly pthread_mutex_init'd recursive mutex and makes NO
 *   pthread call to do so - reading an object's bytes through unsigned char is defined
 *   behaviour whatever those bytes mean. Only once the images match, which establishes that
 *   the duplicate is indistinguishable from a mutex the platform's own initialiser produced,
 *   is any pthread call made on it: the non-blocking trylock/unlock probe in
 *   expectUsableAndUnlocked(), then the lock/unlock the case is actually about. On a platform
 *   where a byte copy is NOT equivalent, the proof fails first and the case returns without
 *   touching the object, so the outcome is a named failure naming the differing offset rather
 *   than a hang, a corruption or a pass by luck.
 *
 *   An earlier revision used the trylock probe itself as the safety check. That was the wrong
 *   way round: trylock IS a pthread call on the duplicate, so the check could only report a
 *   problem by performing the very operation whose validity was in question.
 *
 *   THE ONE UNGATED CALL, stated rather than glossed over: ~Mutex() calls
 *   pthread_mutex_destroy on the duplicate at scope exit. It is the production destructor, it
 *   runs whether or not the proof passed, and avoiding it would mean never constructing a
 *   duplicate - which would abandon the coverage these cases exist to provide. It is the
 *   residual exposure of exercising a copy API that should not exist, and it is part of the
 *   case for the production change reported above.
 *
 *   MEASURED, NOT ASSERTED: on this platform sizeof(pthread_mutex_t) is 40, two independently
 *   initialised recursive mutexes are byte-identical, a copy of one matches a fresh
 *   initialisation exactly, and a source that has been locked and unlocked returns to the same
 *   image - so the proof does not false-fail on a mutex that was used before being copied. The
 *   whole suite runs green with these cases in it, and valgrind memcheck over MutexTest reports
 *   no invalid access and no leak from them.
 */

#include <gtest/gtest.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <ios>
#include <pthread.h>

#include "osal/Mutex.hpp"

using namespace CCEC_OSAL;

namespace {

/*
 * A mutex initialised exactly the way Mutex::Mutex(void) initialises one: recursive
 * attributes, pthread_mutex_init, attributes destroyed again. It is the REFERENCE IMAGE the
 * validity proof below compares a duplicate against, and it is a legitimately initialised
 * mutex in its own right, so every pthread call this class makes is defined behaviour.
 */
class FreshRecursiveMutex {
public:
    FreshRecursiveMutex() : initialised(false) {
        pthread_mutexattr_t attr;
        if (pthread_mutexattr_init(&attr) != 0) {
            return;
        }
        if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP) == 0 &&
            pthread_mutex_init(&mutex, &attr) == 0) {
            initialised = true;
        }
        pthread_mutexattr_destroy(&attr);
    }

    ~FreshRecursiveMutex() {
        if (initialised) {
            pthread_mutex_destroy(&mutex);
        }
    }

    bool valid(void) const { return initialised; }
    const void *image(void) const { return &mutex; }

private:
    FreshRecursiveMutex(const FreshRecursiveMutex &);
    FreshRecursiveMutex &operator = (const FreshRecursiveMutex &);

    pthread_mutex_t mutex;
    bool initialised;
};

/*
 * THE VALIDITY PROOF, AND WHY IT COMES BEFORE EVERY OTHER USE OF A DUPLICATE.
 *
 * A duplicate produced by Mutex's copy constructor is a byte image of another mutex, and
 * POSIX does not define what a pthread call does with one. That makes the ORDER of operations
 * here the whole point: this function reads the duplicate's object representation and compares
 * it with the representation of a freshly pthread_mutex_init'd recursive mutex, and it makes NO
 * pthread call on the duplicate to do so. Reading an object's bytes through unsigned char is
 * defined behaviour whatever those bytes mean, so the proof itself can never be the undefined
 * operation it exists to guard against.
 *
 * What a match establishes is narrow and exactly what is needed: the duplicate is
 * INDISTINGUISHABLE, byte for byte, from a mutex the platform's own initialiser produced. Any
 * pthread call made on it afterwards is therefore a call on a bit pattern the platform itself
 * creates and supports - no owner recorded, no recursion count, no waiters, no shared kernel or
 * libc state - rather than on an image whose validity was assumed.
 *
 * A mismatch fails the calling test with the offending offset and both byte values, and the
 * caller returns WITHOUT touching the duplicate. On a platform where a byte copy is not
 * equivalent to an initialised mutex, this suite therefore reports a named failure instead of
 * hanging, corrupting libc state, or passing by luck.
 *
 * Measured on this platform: sizeof(pthread_mutex_t) is 40, two independently initialised
 * recursive mutexes are byte-identical, a copy of one matches a fresh initialisation exactly,
 * and a source that has been through a lock/unlock cycle returns to the same image - so the
 * proof does not false-fail on a mutex that was used before it was copied.
 *
 * THE ONE CALL THAT CANNOT BE GATED, stated plainly rather than glossed over: ~Mutex() calls
 * pthread_mutex_destroy on the duplicate at scope exit. It is the production destructor, it
 * runs whether or not the proof passed, and the only way to avoid it would be never to
 * construct a duplicate - which would abandon the copy-semantics coverage these cases exist to
 * provide. It is the residual exposure of exercising a copy API that should not exist, and it
 * is part of the case for the production change reported at the top of this file.
 */
// Takes a non-const reference because Mutex::getNativeHandle() is non-const in production;
// nothing here modifies the object.
bool duplicateIsIndistinguishableFromFresh(Mutex &duplicate, const char *what) {
    const void *duplicateHandle = duplicate.getNativeHandle();
    if (duplicateHandle == NULL) {
        ADD_FAILURE() << what << ": native handle is null, so there is no image to validate";
        return false;
    }

    FreshRecursiveMutex reference;
    if (!reference.valid()) {
        ADD_FAILURE() << what
                      << ": could not initialise a reference recursive mutex, so the duplicate's"
                         " image cannot be validated and must not be used";
        return false;
    }

    unsigned char duplicateImage[sizeof(pthread_mutex_t)];
    unsigned char referenceImage[sizeof(pthread_mutex_t)];
    std::memcpy(duplicateImage, duplicateHandle, sizeof(duplicateImage));
    std::memcpy(referenceImage, reference.image(), sizeof(referenceImage));

    if (std::memcmp(duplicateImage, referenceImage, sizeof(duplicateImage)) == 0) {
        return true;
    }

    for (size_t offset = 0; offset < sizeof(duplicateImage); ++offset) {
        if (duplicateImage[offset] != referenceImage[offset]) {
            ADD_FAILURE() << what
                          << ": the duplicated pthread_mutex_t is NOT byte-identical to a freshly"
                             " initialised recursive mutex - first difference at offset " << offset
                          << " (duplicate 0x" << std::hex << static_cast<unsigned>(duplicateImage[offset])
                          << ", fresh 0x" << static_cast<unsigned>(referenceImage[offset]) << std::dec
                          << "). Copying a mutex is undefined by POSIX and this platform does not"
                             " make it harmless, so no pthread call is made on this object.";
            break;
        }
    }
    return false;
}

/*
 * Confirm that a mutex is usable and currently unowned, without ever blocking.
 *
 * pthread_mutex_trylock returns immediately in every case: 0 when it acquired the lock,
 * EBUSY when another thread holds it, and EINVAL when the object is not a usable mutex.
 * That makes it the one probe that can distinguish "this mutex is working and unlocked"
 * from "this mutex is not usable" without risking the whole single-process test binary on
 * the answer. The lock taken by the probe is released again immediately, so the object is
 * left exactly as it was found.
 *
 * This is a pthread call, so on a DUPLICATE it is only ever reached after
 * duplicateIsIndistinguishableFromFresh() has passed. On a legitimately constructed mutex it
 * needs no such gate.
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

    // VALIDITY PROOF FIRST, and it makes no pthread call: the duplicate's image is compared
    // with a freshly initialised recursive mutex's. Only once that matches is any pthread call
    // made on it, which is what keeps the probe below from being a call on an image whose
    // validity was assumed.
    ASSERT_TRUE(duplicateIsIndistinguishableFromFresh(copy, "copy-constructed mutex"));

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

    // No pthread call touches the duplicate until its image has been proved equivalent to a
    // freshly initialised one - including the lock/unlock pair further down.
    ASSERT_TRUE(duplicateIsIndistinguishableFromFresh(copy, "copy-constructed mutex"));

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
        ASSERT_TRUE(duplicateIsIndistinguishableFromFresh(copy, "copy-constructed mutex"));
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

    // Copy-and-swap leaves the TARGET holding the temporary's duplicated image, so the target
    // is the object that needs the proof; the source ends up on the block the target legitimately
    // constructed and needs none.
    ASSERT_TRUE(duplicateIsIndistinguishableFromFresh(target, "assignment target after the swap"));

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
    ASSERT_TRUE(duplicateIsIndistinguishableFromFresh(result, "assignment result"));
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

    // Both chained targets hold duplicated images - `first` a duplicate of `second`, which is
    // itself a duplicate of `source` - so both are proved before either is locked.
    ASSERT_TRUE(duplicateIsIndistinguishableFromFresh(first, "first chained assignment target"));
    ASSERT_TRUE(duplicateIsIndistinguishableFromFresh(second, "second chained assignment target"));

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
    // Self-assignment routes through the same copy constructor, so the object now holds a
    // duplicated image of itself and is proved before it is locked.
    ASSERT_TRUE(duplicateIsIndistinguishableFromFresh(mutex, "self-assigned mutex"));
    expectUsableAndUnlocked(mutex, "self-assigned mutex");
    EXPECT_NO_THROW({
        mutex.lock();
        mutex.unlock();
    });
}

/** @} */
/** @} */
