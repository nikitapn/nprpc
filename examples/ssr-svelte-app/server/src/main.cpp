#include <iostream>
#include <nprpc/nprpc.hpp>

int main(int argc, char* argv[]) {
    boost::asio::io_context ioc;
    auto rpc = nprpc::RpcBuilder()
        .set_debug_level(nprpc::DebugLevel::DebugLevel_Critical)
        .set_hostname("linuxvm")
        .with_http()
            .port(3000)
            .root_dir("/home/nikita/projects/nprpc/examples/ssr-svelte-app/client/build/client")
            .ssl("/home/nikita/projects/nprpc/certs/out/localhost.crt",
                 "/home/nikita/projects/nprpc/certs/out/localhost.key")
            .enable_http3()
            .enable_ssr("/home/nikita/projects/nprpc/examples/ssr-svelte-app/client/build")
        .build(ioc);

    ioc.run();

    return EXIT_SUCCESS;
}