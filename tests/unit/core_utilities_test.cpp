#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

#include "Util/Byte.hpp"
#include "Util/MD5.h"
#include "Util/ResourcePool.h"
#include "Util/SHA1.h"
#include "Util/base64.h"
#include "Util/mini.h"

using namespace toolkit;

TEST(ByteTest, ReadsAndWritesBigEndianAtOffset) {
    std::array<uint8_t, 12> data = {{0}};
    Byte::Set1Byte(data.data(), 1, 0xAB);
    Byte::Set2Bytes(data.data(), 2, 0x1234);
    Byte::Set3Bytes(data.data(), 4, 0x56789A);
    Byte::Set4Bytes(data.data(), 7, 0xBCDEF012);

    EXPECT_EQ(0xAB, Byte::Get1Byte(data.data(), 1));
    EXPECT_EQ(0x1234, Byte::Get2Bytes(data.data(), 2));
    EXPECT_EQ(0x56789Au, Byte::Get3Bytes(data.data(), 4));
    EXPECT_EQ(0xBCDEF012u, Byte::Get4Bytes(data.data(), 7));

    std::array<uint8_t, 10> wide = {{0}};
    Byte::Set8Bytes(wide.data(), 1, UINT64_C(0x0123456789ABCDEF));
    EXPECT_EQ(UINT64_C(0x0123456789ABCDEF), Byte::Get8Bytes(wide.data(), 1));
}

TEST(ByteTest, ReadsAndWritesLittleEndianAtOffset) {
    std::array<uint8_t, 12> data = {{0}};
    Byte::Set2BytesLE(data.data(), 1, 0x1234);
    Byte::Set3BytesLE(data.data(), 3, 0x56789A);
    Byte::Set4BytesLE(data.data(), 6, 0xBCDEF012);

    EXPECT_EQ(0x1234, Byte::Get2BytesLE(data.data(), 1));
    EXPECT_EQ(0x56789Au, Byte::Get3BytesLE(data.data(), 3));
    EXPECT_EQ(0xBCDEF012u, Byte::Get4BytesLE(data.data(), 6));

    std::array<uint8_t, 10> wide = {{0}};
    Byte::Set8BytesLE(wide.data(), 1, UINT64_C(0x0123456789ABCDEF));
    EXPECT_EQ(UINT64_C(0x0123456789ABCDEF), Byte::Get8BytesLE(wide.data(), 1));
}

TEST(ByteTest, PadsToFourByteBoundary) {
    EXPECT_EQ(0u, Byte::PadTo4Bytes(static_cast<uint16_t>(0)));
    EXPECT_EQ(4u, Byte::PadTo4Bytes(static_cast<uint16_t>(1)));
    EXPECT_EQ(4u, Byte::PadTo4Bytes(static_cast<uint32_t>(4)));
    EXPECT_EQ(8u, Byte::PadTo4Bytes(static_cast<uint32_t>(7)));
}

TEST(Base64Test, MatchesStandardVectors) {
    EXPECT_EQ("", encodeBase64(""));
    EXPECT_EQ("Zg==", encodeBase64("f"));
    EXPECT_EQ("Zm8=", encodeBase64("fo"));
    EXPECT_EQ("Zm9v", encodeBase64("foo"));
    EXPECT_EQ("Zm9vYmFy", encodeBase64("foobar"));
}

TEST(Base64Test, RoundTripsBinaryData) {
    const std::string binary("\0\x01\x7f\x80\xff", 5);
    EXPECT_EQ(binary, decodeBase64(encodeBase64(binary)));
}

TEST(Base64Test, RejectsInvalidInput) {
    EXPECT_EQ("", decodeBase64("not valid!"));
    uint8_t output[8] = {0};
    EXPECT_LT(av_base64_decode(output, "@@@", sizeof(output)), 0);
}

TEST(HashTest, Md5MatchesKnownVectorsAndChunkedInput) {
    EXPECT_EQ("d41d8cd98f00b204e9800998ecf8427e", MD5("").hexdigest());
    EXPECT_EQ("900150983cd24fb0d6963f7d28e17f72", MD5("abc").hexdigest());

    MD5 hash;
    hash.update("a", 1);
    hash.update("bc", 2);
    EXPECT_EQ("900150983cd24fb0d6963f7d28e17f72", hash.finalize().hexdigest());
}

TEST(HashTest, Sha1MatchesKnownVectorsAndChunkedInput) {
    EXPECT_EQ("da39a3ee5e6b4b0d3255bfef95601890afd80709", SHA1::encode(""));
    EXPECT_EQ("a9993e364706816aba3e25717850c26c9cd0d89d", SHA1::encode("abc"));

    SHA1 hash;
    hash.update("a");
    hash.update("bc");
    EXPECT_EQ("a9993e364706816aba3e25717850c26c9cd0d89d", hash.final());
}

TEST(VariantTest, ConvertsPrimitiveValues) {
    EXPECT_TRUE(variant("true").as<bool>());
    EXPECT_FALSE(variant("FALSE").as<bool>());
    EXPECT_TRUE(variant("1").as<bool>());
    EXPECT_EQ(123, variant("123").as<int>());
    EXPECT_EQ(255, static_cast<int>(variant("511").as<uint8_t>()));
    EXPECT_EQ(0, variant("invalid").as<int>());
}

TEST(IniTest, ParsesSectionsCommentsAndEmptyValues) {
    mINI ini;
    ini.parse("# heading\nroot=value\n[server]\nport = 8080\nempty=\nflag\n=no-key\n");

    EXPECT_EQ("value", ini[".root"]);
    EXPECT_EQ(8080, ini["server.port"].as<int>());
    EXPECT_EQ("", ini["server.empty"]);
    EXPECT_EQ("", ini["server.flag"]);
    EXPECT_EQ("no-key", ini["server."]);
}

TEST(IniTest, DumpRoundTripsValues) {
    mINI original;
    original[".root"] = "value";
    original["server.host"] = "localhost";
    original["server.port"] = 8080;

    mINI parsed;
    parsed.parse(original.dump("header", "footer"));
    EXPECT_EQ("value", parsed[".root"]);
    EXPECT_EQ("localhost", parsed["server.host"]);
    EXPECT_EQ(8080, parsed["server.port"].as<int>());
}

namespace {
struct TrackedResource {
    TrackedResource() : id(++created) {}
    ~TrackedResource() { ++destroyed; }
    int id;
    static int created;
    static int destroyed;
};
int TrackedResource::created = 0;
int TrackedResource::destroyed = 0;
} // namespace

TEST(ResourcePoolTest, ReusesReleasedObjectAndCallsRecycleCallback) {
    ResourcePool<TrackedResource> pool;
    pool.setSize(1);
    int recycled = 0;
    TrackedResource *address = nullptr;
    {
        auto value = pool.obtain([&recycled](TrackedResource *) { ++recycled; });
        address = value.get();
    }
    auto reused = pool.obtain();
    EXPECT_EQ(address, reused.get());
    EXPECT_EQ(1, recycled);
}

TEST(ResourcePoolTest, HonorsPoolLimitAndQuit) {
    const int destroyed_before = TrackedResource::destroyed;
    ResourcePool<TrackedResource> pool;
    pool.setSize(1);
    auto first = pool.obtain();
    auto second = pool.obtain();
    first.reset();
    second.reset();
    EXPECT_EQ(destroyed_before + 1, TrackedResource::destroyed);

    auto abandoned = pool.obtain();
    abandoned.quit();
    abandoned.reset();
    EXPECT_EQ(destroyed_before + 2, TrackedResource::destroyed);
}
