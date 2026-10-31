#ifdef NDEBUG
#undef NDEBUG
#endif
#include <nprpc/stream_writer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/thread_pool.hpp>
#include <atomic>
#include <cassert>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace std::chrono_literals;

nprpc::StreamWriter<uint8_t> values() {
  for (;;) co_yield uint8_t{1};
}

std::atomic<int> live_producers{0};

nprpc::StreamWriter<uint8_t> counted_values() {
  struct Guard {
    Guard() { ++live_producers; }
    ~Guard() { --live_producers; }
  } guard;
  for (;;) co_yield uint8_t{1};
}

void wait_for_producers_to_end() {
  for (int i = 0; i < 500 && live_producers != 0; ++i)
    std::this_thread::sleep_for(10ms);
  assert(live_producers == 0);
}

// Runs until `done`, however long a loaded machine takes to get there.
template <typename Pred>
void run_until(boost::asio::io_context& io, Pred done) {
  const auto deadline = std::chrono::steady_clock::now() + 30s;
  while (!done() && std::chrono::steady_clock::now() < deadline) {
    io.restart();
    io.run_for(5ms);
  }
  assert(done());
}

int main() {
  boost::asio::io_context io;
  nprpc::SessionContext context;
  auto manager = std::make_shared<nprpc::impl::StreamManager>(context, nullptr, io.get_executor());
  std::vector<std::chrono::steady_clock::time_point> sent;
  manager->set_send_callback([&](nprpc::flat_buffer&&) {
    sent.push_back(std::chrono::steady_clock::now()); return true;
  });
  auto writer = std::make_unique<nprpc::StreamWriter<uint8_t>>(values());
  writer->set_manager(manager.get(), 42);
  writer->set_interval(10ms);
  manager->register_stream(42, std::move(writer), false, 1);
  manager->defer_stream_start(42);
  manager->on_reply_sent();
  run_until(io, [&] { return sent.size() == 1; });
  io.restart(); io.run_for(35ms);
  assert(sent.size() == 1); // no production without credits
  manager->on_window_update(42, 3);
  run_until(io, [&] { return sent.size() == 4; });
  io.restart(); io.run_for(35ms);
  assert(sent.size() == 4);
  for (size_t i = 1; i < sent.size(); ++i)
    assert(sent[i] - sent[i-1] >= 9ms);
  manager->on_stream_cancel(42);
  io.restart(); io.poll();

  // Cancelling a long sampling wait must release the coroutine immediately.
  auto slow = std::make_unique<nprpc::StreamWriter<uint8_t>>(values());
  slow->set_manager(manager.get(), 43);
  slow->set_interval(5s);
  manager->register_stream(43, std::move(slow));
  manager->defer_stream_start(43);
  manager->on_reply_sent();
  io.restart(); io.poll();
  manager->on_stream_cancel(43);
  std::weak_ptr weak = manager;
  manager.reset();
  run_until(io, [&] { return weak.expired(); });

  // A peer that disappears sends no cancel; the session's stop_pumps() has to
  // release a writer parked on credits, or the manager is never freed.
  {
    boost::asio::io_context parked_io;
    nprpc::SessionContext parked_context;
    auto parked = std::make_shared<nprpc::impl::StreamManager>(parked_context, nullptr, parked_io.get_executor());
    parked->set_send_callback([](nprpc::flat_buffer&&) { return true; });
    auto writer = std::make_unique<nprpc::StreamWriter<uint8_t>>(counted_values());
    writer->set_manager(parked.get(), 44);
    parked->register_stream(44, std::move(writer), false, 1);
    parked->defer_stream_start(44);
    parked->on_reply_sent();
    parked_io.run_for(20ms);
    assert(live_producers == 1);
    std::weak_ptr weak_parked = parked;
    parked->stop_pumps();
    parked.reset();
    run_until(parked_io, [&] { return weak_parked.expired(); });
    assert(live_producers == 0);
  }

  // Every way of ending a stream takes it off the gauge.
  {
    boost::asio::io_context gauge_io;
    nprpc::SessionContext gauge_context;
    auto gauged = std::make_shared<nprpc::impl::StreamManager>(gauge_context, nullptr, gauge_io.get_executor());
    std::atomic<int64_t> streams{0};
    gauged->set_active_stream_counter(&streams);
    gauged->set_send_callback([](nprpc::flat_buffer&&) { return true; });
    const auto add_writer = [&](uint64_t id) {
      auto writer = std::make_unique<nprpc::StreamWriter<uint8_t>>(values());
      writer->set_manager(gauged.get(), id);
      gauged->register_stream(id, std::move(writer));
    };
    add_writer(1);
    add_writer(2);
    gauged->register_external_writer(3);
    assert(streams == 3);
    gauged->on_stream_cancel(1);
    assert(streams == 2);
    gauged->cancel_all();
    assert(streams == 0);
  }

  // Delivery follows the stream ID's tag.
  {
    using Manager = nprpc::impl::StreamManager;
    boost::asio::io_context tag_io;
    nprpc::SessionContext tag_context;
    Manager tagged(tag_context, nullptr, tag_io.get_executor());
    int reliable_sends = 0, datagram_sends = 0;
    tagged.set_send_callback([&](nprpc::flat_buffer&&) { ++reliable_sends; return true; });
    tagged.set_send_datagram_callback([&](nprpc::flat_buffer&&) { ++datagram_sends; return true; });
    const auto reliable_id = Manager::generate_stream_id();
    const auto unreliable_id = Manager::generate_stream_id(true);
    assert(!Manager::is_unreliable_stream_id(reliable_id));
    assert(Manager::is_unreliable_stream_id(unreliable_id));
    assert(Manager::generate_stream_id() != reliable_id);

    tagged.defer_stream_start(reliable_id);
    tagged.defer_stream_start(unreliable_id);
    tagged.on_reply_sent();
    tag_io.run();
    const uint8_t byte = 1;
    assert(tagged.send_chunk(reliable_id, {&byte, 1}, 0));
    assert(reliable_sends == 1 && datagram_sends == 0);
    assert(tagged.send_chunk(unreliable_id, {&byte, 1}, 0));
    assert(reliable_sends == 1 && datagram_sends == 1);

    // An older client's random ID can look tagged; a reliable method refuses it.
    bool rejected = false;
    auto writer = std::make_unique<nprpc::StreamWriter<uint8_t>>(values());
    try { tagged.register_stream(unreliable_id, std::move(writer), false); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
  }

  // Cancellation arrives on a transport thread while the pump runs on a pool.
  {
    boost::asio::thread_pool pool(4);
    nprpc::SessionContext pool_context;
    auto racing = std::make_shared<nprpc::impl::StreamManager>(pool_context, nullptr, pool.get_executor());
    racing->set_send_callback([](nprpc::flat_buffer&&) { return true; });
    for (uint64_t id = 100; id < 2100; ++id) {
      auto writer = std::make_unique<nprpc::StreamWriter<uint8_t>>(counted_values());
      writer->set_manager(racing.get(), id);
      racing->register_stream(id, std::move(writer), false, 1000);
      racing->defer_stream_start(id);
      racing->on_reply_sent();
      if (id % 2) std::this_thread::yield();
      racing->on_stream_cancel(id);
    }
    wait_for_producers_to_end();
    racing.reset();
    pool.join();
  }
}
