// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <nprpc/nprpc.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/core/exchange.hpp>
#include <mutex>
#include <cassert>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <deque>

namespace nprpc::impl {

enum MessageId : uint32_t {
	FunctionCall = 0,
	BlockResponse,
	AddReference,
	ReleaseObject,
	Success,
	Exception,
	Error_PoaNotExist,
	Error_ObjectNotExist,
	Error_CommFailure,
	Error_UnknownFunctionIdx,
};

class RpcImpl;
class PoaImpl;

NPRPC_API extern RpcImpl* g_orb;

class Connection 
	: public EndPoint 
{
protected:
	void close();
public:
	virtual void send_receive(
		boost::beast::flat_buffer& buffer, 
		uint32_t timeout_ms) = 0;

	virtual void send_receive_async(
		boost::beast::flat_buffer&& buffer,
		std::function<void(const boost::system::error_code&, boost::beast::flat_buffer&)>&& completion_handler,
		uint32_t timeout_ms) = 0;

	Connection(const EndPoint& endpoint)
		: EndPoint(endpoint)
	{
	}
};

/*
class LocalSocketConnection : public Connection {
public:
	LocalSocketConnection(RpcImpl* orb, const EndPoint& endpoint);
};
*/

class SocketConnection : public Connection {
	struct work {
    virtual void operator()() noexcept = 0;
		virtual void on_failed(const boost::system::error_code& ec) noexcept = 0;
    virtual void on_executed() noexcept = 0;
		virtual boost::beast::flat_buffer& buffer() noexcept = 0;
    virtual ~work() = default;
  };

	boost::asio::deadline_timer inactive_timer_;
	boost::asio::deadline_timer timeout_timer_;
	boost::asio::ip::tcp::socket socket_;
	
	std::condition_variable cv_;
	std::mutex mut_;
  std::deque<std::unique_ptr<work>> wq_;
	uint32_t rx_size_ = 0;
	boost::posix_time::time_duration timeout_;

	void reconnect();
	void add_work(std::unique_ptr<work>&& w);
public:
	void set_timeout(uint32_t timeout_ms) {
		timeout_ = boost::posix_time::milliseconds(timeout_ms);
	}
	
	void check_timeout() noexcept;
	
	boost::beast::flat_buffer& current_rx_buffer() noexcept {
		assert(wq_.size() > 0);
		return wq_.front()->buffer();
	}

	void send_receive(boost::beast::flat_buffer& buffer, uint32_t timeout_ms) override;

	void send_receive_async(
		boost::beast::flat_buffer&& buffer,
		std::function<void(const boost::system::error_code&, boost::beast::flat_buffer&)>&& completion_handler,
		uint32_t timeout_ms) override;

	void on_read_size(const boost::system::error_code& ec, size_t len);
	void on_read_body(const boost::system::error_code& ec, size_t len);
	void do_read_size();
	void do_read_body();

	template<typename WriteHandler>
	void write_async(const boost::beast::flat_buffer& buf, WriteHandler&& handler) {
		timeout_timer_.expires_from_now(timeout_);
		socket_.async_send(buf.cdata(), std::forward<WriteHandler>(handler));
	}

	void pop_and_execute_next_task() {
		wq_.pop_front();
		if (wq_.empty() == false) (*wq_.front())();
	}

	SocketConnection(const EndPoint& endpoint, boost::asio::ip::tcp::socket&& socket);
};

class RpcImpl : public Rpc {
	static constexpr std::uint16_t invalid_port = -1;

	boost::asio::io_context& ioc_;

	std::array<PoaImpl*, 10> poas_;
	poa_idx_t poa_size_ = 0;

	std::mutex connections_mut_;
	std::vector<std::shared_ptr<Connection>> opened_connections_;

	uint16_t port_;

	boost::asio::deadline_timer timer1_;
public:
	std::mutex new_activated_objects_mut_;
	std::vector<ObjectServant*> new_activated_objects_;

	uint16_t port() const noexcept { return port_; }
	NPRPC_API std::shared_ptr<Connection> get_connection(const EndPoint& endpoint);
	NPRPC_API void call(const EndPoint& endpoint, boost::beast::flat_buffer& buffer, uint32_t timeout_ms = 2500);
	
	NPRPC_API void call_async(
		const EndPoint& endpoint, boost::beast::flat_buffer&& buffer, 
		std::function<void(const boost::system::error_code&, boost::beast::flat_buffer&)>&& completion_handler, 
		uint32_t timeout_ms = 2500);

	NPRPC_API std::optional<ObjectGuard> get_object(poa_idx_t poa_idx, oid_t oid);

	boost::asio::io_context& ioc() noexcept { return ioc_; }
	void start() override;
	void destroy() override;
	Poa* create_poa(uint32_t objects_max, std::initializer_list<Policy*> policy_list) override;
	bool close_connection(Connection* con);
	virtual ObjectPtr<Nameserver> get_nameserver(std::string_view nameserver_ip) override;
	void check_unclaimed_objects();

	Object* create_object_from_flat(nprpc::detail::flat::ObjectId_Direct oid, EndPoint remote_endpoint) {
		if (oid.object_id() == invalid_object_id) return nullptr;

		auto obj = new Object();

		assert(oid.ip4() && oid.port());

		if (remote_endpoint.ip4 == localhost_ip4) {
			// could be the object on the same mashine or from any other location
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

		obj->_data().port = oid.port();
		obj->_data().poa_idx = oid.poa_idx();
		obj->_data().object_id = oid.object_id();
		obj->_data().flags = oid.flags();
		obj->_data().class_id = (std::string_view)oid.class_id();

		return obj;

	}

	PoaImpl* get_poa(uint16_t idx) noexcept {
		assert(idx < 10);
		return poas_[idx];
	}

	RpcImpl(boost::asio::io_context& ioc, uint16_t port);
};

class ObjectGuard {
	ObjectServant* obj_;
public:
	ObjectServant* get() noexcept { 
		return !obj_->to_delete_.load() ? obj_ : nullptr; 
	}

	explicit ObjectGuard(ObjectServant* obj) noexcept
		: obj_(obj) 
	{
		++obj->in_use_cnt_;
	}

	ObjectGuard(ObjectGuard&& other) noexcept
		: obj_(boost::exchange(other.obj_, nullptr)) 
	{
	}

	~ObjectGuard() {
		if (obj_) --obj_->in_use_cnt_;
	}

	ObjectGuard(const ObjectGuard&) = delete;
	ObjectGuard& operator=(const ObjectGuard&) = delete;
};

class PoaImpl : public Poa {
	friend RpcImpl;

	template<typename T>
	class Storage {
		struct Item {
			uint32_t next;
			uint32_t gix;
			T val;
		};

		struct Val {
			union {
				uint64_t val;
				struct {
					uint32_t idx;
					uint32_t cnt;
				};
			};
		};

		const uint32_t max_size_;
		std::atomic<Val> tail_ix_;

		using Items = std::aligned_storage_t<sizeof(Item), alignof(Item)>*;
		Items items_;

		constexpr static uint32_t index(uint64_t id) noexcept {
			return id & 0xFFFF'FFFFull;
		}

		constexpr static uint32_t generation_index(uint64_t id) noexcept {
			return (id >> 32) & 0xFFFF'FFFFul;
		}

		constexpr Item& data(size_t ix) noexcept {
			return *std::launder(&reinterpret_cast<Item*>(items_)[ix]);
		}
	public:
		uint64_t add(const T& val) noexcept {
			Val old_free, new_free;
			old_free = tail_ix_.load(std::memory_order_relaxed);
			for (;;) {
				if (old_free.idx == max_size_) [[unlikely]] return -1;
				new_free.idx = data(old_free.idx).next;
				new_free.cnt = old_free.cnt + 1;
				if (tail_ix_.compare_exchange_weak(old_free, new_free, std::memory_order_acquire, std::memory_order_relaxed)) break;
			}

			const uint64_t idx = old_free.idx;
			const uint64_t gix = data(idx).gix;

			data(idx).val = val;

			return (gix << 32) | idx;
		}

		void remove(uint64_t id) noexcept {
			const uint32_t idx = index(id);
			auto& to_be_removed = data(idx);

			++to_be_removed.gix;

			Val new_free;
			new_free.idx = idx;
			Val old_free = tail_ix_.load(std::memory_order_relaxed);
			do {
				to_be_removed.next = old_free.idx;
				new_free.cnt = old_free.cnt + 1;
			} while (!tail_ix_.compare_exchange_weak(old_free, new_free, std::memory_order_relaxed, std::memory_order_relaxed));
		}


		std::remove_pointer_t<T>* get(uint64_t id) noexcept {
			auto& item = data(index(id));
			if (item.gix != generation_index(id)) return nullptr;

			if constexpr (std::is_pointer_v<T> == false) {
				return &item.val;
			} else {
				return item.val;
			}
		}

		void init() noexcept {
			for (uint32_t i = 0; i < max_size_; ++i) {
				data(i).gix = 0;
				data(i).next = i + 1;
			}
			tail_ix_ = Val{ 0 };
		}

		Storage(uint32_t max_size) noexcept
			: max_size_(max_size) {
			items_ = (Items)new char[max_size * sizeof(Item)];
			init();
		}
	};

	Storage<ObjectServant*> object_map_;

	explicit PoaImpl(uint32_t objects_max, uint16_t idx)
		: Poa(idx)
		, object_map_{ objects_max }
	{
	}
public:
	Policy_Lifespan::Type pl_lifespan = Policy_Lifespan::Type::Transient;

	std::optional<ObjectGuard> get_object(oid_t oid) noexcept {
		auto obj = object_map_.get(oid);
		if (obj) return ObjectGuard(obj);
		return std::nullopt;
	}

	virtual ObjectId activate_object(ObjectServant* obj) override {
		obj->poa_ = this;
		obj->activation_time_ = std::chrono::steady_clock::now();

		ObjectId oid;
		oid.ip4 = localhost_ip4;
		oid.port = g_orb->port();
		oid.poa_idx = get_index();
		obj->object_id_ = oid.object_id = object_map_.add(obj);
		oid.flags = (pl_lifespan << Object::Flags::Lifespan);
		oid.class_id = obj->get_class();
		
		if (pl_lifespan == Policy_Lifespan::Type::Transient) {
			std::lock_guard<std::mutex> lk(g_orb->new_activated_objects_mut_);
			g_orb->new_activated_objects_.push_back(obj);
		}

		return oid;
	}

	virtual void deactivate_object(oid_t object_id) override {
		auto obj = object_map_.get(object_id);
		if (obj) {
			obj->to_delete_.store(true);
			object_map_.remove(object_id);
		} else {
			std::cerr << "deactivate_object: object not found. id = " << object_id << '\n';
		}
	}

	static void delete_object(ObjectServant* obj) {
		if (obj->in_use_cnt_.load(std::memory_order_acquire) == 0) {
			obj->destroy();
		} else {
			boost::asio::post(impl::g_orb->ioc(), std::bind(&PoaImpl::delete_object, obj));
		}
	}
};

inline void make_simple_message(boost::beast::flat_buffer& buf, MessageId id) {
	assert(id == MessageId::Success || id == MessageId::Error_CommFailure || id == MessageId::Error_ObjectNotExist);
	buf.consume(buf.size());
	auto mb = buf.prepare(4 + 4);
	*(uint32_t*)mb.data() = 4;
	*((uint32_t*)mb.data() + 1) = id;
	buf.commit(4 + 4);
}

//  0 - Success
//  1 - exception
// -1 - not handled
inline int handle_standart_reply(boost::beast::flat_buffer& buf) {
	if (buf.size() < 8) throw ExceptionCommFailure();
	switch (*((std::uint32_t*)buf.cdata().data() + 1)) {
	case MessageId::Success:
		return 0;
	case MessageId::Exception:
		return 1;
	case MessageId::Error_ObjectNotExist:
		throw ExceptionObjectNotExist();
	case MessageId::Error_CommFailure:
		throw ExceptionCommFailure();
	case MessageId::Error_UnknownFunctionIdx:
		throw ExceptionUnknownFunctionIndex();
	default:
		return -1;
	}
}

inline void dump_message(boost::beast::flat_buffer& buffer, bool rx) {
	auto cb = buffer.cdata();
	auto size = cb.size();
	auto data = (unsigned char*)cb.data();

	std::cout << (rx ? "rx. size: " : "tx. size: ") << size << "\n";
	std::cout << std::hex;
	for (size_t i = 0; i < size; ++i) {
		std::cout << std::setfill('0') << std::setw(2) << (int)data[i];
	}
	std::cout << std::dec << std::endl;
}

struct Config {
	int debug_level;
};

inline void Connection::close() {
	impl::g_orb->close_connection(this);
}

extern Config g_cfg;

} // namespace nprpc::impl