#include <nprpc/nprpc.hpp>
#include <nprpc_monitor.hpp>
#include <iostream>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
  try {
    if (argc < 5) return 2;
    auto arg = [&](int i) { return std::string(argv[i]) == "-" ? std::string{} : std::string(argv[i]); };
    if (std::string(argv[1]) == "server") {
      auto builder = nprpc::RpcBuilder().with_hostname("localhost").with_quic(28444).ssl(arg(2), arg(3));
#ifdef NPRPC_ENABLE_TCP
      builder.with_tcp(28445);
#endif
      if (!arg(4).empty()) builder.require_client_certificates(arg(4));
      else builder.allow_anonymous_telemetry();
      auto* rpc = builder.enable_telemetry().build();
      if (!nprpc::reload_certificates()) return 3; // Reload must retain mTLS policy.
      std::cout << rpc->telemetry_reference() << std::endl;
      rpc->run();
      return 0;
    }
    if (argc != 6) return 2;
    const nprpc::QuicClientTlsConfig tls{arg(3), arg(4), arg(5)};
    auto builder = nprpc::RpcBuilder();
    const bool live = std::string(argv[1]) == "client-live";
    if (!live) builder.quic_client_tls(tls.ca_file, tls.certificate_file, tls.private_key_file);
    auto* rpc = builder.build();
    struct Cleanup { nprpc::Rpc* rpc; ~Cleanup() { rpc->destroy(); } } cleanup{rpc};
    if (live) rpc->configure_quic_client_tls(tls);
    rpc->start_thread_pool(2);
    auto* raw = nprpc::Object::from_string(argv[2]);
    if (!raw) return 4;
    if (std::string(argv[1]) == "client-alt") {
#ifdef NPRPC_ENABLE_TCP
      raw->get_data().urls = "tcp://localhost:28445;";
#else
      delete raw;
      return 77;
#endif
    }
    if (std::string(argv[1]) == "client-ip") {
      raw->get_data().urls = "quic://127.0.0.1:28444;";
    }
    auto object = nprpc::ObjectPtr(nprpc::narrow<nprpc::monitor::Monitor>(raw));
    if (!object || !object->select_endpoint()) return 4;
    object->set_timeout(3000);
    if (std::string(argv[1]) == "client-cap") {
      // kMaxWatchesPerSession in telemetry_service.cpp.
      std::vector<nprpc::StreamReader<nprpc::monitor::Snapshot>> watches;
      for (int i = 0; i < 4; ++i) watches.push_back(object->Watch(250));
      try {
        object->Watch(250);
        return 6;
      } catch (const nprpc::ExceptionBadAccess&) {
      }
      // A cancelled Watch gives its slot back, a moment later.
      watches.back().cancel();
      for (int i = 0; i < 50; ++i) {
        try {
          auto again = object->Watch(250);
          return again.read_next() ? 0 : 5;
        } catch (const nprpc::ExceptionBadAccess&) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
      }
      return 7;
    }
    if (std::string(argv[1]) == "client-abandon") {
      // Never cancelled: the server learns only from the connection going away.
      auto* abandoned = new nprpc::StreamReader<nprpc::monitor::Snapshot>(object->Watch(250));
      return abandoned->read_next() ? 0 : 5;
    }
    if (std::string(argv[1]) == "client-stats") {
      auto stats = object->Watch(250);
      // Earlier clients' connections take a moment to finish closing.
      for (int i = 0; i < 28; ++i) {
        auto value = stats.read_next();
        if (!value) return 5;
        for (const auto& sample : value->transports) {
          if (sample.transport != nprpc::monitor::Transport::Quic) continue;
          // The clients this server already refused, and only this one left.
          if (sample.handshake_failures != 0 && sample.completed != 0 &&
              sample.active_connections == 1 && sample.active_streams == 1) {
            stats.cancel();
            return 0;
          }
          if (i == 27)
            std::cerr << "failures=" << sample.handshake_failures << " completed=" << sample.completed
                      << " connections=" << sample.active_connections << " streams=" << sample.active_streams << '\n';
        }
      }
      return 8;
    }
    auto stream = object->Watch(250);
    for (int i = 0; i < 3; ++i) {
      auto value = stream.read_next();
      if (!value || value->transports.size() != 4) return 5;
    }
    stream.cancel();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
