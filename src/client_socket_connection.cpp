// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/asio.hpp>
#include <iostream>
#include <future>

namespace nprpc::impl {

void SocketConnection::send_receive(flat_buffer& buffer, uint32_t timeout_ms) {
	assert(*(uint32_t*)buffer.data().data() == buffer.size() - 4);

	if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
		dump_message(buffer, false);
	}

	struct work_impl : work {
		flat_buffer& buf;
		SocketConnection& this_;
		uint32_t timeout_ms;

		std::promise<boost::system::error_code> promise;

		void operator()() noexcept override {
			this_.set_timeout(timeout_ms);
			this_.write_async(buf, [&](const boost::system::error_code& ec, size_t bytes_transferred) {
				boost::ignore_unused(bytes_transferred);
				if (ec) {
					on_failed(ec);
					this_.pop_and_execute_next_task();
					return;
				}
				this_.do_read_size();
				});
		}

		void on_failed(const boost::system::error_code& ec) noexcept override {
			promise.set_value(ec);
		}

		void on_executed() noexcept override {
			promise.set_value({});
		}

		flat_buffer& buffer() noexcept override { return buf; };
		std::future<boost::system::error_code> get_future() { return promise.get_future(); }

		work_impl(flat_buffer& _buf, SocketConnection& _this_, uint32_t _timeout_ms)
			: buf(_buf)
			, this_(_this_)
			, timeout_ms(_timeout_ms)
		{
		}
	};

	auto post_work_and_wait = [&]() -> boost::system::error_code {
		auto w = std::make_unique<work_impl>(buffer, *this, timeout_ms);
		auto fut = w->get_future();
		add_work(std::move(w));
		return fut.get();
	};

	auto ec = post_work_and_wait();
	
	if (!ec) {
		if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
			dump_message(buffer, true);
		}
		return;
	}

	if (ec == boost::asio::error::connection_reset || ec == boost::asio::error::broken_pipe) {
		reconnect();
		auto ec = post_work_and_wait();
		if (ec) close();
	} else {
		fail(ec, "send_receive");
		close();
		throw nprpc::ExceptionCommFailure();
	}
}

void SocketConnection::send_receive_async(
	flat_buffer&& buffer,
	std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>>&& completion_handler,
	uint32_t timeout_ms
) {
	assert(*(uint32_t*)buffer.data().data() == buffer.size() - 4);

	struct work_impl : work {
		flat_buffer buf;
		SocketConnection& this_;
		uint32_t timeout_ms;
		std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>> handler;
		
		void operator()() noexcept override {
			this_.set_timeout(timeout_ms);
			this_.write_async(buf, [&](const boost::system::error_code& ec, size_t bytes_transferred) {
				boost::ignore_unused(bytes_transferred);
				if (ec) {
					on_failed(ec);
					this_.pop_and_execute_next_task();
					return;
				}
				this_.do_read_size();
				});
		}

		void on_failed(const boost::system::error_code& ec) noexcept override {
			if (handler) handler.value()(ec, buf);
		}

		void on_executed() noexcept override {
			if (handler) handler.value()(boost::system::error_code{}, buf);
		}

		flat_buffer& buffer() noexcept override { return buf; };

		work_impl(flat_buffer&& _buf, 
			SocketConnection& _this_, 
			std::optional<std::function<void(const boost::system::error_code&, flat_buffer&)>>&& _handler,
			uint32_t _timeout_ms)
			: buf(std::move(_buf))
			, this_(_this_)
			, timeout_ms(_timeout_ms)
			, handler(std::move(_handler))
		{
		}
	};

	add_work(std::make_unique<work_impl>(std::move(buffer), *this, std::move(completion_handler), timeout_ms));
}

void SocketConnection::reconnect() {
	socket_ = std::move(net::ip::tcp::socket(socket_.get_executor()));

	boost::system::error_code ec;
	socket_.connect(tcp::endpoint(net::ip::address_v4(this->ctx_.remote_endpoint.ip4), this->ctx_.remote_endpoint.port), ec);

	if (ec) {
		close();
		throw nprpc::ExceptionCommFailure();
	}
}

void SocketConnection::do_read_size() {
	auto& buf = current_rx_buffer();
	buf.consume(buf.size());
	buf.prepare(4);

	timeout_timer_.expires_from_now(timeout_);
	socket_.async_read_some(net::mutable_buffer(&rx_size_, 4),
		std::bind(&SocketConnection::on_read_size, this, std::placeholders::_1, std::placeholders::_2)
	);
}

void SocketConnection::do_read_body() {
	timeout_timer_.expires_from_now(timeout_);
	socket_.async_read_some(current_rx_buffer().prepare(rx_size_),
		std::bind(&SocketConnection::on_read_body, this,
			std::placeholders::_1, std::placeholders::_2)
	);
}

void SocketConnection::on_read_size(const boost::system::error_code& ec, size_t len) {
	timeout_timer_.expires_at(boost::posix_time::pos_infin);
	
	if (ec) {
		fail(ec, "read");
		(*wq_.front()).on_failed(ec);
		pop_and_execute_next_task();
		return;
	}

	assert(len == 4);

	if (rx_size_ > max_message_size) {
		fail(boost::asio::error::no_buffer_space, "rx_size_ > max_message_size");
		(*wq_.front()).on_failed(ec);
		pop_and_execute_next_task();
		return;
	}

	auto& buf = current_rx_buffer();
	*static_cast<uint32_t*>(buf.data().data()) = rx_size_;
	buf.commit(4);

	do_read_body();
}

void SocketConnection::on_read_body(const boost::system::error_code& ec, size_t len) {
	timeout_timer_.expires_at(boost::posix_time::pos_infin);

	if (ec) {
		fail(ec, "read");
		(*wq_.front()).on_failed(ec);
		pop_and_execute_next_task();
		return;
	}

	auto& buf = current_rx_buffer();

	buf.commit(len);
	rx_size_ -= static_cast<uint32_t>(len);

	if (rx_size_ != 0) {
		do_read_body();
	} else {
		(*wq_.front()).on_executed();
		pop_and_execute_next_task();
	}
}

void SocketConnection::add_work(std::unique_ptr<work>&& w) {
	boost::asio::post(socket_.get_executor(), [w{std::move(w)}, this]() mutable {
		wq_.push_back(std::move(w));
		if (wq_.size() == 1) (*wq_.front())();
	});
}

SocketConnection::SocketConnection(const EndPoint& endpoint, boost::asio::ip::tcp::socket&& socket)
	: Session(socket.get_executor())
	, socket_{std::move(socket)}
{
	ctx_.remote_endpoint = endpoint;
	timeout_timer_.expires_at(boost::posix_time::pos_infin);
	boost::system::error_code ec;
	socket_.connect(tcp::endpoint(net::ip::address_v4(ctx_.remote_endpoint.ip4), ctx_.remote_endpoint.port), ec);
	if (ec) throw nprpc::Exception(("Could not connect to the socket: " + ec.message()).c_str());
	check_timeout();
}

} // namespace nprpc::impl