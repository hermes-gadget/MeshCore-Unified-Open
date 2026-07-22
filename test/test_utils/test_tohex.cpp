#include <gtest/gtest.h>
#include <array>
#include "Utils.h"

using namespace mesh;

#define HEX_BUFFER_SIZE(input) (sizeof(input) * 2 + 1)

TEST(UtilsToHex, ConvertSingleByte) {
    uint8_t input[] = {0xAB};
    char output[HEX_BUFFER_SIZE(input)];

    Utils::toHex(output, input, sizeof(input));

    EXPECT_STREQ("AB", output);
}

TEST(UtilsToHex, ConvertMultipleBytes) {
    uint8_t input[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    char output[HEX_BUFFER_SIZE(input)];

    Utils::toHex(output, input, sizeof(input));

    EXPECT_STREQ("0123456789ABCDEF", output);
}

TEST(UtilsToHex, ConvertZeroByte) {
    uint8_t input[] = {0x00};
    char output[HEX_BUFFER_SIZE(input)];

    Utils::toHex(output, input, sizeof(input));

    EXPECT_STREQ("00", output);
}

TEST(UtilsToHex, ConvertMaxByte) {
    uint8_t input[] = {0xFF};
    char output[HEX_BUFFER_SIZE(input)];

    Utils::toHex(output, input, sizeof(input));

    EXPECT_STREQ("FF", output);
}

TEST(UtilsToHex, NullTerminatesOnEmptyInput) {
    uint8_t input[] = {0xAB};
    char output[] = "X";  // Pre-fill with X.

    Utils::toHex(output, input, 0);

    // Should just null-terminate at position 0
    EXPECT_EQ('\0', output[0]);
}

TEST(UtilsDecrypt, RejectsNonAlignedAndOversizedCiphertextWithoutWriting) {
    std::array<uint8_t, PUB_KEY_SIZE> key{};
    std::array<uint8_t, CIPHER_BLOCK_SIZE * 2> ciphertext{};
    std::array<uint8_t, CIPHER_BLOCK_SIZE + 2> dest{};
    dest.fill(0xA5);

    EXPECT_EQ(0, Utils::decrypt(key.data(), dest.data() + 1,
                                ciphertext.data(), CIPHER_BLOCK_SIZE - 1,
                                CIPHER_BLOCK_SIZE));
    EXPECT_EQ(0, Utils::decrypt(key.data(), dest.data() + 1,
                                ciphertext.data(), CIPHER_BLOCK_SIZE * 2,
                                CIPHER_BLOCK_SIZE));
    EXPECT_EQ(0xA5, dest.front());
    EXPECT_EQ(0xA5, dest.back());
}

TEST(UtilsDecrypt, AcceptsOnlySafePacketLengthsFromZeroThroughMaximum) {
    std::array<uint8_t, PUB_KEY_SIZE> key{};
    std::array<uint8_t, MAX_PACKET_PAYLOAD> source{};
    std::array<uint8_t, MAX_PACKET_PAYLOAD + 2> dest{};

    for (int source_len = 0; source_len <= MAX_PACKET_PAYLOAD; ++source_len) {
        dest.fill(0xA5);
        const int ciphertext_len = source_len - CIPHER_MAC_SIZE;
        const bool valid = source_len > CIPHER_MAC_SIZE &&
                           (ciphertext_len % CIPHER_BLOCK_SIZE) == 0;

        const int result = Utils::MACThenDecrypt(
            key.data(), dest.data() + 1, source.data(), source_len,
            MAX_PACKET_PAYLOAD);

        EXPECT_EQ(valid ? ciphertext_len : 0, result) << "source_len=" << source_len;
        EXPECT_EQ(0xA5, dest.front()) << "source_len=" << source_len;
        EXPECT_EQ(0xA5, dest.back()) << "source_len=" << source_len;
    }
}

TEST(UtilsDecrypt, RejectsFormerMaximumGroupOverflowShape) {
    std::array<uint8_t, PUB_KEY_SIZE> key{};
    std::array<uint8_t, MAX_PACKET_PAYLOAD - 1> source{};
    std::array<uint8_t, MAX_PACKET_PAYLOAD + 2> dest{};
    dest.fill(0xA5);

    EXPECT_EQ(0, Utils::MACThenDecrypt(
        key.data(), dest.data() + 1, source.data(), source.size(),
        MAX_PACKET_PAYLOAD));
    EXPECT_EQ(0xA5, dest.front());
    EXPECT_EQ(0xA5, dest.back());
}

TEST(UtilsDecrypt, RejectsAlignedCiphertextLargerThanDestination) {
    constexpr size_t kCiphertextSize = MAX_PACKET_PAYLOAD + 8;
    std::array<uint8_t, PUB_KEY_SIZE> key{};
    std::array<uint8_t, CIPHER_MAC_SIZE + kCiphertextSize> source{};
    std::array<uint8_t, MAX_PACKET_PAYLOAD + 2> dest{};
    dest.fill(0xA5);

    EXPECT_EQ(0, Utils::MACThenDecrypt(
        key.data(), dest.data() + 1, source.data(), source.size(),
        MAX_PACKET_PAYLOAD));
    EXPECT_EQ(0xA5, dest.front());
    EXPECT_EQ(0xA5, dest.back());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
