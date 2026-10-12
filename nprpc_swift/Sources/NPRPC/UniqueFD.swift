// Copyright (c) 2021-2026, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import CNprpc
import Foundation

/// An owned file descriptor: closed when the last reference goes away.
///
/// The Swift type of the IDL `fd` wherever the descriptor changes hands: a
/// proxy returns one for an `out` or return value, and a servant receives
/// one for an `in` argument and returns one for its outputs.  An `in`
/// argument on the proxy side is a plain `Int32` borrowed for the call —
/// the runtime sends a duplicate, so a descriptor that came from C++ stays
/// C++'s.
///
/// To hand a received descriptor on to C++, take it with `release()`; from
/// then on it is the caller's to close.
///
/// Only a transport that can carry descriptors between processes can send
/// one (shared memory, on Linux); over anything else such a call throws
/// before it is sent.
public final class UniqueFD: @unchecked Sendable {
    private let lock = NSLock()
    private var fd: Int32

    /// Takes ownership of `fd`; -1 for none.
    public init(adopting fd: Int32) {
        self.fd = fd
    }

    /// Owns a close-on-exec duplicate of `fd`, which stays the caller's.
    public convenience init(duplicating fd: Int32) throws {
        let copy = nprpc_fd_dup(fd)
        if copy < 0 {
            throw RuntimeError(message: "dup(\(fd)) failed: errno \(errno)")
        }
        self.init(adopting: copy)
    }

    deinit {
        if fd >= 0 {
            nprpc_fd_close(fd)
        }
    }

    /// The descriptor, still owned; -1 if none.
    public var rawValue: Int32 {
        lock.lock(); defer { lock.unlock() }
        return fd
    }

    /// Whether there is a descriptor.
    public var isValid: Bool { rawValue >= 0 }

    /// Gives up ownership without closing; -1 if there was none.
    public func release() -> Int32 {
        lock.lock(); defer { lock.unlock() }
        let released = fd
        fd = -1
        return released
    }

    /// Closes the descriptor now rather than on deinit.
    public func close() {
        let closing = release()
        if closing >= 0 {
            nprpc_fd_close(closing)
        }
    }
}

// MARK: - Generated-code support

extension FlatBuffer {
    /// Attaches a duplicate of the borrowed `fd`; returns the index an `fd`
    /// field stores.
    public func attachFD(duplicating fd: Int32) throws -> UInt32 {
        let copy = nprpc_fd_dup(fd)
        if copy < 0 {
            throw RuntimeError(message: "dup(\(fd)) failed: errno \(errno)")
        }
        return nprpc_flatbuffer_attach_fd(handle, copy)
    }

    /// Attaches `fd`, which gives up its descriptor to the message.
    public func attachFD(_ fd: UniqueFD) -> UInt32 {
        nprpc_flatbuffer_attach_fd(handle, fd.release())
    }

    /// Takes the descriptor an `fd` field names out of the message; nil if
    /// it did not arrive.
    public func takeFD(_ index: UInt32) -> UniqueFD? {
        let fd = nprpc_flatbuffer_take_fd(handle, index)
        return fd >= 0 ? UniqueFD(adopting: fd) : nil
    }
}

extension NPRPCObject {
    /// Throws unless calls to this object can carry file descriptors.
    /// Generated proxies call it before sending a method with an `fd`.
    public func requireFdPassing() throws {
        if let reason = nprpc_object_check_fd_passing(handle) {
            throw RuntimeError(message: String(cString: reason))
        }
    }
}
