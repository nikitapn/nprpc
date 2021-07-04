// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include <iostream>
#include <memory>
#include <unordered_map>
#include <nprpc/nprpc_nameserver.hpp>

class Nameserver : public nprpc::INameserver_Servant {
	std::unordered_map<std::string, std::unique_ptr<nprpc::Object>> objects_;
public:
	void Bind(nprpc::Object* obj, ::flat::Span<char> name) override {
		std::cout << *obj;
		std::cout << "  Object is binded as: " << (std::string_view)name << std::endl;
		auto const str = std::string((std::string_view)name);
		objects_[str] = std::move(std::unique_ptr<nprpc::Object>(obj));
	}

	bool Resolve(::flat::Span<char> name, nprpc::detail::flat::ObjectId_Direct obj) override {
		auto const str = std::string((std::string_view)name);
		auto found = objects_.find(str);
		
		if (found == objects_.end()) {
			obj.object_id() = nprpc::invalid_object_id;
			return false;
		}
		
		nprpc::assign_to_out(found->second->_data(), obj);
		
		std::cout << "resolving as: " << (std::string_view)name << " ip: " << std::hex << std::setw(8) << std::setfill('0') 
			<< obj.ip4() << std::dec << std::endl;

		return true;
	}
};

int main() {
	Nameserver server;
	boost::asio::io_context ioc;

	nprpc::set_debug_level(nprpc::DebugLevel_Critical);
	auto orb = nprpc::init(ioc, 15000);
	auto p1 = std::make_unique<nprpc::Policy_Lifespan>(nprpc::Policy_Lifespan::Persistent);
	auto poa = orb->create_poa(1, { p1.get() });
	auto oid = poa->activate_object(&server);

	orb->start();
	ioc.run();

	return 0;
}
