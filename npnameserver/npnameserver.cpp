// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include <iostream>
#include <memory>
#include <unordered_map>
#include <nprpc_stub/nprpc_nameserver.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/beast/core/error.hpp>

class Nameserver : public nprpc::INameserver_Servant {
	std::unordered_map<std::string, std::unique_ptr<nprpc::Object>> objects_;
public:
	void Bind(nprpc::Object* obj, nprpc::flat::Span<char> name) override {
		std::cout << *obj;
		std::cout << "  Object is binded as: " << (std::string_view)name << std::endl;
		auto const str = std::string((std::string_view)name);
		objects_[str] = std::move(std::unique_ptr<nprpc::Object>(obj));
	}

	bool Resolve(nprpc::flat::Span<char> name, nprpc::detail::flat::ObjectId_Direct obj) override {
		auto const str = std::string((std::string_view)name);
		auto found = objects_.find(str);
		
		if (found == objects_.end()) {
			obj.object_id() = nprpc::invalid_object_id;
			return false;
		}

		const auto& oid = found->second->get_data();
		nprpc::detail::helpers::assign_from_cpp_ObjectId(obj, oid);

		std::cout << "Resolving: " << str << " with urls: " << oid.urls << std::endl;

		return true;
	}
};

int main() {
	Nameserver server;
	boost::asio::io_context ioc;

	boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
	signals.async_wait([&](boost::beast::error_code const&, int) {
		ioc.stop();
		});

	auto rpc = nprpc::RpcBuilder()
		.set_debug_level(nprpc::DebugLevel::DebugLevel_Critical)
		.set_listen_tcp_port(15000)
		.set_listen_http_port(15001)
		// .set_hostname("localhost")
		.build(ioc);

	auto poa = nprpc::PoaBuilder(rpc)
		.with_max_objects(1)
		.with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
		.build();

	auto oid = poa->activate_object(&server,
		nprpc::ObjectActivationFlags::ALLOW_TCP |
		nprpc::ObjectActivationFlags::ALLOW_WEBSOCKET
	);

	ioc.run();
	rpc->destroy();

	return 0;
}
