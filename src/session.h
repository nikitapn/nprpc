#pragma once

#include <nprpc/basic.hpp>
#include <nprpc/buffer.hpp>
#include <vector>

namespace nprpc {
class ObjectServant;
}

namespace nprpc::impl {

class Session {
	ReferenceList ref_list_;
protected:
	Buffers rx_buffer_;
	EndPoint remote_endpoint_;
public:
	virtual void send(void* data, size_t len) = 0;
	void handle_request();
	virtual ~Session() = default;
};

}