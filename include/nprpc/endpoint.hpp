#ifndef NPRPC_ENDPOINT_
#define NPRPC_ENDPOINT_

#include <boost/system/errc.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/ip/address_v4.hpp>

namespace nprpc {
struct EndPoint {
	union {
		struct {
			uint32_t ip4;
			uint32_t port;
		};
		uint64_t hash;
	};
	bool websocket;
	EndPoint() = default;
	EndPoint(uint32_t _ip4, uint32_t _port, bool _websocket) : ip4{ _ip4 }, port{ _port }, websocket{ _websocket } {}
	bool is_local() const noexcept { return hash == 0; }
	bool operator==(const EndPoint& other) const noexcept { return hash == other.hash; }
};

inline std::ostream& operator << (std::ostream& os, const EndPoint& endpoint) {
	return os << boost::asio::ip::address_v4(endpoint.ip4).to_string() << ':' << endpoint.port;
}

}// namespace nprpc



#endif // NPRPC_ENDPOINT_