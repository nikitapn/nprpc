// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import NPRPC

/// Swift servant matching C++ BenchmarkServerImpl for apples-to-apples benches.
/// (Generated base is also named BenchmarkServant.)
final class BenchmarkServantImpl: BenchmarkServant, @unchecked Sendable {
    override func ping() {}

    override func func1(a: UInt32, b: UInt32) -> UInt32 {
        a &+ b
    }

    override func func2(data: String) {
        _ = data
    }

    override func processEmployee(employee: Employee) -> Employee {
        employee
    }

    override func processLargeData(data: [UInt8]) -> [UInt8] {
        data
    }
}
