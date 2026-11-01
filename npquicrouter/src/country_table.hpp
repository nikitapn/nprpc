// Maps an IP address to its country, from a DB-IP "country lite" CSV:
//   start_ip,end_ip,CC
// Addresses are only looked up. Nothing here keeps them.
#pragma once

#include <arpa/inet.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Two letters index a flat array, so counting by country needs no map.
inline constexpr size_t kCountryCodes = 26 * 26;

constexpr uint16_t country_index(char a, char b) noexcept {
    return static_cast<uint16_t>((a - 'A') * 26 + (b - 'A'));
}

// DB-IP's own code for addresses with no country.
inline constexpr uint16_t kUnknownCountry = country_index('Z', 'Z');

inline std::string country_code(size_t index) {
    return {static_cast<char>('A' + index / 26), static_cast<char>('A' + index % 26)};
}

class CountryTable {
public:
    using V6 = std::array<uint8_t, 16>;

    // Null if the file can't be read or holds no valid ranges.
    static std::shared_ptr<const CountryTable> load(const std::string& path) {
        std::ifstream file(path);
        if (!file) return nullptr;
        auto table = std::make_shared<CountryTable>();
        std::string line;
        while (std::getline(file, line)) table->add_line(line);
        if (table->v4_.empty() && table->v6_.empty()) return nullptr;
        table->finish();
        return table;
    }

    uint16_t lookup(uint32_t address) const noexcept { return find(v4_, address); }
    uint16_t lookup(const V6& address) const noexcept { return find(v6_, address); }

    size_t ranges() const noexcept { return v4_.size() + v6_.size(); }

private:
    template <class Address> struct Range {
        Address  first;
        Address  last;
        uint16_t country;
    };

    void add_line(std::string_view line) {
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto first_comma = line.find(',');
        if (first_comma == line.npos) return;
        const auto second_comma = line.find(',', first_comma + 1);
        if (second_comma == line.npos) return;
        const std::string first(line.substr(0, first_comma));
        const std::string last(line.substr(first_comma + 1, second_comma - first_comma - 1));
        const auto code = line.substr(second_comma + 1);
        if (code.size() != 2 || code[0] < 'A' || code[0] > 'Z' || code[1] < 'A' || code[1] > 'Z') return;
        const auto country = country_index(code[0], code[1]);

        if (in_addr a{}, b{}; inet_pton(AF_INET, first.c_str(), &a) == 1 &&
                              inet_pton(AF_INET, last.c_str(), &b) == 1) {
            v4_.push_back({ntohl(a.s_addr), ntohl(b.s_addr), country});
        } else if (V6 c{}, d{}; inet_pton(AF_INET6, first.c_str(), c.data()) == 1 &&
                                inet_pton(AF_INET6, last.c_str(), d.data()) == 1) {
            v6_.push_back({c, d, country});
        }
    }

    void finish() {
        tidy(v4_);
        tidy(v6_);
    }

    template <class Address> static void tidy(std::vector<Range<Address>>& ranges) {
        std::erase_if(ranges, [](const auto& range) { return range.last < range.first; });
        std::ranges::sort(ranges, {}, &Range<Address>::first);
        ranges.shrink_to_fit();
    }

    template <class Address>
    static uint16_t find(const std::vector<Range<Address>>& ranges, const Address& address) noexcept {
        auto it = std::ranges::upper_bound(ranges, address, {}, &Range<Address>::first);
        if (it == ranges.begin()) return kUnknownCountry;
        --it;
        return address <= it->last ? it->country : kUnknownCountry;
    }

    std::vector<Range<uint32_t>> v4_;
    std::vector<Range<V6>>       v6_;
};
