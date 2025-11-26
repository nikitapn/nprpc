// gRPC Benchmark Server

#include <grpcpp/grpcpp.h>
#include "benchmark.grpc.pb.h"

#include <iostream>
#include <memory>
#include <string>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

// Implement ServerControl service
class ServerControlServiceImpl final : public grpc::benchmark::ServerControl::Service {
  Status Shutdown(ServerContext* context, 
                 const grpc::benchmark::ShutdownRequest* request,
                 grpc::benchmark::ShutdownResponse* response) override {
    std::cout << "Shutdown requested" << std::endl;
    // Signal server to shutdown (handled in main)
    return Status::OK;
  }
};

// Implement Benchmark service
class BenchmarkServiceImpl final : public grpc::benchmark::Benchmark::Service {
  Status Ping(ServerContext* context,
             const grpc::benchmark::PingRequest* request,
             grpc::benchmark::PingResponse* response) override {
    // Empty call - just return
    return Status::OK;
  }

  Status Func1(ServerContext* context,
              const grpc::benchmark::Func1Request* request,
              grpc::benchmark::Func1Response* response) override {
    response->set_result(request->a() + request->b());
    return Status::OK;
  }

  Status Func2(ServerContext* context,
              const grpc::benchmark::Func2Request* request,
              grpc::benchmark::Func2Response* response) override {
    // Just receive the string, no processing needed
    return Status::OK;
  }
  
  Status ProcessEmployee(ServerContext* context,
                        const grpc::benchmark::ProcessEmployeeRequest* request,
                        grpc::benchmark::ProcessEmployeeResponse* response) override {
    // Echo back the employee data
    *response->mutable_result() = request->employee();
    return Status::OK;
  }
  
  Status ProcessLargeData(ServerContext* context,
                         const grpc::benchmark::ProcessLargeDataRequest* request,
                         grpc::benchmark::ProcessLargeDataResponse* response) override {
    // Echo back the data
    response->set_result(request->data());
    return Status::OK;
  }
};

void RunServer() {
  std::string server_address("0.0.0.0:50051");
  
  ServerControlServiceImpl control_service;
  BenchmarkServiceImpl benchmark_service;

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.SetMaxReceiveMessageSize(32 * 1024 * 1024);  // 32 MB
  builder.SetMaxSendMessageSize(32 * 1024 * 1024);     // 32 MB
  builder.RegisterService(&control_service);
  builder.RegisterService(&benchmark_service);

  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "gRPC benchmark server listening on " << server_address << std::endl;

  server->Wait();
}

int main(int argc, char** argv) {
  RunServer();
  return 0;
}
