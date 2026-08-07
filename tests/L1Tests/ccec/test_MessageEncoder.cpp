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

#include <string.h>

#include <vector>

#include <gtest/gtest.h>
#include "ccec/MessageEncoder.hpp"
#include "ccec/Messages.hpp"
#include "ccec/Header.hpp"



class MessageEncoderTest : public ::testing::Test {
protected:
    MessageEncoder encoder;
};

// Helper to extract the raw buffer from a CECFrame
static void getBuf(const CECFrame &frame, const uint8_t **buf, size_t *len) {
    frame.getBuffer(buf, len);
}

// IMAGE_VIEW_ON has no operands — encoded frame is exactly 1 byte: opcode only.
TEST_F(MessageEncoderTest, EncodeImageViewOn) {
    ImageViewOn msg;
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 1u);
    EXPECT_EQ(buf[0], (uint8_t)IMAGE_VIEW_ON); // 0x04
}

// TEXT_VIEW_ON has no operands — exactly 1 byte.
TEST_F(MessageEncoderTest, EncodeTextViewOn) {
    TextViewOn msg;
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 1u);
    EXPECT_EQ(buf[0], (uint8_t)TEXT_VIEW_ON); // 0x0D
}

// ACTIVE_SOURCE carries a 2-byte physical address (4 nibbles packed into 2 bytes).
// PhysicalAddress(1,0,0,0): byte1 = (1<<4)|0 = 0x10, byte2 = (0<<4)|0 = 0x00
TEST_F(MessageEncoderTest, EncodeActiveSource) {
    PhysicalAddress phy(1, 0, 0, 0);
    ActiveSource msg(phy);
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 3u);                          // opcode + 2-byte physical address
    EXPECT_EQ(buf[0], (uint8_t)ACTIVE_SOURCE);  // 0x82
    EXPECT_EQ(buf[1], 0x10u);                    // (1<<4)|0
    EXPECT_EQ(buf[2], 0x00u);                    // (0<<4)|0
}

// STANDBY has no operands.
TEST_F(MessageEncoderTest, EncodeStandby) {
    Standby msg;
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 1u);
    EXPECT_EQ(buf[0], (uint8_t)STANDBY); // 0x36
}

// INACTIVE_SOURCE: opcode + 2-byte physical address
// PhysicalAddress(2,1,0,0): byte1=(2<<4)|1=0x21, byte2=(0<<4)|0=0x00
TEST_F(MessageEncoderTest, EncodeInActiveSource) {
    PhysicalAddress phy(2, 1, 0, 0);
    InActiveSource msg(phy);
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 3u);
    EXPECT_EQ(buf[0], (uint8_t)INACTIVE_SOURCE); // 0x9D
    EXPECT_EQ(buf[1], 0x21u);                      // (2<<4)|1
    EXPECT_EQ(buf[2], 0x00u);                      // (0<<4)|0
}

// CEC_VERSION: opcode + 1-byte version. Version::V_1_4 = 5.
TEST_F(MessageEncoderTest, EncodeCECVersion) {
    CECVersion msg(Version::V_1_4);
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 2u);                          // opcode + 1-byte version
    EXPECT_EQ(buf[0], (uint8_t)CEC_VERSION);    // 0x9E
    EXPECT_EQ(buf[1], (uint8_t)Version::V_1_4); // 5
}

// REPORT_POWER_STATUS: opcode + 1-byte power status. PowerStatus::ON = 0.
TEST_F(MessageEncoderTest, EncodeReportPowerStatus) {
    ReportPowerStatus msg(PowerStatus::ON);
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 2u);
    EXPECT_EQ(buf[0], (uint8_t)REPORT_POWER_STATUS);  // 0x90
    EXPECT_EQ(buf[1], (uint8_t)PowerStatus::ON);       // 0
}

// REPORT_PHYSICAL_ADDRESS: opcode + 2-byte physical address + 1-byte device type
// PhysicalAddress(1,2,3,4): byte1=(1<<4)|2=0x12, byte2=(3<<4)|4=0x34
// DeviceType::PLAYBACK_DEVICE = 4
TEST_F(MessageEncoderTest, EncodeReportPhysicalAddress) {
    PhysicalAddress phy(1, 2, 3, 4);
    DeviceType dt(DeviceType::PLAYBACK_DEVICE);
    ReportPhysicalAddress msg(phy, dt);
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 4u);                                  // opcode + 2-byte addr + 1-byte type
    EXPECT_EQ(buf[0], (uint8_t)REPORT_PHYSICAL_ADDRESS); // 0x84
    EXPECT_EQ(buf[1], 0x12u);                             // (1<<4)|2
    EXPECT_EQ(buf[2], 0x34u);                             // (3<<4)|4
    EXPECT_EQ(buf[3], (uint8_t)DeviceType::PLAYBACK_DEVICE); // 4
}

// Encode with Header: header byte is prepended before the opcode.
// Header(PLAYBACK_DEVICE_1, TV): from=4, to=0 -> (4<<4)|0 = 0x40
TEST_F(MessageEncoderTest, EncodeWithHeader) {
    Header hdr(LogicalAddress::PLAYBACK_DEVICE_1, LogicalAddress::TV);
    Standby msg;
    CECFrame frame = encoder.encode(hdr, msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 2u);
    EXPECT_EQ(buf[0], 0x40u);                 // header: src=4 (PB1), dst=0 (TV)
    EXPECT_EQ(buf[1], (uint8_t)STANDBY);     // 0x36
}

// Round-trip: encode then re-parse the physical address from the raw bytes.
TEST_F(MessageEncoderTest, EncodeActiveSsourceRoundTrip) {
    PhysicalAddress original(3, 2, 1, 0);
    ActiveSource msg(original);
    CECFrame frame = encoder.encode(msg);
    // Re-parse from the frame (skip opcode byte at index 0)
    ActiveSource decoded(frame, 1);
    EXPECT_STREQ(decoded.physicalAddress.toString().c_str(), "3.2.1.0");
}

// =======================================================================================
// ADDITIVE SECTION - message types in ccec/include/ccec/Messages.hpp that no test
// encoded.  Nothing above this line is modified.
//
// WHY THESE CASES EXIST
// ---------------------
// Messages.hpp is production source inside the coverage denominator (no exclusion glob
// covers ccec/include) and it measured 228/298 = 76.5% line coverage, below the 80% bar,
// with the 70 uncovered lines belonging to exactly seventeen message classes whose
// opCode() and serialize() had never run.  Every one of them is a message the library
// publishes for callers to send, so "no test encodes it" is a genuine gap and not an
// artefact of the metric: an operand-ordering slip in any of these serialize() bodies
// would put malformed bytes on the CEC bus and no existing test would notice.
//
// The cases below follow the shape already established in this file - construct the
// message, encode it, then assert the exact opcode byte and every operand byte at its
// exact offset - and add the negative/boundary variants the two-armed serializers need.
// The offsets are asserted rather than only the length, because the whole risk in a
// serialize() body of the form `return b.serialize(a.serialize(frame))` is that a and b
// come out in the wrong order, which a length check cannot see.
// =======================================================================================

// ---------------------------------------------------------------------------------------
// Single-operand messages.
// ---------------------------------------------------------------------------------------

// SET_MENU_LANGUAGE carries a 3-character ISO-639 language code.
TEST_F(MessageEncoderTest, EncodeSetMenuLanguage) {
    Language language("eng");
    SetMenuLanguage msg(language);
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 4u);                                   // opcode + 3 bytes
    EXPECT_EQ(buf[0], (uint8_t)SET_MENU_LANGUAGE);        // 0x32
    EXPECT_EQ(buf[1], (uint8_t)'e');
    EXPECT_EQ(buf[2], (uint8_t)'n');
    EXPECT_EQ(buf[3], (uint8_t)'g');
    EXPECT_STREQ(msg.language.toString().c_str(), "eng");
}

// Round-trip through the offset the decoder uses: MessageDecoder hands the message a frame
// whose operands start at index 0, so re-parsing at index 1 of an encoded frame - i.e. just
// past the opcode - must recover the same value.
TEST_F(MessageEncoderTest, EncodeSetMenuLanguageRoundTrip) {
    SetMenuLanguage msg(Language("spa"));
    CECFrame frame = encoder.encode(msg);
    SetMenuLanguage decoded(frame, 1);
    EXPECT_STREQ(decoded.language.toString().c_str(), "spa");
    EXPECT_EQ((int)SET_MENU_LANGUAGE, (int)decoded.opCode());
}

// SET_OSD_NAME carries a free-form name of up to 14 bytes.
TEST_F(MessageEncoderTest, EncodeSetOSDName) {
    OSDName name("TV Box");
    SetOSDName msg(name);
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 7u);                            // opcode + strlen("TV Box")
    EXPECT_EQ(buf[0], (uint8_t)SET_OSD_NAME);      // 0x47
    EXPECT_EQ(0, memcmp(&buf[1], "TV Box", 6));
    EXPECT_STREQ(msg.osdName.toString().c_str(), "TV Box");
}

// Boundary: OSDName::MAX_LEN is 14, and a name of exactly that length must survive intact.
TEST_F(MessageEncoderTest, EncodeSetOSDNameAtMaximumLength) {
    const char *maximum = "ABCDEFGHIJKLMN";           // exactly 14 characters
    // A named operand rather than a temporary: `SetOSDName msg(OSDName(maximum))` is a
    // function declaration, not an object definition (the most vexing parse).
    OSDName name(maximum);
    SetOSDName msg(name);
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 1u + (size_t)OSDName::MAX_LEN);
    EXPECT_EQ(buf[0], (uint8_t)SET_OSD_NAME);
    EXPECT_EQ(0, memcmp(&buf[1], maximum, (size_t)OSDName::MAX_LEN));
}

// Corner case: a single-character name is the shortest meaningful payload.
TEST_F(MessageEncoderTest, EncodeSetOSDNameAtMinimumLength) {
    SetOSDName msg(OSDName("X"));
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 2u);
    EXPECT_EQ(buf[0], (uint8_t)SET_OSD_NAME);
    EXPECT_EQ(buf[1], (uint8_t)'X');
}

// SET_OSD_STRING carries a display string of up to 13 bytes.
TEST_F(MessageEncoderTest, EncodeSetOSDString) {
    SetOSDString msg(OSDString("Hello"));
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 6u);
    EXPECT_EQ(buf[0], (uint8_t)SET_OSD_STRING);    // 0x64
    EXPECT_EQ(0, memcmp(&buf[1], "Hello", 5));
    EXPECT_STREQ(msg.osdString.toString().c_str(), "Hello");
}

// Boundary: OSDString::MAX_LEN is 13.
TEST_F(MessageEncoderTest, EncodeSetOSDStringAtMaximumLength) {
    const char *maximum = "ABCDEFGHIJKLM";            // exactly 13 characters
    OSDString text(maximum);
    SetOSDString msg(text);
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 1u + (size_t)OSDString::MAX_LEN);
    EXPECT_EQ(0, memcmp(&buf[1], maximum, (size_t)OSDString::MAX_LEN));
}

// DEVICE_VENDOR_ID carries a 3-byte IEEE OUI.
TEST_F(MessageEncoderTest, EncodeDeviceVendorID) {
    DeviceVendorID msg(VendorID(0x00, 0x19, 0xFB));
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 4u);
    EXPECT_EQ(buf[0], (uint8_t)DEVICE_VENDOR_ID);  // 0x87
    EXPECT_EQ(buf[1], 0x00u);
    EXPECT_EQ(buf[2], 0x19u);
    EXPECT_EQ(buf[3], 0xFBu);
}

// Boundary: the all-zero and all-ones vendor IDs are the two ends of the operand's range.
TEST_F(MessageEncoderTest, EncodeDeviceVendorIDAtByteBoundaries) {
    DeviceVendorID minimum(VendorID(0x00, 0x00, 0x00));
    DeviceVendorID maximum(VendorID(0xFF, 0xFF, 0xFF));

    CECFrame minimumFrame = encoder.encode(minimum);
    CECFrame maximumFrame = encoder.encode(maximum);
    const uint8_t *buf; size_t len;

    getBuf(minimumFrame, &buf, &len);
    ASSERT_EQ(len, 4u);
    EXPECT_EQ(buf[1], 0x00u); EXPECT_EQ(buf[2], 0x00u); EXPECT_EQ(buf[3], 0x00u);

    getBuf(maximumFrame, &buf, &len);
    ASSERT_EQ(len, 4u);
    EXPECT_EQ(buf[1], 0xFFu); EXPECT_EQ(buf[2], 0xFFu); EXPECT_EQ(buf[3], 0xFFu);
}

// ROUTING_INFORMATION carries the physical address of the sink being routed to.
TEST_F(MessageEncoderTest, EncodeRoutingInformation) {
    RoutingInformation msg(PhysicalAddress(2, 1, 0, 0));
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 3u);
    EXPECT_EQ(buf[0], (uint8_t)ROUTING_INFORMATION);  // 0x81
    EXPECT_EQ(buf[1], 0x21u);                          // (2<<4)|1
    EXPECT_EQ(buf[2], 0x00u);
    EXPECT_STREQ(msg.toSink.toString().c_str(), "2.1.0.0");
}

// SET_STREAM_PATH carries the physical address whose stream should become active.
TEST_F(MessageEncoderTest, EncodeSetStreamPath) {
    SetStreamPath msg(PhysicalAddress(4, 3, 2, 1));
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 3u);
    EXPECT_EQ(buf[0], (uint8_t)SET_STREAM_PATH);       // 0x86
    EXPECT_EQ(buf[1], 0x43u);                           // (4<<4)|3
    EXPECT_EQ(buf[2], 0x21u);                           // (2<<4)|1
    EXPECT_STREQ(msg.toSink.toString().c_str(), "4.3.2.1");
}

// SET_SYSTEM_AUDIO_MODE carries a single on/off status byte.
TEST_F(MessageEncoderTest, EncodeSetSystemAudioMode) {
    SetSystemAudioMode on(SystemAudioStatus(SystemAudioStatus::ON));
    SetSystemAudioMode off(SystemAudioStatus(SystemAudioStatus::OFF));

    CECFrame onFrame = encoder.encode(on);
    CECFrame offFrame = encoder.encode(off);
    const uint8_t *buf; size_t len;

    getBuf(onFrame, &buf, &len);
    ASSERT_EQ(len, 2u);
    EXPECT_EQ(buf[0], (uint8_t)SET_SYSTEM_AUDIO_MODE);  // 0x72
    EXPECT_EQ(buf[1], (uint8_t)SystemAudioStatus::ON);

    getBuf(offFrame, &buf, &len);
    ASSERT_EQ(len, 2u);
    EXPECT_EQ(buf[1], (uint8_t)SystemAudioStatus::OFF);
}

// REPORT_AUDIO_STATUS packs the mute flag into bit 7 and the volume into bits 0-6.
TEST_F(MessageEncoderTest, EncodeReportAudioStatus) {
    ReportAudioStatus unmuted(AudioStatus((uint8_t)0x32));   // mute clear, volume 50
    ReportAudioStatus muted(AudioStatus((uint8_t)0xB2));     // mute set, volume 50

    CECFrame unmutedFrame = encoder.encode(unmuted);
    CECFrame mutedFrame = encoder.encode(muted);
    const uint8_t *buf; size_t len;

    getBuf(unmutedFrame, &buf, &len);
    ASSERT_EQ(len, 2u);
    EXPECT_EQ(buf[0], (uint8_t)REPORT_AUDIO_STATUS);         // 0x7A
    EXPECT_EQ(buf[1], 0x32u);
    EXPECT_EQ(50, unmuted.status.getAudioVolume());
    EXPECT_FALSE(unmuted.status.getAudioMuteStatus());

    getBuf(mutedFrame, &buf, &len);
    ASSERT_EQ(len, 2u);
    EXPECT_EQ(buf[1], 0xB2u);
    EXPECT_TRUE(muted.status.getAudioMuteStatus());
}

// Boundary: minimum and maximum volume with the mute bit clear.
TEST_F(MessageEncoderTest, EncodeReportAudioStatusAtVolumeBoundaries) {
    ReportAudioStatus minimum(AudioStatus((uint8_t)0x00));   // volume 0
    ReportAudioStatus maximum(AudioStatus((uint8_t)0x64));   // volume 100

    const uint8_t *buf; size_t len;
    CECFrame minimumFrame = encoder.encode(minimum);
    getBuf(minimumFrame, &buf, &len);
    ASSERT_EQ(len, 2u);
    EXPECT_EQ(buf[1], 0x00u);
    EXPECT_EQ(0, minimum.status.getAudioVolume());

    CECFrame maximumFrame = encoder.encode(maximum);
    getBuf(maximumFrame, &buf, &len);
    ASSERT_EQ(len, 2u);
    EXPECT_EQ(buf[1], 0x64u);
    EXPECT_EQ(100, maximum.status.getAudioVolume());
}

// USER_CONTROL_PRESSED carries the UI command code for the key being pressed.
TEST_F(MessageEncoderTest, EncodeUserControlPressed) {
    UserControlPressed msg(UICommand(UICommand::UI_COMMAND_VOLUME_UP));
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 2u);
    EXPECT_EQ(buf[0], (uint8_t)USER_CONTROL_PRESSED);        // 0x44
    EXPECT_EQ(buf[1], (uint8_t)UICommand::UI_COMMAND_VOLUME_UP);
    EXPECT_EQ((int)UICommand::UI_COMMAND_VOLUME_UP, msg.uiCommand.toInt());
}

// Boundary: the numeric extremes of a one-byte UI command.
TEST_F(MessageEncoderTest, EncodeUserControlPressedAtCommandCodeBoundaries) {
    UserControlPressed minimum(UICommand(0x00));
    UserControlPressed maximum(UICommand(0xFF));

    const uint8_t *buf; size_t len;
    CECFrame minimumFrame = encoder.encode(minimum);
    getBuf(minimumFrame, &buf, &len);
    ASSERT_EQ(len, 2u);
    EXPECT_EQ(buf[1], 0x00u);

    CECFrame maximumFrame = encoder.encode(maximum);
    getBuf(maximumFrame, &buf, &len);
    ASSERT_EQ(len, 2u);
    EXPECT_EQ(buf[1], 0xFFu);
}

// ---------------------------------------------------------------------------------------
// Two-operand messages.  Operand ORDER is the property under test: each of these
// serialize() bodies is written as an inside-out nest, which is the easiest place in the
// header to get the order wrong.
// ---------------------------------------------------------------------------------------

// FEATURE_ABORT carries the aborted opcode first and the reason second.  The body is
// `reason.serialize(feature.serialize(frame))`, so feature must appear at offset 1.
TEST_F(MessageEncoderTest, EncodeFeatureAbortPutsTheFeatureBeforeTheReason) {
    FeatureAbort msg(OpCode(GIVE_DEVICE_POWER_STATUS),
                     AbortReason(AbortReason::UNRECOGNIZED_OPCODE));
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 3u);
    EXPECT_EQ(buf[0], (uint8_t)FEATURE_ABORT);                       // 0x00
    EXPECT_EQ(buf[1], (uint8_t)GIVE_DEVICE_POWER_STATUS);            // the aborted opcode
    EXPECT_EQ(buf[2], (uint8_t)AbortReason::UNRECOGNIZED_OPCODE);    // the reason
}

// Every documented abort reason must serialize to its own byte.  These are the values the
// sink plugin's reportFeatureAbortEvent fans out, so a collision would be user-visible.
TEST_F(MessageEncoderTest, EncodeFeatureAbortCarriesEveryAbortReason) {
    const int reasons[] = {
        AbortReason::UNRECOGNIZED_OPCODE,
        AbortReason::NOT_IN_CORRECT_MODE_TO_RESPOND,
        AbortReason::CANNOT_OVERIDE_SOURCE,
        AbortReason::INVALID_OPERAND,
        AbortReason::REFUSED,
        AbortReason::UNABLE_TO_DETERMINE
    };

    for (size_t i = 0; i < sizeof(reasons) / sizeof(reasons[0]); ++i) {
        FeatureAbort msg(OpCode(SET_STREAM_PATH), AbortReason(reasons[i]));
        CECFrame frame = encoder.encode(msg);
        const uint8_t *buf; size_t len;
        getBuf(frame, &buf, &len);
        ASSERT_EQ(len, 3u) << "reason index " << i;
        EXPECT_EQ(buf[1], (uint8_t)SET_STREAM_PATH) << "reason index " << i;
        EXPECT_EQ(buf[2], (uint8_t)reasons[i]) << "reason index " << i;
    }
}

// Corner case: POLLING is the one opcode OpCode::serialize deliberately does NOT append, so
// a FeatureAbort naming it produces a SHORTER frame.  That asymmetry is real behaviour and
// worth pinning: a caller decoding by fixed offsets would misread this frame.
TEST_F(MessageEncoderTest, EncodeFeatureAbortForPollingOmitsTheFeatureByte) {
    FeatureAbort msg(OpCode(POLLING), AbortReason(AbortReason::REFUSED));
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 2u) << "OpCode::serialize suppresses POLLING, so only the reason follows";
    EXPECT_EQ(buf[0], (uint8_t)FEATURE_ABORT);
    EXPECT_EQ(buf[1], (uint8_t)AbortReason::REFUSED);
}

// ROUTING_CHANGE carries the original address first and the new one second.  The body is
// `to.serialize(from.serialize(frame))`, so from must appear at offsets 1-2.
TEST_F(MessageEncoderTest, EncodeRoutingChangePutsFromBeforeTo) {
    RoutingChange msg(PhysicalAddress(1, 0, 0, 0), PhysicalAddress(2, 0, 0, 0));
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 5u);                                 // opcode + 2 + 2
    EXPECT_EQ(buf[0], (uint8_t)ROUTING_CHANGE);         // 0x80
    EXPECT_EQ(buf[1], 0x10u);                            // from = 1.0.0.0
    EXPECT_EQ(buf[2], 0x00u);
    EXPECT_EQ(buf[3], 0x20u);                            // to   = 2.0.0.0
    EXPECT_EQ(buf[4], 0x00u);
    EXPECT_STREQ(msg.from.toString().c_str(), "1.0.0.0");
    EXPECT_STREQ(msg.to.toString().c_str(), "2.0.0.0");
}

// Corner case: a routing change that does not move.  The library must still emit both
// addresses rather than collapsing them.
TEST_F(MessageEncoderTest, EncodeRoutingChangeWithIdenticalEndpoints) {
    RoutingChange msg(PhysicalAddress(3, 0, 0, 0), PhysicalAddress(3, 0, 0, 0));
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 5u);
    EXPECT_EQ(buf[1], 0x30u);
    EXPECT_EQ(buf[3], 0x30u);
}

// ---------------------------------------------------------------------------------------
// Messages whose serialize() has TWO ARMS.  Both arms are exercised, because taking only
// the happy one is exactly how these lines came to be uncovered.
// ---------------------------------------------------------------------------------------

// SYSTEM_AUDIO_MODE_REQUEST omits the physical address entirely when it is the unset value
// F.F.F.F - that is how the message asks the amplifier to turn system audio OFF - and
// includes it otherwise.  Default construction is the unset form.
TEST_F(MessageEncoderTest, EncodeSystemAudioModeRequestWithoutAnAddressOmitsTheOperand) {
    SystemAudioModeRequest msg;                     // defaults to F.F.F.F
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 1u) << "an unset address must serialize to the opcode alone";
    EXPECT_EQ(buf[0], (uint8_t)SYSTEM_AUDIO_MODE_REQUEST);   // 0x70
}

TEST_F(MessageEncoderTest, EncodeSystemAudioModeRequestWithAnAddressIncludesTheOperand) {
    SystemAudioModeRequest msg(PhysicalAddress(1, 2, 0, 0));
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 3u);
    EXPECT_EQ(buf[0], (uint8_t)SYSTEM_AUDIO_MODE_REQUEST);
    EXPECT_EQ(buf[1], 0x12u);
    EXPECT_EQ(buf[2], 0x00u);
}

// Corner case: only the LAST nibble differs from the unset value, so the four-way guard has
// to look at every nibble rather than short-circuiting on the first.
TEST_F(MessageEncoderTest, EncodeSystemAudioModeRequestWithAlmostUnsetAddressStillIncludesIt) {
    SystemAudioModeRequest msg(PhysicalAddress(0xF, 0xF, 0xF, 0x0));
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 3u) << "F.F.F.0 is a real address and must not be treated as unset";
    EXPECT_EQ(buf[1], 0xFFu);
    EXPECT_EQ(buf[2], 0xF0u);
}

// REQUEST_CURRENT_LATENCY has the identical two-armed serializer, and the same two arms.
TEST_F(MessageEncoderTest, EncodeRequestCurrentLatencyWithoutAnAddressOmitsTheOperand) {
    RequestCurrentLatency msg;                       // defaults to F.F.F.F
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 1u);
    EXPECT_EQ(buf[0], (uint8_t)REQUEST_CURRENT_LATENCY);      // 0xA7
}

TEST_F(MessageEncoderTest, EncodeRequestCurrentLatencyWithAnAddressIncludesTheOperand) {
    RequestCurrentLatency msg(PhysicalAddress(2, 0, 0, 0));
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 3u);
    EXPECT_EQ(buf[0], (uint8_t)REQUEST_CURRENT_LATENCY);
    EXPECT_EQ(buf[1], 0x20u);
    EXPECT_EQ(buf[2], 0x00u);
}

// REPORT_CURRENT_LATENCY appends the audio-output delay ONLY when the latency flags say the
// audio output is compensated (low two bits == 3).  Both arms are covered.
TEST_F(MessageEncoderTest, EncodeReportCurrentLatencyWithoutAudioCompensation) {
    ReportCurrentLatency msg(PhysicalAddress(1, 0, 0, 0),
                             /* videoLatency  */ 0x0A,
                             /* latencyFlags  */ 0x01);
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 5u);                                       // opcode + 2 addr + 2 latency
    EXPECT_EQ(buf[0], (uint8_t)REPORT_CURRENT_LATENCY);       // 0xA8
    EXPECT_EQ(buf[1], 0x10u);
    EXPECT_EQ(buf[2], 0x00u);
    EXPECT_EQ(buf[3], 0x0Au);                                 // video latency
    EXPECT_EQ(buf[4], 0x01u);                                 // latency flags
}

TEST_F(MessageEncoderTest, EncodeReportCurrentLatencyWithAudioCompensationAppendsTheDelay) {
    ReportCurrentLatency msg(PhysicalAddress(1, 0, 0, 0),
                             /* videoLatency     */ 0x0A,
                             /* latencyFlags     */ 0x03,
                             /* audioOutputDelay */ 0x14);
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 6u) << "latencyFlags 3 means the audio delay is present";
    EXPECT_EQ(buf[3], 0x0Au);
    EXPECT_EQ(buf[4], 0x03u);
    EXPECT_EQ(buf[5], 0x14u);
}

// ---------------------------------------------------------------------------------------
// Variable-length messages: the operand count is carried by a loop, so the boundaries of
// that loop - one descriptor, and the four-descriptor clamp - are what must be pinned.
// ---------------------------------------------------------------------------------------

// REQUEST_SHORT_AUDIO_DESCRIPTOR packs each request as (formatId << 6) | (formatCode & 0x3F).
TEST_F(MessageEncoderTest, EncodeRequestShortAudioDescriptorWithOneDescriptor) {
    std::vector<uint8_t> formatIds;
    std::vector<uint8_t> formatCodes;
    formatIds.push_back(0);
    formatCodes.push_back(ShortAudioDescriptor::SAD_FMT_CODE_AC3);

    RequestShortAudioDescriptor msg(formatIds, formatCodes, 1);
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 2u);
    EXPECT_EQ(buf[0], (uint8_t)REQUEST_SHORT_AUDIO_DESCRIPTOR);     // 0xA4
    EXPECT_EQ(buf[1], (uint8_t)ShortAudioDescriptor::SAD_FMT_CODE_AC3);
    EXPECT_EQ((uint8_t)1, msg.numberofdescriptor);
}

// Boundary: the constructor clamps the descriptor count to 4, so asking for more must not
// read past the caller's vectors nor emit more than four operand bytes.
TEST_F(MessageEncoderTest, EncodeRequestShortAudioDescriptorClampsToFourDescriptors) {
    std::vector<uint8_t> formatIds;
    std::vector<uint8_t> formatCodes;
    for (int i = 0; i < 6; ++i) {
        formatIds.push_back(0);
        formatCodes.push_back((uint8_t)(ShortAudioDescriptor::SAD_FMT_CODE_LPCM + i));
    }

    RequestShortAudioDescriptor msg(formatIds, formatCodes, 6);
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    EXPECT_EQ((uint8_t)4, msg.numberofdescriptor);
    ASSERT_EQ(len, 5u) << "opcode + exactly four clamped descriptors";
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(buf[1 + i], (uint8_t)(ShortAudioDescriptor::SAD_FMT_CODE_LPCM + i)) << "descriptor " << i;
    }
}

// Corner case: a non-zero format id occupies the top two bits, so it must not corrupt the
// format code in the low six.
TEST_F(MessageEncoderTest, EncodeRequestShortAudioDescriptorPacksTheFormatIdIntoTheTopBits) {
    std::vector<uint8_t> formatIds;
    std::vector<uint8_t> formatCodes;
    formatIds.push_back(1);
    formatCodes.push_back(ShortAudioDescriptor::SAD_FMT_CODE_EXTENDED);   // 15

    RequestShortAudioDescriptor msg(formatIds, formatCodes, 1);
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 2u);
    EXPECT_EQ(buf[1], (uint8_t)((1 << 6) | ShortAudioDescriptor::SAD_FMT_CODE_EXTENDED));
}

// REPORT_SHORT_AUDIO_DESCRIPTOR carries three bytes per descriptor.
//
// PRODUCTION DEFECT, REPORTED HERE AND NOT WORKED AROUND IN PRODUCTION CODE
// -------------------------------------------------------------------------
// ReportShortAudioDescriptor has TWO constructors and only one of them is safe to
// serialize:
//
//   ReportShortAudioDescriptor(const std::vector<uint32_t>, uint8_t numberofdescriptor = 1)
//
// names its parameter `numberofdescriptor`, which SHADOWS the member of the same name, so
// the clamp `numberofdescriptor = numberofdescriptor > 4 ? 4 : numberofdescriptor` assigns
// the PARAMETER and the MEMBER IS NEVER INITIALISED.  serialize() and print() then loop to
// `numberofdescriptor`, i.e. to an indeterminate value, and index shortAudioDescriptor with
// it.  Encoding a message built with that constructor reads past the end of the vector; it
// segfaulted this suite when first attempted here, reproducibly, at
// Messages.hpp:521-534 vs :546-553.  Compare the sibling class
// RequestShortAudioDescriptor, whose parameter is spelled `number_of_descriptor` and which
// therefore assigns its member correctly - the two were clearly meant to be identical.
//
// The fix is a one-word rename of that parameter in production source, which this pass is
// forbidden to make (production/implementation source is entirely out of scope: modify ZERO
// source files).  It is therefore REPORTED, and the coverage of serialize() is obtained
// through the OTHER constructor - the decoder-facing
// ReportShortAudioDescriptor(const CECFrame&, int) - which sets the member from the frame
// length and is safe.  That is not a workaround for the defect: it is the only entry point
// whose post-condition the class actually establishes.
TEST_F(MessageEncoderTest, EncodeReportShortAudioDescriptorWithOneDescriptor) {
    // Three operand bytes -> exactly one descriptor.  Built as a frame because the
    // frame-taking constructor is the one that initialises numberofdescriptor.
    const uint8_t operands[] = { 0x01, 0x03, 0x02 };
    CECFrame operandFrame(operands, sizeof(operands));

    ReportShortAudioDescriptor msg(operandFrame, 0);
    ASSERT_EQ((uint8_t)1, msg.numberofdescriptor);

    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 4u);                                          // opcode + 3 bytes
    EXPECT_EQ(buf[0], (uint8_t)REPORT_SHORT_AUDIO_DESCRIPTOR);   // 0xA3
    EXPECT_EQ(buf[1], 0x01u);
    EXPECT_EQ(buf[2], 0x03u);
    EXPECT_EQ(buf[3], 0x02u);
}

// Several descriptors: the serialize loop must emit every one, in order, three bytes each.
TEST_F(MessageEncoderTest, EncodeReportShortAudioDescriptorWithSeveralDescriptors) {
    const uint8_t operands[] = { 0x01, 0x03, 0x02,
                                 0x06, 0x07, 0x00,
                                 0x0A, 0x01, 0x01 };
    CECFrame operandFrame(operands, sizeof(operands));

    ReportShortAudioDescriptor msg(operandFrame, 0);
    ASSERT_EQ((uint8_t)3, msg.numberofdescriptor);

    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 10u) << "opcode + three descriptors of three bytes each";
    EXPECT_EQ(buf[0], (uint8_t)REPORT_SHORT_AUDIO_DESCRIPTOR);
    EXPECT_EQ(0, memcmp(&buf[1], operands, sizeof(operands)));
}

// Corner case: fewer than three operand bytes means zero descriptors, so serialize() must
// emit the opcode alone rather than reading a partial descriptor.
TEST_F(MessageEncoderTest, EncodeReportShortAudioDescriptorWithNoCompleteDescriptor) {
    const uint8_t operands[] = { 0x01, 0x03 };
    CECFrame operandFrame(operands, sizeof(operands));

    ReportShortAudioDescriptor msg(operandFrame, 0);
    ASSERT_EQ((uint8_t)0, msg.numberofdescriptor);

    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 1u);
    EXPECT_EQ(buf[0], (uint8_t)REPORT_SHORT_AUDIO_DESCRIPTOR);
}

// REPORT_FEATURES carries [CEC Version][All Device Types][RC Profile...][Device Features...].
// The order is the whole content of its serialize(), and getting version and device types
// the wrong way round would be invisible to a length check.
TEST_F(MessageEncoderTest, EncodeReportFeatures) {
    std::vector<RcProfile> rcProfiles;
    std::vector<DeviceFeatures> deviceFeatures;
    rcProfiles.push_back(RcProfile((uint8_t)0x0A));
    deviceFeatures.push_back(DeviceFeatures((uint8_t)0x40));

    ReportFeatures msg(Version(Version::V_1_4),
                       AllDeviceTypes((uint8_t)0x80),
                       rcProfiles,
                       deviceFeatures);
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 5u);                                    // opcode + version + types + 1 + 1
    EXPECT_EQ(buf[0], (uint8_t)REPORT_FEATURES);           // 0xA6
    EXPECT_EQ(buf[1], (uint8_t)Version::V_1_4);
    EXPECT_EQ(buf[2], 0x80u);                               // all device types
    EXPECT_EQ(buf[3], 0x0Au);                               // rc profile
    EXPECT_EQ(buf[4], 0x40u);                               // device features
}

// Corner case: several RC profiles and several device features, which is what the extension
// bit is for.  Every entry must be emitted, in order, profiles before features.
TEST_F(MessageEncoderTest, EncodeReportFeaturesWithMultipleProfilesAndFeatures) {
    std::vector<RcProfile> rcProfiles;
    std::vector<DeviceFeatures> deviceFeatures;
    rcProfiles.push_back(RcProfile((uint8_t)0x8A));       // extension bit set
    rcProfiles.push_back(RcProfile((uint8_t)0x0B));
    deviceFeatures.push_back(DeviceFeatures((uint8_t)0xC0));
    deviceFeatures.push_back(DeviceFeatures((uint8_t)0x02));

    ReportFeatures msg(Version(Version::V_2_0),
                       AllDeviceTypes((uint8_t)0x08),
                       rcProfiles,
                       deviceFeatures);
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 7u);
    EXPECT_EQ(buf[1], (uint8_t)Version::V_2_0);
    EXPECT_EQ(buf[2], 0x08u);
    EXPECT_EQ(buf[3], 0x8Au);
    EXPECT_EQ(buf[4], 0x0Bu);
    EXPECT_EQ(buf[5], 0xC0u);
    EXPECT_EQ(buf[6], 0x02u);
}

// Corner case: no profiles and no features at all.  The loops must simply not run, leaving
// a three-byte frame rather than reading from an empty vector.
TEST_F(MessageEncoderTest, EncodeReportFeaturesWithNoProfilesOrFeatures) {
    std::vector<RcProfile> rcProfiles;
    std::vector<DeviceFeatures> deviceFeatures;

    ReportFeatures msg(Version(Version::V_1_4),
                       AllDeviceTypes((uint8_t)0x00),
                       rcProfiles,
                       deviceFeatures);
    CECFrame frame = encoder.encode(msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 3u);
    EXPECT_EQ(buf[0], (uint8_t)REPORT_FEATURES);
    EXPECT_EQ(buf[1], (uint8_t)Version::V_1_4);
    EXPECT_EQ(buf[2], 0x00u);
}

// ---------------------------------------------------------------------------------------
// Header-prefixed forms of the newly covered messages.  MessageEncoder has a four-argument
// overload that writes into a caller-supplied frame; using it here proves the additive
// messages compose with the header path the same way the pre-existing ones do.
// ---------------------------------------------------------------------------------------

TEST_F(MessageEncoderTest, EncodeSetOSDNameWithHeaderIntoAnExistingFrame) {
    Header hdr(LogicalAddress::PLAYBACK_DEVICE_1, LogicalAddress::TV);
    SetOSDName msg(OSDName("Box"));
    CECFrame out;

    CECFrame &returned = encoder.encode(hdr, msg, out);

    EXPECT_EQ(&out, &returned) << "the four-argument overload must return the frame it was given";
    const uint8_t *buf; size_t len;
    getBuf(out, &buf, &len);
    ASSERT_EQ(len, 5u);                                 // header + opcode + "Box"
    EXPECT_EQ(buf[0], 0x40u);                            // from 4 to 0
    EXPECT_EQ(buf[1], (uint8_t)SET_OSD_NAME);
    EXPECT_EQ(0, memcmp(&buf[2], "Box", 3));
}

TEST_F(MessageEncoderTest, EncodeFeatureAbortWithHeaderToBroadcast) {
    Header hdr(LogicalAddress::TV, LogicalAddress::BROADCAST);
    FeatureAbort msg(OpCode(GIVE_OSD_NAME), AbortReason(AbortReason::INVALID_OPERAND));

    CECFrame frame = encoder.encode(hdr, msg);
    const uint8_t *buf; size_t len;
    getBuf(frame, &buf, &len);
    ASSERT_EQ(len, 4u);
    EXPECT_EQ(buf[0], 0x0Fu);                            // from 0 (TV) to 15 (broadcast)
    EXPECT_EQ(buf[1], (uint8_t)FEATURE_ABORT);
    EXPECT_EQ(buf[2], (uint8_t)GIVE_OSD_NAME);
    EXPECT_EQ(buf[3], (uint8_t)AbortReason::INVALID_OPERAND);
}
