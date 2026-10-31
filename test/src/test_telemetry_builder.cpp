#ifdef NDEBUG
#undef NDEBUG
#endif
#include <nprpc/nprpc.hpp>
#include <cassert>
#include <string>

int main() {
  // Disabled is usable without any listener or certificate.
  auto* rpc = nprpc::RpcBuilder().build();
  assert(rpc->telemetry_reference().empty());
  rpc->destroy();

  bool rejected = false;
  try { nprpc::RpcBuilder().enable_telemetry().build(); }
  catch (const std::exception& e) {
    rejected = std::string(e.what()).find("QUIC listener") != std::string::npos;
  }
  assert(rejected);

  for (uint32_t interval : {0, 249, 10001}) {
    rejected = false;
    try {
      nprpc::RpcBuilder().with_quic(28444).enable_telemetry(true, interval).build();
    } catch (const std::exception& e) {
      rejected = std::string(e.what()).find("minimum interval") != std::string::npos;
    }
    assert(rejected);
  }

  // Reading the counters anonymously has to be asked for.
  rejected = false;
  try { nprpc::RpcBuilder().with_quic(28444).enable_telemetry().build(); }
  catch (const std::exception& e) {
    rejected = std::string(e.what()).find("allow_anonymous_telemetry") != std::string::npos;
  }
  assert(rejected);
}
