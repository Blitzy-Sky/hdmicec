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

// L1 unit tests for the Operand BASE CLASS in ccec/include/ccec/Operand.hpp.
//
// WHY THIS FILE EXISTS
// --------------------
// Operand.hpp is production source inside the coverage denominator (no exclusion glob covers
// ccec/include) and it measured 0/6 = 0.0% line coverage - the worst figure in the
// middleware.  The six lines are the bodies of the three NON-pure virtuals the base supplies
// as defaults:
//
//     toString() -> "Not Implemented"      (lines 50-51)
//     name()     -> "Operand"              (lines 53-54)
//     validate() -> true                   (lines 56-57)
//
// Every operand the library ships (everything in Operands.hpp, all of it derived from
// CECBytes) overrides at least toString() and name(), and CECBytes overrides validate() too,
// so no production type ever reaches the base bodies.  test_Operands.cpp therefore covers
// Operands.hpp thoroughly while leaving Operand.hpp at zero: the defaults are only reachable
// from a type that deliberately declines to override them.
//
// This file supplies exactly that type.  MinimalOperand implements ONLY the pure virtual
// serialize(CECFrame&) - which it must, or it could not be instantiated - and inherits the
// three defaults.  That is not an artificial construct invented for coverage: it is the
// contract the base class publishes to anyone adding a new operand, and these tests are what
// pins that contract.  A future change that made toString() pure virtual, or that changed
// the default name from "Operand", would break a real extension point and is caught here.
//
// WHAT IS ASSERTED
// ----------------
// The three default return values, that a partial override leaves the remaining defaults in
// place, that an override is dispatched through a base reference, and that the base's
// non-virtual serialize(void) convenience overload forwards to the derived
// serialize(CECFrame&) and returns the frame by value.  The last one matters because it is
// the overload MessageEncoder-style callers use when they have no frame to append to.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ccec/CECFrame.hpp"
#include "ccec/Operand.hpp"

namespace {

// Overrides ONLY the pure virtual.  toString(), name() and validate() are inherited, which
// is the whole point of this fixture: those three inherited bodies are the uncovered lines.
class MinimalOperand : public Operand {
public:
    // Declaring serialize(CECFrame&) HIDES the base's non-virtual serialize(void) overload -
    // ordinary C++ name hiding, and a real property of this extension point: a derived
    // operand has to re-expose the convenience overload for callers to reach it.  Every
    // operand the library ships derives from CECBytes, which does the same thing, so this
    // `using` is not a test artefact but the shape a new operand must adopt.
    using Operand::serialize;

    explicit MinimalOperand(uint8_t payload)
        : m_payload(payload)
        , m_serializeCalls(0)
    {
    }

    CECFrame &serialize(CECFrame &frame) const override
    {
        ++m_serializeCalls;
        frame.append(m_payload);
        return frame;
    }

    int serializeCalls() const { return m_serializeCalls; }

private:
    uint8_t m_payload;
    // Counting calls from a const method is the reason for mutable; the counter is test-only
    // bookkeeping and is not part of the serialized form.
    mutable int m_serializeCalls;
};

// Overrides the pure virtual AND toString(), leaving name() and validate() at their
// defaults.  This is the common shape of a real operand part-way through implementation, and
// it proves the defaults are inherited independently rather than as a block.
class PartiallyOverridingOperand : public Operand {
public:
    CECFrame &serialize(CECFrame &frame) const override
    {
        frame.append((uint8_t)0x7F);
        return frame;
    }

    const std::string toString(void) const override { return "PartiallyOverriding"; }
};

// Overrides everything, including validate() returning false.  Used to show that the base
// default is a default and not a hard-coded answer.
class FullyOverridingOperand : public Operand {
public:
    CECFrame &serialize(CECFrame &frame) const override
    {
        frame.append((uint8_t)0x01);
        return frame;
    }

    const std::string toString(void) const override { return "Fully"; }
    const std::string name(void) const override { return "FullyOverridingOperand"; }
    bool validate(void) const override { return false; }
};

} // namespace

class OperandBaseTest : public ::testing::Test {
};

// ---------------------------------------------------------------------------------------
// The three inherited defaults.
// ---------------------------------------------------------------------------------------

TEST_F(OperandBaseTest, InheritedToStringReportsNotImplemented)
{
    MinimalOperand operand(0x42);
    EXPECT_EQ("Not Implemented", operand.toString());
}

TEST_F(OperandBaseTest, InheritedNameReportsOperand)
{
    MinimalOperand operand(0x42);
    EXPECT_EQ("Operand", operand.name());
}

TEST_F(OperandBaseTest, InheritedValidateAcceptsByDefault)
{
    MinimalOperand operand(0x42);
    EXPECT_TRUE(operand.validate());
}

// Dispatch through a base reference must reach the same inherited bodies.  This is how
// MessageEncoder and the print paths see an operand, so it is the path that matters.
TEST_F(OperandBaseTest, InheritedDefaultsAreReachedThroughABaseReference)
{
    MinimalOperand operand(0x42);
    const Operand &asBase = operand;

    EXPECT_EQ("Not Implemented", asBase.toString());
    EXPECT_EQ("Operand", asBase.name());
    EXPECT_TRUE(asBase.validate());
}

// ---------------------------------------------------------------------------------------
// Partial and full overriding: the defaults are per-function, not all-or-nothing.
// ---------------------------------------------------------------------------------------

TEST_F(OperandBaseTest, PartialOverrideKeepsTheRemainingDefaults)
{
    PartiallyOverridingOperand operand;
    const Operand &asBase = operand;

    EXPECT_EQ("PartiallyOverriding", asBase.toString());
    EXPECT_EQ("Operand", asBase.name());
    EXPECT_TRUE(asBase.validate());
}

TEST_F(OperandBaseTest, FullOverrideReplacesEveryDefaultIncludingValidate)
{
    FullyOverridingOperand operand;
    const Operand &asBase = operand;

    EXPECT_EQ("Fully", asBase.toString());
    EXPECT_EQ("FullyOverridingOperand", asBase.name());
    EXPECT_FALSE(asBase.validate());
}

// ---------------------------------------------------------------------------------------
// The base's non-virtual serialize(void) convenience overload.
// ---------------------------------------------------------------------------------------

TEST_F(OperandBaseTest, SerializeWithoutAFrameForwardsToTheDerivedImplementation)
{
    MinimalOperand operand(0xA5);

    CECFrame frame = operand.serialize();

    EXPECT_EQ(1, operand.serializeCalls());
    ASSERT_EQ((size_t)1, frame.length());
    EXPECT_EQ((uint8_t)0xA5, frame.at(0));
}

TEST_F(OperandBaseTest, SerializeIntoAnExistingFrameAppendsRatherThanReplaces)
{
    MinimalOperand operand(0x0F);
    CECFrame frame;
    frame.append((uint8_t)0xEE);

    CECFrame &returned = operand.serialize(frame);

    EXPECT_EQ(&frame, &returned) << "serialize(CECFrame&) must return the frame it was given";
    ASSERT_EQ((size_t)2, frame.length());
    EXPECT_EQ((uint8_t)0xEE, frame.at(0));
    EXPECT_EQ((uint8_t)0x0F, frame.at(1));
}

// Corner case: the payload boundaries a single-byte operand can carry.  Zero and 0xFF are
// the values most likely to be mishandled by an append path.
TEST_F(OperandBaseTest, SerializeCarriesMinimumAndMaximumByteValues)
{
    MinimalOperand minimum(0x00);
    MinimalOperand maximum(0xFF);

    CECFrame minimumFrame = minimum.serialize();
    CECFrame maximumFrame = maximum.serialize();

    ASSERT_EQ((size_t)1, minimumFrame.length());
    ASSERT_EQ((size_t)1, maximumFrame.length());
    EXPECT_EQ((uint8_t)0x00, minimumFrame.at(0));
    EXPECT_EQ((uint8_t)0xFF, maximumFrame.at(0));
}

// Repeated serialization of the same operand must be idempotent in the operand itself: each
// call appends once more to whatever frame it is handed, and no state is carried between
// calls beyond the test-only counter.
TEST_F(OperandBaseTest, RepeatedSerializationAppendsOncePerCall)
{
    MinimalOperand operand(0x33);
    CECFrame frame;

    operand.serialize(frame);
    operand.serialize(frame);
    operand.serialize(frame);

    EXPECT_EQ(3, operand.serializeCalls());
    ASSERT_EQ((size_t)3, frame.length());
    for (size_t i = 0; i < frame.length(); ++i) {
        EXPECT_EQ((uint8_t)0x33, frame.at(i)) << "byte " << i;
    }
}

// The BROADCAST/UNICAST addressing-mode constants live in Operand.hpp beside the class.  They
// are consumed by the message-processing paths as a bitmask, so the properties worth pinning
// are their values and that they occupy distinct bits.
TEST_F(OperandBaseTest, AddressingModeConstantsAreDistinctSingleBits)
{
    EXPECT_EQ(0x01, (int)BROADCAST);
    EXPECT_EQ(0x02, (int)UNICAST);
    EXPECT_EQ(0, (int)(BROADCAST & UNICAST)) << "the two modes must not share a bit";
    EXPECT_EQ(0x03, (int)(BROADCAST | UNICAST));
}
