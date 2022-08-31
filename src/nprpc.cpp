// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/nprpc_nameserver.hpp>
#include <boost/bind.hpp>

using namespace nprpc;

namespace nprpc::impl {

extern void init_socket(boost::asio::io_context& ioc);
extern void init_web_socket(boost::asio::io_context& ioc);
/*
void RpcImpl::check_unclaimed_objects(boost::system::error_code ec) {
	if (ec) return;

	if (timer1_.expires_at() <= boost::asio::deadline_timer::traits_type::now()) {
		std::lock_guard<std::mutex> lk(new_activated_objects_mut_);
		auto now = std::chrono::system_clock::now();
		for (auto it = begin(new_activated_objects_); it != end(new_activated_objects_);) {
			auto obj = (*it);
			if (obj->activation_time() + std::chrono::milliseconds(5000) <= now) {
				std::cout << "Nobody claimed '" << obj->get_class() << "', id: " << obj->oid() << " cleaning up..." << std::endl;
				obj->poa()->deactivate_object(obj->oid());
				obj->destroy();
				it = new_activated_objects_.erase(it);
			} else {
				++it;
			}
		}
	}

	timer1_.expires_from_now(boost::posix_time::milliseconds(2500));
	timer1_.async_wait(boost::bind(&RpcImpl::check_unclaimed_objects, this, std::placeholders::_1));
}
*/

Config g_cfg;
NPRPC_API RpcImpl* g_orb;

void RpcImpl::destroy() {
//	timer1_.cancel();
	delete this;
	g_orb = nullptr;
}

Poa* RpcImpl::create_poa(uint32_t objects_max, std::initializer_list<Policy*> policy_list) {
	auto idx = poa_size_.fetch_add(1);
	auto poa = new PoaImpl(objects_max, idx);
	poas_[idx] = std::unique_ptr<PoaImpl>(poa);
	for (auto policy : policy_list) {
		policy->apply_policy(poa);
	}
	return poa;
}

NPRPC_API std::shared_ptr<Session> RpcImpl::get_session(const EndPoint& endpoint) {
	std::shared_ptr<Session> con;
	{
		std::lock_guard<std::mutex> lk(connections_mut_);
		if (auto founded = std::find_if(opened_sessions_.begin(), opened_sessions_.end(),
			[&endpoint](auto const& ptr) { return ptr->remote_endpoint() == endpoint; });
			founded != opened_sessions_.end()) {
			con = (*founded);
		} else {
			if (endpoint.websocket) throw nprpc::ExceptionCommFailure();
			if (endpoint.is_local()) {
				// not impl
			} else {
				con = std::make_shared<SocketConnection>(
					endpoint,
					boost::asio::ip::tcp::socket(boost::asio::make_strand(ioc_))
					);
			}
			opened_sessions_.push_back(con);
		}
	}
	return con;
}

NPRPC_API void RpcImpl::call(
	const EndPoint& endpoint,
	flat_buffer& buffer,
	uint32_t timeout_ms) {
	get_session(endpoint)->send_receive(buffer, timeout_ms);
}

NPRPC_API void RpcImpl::call_async(const EndPoint& endpoint, flat_buffer&& buffer,
	std::function<void(const boost::system::error_code&, flat_buffer&)>&& completion_handler, uint32_t timeout_ms) {
	get_session(endpoint)->send_receive_async(std::move(buffer), std::move(completion_handler), timeout_ms);
}

NPRPC_API std::optional<ObjectGuard> RpcImpl::get_object(poa_idx_t poa_idx, oid_t object_id) {
	auto poa = g_orb->get_poa(poa_idx);
	if (!poa) return std::nullopt;
	return poa->get_object(object_id);
}

bool RpcImpl::has_session(const EndPoint& endpoint) const noexcept {
	std::lock_guard<std::mutex> lk(connections_mut_);
	return std::find_if(opened_sessions_.begin(), opened_sessions_.end(),
		[endpoint](auto const& ptr) { return ptr->remote_endpoint() == endpoint; }) != opened_sessions_.end();
}

bool RpcImpl::close_session(Session* session) {
	std::lock_guard<std::mutex> lk(connections_mut_);
	if (auto founded = std::find_if(opened_sessions_.begin(), opened_sessions_.end(),
		[session](auto const& ptr) { return ptr->remote_endpoint() == session->remote_endpoint(); });
		founded != opened_sessions_.end()) {
		opened_sessions_.erase(founded);
	} else {
		std::cerr << "Error: session was not found\n";
		return false;
	}
	return true;
}

ObjectPtr<Nameserver> RpcImpl::get_nameserver(std::string_view nameserver_ip) {
	ObjectPtr<Nameserver> obj(new Nameserver(0));
	obj->_data().ip4 = boost::asio::ip::address_v4::from_string(nameserver_ip.data()).to_uint();
	obj->_data().port = 15000;
	obj->_data().poa_idx = 0;
	obj->_data().object_id = 0;
	obj->_data().flags = (Policy_Lifespan::Persistent << static_cast<int>(detail::ObjectFlag::Policy_Lifespan));
	obj->_data().class_id = INameserver_Servant::_get_class();
	return obj;
}

RpcImpl::RpcImpl(boost::asio::io_context& ioc)
	: ioc_{ioc}
//	, timer1_{ioc}
{
//	timer1_.expires_from_now(boost::posix_time::milliseconds(2500));
//	check_unclaimed_objects(boost::system::error_code{});

	init_socket(ioc_);
	init_web_socket(ioc_);
}

class ReferenceListImpl {
	std::vector<std::pair<nprpc::detail::ObjectIdLocal, ObjectServant*>> refs_;
public:
	void add_ref(ObjectServant* obj) {
		if (auto it = std::find_if(begin(refs_), end(refs_),
			[obj](auto& pair) { return pair.second == obj; }); it != end(refs_)) {
			std::cerr << "duplicate reference: " << obj->get_class() << '\n';
			return;
		}

		refs_.push_back({{obj->poa_index(), obj->oid()}, obj});
		obj->add_ref();
	}

	bool remove_ref(poa_idx_t poa_idx, oid_t oid) {
		if (auto it = std::find_if(begin(refs_), end(refs_), [poa_idx, oid](auto& pair) { 
				return pair.first.poa_idx == poa_idx && pair.first.object_id == oid; 
			}); 
			it != end(refs_)) 
		{
			auto ptr = (*it).second;
			refs_.erase(it);
			ptr->release();
			return true;
		}
		return false;
	}

	~ReferenceListImpl() {
		for (auto& ref : refs_) {
			ref.second->release();
		}
	}
};

NPRPC_API Object* create_object_from_flat(detail::flat::ObjectId_Direct oid, EndPoint remote_endpoint) {
	if (oid.object_id() == invalid_object_id) return nullptr;

	auto obj = new Object();
	obj->local_ref_cnt_ = 1;

	assert(oid.ip4());

	if (oid.flags() & (1 << static_cast<int>(detail::ObjectFlag::WebObject))) {
		// std::cerr << "remote_endpoint.port: " << remote_endpoint.port << '\n';
		obj->_data().port = remote_endpoint.port;
		obj->_data().websocket_port = remote_endpoint.port;
	} else {
		assert(oid.port());
		obj->_data().port = oid.port();
		obj->_data().websocket_port = oid.websocket_port();
	}

	obj->_data().object_id = oid.object_id();

	if (remote_endpoint.ip4 == localhost_ip4) {
		// could be the object on the same machine or from any other location
		obj->_data().ip4 = oid.ip4();
	} else {
		if (oid.ip4() == localhost_ip4) {
			// remote object has localhost ip converting to endpoint ip
			obj->_data().ip4 = remote_endpoint.ip4;
		} else {
			// remote object with ip != localhost
			obj->_data().ip4 = oid.ip4();
		}
	}

	obj->_data().poa_idx = oid.poa_idx();
	obj->_data().flags = oid.flags();
	obj->_data().class_id = (std::string_view)oid.class_id();
	obj->_data().hostname = (std::string_view)oid.hostname();

	return obj;
}

} // namespace nprpc::impl

namespace nprpc {

NPRPC_API Rpc* init(boost::asio::io_context& ioc, Config&& cfg) {
	impl::g_cfg = std::move(cfg);
	if (impl::g_orb) throw Exception("rpc has been previously initialized");
	impl::g_orb = new impl::RpcImpl(ioc);
	return impl::g_orb;
}

void Policy_Lifespan::apply_policy(Poa* poa) {
	static_cast<impl::PoaImpl*>(poa)->pl_lifespan = this->policy_;
}

uint32_t ObjectServant::release() noexcept {
	if (static_cast<impl::PoaImpl*>(poa_)->pl_lifespan == 
		Policy_Lifespan::Type::Persistent) {
		return 1;
	}

	assert(is_unused() == false);

	auto cnt = ref_cnt_.fetch_sub(1, std::memory_order_acquire) - 1;
	if (cnt == 0) {
		static_cast<impl::PoaImpl*>(poa_)->deactivate_object(object_id_);
		impl::PoaImpl::delete_object(this);
	}
	
	return cnt;
}

NPRPC_API uint32_t Object::add_ref() {
	auto const cnt = local_ref_cnt_.fetch_add(1, std::memory_order_release);
	if (policy_lifespan() == Policy_Lifespan::Persistent || cnt) return cnt + 1;

	flat_buffer buf;

	auto constexpr msg_size = sizeof(impl::Header) + sizeof(::nprpc::detail::flat::ObjectIdLocal);
	auto mb = buf.prepare(msg_size);
	buf.commit(msg_size);

	static_cast<impl::Header*>(mb.data())->size = msg_size - 4;
	static_cast<impl::Header*>(mb.data())->msg_id = impl::MessageId::AddReference;
	static_cast<impl::Header*>(mb.data())->msg_type = impl::MessageType::Request;

	::nprpc::detail::flat::ObjectIdLocal_Direct msg(buf, sizeof(impl::Header));
	msg.object_id() = this->_data().object_id;
	msg.poa_idx() = this->_data().poa_idx;

	::nprpc::impl::g_orb->call_async(
		get_endpoint(),
		std::move(buf),
		[](const boost::system::error_code& ec, flat_buffer& buf) {
			//if (!ec) {
				//auto std_reply = nprpc::impl::handle_standart_reply(buf);
				//if (std_reply == false) {
				//	std::cerr << "received an unusual reply for function with no output arguments" << std::endl;
				//}
			//}
		}
	);

	return cnt + 1;
}

NPRPC_API uint32_t Object::release() {
	auto cnt = --local_ref_cnt_;
	if (cnt != 0) return cnt;

	if (policy_lifespan() == Policy_Lifespan::Transient) {
		const auto endpoint = get_endpoint();

		if (is_web_origin() && ::nprpc::impl::g_orb->has_session(endpoint) == false) {
			// websocket session was closed.
		} else {
			flat_buffer buf;

			auto constexpr msg_size = sizeof(impl::Header) + sizeof(::nprpc::detail::flat::ObjectIdLocal);
			auto mb = buf.prepare(msg_size);
			buf.commit(msg_size);

			static_cast<impl::Header*>(mb.data())->size = msg_size - 4;
			static_cast<impl::Header*>(mb.data())->msg_id = impl::MessageId::ReleaseObject;
			static_cast<impl::Header*>(mb.data())->msg_type = impl::MessageType::Request;

			::nprpc::detail::flat::ObjectIdLocal_Direct msg(buf, sizeof(impl::Header));
			msg.object_id() = this->_data().object_id;
			msg.poa_idx() = this->_data().poa_idx;

			try {
				::nprpc::impl::g_orb->call_async(
					endpoint,
					std::move(buf),
					[](const boost::system::error_code& ec, flat_buffer& buf) {
						//if (!ec) {
							//auto std_reply = nprpc::impl::handle_standart_reply(buf);
							//if (std_reply == false) {
							//	std::cerr << "received an unusual reply for function with no output arguments" << std::endl;
							//}
						//}
					}
				);
			} catch (Exception& ex) {
				std::cerr << ex.what() << '\n';
			}
		}
	}

	delete this;

	return 0;
}

NPRPC_API uint32_t ObjectServant::add_ref() noexcept { 
	auto cnt = ref_cnt_.fetch_add(1, std::memory_order_release) + 1;

//	if (cnt == 1 && static_cast<impl::PoaImpl*>(poa())->pl_lifespan == Policy_Lifespan::Transient) {
//		std::lock_guard<std::mutex> lk(impl::g_orb->new_activated_objects_mut_);
//		auto& list = impl::g_orb->new_activated_objects_;
//		list.erase(std::find(begin(list), end(list), this));
//	}

	return cnt;
}

ReferenceList::ReferenceList() noexcept {
	impl_ = new impl::ReferenceListImpl();
}

ReferenceList::~ReferenceList() {
	delete impl_;
}

void ReferenceList::add_ref(ObjectServant* obj) {
	impl_->add_ref(obj);
}

bool ReferenceList::remove_ref(poa_idx_t poa_idx, oid_t oid) {
	return impl_->remove_ref(poa_idx, oid);
}

} // namespace nprpc

#include <nprpc/serialization/nvp.hpp>