#include <cstdlib>
#include <cstring>
#include "sockutil.h"
#include "Util/logger.h"
#include "Util/util.h"
#include "IpRange.h"

using namespace std;

namespace toolkit {

bool IpRanges::toAddress(const string &ip, Address &out) {
    struct sockaddr_storage storage;
    try {
        // Numeric addresses only (inet_pton): never a DNS lookup.
        storage = SockUtil::make_sockaddr(ip.data(), 0);
    } catch (std::exception &) {
        return false;
    }
    out = Address();
    out.family = storage.ss_family;
    switch (storage.ss_family) {
        case AF_INET: memcpy(out.bytes, &(reinterpret_cast<const struct sockaddr_in &>(storage).sin_addr), 4); return true;
        case AF_INET6: memcpy(out.bytes, &(reinterpret_cast<const struct sockaddr_in6 &>(storage).sin6_addr), 16); return true;
        default: return false;
    }
}

int IpRanges::compare(const Address &a, const Address &b) {
    return memcmp(a.bytes, b.bytes, a.family == AF_INET ? 4 : 16);
}

bool IpRanges::parseItem(const string &item, Range &out) {
    auto slash = item.find('/');
    if (slash != string::npos) {
        // CIDR block: the address with its host bits cleared/set gives the range ends.
        string ip = item.substr(0, slash);
        string prefix_str = item.substr(slash + 1);
        Address addr;
        if (!toAddress(trim(ip), addr) || trim(prefix_str).empty()) {
            return false;
        }
        char *end = nullptr;
        long prefix = strtol(prefix_str.data(), &end, 10);
        const int bits = addr.family == AF_INET ? 32 : 128;
        if (*end != '\0' || prefix < 0 || prefix > bits) {
            return false;
        }
        out.min = addr;
        out.max = addr;
        for (int i = 0; i < bits / 8; ++i) {
            int keep = static_cast<int>(prefix) - i * 8;
            uint8_t mask = keep >= 8 ? 0xFF : (keep <= 0 ? 0x00 : static_cast<uint8_t>(0xFF << (8 - keep)));
            out.min.bytes[i] &= mask;
            out.max.bytes[i] |= static_cast<uint8_t>(~mask);
        }
        return true;
    }

    auto range = split(item, "-");
    if (range.size() == 2) {
        return toAddress(trim(range[0]), out.min) && toAddress(trim(range[1]), out.max) &&
               out.min.family == out.max.family;
    }
    if (range.size() == 1) {
        if (!toAddress(trim(range[0]), out.min)) {
            return false;
        }
        out.max = out.min;
        return true;
    }
    return false;
}

IpRanges IpRanges::parse(const string &str) {
    IpRanges ret;
    for (auto &item : split(str, ",")) {
        if (trim(item).empty()) {
            continue;
        }
        Range range;
        if (parseItem(item, range)) {
            ret._ranges.emplace_back(range);
        } else {
            WarnL << "Invalid ip range: " << item;
        }
    }
    return ret;
}

bool IpRanges::contains(const string &ip) const {
    Address addr;
    if (_ranges.empty() || !toAddress(ip, addr)) {
        return false;
    }
    for (auto &range : _ranges) {
        if (range.min.family == addr.family && compare(addr, range.min) >= 0 && compare(addr, range.max) <= 0) {
            return true;
        }
    }
    return false;
}

} // namespace toolkit
