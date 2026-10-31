#include <nprpc/nprpc.hpp>
#include <iostream>
#include <string>

// usage: http3_rebinding_fixture <cert> <key> <root> <port> [<egress ring> <ingress ring>]
int main(int argc, char** argv) {
  if (argc != 5 && argc != 7) return 2;
  auto builder = nprpc::RpcBuilder();
  builder.with_hostname("localhost").set_log_level(nprpc::LogLevel::error);
  auto http = builder.with_http(static_cast<uint16_t>(std::stoi(argv[4])));
  http.ssl(argv[1], argv[2]).root_dir(argv[3]).enable_http3().http3_workers(1);
  if (argc == 7) http.shm_egress_channel(argv[5], argv[6]);
  auto* rpc = http.build();
  std::cout << "ready" << std::endl;
  rpc->run();
}
