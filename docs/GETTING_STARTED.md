# Getting Started

This guide builds a small calculator service: an interface in IDL, a C++
server that implements it, and a C++ client that calls it over TCP. It then
shows how the same object is reached from a browser and from Swift.

## 1. Install NPRPC

Build and install NPRPC with the default options, as described in
[BUILD.md](BUILD.md):

```bash
git clone --recursive https://github.com/nikitapn/nprpc.git
cmake -S nprpc -B nprpc/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build nprpc/build
sudo cmake --install nprpc/build
```

This installs the library, the `npidl` code generator, and a CMake package.

## 2. Describe the interface

An interface is written in NPRPC's IDL. Create `idl/calculator.npidl`:

```npidl
module example;

/// Raised by Divide for a zero divisor.
exception CalculationError {
  reason: string;
}

/// A calculator.
interface Calculator {
  f64 Add(a: f64, b: f64);
  f64 Divide(a: f64, b: f64) raises(CalculationError);
}
```

`npidl` turns this into C++, Swift or TypeScript. For each interface you get:

- **a servant base class** (`ICalculator_Servant` in C++) that the server
  implements;
- **a proxy** (`Calculator`) whose methods send calls to a remote object;
- the messages and exceptions as ordinary types.

`///` comments are carried into the generated code.

## 3. Set up the project

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(calculator CXX)
set(CMAKE_CXX_STANDARD 23)

find_package(nprpc REQUIRED)

# Runs npidl at build time; generated files go into the build directory.
npidl_generate_idl_files("${CMAKE_CURRENT_SOURCE_DIR}/idl/calculator.npidl" calculator_stub)

foreach(app server client)
  add_executable(${app} ${app}.cpp ${calculator_stub_GENERATED_SOURCES})
  target_include_directories(${app} PRIVATE ${calculator_stub_INCLUDE_DIR})
  target_link_libraries(${app} PRIVATE nprpc::nprpc)
endforeach()
```

## 4. Write the server

`server.cpp`:

```cpp
#include <fstream>
#include <iostream>

#include <nprpc/nprpc.hpp>
#include "calculator.hpp"

class CalculatorImpl : public example::ICalculator_Servant {
public:
  double Add(double a, double b) override { return a + b; }

  double Divide(double a, double b) override {
    if (b == 0)
      throw example::CalculationError("division by zero");
    return a / b;
  }
};

int main() {
  auto* rpc = nprpc::RpcBuilder()
                  .set_log_level(nprpc::LogLevel::warn)
                  .with_hostname("localhost")
                  .with_tcp(15100)
                  .build();

  // Persistent: the object lives until the server stops, whoever holds it.
  auto* poa = rpc->create_poa()
                  .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
                  .build();
  auto oid = poa->activate_object(new CalculatorImpl(),
                                  nprpc::ObjectActivationFlags::tcp);

  // Hand the reference to clients; here, through a file.
  std::ofstream("calculator.ref") << oid.to_string();
  std::cout << "Calculator ready\n";

  rpc->run();   // serve until the process is stopped
}
```

What each step does:

- **`RpcBuilder`** starts the runtime and chooses which transports to listen
  on: here, TCP on port 15100. `with_hostname` is the address written into
  object references, so clients know where to connect.
- **A POA** (Portable Object Adapter) holds servants. A *persistent* POA keeps
  them until the server stops, which is what an object published at startup
  needs. A *transient* POA, the default, reference-counts its objects and is
  meant for objects created during a call for one client. See
  [POA.md](POA.md).
- **`activate_object`** registers the servant and returns its `ObjectId`: the
  object's address, plus which transports may reach it (`tcp` here).
- **`to_string()`** turns the `ObjectId` into a string that any client can turn
  back into a proxy. Writing it to a file is the simplest way to hand it over;
  section 7 shows other ways.

## 5. Write the client

`client.cpp`:

```cpp
#include <fstream>
#include <iostream>
#include <iterator>

#include <nprpc/nprpc.hpp>
#include "calculator.hpp"

int main() {
  // A client needs no listeners.
  auto* rpc = nprpc::RpcBuilder().set_log_level(nprpc::LogLevel::warn).build();
  rpc->start_thread_pool(1);   // runs the I/O for our calls

  std::ifstream file("calculator.ref");
  std::string ref(std::istreambuf_iterator<char>(file), {});

  nprpc::Object* obj = nprpc::Object::from_string(ref);
  if (!obj) {
    std::cerr << "bad reference\n";
    return 1;
  }
  nprpc::ObjectPtr<example::Calculator> calc(nprpc::narrow<example::Calculator>(obj));
  if (!calc) {
    std::cerr << "not a Calculator\n";
    return 1;
  }

  std::cout << "2 + 3 = " << calc->Add(2, 3) << '\n';

  try {
    calc->Divide(1, 0);
  } catch (const example::CalculationError& e) {
    std::cout << "Divide failed: " << e.reason << '\n';
  }
}
```

- **`Object::from_string`** parses the reference and picks a transport the
  client can use.
- **`narrow`** checks that the object implements `Calculator` and returns the
  typed proxy, or nullptr if it does not.
- **`ObjectPtr`** releases the proxy when it goes out of scope.
- Each proxy method is a blocking call. An exception the server raises
  arrives as the same C++ type. Each method also has an `Async` form, e.g.
  `AddAsync`, that returns an awaitable `nprpc::Task`.

## 6. Run it

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/server &
./build/client
```

The runtime prints a few version lines when it starts, then:

```
2 + 3 = 5
Divide failed: division by zero
```

## 7. Where to go next

### Reach the object from other places

**Other transports.** Add listeners to the server's builder, and matching flags
to `activate_object`. For example `.with_http(8080)` together with the `ws`
and `http` flags lets browsers connect. Shared memory (`shm`) is the fastest
option between processes on one machine and needs no listener.

**From a browser.** Serve a directory over HTTP and publish the object in
`host.json`, which the TypeScript runtime loads at startup:

```cpp
auto* rpc = nprpc::RpcBuilder()
                .with_hostname("localhost")
                .with_http(8080)
                    .root_dir("www")   // static files, and where host.json goes
                .build();
// ... create the POA, then activate with
// nprpc::ObjectActivationFlags::ws | nprpc::ObjectActivationFlags::http
rpc->add_to_host_json("calculator", oid);
rpc->produce_host_json();
```

```ts
import * as NPRPC from 'nprpc';
import { Calculator } from './gen/calculator';   // npidl --ts output

const rpc = await NPRPC.init();
const calc = NPRPC.narrow(rpc.host_info.objects.calculator, Calculator);
console.log(await calc.Add(2, 3));
```

**From Swift**, with stubs from `npidl --swift`:

```swift
import NPRPC

let rpc = try RpcBuilder().build()
try rpc.startThreadPool(1)

guard let obj = NPRPCObject.fromString(ref),
      let calc = narrow(obj, to: Calculator.self) else { fatalError("bad reference") }
print(try await calc.add(a: 2, b: 3))
```

**Through the nameserver.** `npnameserver` keeps a directory of objects by
name, so clients only need its address: the server calls
`rpc->get_nameserver("127.0.0.1")->Bind(oid, "calculator")`, and clients
call `Resolve("calculator", obj)` instead of reading a file.

### Learn more

- [STREAMS.md](STREAMS.md): methods that send sequences of values.
- [POA.md](POA.md): object lifetimes, and which thread servant methods run on.
- [PAGE_RENDERING.md](PAGE_RENDERING.md): serving HTML pages from the same
  process.
- [HTTP_AUTH.md](HTTP_AUTH.md): cookies and authentication for browser clients.
- The API reference, generated from the headers and IDL, is on this site under
  C++, Swift and IDL.
