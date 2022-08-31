// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once


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

#include <nprpc/nprpc.hpp>
#include <nprpc/impl/session.hpp>

namespace nprpc::impl {

extern Config g_cfg;

class RpcImpl;
class PoaImpl;

NPRPC_API extern RpcImpl* g_orb;


/*
class LocalSocketConnection : public Connection {
public:
	LocalSocketConnection(RpcImpl* orb, const EndPoint& endpoint);
};
*/

class SocketConnection : public Session {
	boost::asio::ip::tcp::socket socket_;
	uint32_t rx_size_ = 0;

	struct work {
    virtual void operator()() = 0;
		virtual void on_failed(const boost::system::error_code& ec) noexcept = 0;
    virtual void on_executed() noexcept = 0;
		virtual flat_buffer& buffer() noexcept = 0;
    virtual ~work() = default;
  };
	std::deque<std::unique_ptr<work>> wq_;
	void add_work(std::unique_ptr<work>&& w);
	void reconnect();

	flat_buffer& current_rx_buffer() noexcept {
		assert(wq_.size() > 0);
		return wq_.front()->buffer();
	}

	void pop_and_execute_next_task() {
		wq_.pop_front();
		if (wq_.empty() == false) (*wq_.front())();
	}
protected:
	virtual void timeout_action() final {
		boost::system::error_code ec;
		socket_.cancel(ec);
		if (ec) fail(ec, "socket::cancel()");
	}
public:
	void send_receive(flat_buffer& buffer, uint32_t timeout_ms) override;

	void send_receive_async(
		flat_buffer&& buffer,
		std::function<void(const boost::system::error_code&, flat_buffer&)>&& completion_handler,
		uint32_t timeout_ms) override;

	void on_read_size(const boost::system::error_code& ec, size_t len);
	void on_read_body(const boost::system::error_code& ec, size_t len);
	void do_read_size();
	void do_read_body();

	template<typename WriteHandler>
	void write_async(const flat_buffer& buf, WriteHandler&& handler) {
		timeout_timer_.expires_from_now(timeout_);
		socket_.async_send(buf.cdata(), std::forward<WriteHandler>(handler));
	}

	SocketConnection(const EndPoint& endpoint, boost::asio::ip::tcp::socket&& socket);
};

class RpcImpl : public Rpc {
	static constexpr std::uint16_t invalid_port = -1;

	boost::asio::io_context& ioc_;

	std::array<std::unique_ptr<PoaImpl>, 10> poas_;
	std::atomic<poa_idx_t> poa_size_ = 0;

	mutable std::mutex connections_mut_;
	std::vector<std::shared_ptr<Session>> opened_sessions_;

	//boost::asio::deadline_timer timer1_;
public:
	//std::mutex new_activated_objects_mut_;
	//std::vector<ObjectServant*> new_activated_objects_;

	uint16_t port() const noexcept { return g_cfg.port; }
	uint16_t websocket_port() const noexcept { return g_cfg.websocket_port; }

	void add_connection(std::shared_ptr<Session>&& session) {
		opened_sessions_.push_back(std::move(session));
	}

	bool has_session(const EndPoint& endpoint) const noexcept;
	NPRPC_API std::shared_ptr<Session> get_session(const EndPoint& endpoint);
	NPRPC_API void call(const EndPoint& endpoint, flat_buffer& buffer, uint32_t timeout_ms = 2500);
	
	NPRPC_API void call_async(
		const EndPoint& endpoint, flat_buffer&& buffer, 
		std::function<void(const boost::system::error_code&, flat_buffer&)>&& completion_handler, 
		uint32_t timeout_ms = 2500);

	NPRPC_API std::optional<ObjectGuard> get_object(poa_idx_t poa_idx, oid_t oid);

	boost::asio::io_context& ioc() noexcept { return ioc_; }
	//void start() override;
	void destroy() override;
	Poa* create_poa(uint32_t objects_max, std::initializer_list<Policy*> policy_list) override;
	bool close_session(Session* con);
	virtual ObjectPtr<Nameserver> get_nameserver(std::string_view nameserver_ip) override;
//	void check_unclaimed_objects(boost::system::error_code ec);

	PoaImpl* get_poa(uint16_t idx) noexcept {
		assert(idx < 10);
		return poas_[idx].get();
	}

	RpcImpl(boost::asio::io_context& ioc);
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
				if (old_free.idx == max_size_) [[unlikely]] return (uint64_t)(-1);
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
			new_free.val = 0ull;
			new_free.idx = idx;
			Val old_free = tail_ix_.load(std::memory_order_relaxed);
			do {
				to_be_removed.next = old_free.idx;
				new_free.cnt = old_free.cnt + 1;
			} while (!tail_ix_.compare_exchange_weak(old_free, new_free, std::memory_order_relaxed, std::memory_order_relaxed));
		}


		std::remove_pointer_t<T>* get(uint64_t id) noexcept {
			const auto idx = index(id);
			if (idx >= max_size_) return nullptr;
			auto& item = data(idx);
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
				if constexpr (std::is_pointer_v<T>) {
					data(i).val = nullptr;
				}
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

	virtual ~PoaImpl() {
		if (pl_lifespan == Policy_Lifespan::Type::Persistent) return;

		// remove references
	}

	std::optional<ObjectGuard> get_object(oid_t oid) noexcept {
		auto obj = object_map_.get(oid);
		if (obj) return ObjectGuard(obj);
		return std::nullopt;
	}

	virtual ObjectId activate_object(ObjectServant* obj, SessionContext* ctx, bool session_specific) override {
		obj->poa_ = this;
		obj->activation_time_ = std::chrono::system_clock::now();

		ObjectId oid;
		auto object_id_internal = object_map_.add(obj);
		if (object_id_internal == (uint64_t)(-1)) throw Exception("Poa fixed size has been exceeded");
		obj->object_id_ = oid.object_id = object_id_internal;

		oid.ip4 = localhost_ip4;
		oid.port = g_orb->port();
		oid.websocket_port = g_orb->websocket_port();
		
		oid.poa_idx = get_index();
		
		oid.flags = 
			(pl_lifespan << static_cast<int>(detail::ObjectFlag::Policy_Lifespan)) |
			(g_cfg.use_ssl << static_cast<int>(detail::ObjectFlag::Secured));
		
		oid.class_id = obj->get_class();
		oid.hostname = g_cfg.hostname;

		if (pl_lifespan == Policy_Lifespan::Type::Transient) {
			// std::lock_guard<std::mutex> lk(g_orb->new_activated_objects_mut_);
			// g_orb->new_activated_objects_.push_back(obj);
			if (!ctx) throw std::runtime_error("Object created with transient policy requires session context for activation");

			ctx->ref_list.add_ref(obj);
		}

		if (session_specific) {
			obj->session_ctx_ = ctx;
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

inline void make_simple_answer(flat_buffer& buf, MessageId id) {
	assert(id == MessageId::Success 
		|| id == MessageId::Error_ObjectNotExist
		|| id == MessageId::Error_CommFailure 
		|| id == MessageId::Error_UnknownFunctionIdx
		|| id == MessageId::Error_UnknownMessageId
		|| id == MessageId::Error_BadAccess
		|| id == MessageId::Error_BadInput
	);
	buf.consume(buf.size());
	auto mb = buf.prepare(sizeof(impl::Header));
	static_cast<impl::flat::Header*>(mb.data())->size = sizeof(impl::flat::Header) - 4;
	static_cast<impl::flat::Header*>(mb.data())->msg_id = id;
	static_cast<impl::flat::Header*>(mb.data())->msg_type = impl::MessageType::Answer;
	buf.commit(sizeof(impl::flat::Header));
}

inline void dump_message(flat_buffer& buffer, bool rx) {
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

//  0 - Success
//  1 - exception
// -1 - not handled
inline int handle_standart_reply(flat_buffer& buf) {
	if (buf.size() < sizeof(impl::Header)) throw ExceptionCommFailure();
	auto header = static_cast<const impl::Header*>(buf.cdata().data());
	assert(header->size == buf.size() - 4);
	switch (header->msg_id) {
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
	case MessageId::Error_UnknownMessageId:
		throw ExceptionUnknownMessageId();
	case MessageId::Error_BadAccess:
		throw ExceptionBadAccess();
	case MessageId::Error_BadInput:
		throw ExceptionBadInput();
	default:
		return -1;
	}
}

inline void Session::close() {
	closed_ = true;
	impl::g_orb->close_session(this);
}

} // namespace nprpc::impl