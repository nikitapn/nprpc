cat > /tmp/test_compat.cpp << 'EOF'
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/async_result.hpp>
#include <exception>

// Minimal nprpc::Task<void> mock
namespace nprpc {
template<typename T = void>
class Task {
public:
  using completion_fn = std::function<void(std::exception_ptr)>;
  void set_completion_handler(completion_fn fn) { fn(nullptr); }
  bool done() const { return true; }
};
}

// Our adapter: call async_initiate with void(exception_ptr) signature
template<typename T>
auto as_awaitable(nprpc::Task<T> task) {
  auto shared = std::make_shared<nprpc::Task<T>>(std::move(task));
  return boost::asio::async_initiate<
      const boost::asio::use_awaitable_t<>&,
      void(std::exception_ptr)
  >(
    [shared](auto handler) mutable {
      auto ex = boost::asio::get_associated_executor(handler);
      shared->set_completion_handler(
        [shared, handler = std::move(handler), ex](std::exception_ptr ep) mutable {
          boost::asio::post(ex, [shared = std::move(shared), handler = std::move(handler), ep]() mutable {
            shared.reset();
            std::move(handler)(ep);
          });
        }
      );
    },
    boost::asio::use_awaitable
  );
}

boost::asio::awaitable<void> test_coroutine() {
  co_await as_awaitable(nprpc::Task<void>{});
}

int main() {
  boost::asio::io_context ioc;
  boost::asio::co_spawn(ioc, test_coroutine(), boost::asio::detached);
  ioc.run();
  return 0;
}
EOF
exec g++ -std=c++23 -o /tmp/test_compat /tmp/test_compat.cpp -I/usr/include/boost