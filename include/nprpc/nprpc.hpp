#pragma once

#include "export.hpp"

#include <array>
#include <string_view>
#include <initializer_list>
#include <stdexcept>
#include <atomic>
#include <chrono>

#include <boost/asio/io_context.hpp>
#include <boost/beast/core/flat_buffer.hpp>

#include <nprpc/nprpc_base.hpp>
#include <nprpc/basic.hpp>
#include <nprpc/buffer.hpp>
#include <nprpc/object_ptr.hpp>

namespace nprpc {

class Nameserver;

constexpr uint32_t localhost_ip4 = 0x7F'00'00'01;
constexpr uint32_t invalid_object_id = (uint32_t)-1;

class Poa;
class ObjectServant;

namespace impl {
class PoaImpl;
class RpcImpl;
}

class ObjectId : private detail::ObjectId {
	friend impl::PoaImpl;
public:
	ObjectId() {
		this->object_id = invalid_object_id;
	}

	ObjectId(const ObjectId&) = default;

	ObjectId(const detail::ObjectId& other) :
		detail::ObjectId(other) {}

	ObjectId(ObjectId&&) = default;
	
	detail::ObjectId& _data() noexcept { return static_cast<detail::ObjectId&>(*this); }
	const detail::ObjectId& _data() const noexcept { return static_cast<const detail::ObjectId&>(*this); }

	ObjectId& operator=(const detail::ObjectId& other) {
		_data() = other;
		return *this;
	}

	ObjectId& operator=(ObjectId&& other) noexcept {
		_data() = std::move(other._data());
		return *this;
	}
};

class NPRPC_API Policy {
public:
	virtual void apply_policy(Poa* poa) = 0;
	virtual ~Policy() = default;
};

class NPRPC_API Policy_Lifespan : public Policy {

public:
	enum Type { Transient, Persistent } policy_;
	
	virtual void apply_policy(Poa* poa) override;
	
	Policy_Lifespan(Type policy) 
		: policy_(policy) {}
};

class NPRPC_API Poa {
	poa_idx_t idx_;
public:
	virtual ObjectId activate_object(ObjectServant* obj) = 0;
	virtual void deactivate_object(oid_t object_id) = 0;
	poa_idx_t get_index() const noexcept { return idx_; }
	Poa(poa_idx_t idx) : idx_{ idx } {}
	virtual ~Poa() = default;
};

namespace impl {
class ObjectGuard;
}

class ObjectServant {
	friend impl::PoaImpl;
	friend impl::ObjectGuard;
	
	Poa* poa_;
	oid_t object_id_;

	std::atomic_uint32_t ref_cnt_{ 0 };
	std::atomic_uint32_t in_use_cnt_{ 0 };
	std::atomic_bool to_delete_{false};
	std::chrono::steady_clock::time_point activation_time_;
public:
	virtual std::string_view get_class() const noexcept = 0;
	virtual void dispatch(Buffers& bufs, EndPoint remote_endpoint, bool from_parent, ReferenceList& ref_list) = 0;
	virtual void destroy() noexcept { delete this; }
	Poa* poa() const noexcept { return poa_; }
	oid_t oid() const noexcept { return object_id_; }
	poa_idx_t poa_index() const noexcept { return poa_->get_index(); }
	NPRPC_API uint32_t add_ref() noexcept;
	auto activation_time() const noexcept { return activation_time_; }
	bool is_unused() const noexcept { return ref_cnt_.load() == 0; }
	uint32_t release() noexcept;
	virtual ~ObjectServant() = default;
};

class Object : public ObjectId {
	friend impl::RpcImpl;
	template<typename T>
	friend T* narrow(Object*&) noexcept;
	std::atomic_uint32_t local_ref_cnt_{ 0 };
	uint32_t timeout_ms_= 1000;
public:
	enum Flags {
		Lifespan = 0
	};

	std::string_view get_class() const noexcept { return _data().class_id; };
	
	Policy_Lifespan::Type policy_lifespan() const noexcept {
		return static_cast<Policy_Lifespan::Type>(_data().flags & (1 << Flags::Lifespan));
	}

	NPRPC_API uint32_t add_ref();
	NPRPC_API uint32_t release();
	uint32_t set_timeout(uint32_t timeout_ms) noexcept { 
		return boost::exchange(timeout_ms_, timeout_ms); 
	}
	uint32_t get_timeout() const noexcept { return timeout_ms_; }
	
	NPRPC_API virtual ~Object() = default;
protected:
	NPRPC_API Object* create_from_object_id(detail::flat::ObjectId_Direct oid);
};

class NPRPC_API Rpc {
public:
	virtual Poa* create_poa(uint32_t objects_max, std::initializer_list<Policy*> policy_list = {}) = 0;
	virtual void start() = 0;
	virtual void destroy() = 0;
	virtual ObjectPtr<Nameserver> get_nameserver(std::string_view nameserver_ip) = 0;
	virtual ~Rpc() = default;
};

NPRPC_API Rpc* init(boost::asio::io_context& ioc, uint16_t port);

template<typename T>
T* narrow(Object*& obj) noexcept {
	static_assert(std::is_base_of_v<Object, T>);
	if (obj->get_class() != T::servant_t::_get_class()) return nullptr;
	auto t = new T(0);
	static_cast<ObjectId&>(*t) = static_cast<ObjectId&&>(*obj);
	delete obj;
	obj = nullptr;
	return t;
}

inline void assign_to_out(const nprpc::ObjectId& oid, nprpc::detail::flat::ObjectId_Direct& out) {
	out.ip4() = oid._data().ip4;
	out.port() = oid._data().port;
	out.poa_idx() = oid._data().poa_idx;
	out.object_id() = oid._data().object_id;
	out.flags() = oid._data().flags;
	out.class_id(oid._data().class_id);
}

enum DebugLevel {
	DebugLevel_Critical,
	DebugLevel_InactiveTimeout,
	DebugLevel_EveryCall,
	DebugLevel_EveryMessageContent,
	DebugLevel_TraceAll,
};

NPRPC_API void set_debug_level(DebugLevel level);

} // namespace nprpc

#include <ostream>
#include <iomanip>

inline std::ostream& operator<<(std::ostream& os, const nprpc::Object& obj) {
	os << obj._data().class_id << "\n"
		"  ip4: " << std::hex << std::setw(8) << std::setfill('0') << obj._data().ip4 << std::dec << "\n"
		"  port: " << obj._data().port << "\n"
		"  poa_index: " << obj._data().poa_idx << "\n"
		"  object_ id: " << obj._data().object_id << "\n";
	return os;
}