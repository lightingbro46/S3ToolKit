#ifndef TOOLKIT_NETWORK_IPRANGE_H
#define TOOLKIT_NETWORK_IPRANGE_H

#include <cstdint>
#include <string>
#include <vector>

namespace toolkit {

/**
 * A list of IPv4/IPv6 address ranges, parsed from a comma separated config string.
 * Each item is one address ("10.0.0.1"), an inclusive range ("10.0.0.1-10.0.0.9") or a CIDR block
 * ("10.0.0.0/24", "fd00::/8"). Invalid items are skipped with a warning; the others are kept.
 */
class IpRanges {
public:
    IpRanges() = default;

    static IpRanges parse(const std::string &str);

    bool empty() const { return _ranges.empty(); }

    /**
     * Whether `ip` is inside one of the ranges. An address only matches a range of its own family.
     */
    bool contains(const std::string &ip) const;

private:
    struct Address {
        int family = -1;
        uint8_t bytes[16] = {0};
    };
    struct Range {
        Address min;
        Address max;
    };

    static bool toAddress(const std::string &ip, Address &out);
    static int compare(const Address &a, const Address &b);
    static bool parseItem(const std::string &item, Range &out);

    std::vector<Range> _ranges;
};

} // namespace toolkit

#endif // TOOLKIT_NETWORK_IPRANGE_H
