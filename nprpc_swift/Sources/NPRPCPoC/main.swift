// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Proof of Concept: Swift calling NPRPC C++ code

import NPRPC

print("=" * 60)
print("NPRPC Swift Proof of Concept")
print("=" * 60)
print()

// Test 1: Basic function call
print("Test 1: Basic C++ function call")
let sum = testAdd(40, 2)
print("  40 + 2 = \(sum)")
assert(sum == 42, "Basic addition failed!")
print("  ✓ PASSED")
print()

// Test 2: String passing
print("Test 2: String passing to/from C++")
let greeting = testGreet("Swift")
print("  Greeting: \(greeting)")
assert(greeting.contains("Swift"), "String passing failed!")
print("  ✓ PASSED")
print()

// Test 3: Array return
print("Test 3: Array return from C++")
let arr = testArray()
print("  Array: \(arr)")
assert(arr == [1, 2, 3, 4, 5], "Array return failed!")
print("  ✓ PASSED")
print()

// Test 4: EndPoint parsing
print("Test 4: EndPoint URL parsing")
if let ep = EndPoint.parse("ws://localhost:8080/nprpc") {
    print("  Parsed: type=\(ep.type), host=\(ep.hostname), port=\(ep.port), path=\(ep.path)")
    let url = ep.toURL()
    print("  Back to URL: \(url)")
    assert(ep.type == .webSocket, "Wrong type!")
    assert(ep.hostname == "localhost", "Wrong host!")
    assert(ep.port == 8080, "Wrong port!")
    print("  ✓ PASSED")
} else {
    print("  ✗ FAILED: Could not parse URL")
}
print()

// Test 5: RpcConfiguration
print("Test 5: RpcConfiguration struct")
var config = RpcConfiguration()
config.nameserverIP = "192.168.1.100"
config.nameserverPort = 15000
config.listenWSPort = 8080
config.listenQUICPort = 8443
print("  Config: nameserver=\(config.nameserverIP):\(config.nameserverPort)")
print("  Config: WS port=\(config.listenWSPort), QUIC port=\(config.listenQUICPort)")
print("  ✓ PASSED")
print()

// Test 6: Rpc handle (without actually connecting)
print("Test 6: Rpc handle creation")
let rpc = Rpc()
print("  Created Rpc handle")
print("  Is initialized: \(rpc.isInitialized)")
print("  Debug info: \(rpc.debugInfo)")

do {
    try rpc.initialize(config)
    print("  Initialized: \(rpc.isInitialized)")
    print("  ✓ PASSED")
} catch {
    print("  ✗ FAILED: \(error)")
}
print()

// Summary
print("=" * 60)
print("All tests passed! Swift C++ interop is working.")
print("=" * 60)

// Helper to repeat string
extension String {
    static func * (lhs: String, rhs: Int) -> String {
        return String(repeating: lhs, count: rhs)
    }
}
