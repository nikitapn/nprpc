#include <nprpc/nprpc_impl.hpp>
#include <nprpc/nprpc_nameserver.hpp>

extern void init_socket(boost::asio::io_context& ioc, unsigned short port);
extern void init_web_socket(boost::asio::io_context& ioc, unsigned short port);

using namespace nprpc;

namespace nprpc::impl {

void RpcImpl::check_unclaimed_objects() {
	if (timer1_.expires_at() <= boost::asio::deadline_timer::traits_type::now()) {
		std::lock_guard<std::mutex> lk(new_activated_objects_mut_);
		auto now = std::chrono::steady_clock::now();
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
	timer1_.async_wait(std::bind(&RpcImpl::check_unclaimed_objects, this));
}

Config g_cfg;
NPRPC_API RpcImpl* g_orb;

void RpcImpl::start() {
	init_socket(ioc_, port_);
	init_web_socket(ioc_, port_ + 1);
}

void RpcImpl::destroy() {
	delete this;
	g_orb = nullptr;
}

Poa* RpcImpl::create_poa(uint32_t objects_max, std::initializer_list<Policy*> policy_list) {
	auto poa = new PoaImpl(objects_max, poa_size_);
	poas_[poa_size_++] = poa;
	for (auto policy : policy_list) {
		policy->apply_policy(poa);
	}
	return poa;
}

NPRPC_API std::shared_ptr<Connection> RpcImpl::get_connection(const EndPoint& endpoint) {
	std::shared_ptr<Connection> con;
	{
		std::lock_guard<std::mutex> lk(connections_mut_);
		if (auto founded = std::find_if(opened_connections_.begin(), opened_connections_.end(),
			[&endpoint](auto const& ptr) { return static_cast<EndPoint&>(*ptr.get()) == endpoint; });
			founded != opened_connections_.end()) {
			con = (*founded);
		} else {
			if (endpoint.is_local()) {
				// not impl
			} else {
				con = std::make_shared<SocketConnection>(
					endpoint,
					boost::asio::ip::tcp::socket(boost::asio::make_strand(ioc_))
					);
			}
			opened_connections_.push_back(con);
		}
	}
	return con;
}

NPRPC_API void RpcImpl::call(
	const EndPoint& endpoint,
	boost::beast::flat_buffer& buffer,
	uint32_t timeout_ms) {
	get_connection(endpoint)->send_receive(buffer, timeout_ms);
}

NPRPC_API void RpcImpl::call_async(const EndPoint& endpoint, boost::beast::flat_buffer&& buffer,
	std::function<void(const boost::system::error_code&, boost::beast::flat_buffer&)>&& completion_handler, uint32_t timeout_ms) {
	get_connection(endpoint)->send_receive_async(std::move(buffer), std::move(completion_handler), timeout_ms);
}

NPRPC_API std::optional<ObjectGuard> RpcImpl::get_object(poa_idx_t poa_idx, oid_t object_id) {
	auto poa = g_orb->get_poa(poa_idx);
	if (!poa) return std::nullopt;
	return poa->get_object(object_id);
}

bool RpcImpl::close_connection(Connection* con) {
	std::lock_guard<std::mutex> lk(connections_mut_);
	if (auto founded = std::find_if(opened_connections_.begin(), opened_connections_.end(),
		[con](auto const& ptr) { return static_cast<EndPoint&>(*ptr.get()) == static_cast<EndPoint&>(*con); });
		founded != opened_connections_.end()) {
		opened_connections_.erase(founded);
	} else {
		std::cerr << "Error: connection was not found\n";
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
	obj->_data().flags = (Policy_Lifespan::Persistent << Object::Flags::Lifespan);
	obj->_data().class_id = INameserver_Servant::_get_class();
	obj->add_ref();
	return obj;
}

RpcImpl::RpcImpl(boost::asio::io_context& ioc, uint16_t port)
	: ioc_{ioc}
	, port_{port}
	, timer1_{ioc}
{
	timer1_.expires_from_now(boost::posix_time::milliseconds(2500));
	check_unclaimed_objects();
}

class ReferenceListImpl {
	std::vector<std::pair<nprpc::detail::ObjectIdLocal, ObjectServant*>> refs_;
public:
	void add_ref(ObjectServant* obj) {
#if defined(DEBUG) || defined(_DEBUG) 
		if (auto it = std::find_if(begin(refs_), end(refs_),
			[obj](auto& pair) { return pair.second == obj; }); it != end(refs_)) {
			assert(false);
		}
#endif
		refs_.push_back({{obj->poa_index() ,obj->oid(),}, obj});
		obj->add_ref();
	}

	bool remove_ref(poa_idx_t poa_idx, oid_t oid) {
		if (auto it = std::find_if(begin(refs_), end(refs_), 
			[poa_idx, oid](auto& pair) { return pair.first.poa_idx == poa_idx && pair.first.object_id == oid; }); it != end(refs_)) 
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

} // namespace nprpc::impl

namespace nprpc {

NPRPC_API Rpc* init(boost::asio::io_context& ioc, uint16_t port) {
	if (impl::g_orb) throw Exception("rpc has been previously initialized");
	impl::g_orb = new impl::RpcImpl(ioc, port);
	return impl::g_orb;
}

void Policy_Lifespan::apply_policy(Poa* poa) {
	static_cast<impl::PoaImpl*>(poa)->pl_lifespan = this->policy_;
}


NPRPC_API void set_debug_level(DebugLevel level) {
	impl::g_cfg.debug_level = level;
}

uint32_t ObjectServant::release() noexcept {
	if (static_cast<impl::PoaImpl*>(poa_)->pl_lifespan == 
		Policy_Lifespan::Type::Persistent) {
		return 1;
	}

	assert(is_unused() == false);

	auto cnt = ref_cnt_.fetch_add(-1, std::memory_order_acquire) - 1;
	if (cnt == 0) {
		static_cast<impl::PoaImpl*>(poa_)->deactivate_object(object_id_);
		impl::PoaImpl::delete_object(this);
	}
	
	return cnt;
}

NPRPC_API uint32_t Object::add_ref() {
	auto const cnt = local_ref_cnt_.fetch_add(1, std::memory_order_release);
	if (policy_lifespan() == Policy_Lifespan::Persistent || cnt) return cnt + 1;

	boost::beast::flat_buffer buf;

	auto constexpr msg_size = 4 + 4 + sizeof(nprpc::detail::flat::ObjectIdLocal);
	auto mb = buf.prepare(msg_size);
	buf.commit(msg_size);

	*((uint32_t*)mb.data()) = msg_size - 4;
	*((uint32_t*)mb.data() + 1) = ::nprpc::impl::MessageId::AddReference;

	::nprpc::detail::flat::ObjectIdLocal_Direct msg(buf, 4 + 4);
	msg.object_id() = this->_data().object_id;
	msg.poa_idx() = this->_data().poa_idx;

	::nprpc::impl::g_orb->call_async(
		nprpc::EndPoint(this->_data().ip4, this->_data().port), 
		std::move(buf),
		[](const boost::system::error_code& ec, boost::beast::flat_buffer& buf) {
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
		boost::beast::flat_buffer buf;

		auto constexpr msg_size = 4 + 4 + sizeof(nprpc::detail::flat::ObjectIdLocal);
		auto mb = buf.prepare(msg_size);
		buf.commit(msg_size);

		*((uint32_t*)mb.data()) = msg_size - 4;
		*((uint32_t*)mb.data() + 1) = ::nprpc::impl::MessageId::ReleaseObject;

		::nprpc::detail::flat::ObjectIdLocal_Direct msg(buf, 4 + 4);
		msg.object_id() = this->_data().object_id;
		msg.poa_idx() = this->_data().poa_idx;

		::nprpc::impl::g_orb->call_async(
			nprpc::EndPoint(this->_data().ip4, this->_data().port),
			std::move(buf),
			[](const boost::system::error_code& ec, boost::beast::flat_buffer& buf) {
				//if (!ec) {
					//auto std_reply = nprpc::impl::handle_standart_reply(buf);
					//if (std_reply == false) {
					//	std::cerr << "received an unusual reply for function with no output arguments" << std::endl;
					//}
				//}
			}
		);
	}

	delete this;

	return 0;
}

NPRPC_API uint32_t ObjectServant::add_ref() noexcept { 
	auto cnt = ref_cnt_.fetch_add(1, std::memory_order_release) + 1;

	if (cnt == 1 && static_cast<impl::PoaImpl*>(poa())->pl_lifespan == Policy_Lifespan::Transient) {
		std::lock_guard<std::mutex> lk(impl::g_orb->new_activated_objects_mut_);
		auto& list = impl::g_orb->new_activated_objects_;
		list.erase(std::find(begin(list), end(list), this));
	}

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

NPRPC_API Object* Object::create_from_object_id(detail::flat::ObjectId_Direct oid) {
		if (oid.object_id() == invalid_object_id) return nullptr;

		auto obj = new Object();
		assert(oid.ip4() && oid.port());
		
		if (this->_data().ip4 == oid.ip4()) {
			obj->_data().ip4 = oid.ip4();
		} else if (oid.ip4() == localhost_ip4) {
			obj->_data().ip4 = this->_data().ip4;
		} else {
			obj->_data().ip4 = oid.ip4();
		}
		
		obj->_data().port = oid.port();
		obj->_data().poa_idx = oid.poa_idx();
		obj->_data().object_id = oid.object_id();
		obj->_data().flags = oid.flags();
		obj->_data().class_id = (std::string_view)oid.class_id();

		return obj;
	}

} // namespace nprpc

