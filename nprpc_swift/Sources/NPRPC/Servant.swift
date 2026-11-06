// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import CNprpc

/// Inside a servant method: whether the client cancelled this call or went away.
/// A long-running method can check this and stop early. It only ever turns true
/// on a POA created with a `dispatch:` other than `.inlineOnTransportThread`.
public var currentCallIsCancelled: Bool { nprpc_current_call_cancelled() }

/// A `[coroutine]` servant method on its way: generated dispatch begins one, and it
/// sends the reply once the method returns. On a POA with a `dispatch:` other than
/// `.inlineOnTransportThread`, the method keeps running on that executor across its
/// awaits. A client's cancellation cancels its task.
public final class NPRPCDeferredCall: @unchecked Sendable {
    private let handle: UnsafeMutableRawPointer

    private init(handle: UnsafeMutableRawPointer) { self.handle = handle }
    deinit { nprpc_deferred_call_release(handle) }

    /// Nil if the session is at its limit; the client has been answered then.
    public static func begin() -> NPRPCDeferredCall? {
        nprpc_deferred_call_begin().map { NPRPCDeferredCall(handle: $0) }
    }

    /// Runs the method's body with the buffer to build the reply in.
    public func start(_ body: @escaping @Sendable (FlatBuffer) async -> Void) {
        let buffer = FlatBuffer(wrapping: nprpc_deferred_call_tx(handle))
        let run: @Sendable () async -> Void = { [self] in
            await body(buffer)
            nprpc_deferred_call_finish(handle)
        }

        let task: Task<Void, Never>
        if #available(macOS 15.0, iOS 18.0, *), nprpc_deferred_call_has_executor(handle) {
            task = Task(executorPreference: NPRPCPoaTaskExecutor(call: self)) { await run() }
        } else {
            task = Task { await run() }
        }

        let cancel = Unmanaged.passRetained(CancelBox(task)).toOpaque()
        nprpc_deferred_call_on_stop(handle, cancel, { context in
            Unmanaged<CancelBox>.fromOpaque(context!).takeUnretainedValue().task.cancel()
        }, { context in
            Unmanaged<CancelBox>.fromOpaque(context!).release()
        })
    }

    fileprivate func post(_ context: UnsafeMutableRawPointer, _ fn: @convention(c) (UnsafeMutableRawPointer?) -> Void) {
        nprpc_deferred_call_post(handle, context, fn)
    }

    private final class CancelBox {
        let task: Task<Void, Never>
        init(_ task: Task<Void, Never>) { self.task = task }
    }
}

@available(macOS 15.0, iOS 18.0, *)
private final class NPRPCPoaTaskExecutor: TaskExecutor, @unchecked Sendable {
    private let call: NPRPCDeferredCall

    init(call: NPRPCDeferredCall) { self.call = call }

    private final class JobBox {
        let job: UnownedJob
        let executor: NPRPCPoaTaskExecutor
        init(job: UnownedJob, executor: NPRPCPoaTaskExecutor) {
            self.job = job
            self.executor = executor
        }
    }

    func enqueue(_ job: consuming ExecutorJob) {
        let box = JobBox(job: UnownedJob(job), executor: self)
        call.post(Unmanaged.passRetained(box).toOpaque()) { context in
            let box = Unmanaged<JobBox>.fromOpaque(context!).takeRetainedValue()
            box.job.runSynchronously(on: box.executor.asUnownedTaskExecutor())
        }
    }
}

/// Endpoint information for connecting to remote objects
public struct NPRPCEndpoint {
    /// Type of transport (TCP, HTTP, WebSocket, etc.)
    public let type: EndPointType
    
    /// Hostname or IP address
    public let hostname: String
    
    /// Port number (0 for shared memory)
    public let port: UInt16
    
    /// Initialize with all components
    public init(type: EndPointType = .Tcp, hostname: String = "", port: UInt16 = 0) {
        self.type = type
        self.hostname = hostname
        self.port = port
    }
    
    /// Check if this endpoint is empty/uninitialized
    public var isEmpty: Bool {
        hostname.isEmpty
    }
    
    /// Convert to URL string (e.g., "tcp://localhost:15000")
    public func toURL() -> String {
        let prefix: String
        switch type {
        case .Tcp, .TcpPrivate: prefix = "tcp://"
        case .WebSocket: prefix = "ws://"
        case .WebTransport: prefix = "wt://"
        case .SecuredWebSocket: prefix = "wss://"
        case .Http: prefix = "http://"
        case .SecuredHttp: prefix = "https://"
        case .SharedMemory: return "mem://\(hostname)"  // No port for shared memory
        case .Quic: prefix = "quic://"
        }
        return "\(prefix)\(hostname):\(port)"
    }
}

/// Base class for Swift servants
open class NPRPCServant: @unchecked Sendable {
    /// Session context pointer for streaming operations (set during dispatch)
    public var sessionContext: UnsafeMutableRawPointer?
    
    /// A servant not yet activated; see `Poa.activateObject`.
    public init() {}
    
    /// Override this to return the class name/ID of this servant
    /// This is typically the fully qualified interface name from the IDL
    open func getClass() -> String {
        fatalError("Subclass must override getClass to return the interface class ID")
    }
    
    /// Override this to handle RPC dispatch
    open func dispatch(buffer: FlatBuffer, remoteEndpoint: NPRPCEndpoint) {
        fatalError("Subclass must override dispatch")
    }
}

/// Swift servant bridge - connects Swift servants to C++ POA
internal class SwiftServantBridge {
    weak var servant: NPRPCServant?
    let className: String
    
    init(servant: NPRPCServant, className: String) {
        self.servant = servant
        self.className = className
    }
    
    /// C-compatible dispatch function that will be called from C++
    /// Parameters: swiftServant, rxBuffer, txBuffer, endpoint, sessionContext
    static let dispatchTrampoline: @convention(c) (UnsafeMutableRawPointer?, UnsafeMutableRawPointer?, UnsafeMutableRawPointer?, UnsafeMutableRawPointer?, UnsafeMutableRawPointer?) -> Void = { swiftServantPtr, rxBufferPtr, txBufferPtr, endpointPtr, sessionCtxPtr in
        guard let swiftServantPtr = swiftServantPtr,
              let rxBufferPtr = rxBufferPtr else {
            return
        }
        
        let bridge = Unmanaged<SwiftServantBridge>.fromOpaque(swiftServantPtr).takeUnretainedValue()
        guard let servant = bridge.servant else { return }
        
        // Store session context for streaming operations
        servant.sessionContext = sessionCtxPtr
        defer { servant.sessionContext = nil }
        
        // Wrap C++ flat_buffer without taking ownership
        let buffer = FlatBuffer(wrapping: rxBufferPtr)
        let endpoint = NPRPCEndpoint() // TODO: Extract from endpointPtr
        
        servant.dispatch(buffer: buffer, remoteEndpoint: endpoint)
    }
}
