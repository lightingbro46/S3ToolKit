#include <gtest/gtest.h>
#include "Network/IpRange.h"

using namespace toolkit;

TEST(IpRanges, EmptyStringMatchesNothing) {
    auto ranges = IpRanges::parse("");
    EXPECT_TRUE(ranges.empty());
    EXPECT_FALSE(ranges.contains("127.0.0.1"));

    EXPECT_TRUE(IpRanges::parse(" , ,").empty());
}

TEST(IpRanges, SingleAddress) {
    auto ranges = IpRanges::parse("10.0.0.5, ::1");
    EXPECT_FALSE(ranges.empty());
    EXPECT_TRUE(ranges.contains("10.0.0.5"));
    EXPECT_FALSE(ranges.contains("10.0.0.4"));
    EXPECT_FALSE(ranges.contains("10.0.0.6"));
    EXPECT_TRUE(ranges.contains("::1"));
    EXPECT_FALSE(ranges.contains("::2"));
}

TEST(IpRanges, InclusiveRange) {
    auto ranges = IpRanges::parse("192.168.5.10 - 192.168.5.20");
    EXPECT_TRUE(ranges.contains("192.168.5.10"));
    EXPECT_TRUE(ranges.contains("192.168.5.15"));
    EXPECT_TRUE(ranges.contains("192.168.5.20"));
    EXPECT_FALSE(ranges.contains("192.168.5.9"));
    EXPECT_FALSE(ranges.contains("192.168.5.21"));
    EXPECT_FALSE(ranges.contains("192.168.6.15"));
}

TEST(IpRanges, Ipv4Cidr) {
    auto ranges = IpRanges::parse("10.10.2.0/24,172.16.0.0/12");
    EXPECT_TRUE(ranges.contains("10.10.2.0"));
    EXPECT_TRUE(ranges.contains("10.10.2.255"));
    EXPECT_FALSE(ranges.contains("10.10.3.0"));
    EXPECT_FALSE(ranges.contains("10.10.1.255"));
    EXPECT_TRUE(ranges.contains("172.16.0.1"));
    EXPECT_TRUE(ranges.contains("172.31.255.255"));
    EXPECT_FALSE(ranges.contains("172.32.0.0"));
    EXPECT_FALSE(ranges.contains("172.15.255.255"));
}

TEST(IpRanges, CidrHostBitsAreIgnored) {
    // 10.10.2.77/24 covers the whole /24, not only addresses from .77 on.
    auto ranges = IpRanges::parse("10.10.2.77/24");
    EXPECT_TRUE(ranges.contains("10.10.2.1"));
    EXPECT_TRUE(ranges.contains("10.10.2.254"));
}

TEST(IpRanges, CidrEdgePrefixes) {
    auto host = IpRanges::parse("10.0.0.7/32");
    EXPECT_TRUE(host.contains("10.0.0.7"));
    EXPECT_FALSE(host.contains("10.0.0.6"));

    auto any = IpRanges::parse("0.0.0.0/0");
    EXPECT_TRUE(any.contains("1.2.3.4"));
    EXPECT_TRUE(any.contains("255.255.255.255"));
    EXPECT_FALSE(any.contains("::1"));

    auto odd = IpRanges::parse("10.0.0.0/23");
    EXPECT_TRUE(odd.contains("10.0.1.255"));
    EXPECT_FALSE(odd.contains("10.0.2.0"));
}

TEST(IpRanges, Ipv6Cidr) {
    auto ranges = IpRanges::parse("fd00::/8, 2001:db8::/32");
    EXPECT_TRUE(ranges.contains("fd12:3456::1"));
    EXPECT_FALSE(ranges.contains("fe80::1"));
    EXPECT_TRUE(ranges.contains("2001:db8:ffff::1"));
    EXPECT_FALSE(ranges.contains("2001:db9::1"));
}

TEST(IpRanges, FamilyMismatchNeverMatches) {
    auto ranges = IpRanges::parse("0.0.0.0-255.255.255.255");
    EXPECT_FALSE(ranges.contains("::1"));
    // A range whose two ends are of different families is invalid.
    EXPECT_TRUE(IpRanges::parse("10.0.0.1-::1").empty());
}

TEST(IpRanges, InvalidItemsAreSkipped) {
    auto ranges = IpRanges::parse("bad, 10.0.0.1, 10.0.0.0/33, 10.0.0.0/, 10.0.0.0/x, 1-2-3, ::/129, example.com");
    EXPECT_FALSE(ranges.empty());
    EXPECT_TRUE(ranges.contains("10.0.0.1"));
    EXPECT_FALSE(ranges.contains("10.0.0.2"));
}

TEST(IpRanges, InvalidPeerNeverMatches) {
    auto ranges = IpRanges::parse("0.0.0.0/0,::/0");
    EXPECT_FALSE(ranges.contains(""));
    EXPECT_FALSE(ranges.contains("not-an-ip"));
}

TEST(IpRanges, LegacyAllowIpRangeDefault) {
    // The default of http.allow_ip_range keeps matching as before.
    auto ranges = IpRanges::parse(
        "::1,127.0.0.1,172.16.0.0-172.31.255.255,192.168.0.0-192.168.255.255,10.0.0.0-10.255.255.255");
    EXPECT_TRUE(ranges.contains("127.0.0.1"));
    EXPECT_TRUE(ranges.contains("::1"));
    EXPECT_TRUE(ranges.contains("172.20.1.1"));
    EXPECT_TRUE(ranges.contains("192.168.1.14"));
    EXPECT_TRUE(ranges.contains("10.1.2.3"));
    EXPECT_FALSE(ranges.contains("8.8.8.8"));
    EXPECT_FALSE(ranges.contains("127.0.0.2"));
}
