// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#include <nprpc/impl/websocket_session.hpp>

#include <nprpc/impl/nprpc_impl.hpp>

#include <future>

namespace nprpc::impl {

void fail(beast::error_code ec, char const *what) {
  // ssl::error::stream_truncated, also known as an SSL "short read",
  // indicates the peer closed the connection without performing the
  // required closing handshake (for example, Google does this to
  // improve performance). Generally this can be a security issue,
  // but if your communication protocol is self-terminated (as
  // it is with both HTTP and WebSocket) then you may simply
  // ignore the lack of close_notify.
  //
  // https://github.com/boostorg/beast/issues/38
  //
  // https://security.stackexchange.com/questions/91435/how-to-handle-a-malicious-ssl-tls-shutdown
  //
  // When a short read would cut off the end of an HTTP message,
  // Beast returns the error beast::http::error::partial_message.
  // Therefore, if we see a short read here, it has occurred
  // after the message has been completed, so it is safe to ignore it.

  if (ec == net::ssl::error::stream_truncated || ec == beast::error::timeout)
    return;

  // Do not report these
  if (ec == beast::error::timeout)
    return;

  std::cerr << what << ": " << ec.message() << '\n';
}

template <class Derived>
void WebSocketSession<Derived>::do_read() {
  reading_ = true;
  derived().ws().async_read(
      rx_buffer_(),
      beast::bind_front_handler(
          &WebSocketSession::on_read,
          derived().shared_from_this()));
}

template <class Derived>
void WebSocketSession<Derived>::on_read(
  beast::error_code ec, std::size_t bytes_transferred)
{
  boost::ignore_unused(bytes_transferred);

  reading_ = false;

  // This indicates that the WebSocketSession was closed
  if (ec == websocket::error::closed) {
    close();
    return;
  }

  if (ec) {
    close();
    return fail(ec, "read");
  }

  nprpc::impl::flat::Header_Direct header(rx_buffer_(), 0);

  if (header.msg_type() == nprpc::impl::MessageType::Request) {
    handle_request();

    struct work_send_reply : work {
      Derived &self_;
      flat_buffer buffer_;

      work_send_reply(Derived &self, flat_buffer &&buffer)
        : self_(self), buffer_(std::move(buffer)) {
      }

      virtual void operator()() final {
        self_.ws().text(false); // binary mode only
        self_.writing_ = true;
        self_.ws().async_write(
            buffer_.data(),
            std::bind(
                &WebSocketSession::on_write,
                self_.shared_from_this(),
                std::placeholders::_1,
                std::placeholders::_2,
                true));
      }
      virtual void on_failed(const boost::system::error_code&) noexcept {}
      virtual void on_executed(flat_buffer &&) noexcept {}
    };

    answers_.emplace_back(std::make_unique<work_send_reply>(derived(), std::move(rx_buffer_(true))));

    if (!writing_) (*answers_.front())();
  } else { // received an answer
    request_sended_waiting_for_answer_ = false;
    assert(!requests_.empty());
    if (!ec) {
      requests_.front()->on_executed(rx_buffer_(true));
    } else {
      requests_.front()->on_failed(ec);
    }
    requests_.pop_front();
    if (requests_.size()) (*requests_.front())();
    else
      do_read();
  }
}

template <class Derived>
void WebSocketSession<Derived>::on_write(beast::error_code ec, std::size_t bytes_transferred, bool is_answer) {
  boost::ignore_unused(bytes_transferred);
  writing_ = false;

  if (ec == websocket::error::closed) {
    close();
    return;
  }

  if (ec) {
    close();
    return fail(ec, "write");
  }

  if (is_answer) { // answer sended
    assert(answers_.size() >= 1);
    answers_.pop_front();
    if (answers_.empty() == false) return (*answers_.front())();
    if (requests_.empty() == false && request_sended_waiting_for_answer_ == false) return (*requests_.front())();
    return do_read();
  } else {
    request_sended_waiting_for_answer_ = true;
    assert(requests_.size() >= 1);
    if (reading_ == false) return do_read(); // read an answer to the request
  }
}

template <class Derived>
void WebSocketSession<Derived>::add_request(std::shared_ptr<work> w) {
  boost::asio::post(derived().ws().get_executor(), [w, this]() mutable {
    requests_.push_back(std::move(w));
    if (requests_.size() == 1 && answers_.empty() && !writing_)
      (*requests_.front())();
  });
}

template <class Derived>
void WebSocketSession<Derived>::close()
{
  const boost::system::error_code ec{boost::asio::error::connection_aborted};
  for (auto &r : requests_) {
    r->on_failed(ec);
  }
  requests_.clear();
  answers_.clear();
  Session::close();
}

template <class Derived>
void WebSocketSession<Derived>::timeout_action() 
{

}

template <class Derived>
void WebSocketSession<Derived>::send_receive(
  flat_buffer &buffer,
  uint32_t timeout_ms)
{
  assert(*(uint32_t *)buffer.data().data() == buffer.size() - 4);

  if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
    dump_message(buffer, false);
  }

  struct work_impl : work {
    Derived &this_;
    flat_buffer &buffer_;
    uint32_t timeout_ms;

    std::promise<boost::system::error_code> promise;

    void operator()() noexcept override {
      // this_.set_timeout(timeout_ms);
      this_.ws().text(false); // binary mode only
      this_.writing_ = true;
      this_.ws().async_write(
          buffer_.data(),
          std::bind(
              &WebSocketSession::on_write,
              this_.shared_from_this(),
              std::placeholders::_1,
              std::placeholders::_2,
              false));
    }

    void on_failed(const boost::system::error_code &ec) noexcept override {
      promise.set_value(ec);
    }

    void on_executed(flat_buffer &&buffer) noexcept override {
      buffer_ = std::move(buffer);
      promise.set_value({});
    }

    std::future<boost::system::error_code> get_future() { return promise.get_future(); }

    work_impl(Derived &_this_, flat_buffer &_buf, uint32_t _timeout_ms)
      : this_(_this_)
      , buffer_(_buf)
      , timeout_ms(_timeout_ms) 
    {
    }
  };

  auto w = std::make_shared<work_impl>(derived(), buffer, timeout_ms);
  auto f = w->get_future();

  add_request(w);

  auto ec = f.get();

  if (!ec) {
    if (g_cfg.debug_level >= DebugLevel::DebugLevel_EveryMessageContent) {
      dump_message(buffer, true);
    }
  } else {
    throw nprpc::ExceptionCommFailure();
  }
}

template <class Derived>
void WebSocketSession<Derived>::send_receive_async(
  flat_buffer &&buffer,
  std::optional<std::function<void(const boost::system::error_code &, flat_buffer &)>> &&completion_handler,
  uint32_t timeout_ms)
{
  assert(*(uint32_t *)buffer.data().data() == buffer.size() - 4);

  struct work_impl : work {
    flat_buffer buffer_;
    Derived &this_;
    uint32_t timeout_ms;
    std::optional<std::function<void(const boost::system::error_code &, flat_buffer &)>> handler;

    void operator()() noexcept override {
      // this_.set_timeout(timeout_ms);
      this_.ws().text(false); // binary mode only
      this_.writing_ = true;
      this_.ws().async_write(
          buffer_.data(),
          std::bind(
              &WebSocketSession::on_write,
              this_.shared_from_this(),
              std::placeholders::_1,
              std::placeholders::_2,
              false));
    }

    void on_failed(const boost::system::error_code &ec) noexcept override {
      if (handler) (*handler)(ec, buffer_);
    }

    void on_executed(flat_buffer &&buffer) noexcept override {
      buffer_ = std::move(buffer);
      if (handler) (*handler)(boost::system::error_code{}, buffer_);
    }

    work_impl(flat_buffer &&_buf,
              Derived &_this_,
              std::optional<std::function<void(const boost::system::error_code &, flat_buffer &)>> &&_handler,
              uint32_t _timeout_ms)
      : buffer_(std::move(_buf)), this_(_this_), timeout_ms(_timeout_ms), handler(std::move(_handler)) {
    }
  };

  add_request(std::make_shared<work_impl>(std::move(buffer), derived(), std::move(completion_handler), timeout_ms));
}

// Explicit instantiation for the WebSocketSession class template
template class WebSocketSession<AcceptingPlainWebSocketSession>;
template class WebSocketSession<AcceptingSSLWebSocketSession>;

template class WebSocketSession<ClientPlainWebSocketSession>;
template class WebSocketSession<ClientSSLWebSocketSession>;
} // namespace nprpc::impl