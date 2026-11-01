#ifdef NDEBUG
#undef NDEBUG
#endif
#include "country_table.hpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <string>

static uint32_t v4(const char* text) {
  in_addr a{};
  assert(inet_pton(AF_INET, text, &a) == 1);
  return ntohl(a.s_addr);
}
static CountryTable::V6 v6(const char* text) {
  CountryTable::V6 a{};
  assert(inet_pton(AF_INET6, text, a.data()) == 1);
  return a;
}

int main() {
  constexpr auto de = country_index('D', 'E');
  constexpr auto us = country_index('U', 'S');
  assert(country_code(de) == "DE" && country_code(kUnknownCountry) == "ZZ");
  assert(kUnknownCountry == kCountryCodes - 1);

  const auto base = std::filesystem::temp_directory_path() / ("country_table_" + std::to_string(getpid()));
  const std::string path = base.string() + ".csv";
  {
    std::ofstream out(path);
    out << "0.0.0.0,0.255.255.255,ZZ\n"
           "1.0.0.0,1.0.0.255,DE\r\n"
           "1.0.1.0,1.0.1.255,DE\n"
           "1.0.3.0,1.0.3.255,US\n" // a gap before this one
           "255.255.255.0,255.255.255.255,US\n"
           "not,a,line\n"
           "1.1.1.1\n"
           "2.0.0.0,2.0.0.255,de\n" // bad code
           "2.0.1.0,2.0.1.255,DEU\n"
           "2.0.2.0,2.0.1.0,US\n" // backwards
           "2001:db8::,2001:db8:0:ffff:ffff:ffff:ffff:ffff,DE\n"
           "2001:db8:1::,2001:db8:1:ffff:ffff:ffff:ffff:ffff,US\n"
           "fec0::,ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff,US\n";
  }
  const auto table = CountryTable::load(path);
  std::remove(path.c_str());
  assert(table);
  assert(table->ranges() == 8);
  assert(table->lookup(v4("0.1.2.3")) == kUnknownCountry);
  assert(table->lookup(v4("1.0.0.0")) == de);
  assert(table->lookup(v4("1.0.1.255")) == de);
  assert(table->lookup(v4("1.0.2.0")) == kUnknownCountry);
  assert(table->lookup(v4("1.0.3.7")) == us);
  assert(table->lookup(v4("2.0.0.5")) == kUnknownCountry);
  assert(table->lookup(v4("2.0.2.0")) == kUnknownCountry);
  assert(table->lookup(v4("255.255.255.255")) == us);
  assert(table->lookup(v6("::1")) == kUnknownCountry);
  assert(table->lookup(v6("2001:db8::1")) == de);
  assert(table->lookup(v6("2001:db8:0:ffff:ffff:ffff:ffff:ffff")) == de);
  assert(table->lookup(v6("2001:db8:1::")) == us);
  assert(table->lookup(v6("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")) == us);

  assert(!CountryTable::load("/nonexistent/geoip.csv"));
  const std::string junk = base.string() + ".junk";
  { std::ofstream out(junk); out << "<html>not found</html>\n"; }
  assert(!CountryTable::load(junk));
  std::remove(junk.c_str());
}
