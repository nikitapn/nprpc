// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include <nprpc/nprpc.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <nplib/webserver/network.hpp>

#include "session.h"

namespace net = boost::asio;
using boost::asio::ip::tcp;
using boost::asio::io_context;

class Session_Websocket 
	: public networking::Session 
	, public nprpc::impl::Session
  , public std::enable_shared_from_this<Session_Websocket> 
{
public:
	virtual std::string handle_client_message(networking::net_buffer&& buffer) {
		rx_buffer_() = std::move(buffer);
		handle_request();
		return std::string(static_cast<char*>(rx_buffer_().data().data()), rx_buffer_().size());
	}

	virtual void send(void* data, size_t len) {
		networking::Session::send(std::string(static_cast<char*>(data), len));
	}

	Session_Websocket(networking::SessionData& data)
		: networking::Session(data)
	{
	}
};

class FactoryImpl : public networking::Factory {
public:
	virtual std::unique_ptr<networking::Session> create_session(networking::SessionData& data) {
		return std::make_unique<Session_Websocket>(data);
	}
};

static auto state = std::make_shared<networking::shared_state>();
static auto net_factory = std::make_unique<FactoryImpl>();

void init_web_socket(net::io_context& ioc, unsigned short port, std::string_view http_root_dir) {
	networking::init({}, ioc, state, net_factory.get(), "127.0.0.1", port, http_root_dir);
}
