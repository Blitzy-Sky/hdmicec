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

// L1 unit tests for ccec/include/ccec/Exception.hpp.
//
// WHY THIS FILE EXISTS
// --------------------
// Exception.hpp is production source inside the coverage denominator (no exclusion glob
// covers ccec/include), and it measured 4/12 = 33.3% line coverage: two of its six what()
// bodies had never executed.  The header is a pure interface - every function is an inline
// what() returning a literal - so the only way to cover it is to construct each type and
// ask it for its message.  That is exactly what the existing suites never do: they assert
// that an exception of the right TYPE is thrown (EXPECT_THROW / catch by type) and never
// read the message, so the bodies stay unexecuted.
//
// The uncovered lines were 45,47 (Exception), 55,57 (CECNoAckException), 84,86
// (InvalidStateException) and 102,104 (AddressNotAvailableException).  All six types are
// exercised below so the whole header is covered rather than only the four that were short,
// because covering a subset would leave the file's figure dependent on which sibling test
// happens to catch which type.
//
// WHAT IS ASSERTED, AND WHY IT IS NOT A COVERAGE-ONLY TEST
// -------------------------------------------------------
// Each case asserts the exact message text.  These strings are part of the library's
// observable behaviour - CCEC_LOG output and every caller's diagnostic path quote them - so
// pinning them is a real regression guard: a copy-paste slip that made two exception types
// report the same text would be caught here and nowhere else.  The polymorphic cases
// additionally assert that dispatch through a base reference and through a
// std::exception reference reaches the derived body, which is the property callers rely on
// when they catch by base type.

#include <exception>
#include <string>

#include <gtest/gtest.h>

#include "ccec/Exception.hpp"

// Every type in Exception.hpp derives from Exception, which derives from std::exception.
// Catching by std::exception& and reading what() is the shape production callers use, so it
// is asserted through that reference type rather than only through the concrete type.
namespace {

// Reads what() through a std::exception reference so the call cannot be devirtualised into
// a direct call on a known concrete type: this is the dispatch path a real catch site takes.
std::string messageOf(const std::exception &e)
{
    return std::string(e.what());
}

} // namespace

class ExceptionTest : public ::testing::Test {
};

// ---------------------------------------------------------------------------------------
// Positive cases: each type reports its own message.
// ---------------------------------------------------------------------------------------

TEST_F(ExceptionTest, BaseExceptionReportsItsOwnMessage)
{
    Exception e;
    EXPECT_STREQ("Base Exception..", e.what());
    EXPECT_EQ("Base Exception..", messageOf(e));
}

TEST_F(ExceptionTest, CECNoAckExceptionReportsItsOwnMessage)
{
    CECNoAckException e;
    EXPECT_STREQ("Ack not received..", e.what());
    EXPECT_EQ("Ack not received..", messageOf(e));
}

TEST_F(ExceptionTest, OperationNotSupportedExceptionReportsItsOwnMessage)
{
    OperationNotSupportedException e;
    EXPECT_STREQ("Operation Not Supported..", e.what());
    EXPECT_EQ("Operation Not Supported..", messageOf(e));
}

TEST_F(ExceptionTest, IOExceptionReportsItsOwnMessage)
{
    IOException e;
    EXPECT_STREQ("IO Exception..", e.what());
    EXPECT_EQ("IO Exception..", messageOf(e));
}

TEST_F(ExceptionTest, InvalidStateExceptionReportsItsOwnMessage)
{
    InvalidStateException e;
    EXPECT_STREQ("Invalid State Exception..", e.what());
    EXPECT_EQ("Invalid State Exception..", messageOf(e));
}

TEST_F(ExceptionTest, InvalidParamExceptionReportsItsOwnMessage)
{
    InvalidParamException e;
    EXPECT_STREQ("Invalid Param Exception..", e.what());
    EXPECT_EQ("Invalid Param Exception..", messageOf(e));
}

TEST_F(ExceptionTest, AddressNotAvailableExceptionReportsItsOwnMessage)
{
    AddressNotAvailableException e;
    EXPECT_STREQ("Address Not Available Exception..", e.what());
    EXPECT_EQ("Address Not Available Exception..", messageOf(e));
}

// ---------------------------------------------------------------------------------------
// Corner case: the messages must be distinct.  A caller that logs what() to tell an
// unacknowledged frame from an invalid state has nothing to go on if two types share a
// string, and nothing else in the suite would notice.
// ---------------------------------------------------------------------------------------

TEST_F(ExceptionTest, EveryExceptionTypeReportsADistinctMessage)
{
    const std::string messages[] = {
        Exception().what(),
        CECNoAckException().what(),
        OperationNotSupportedException().what(),
        IOException().what(),
        InvalidStateException().what(),
        InvalidParamException().what(),
        AddressNotAvailableException().what()
    };
    const size_t count = sizeof(messages) / sizeof(messages[0]);

    for (size_t i = 0; i < count; ++i) {
        EXPECT_FALSE(messages[i].empty()) << "message " << i << " is empty";
        for (size_t j = i + 1; j < count; ++j) {
            EXPECT_NE(messages[i], messages[j])
                << "messages " << i << " and " << j << " are identical: " << messages[i];
        }
    }
}

// ---------------------------------------------------------------------------------------
// Polymorphic dispatch: caught by base reference, the DERIVED body must run.  This is the
// property every `catch (Exception &e)` site in the library depends on.
// ---------------------------------------------------------------------------------------

TEST_F(ExceptionTest, ThrownCECNoAckExceptionDispatchesToDerivedWhatThroughBaseReference)
{
    try {
        throw CECNoAckException();
    }
    catch (Exception &e) {
        EXPECT_STREQ("Ack not received..", e.what());
        return;
    }
    FAIL() << "CECNoAckException was not caught by an Exception reference";
}

TEST_F(ExceptionTest, ThrownInvalidStateExceptionDispatchesToDerivedWhatThroughBaseReference)
{
    try {
        throw InvalidStateException();
    }
    catch (Exception &e) {
        EXPECT_STREQ("Invalid State Exception..", e.what());
        return;
    }
    FAIL() << "InvalidStateException was not caught by an Exception reference";
}

TEST_F(ExceptionTest, ThrownInvalidParamExceptionDispatchesToDerivedWhatThroughBaseReference)
{
    try {
        throw InvalidParamException();
    }
    catch (Exception &e) {
        EXPECT_STREQ("Invalid Param Exception..", e.what());
        return;
    }
    FAIL() << "InvalidParamException was not caught by an Exception reference";
}

TEST_F(ExceptionTest, ThrownAddressNotAvailableExceptionDispatchesToDerivedWhatThroughStdExceptionReference)
{
    try {
        throw AddressNotAvailableException();
    }
    catch (const std::exception &e) {
        EXPECT_STREQ("Address Not Available Exception..", e.what());
        return;
    }
    FAIL() << "AddressNotAvailableException was not caught by a std::exception reference";
}

TEST_F(ExceptionTest, ThrownOperationNotSupportedExceptionDispatchesToDerivedWhatThroughStdExceptionReference)
{
    try {
        throw OperationNotSupportedException();
    }
    catch (const std::exception &e) {
        EXPECT_STREQ("Operation Not Supported..", e.what());
        return;
    }
    FAIL() << "OperationNotSupportedException was not caught by a std::exception reference";
}

TEST_F(ExceptionTest, ThrownIOExceptionDispatchesToDerivedWhatThroughStdExceptionReference)
{
    try {
        throw IOException();
    }
    catch (const std::exception &e) {
        EXPECT_STREQ("IO Exception..", e.what());
        return;
    }
    FAIL() << "IOException was not caught by a std::exception reference";
}

// A derived type must NOT be caught by a sibling: the hierarchy is a single chain from
// Exception, so `catch (CECNoAckException&)` must not swallow an InvalidStateException.
// Callers in DriverImpl and LibCCEC rely on exactly this to distinguish an unacknowledged
// frame from a misused API.
TEST_F(ExceptionTest, SiblingExceptionTypesDoNotCatchEachOther)
{
    bool caughtBySibling = false;
    bool caughtByBase = false;

    try {
        throw InvalidStateException();
    }
    catch (CECNoAckException &) {
        caughtBySibling = true;
    }
    catch (Exception &e) {
        caughtByBase = true;
        EXPECT_STREQ("Invalid State Exception..", e.what());
    }

    EXPECT_FALSE(caughtBySibling);
    EXPECT_TRUE(caughtByBase);
}

// The Throw_e tag type in the same header is the disambiguator used by the throwing
// overloads across the library (Connection::poll, DriverImpl::write and friends take a
// `const Throw_e &` to select the throwing form).  It is an empty struct, so the only
// meaningful assertions are that it is default-constructible and copyable, which is what
// those signatures require of it.
TEST_F(ExceptionTest, ThrowTagTypeIsDefaultConstructibleAndCopyable)
{
    Throw_e tag;
    Throw_e copy(tag);
    Throw_e assigned = copy;
    (void)assigned;
    EXPECT_EQ(sizeof(Throw_e), sizeof(assigned));
}
