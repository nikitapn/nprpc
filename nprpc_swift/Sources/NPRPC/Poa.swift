// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Swift wrapper for NPRPC POA (Portable Object Adapter)

import CNprpc
import Dispatch
import Foundation

#if canImport(Glibc)
import Glibc
#elseif canImport(Darwin)
import Darwin
#endif

// /// Object activation flags controlling which transports an object is available on
public struct ObjectActivationFlags: OptionSet, Sendable {
    public let rawValue: UInt32

    public init(rawValue: UInt32) {
        self.rawValue = rawValue
    }

    /// Make object session-specific (Ephemeral to current session)
    public static let privateSession = ObjectActivationFlags(rawValue: detail.ObjectActivationFlags.privateSession.rawValue)

    /// Allow TCP connections
    public static let tcp = ObjectActivationFlags(rawValue: detail.ObjectActivationFlags.tcp.rawValue)

    /// Allow WebSocket connections
    public static let ws = ObjectActivationFlags(rawValue: detail.ObjectActivationFlags.ws.rawValue)

    /// Allow SSL WebSocket connections
    public static let wss = ObjectActivationFlags(rawValue: detail.ObjectActivationFlags.wss.rawValue)

    /// Allow HTTP connections
    public static let http = ObjectActivationFlags(rawValue: detail.ObjectActivationFlags.http.rawValue)

    /// Allow secured HTTP connections
    public static let https = ObjectActivationFlags(rawValue: detail.ObjectActivationFlags.https.rawValue)

    /// Allow WebTransport connections
    public static let wt = ObjectActivationFlags(rawValue: detail.ObjectActivationFlags.wt.rawValue)

    /// Allow shared memory transport
    public static let shm = ObjectActivationFlags(rawValue: detail.ObjectActivationFlags.shm.rawValue)

    /// Allow QUIC connections
    public static let quic = ObjectActivationFlags(rawValue: detail.ObjectActivationFlags.quic.rawValue)

    /// Allow all transports
    public static let allowAll: ObjectActivationFlags = [
        .tcp, .ws, .wss, .http,
        .https, .wt, .quic, .shm
    ]

    /// Network transports only (no shared memory)
    public static let networkOnly: ObjectActivationFlags = [
        .tcp, .ws, .wss, .http, .https, .wt, .quic
    ]
}

/// Object ID policy for POA
public enum ObjectIdPolicy {
    /// POA generates object IDs automatically
    case systemGenerated

    /// User supplies object IDs explicitly
    case userSupplied
}

/// POA lifetime policy
public enum PoaLifetime {
    /// POA lives until explicitly destroyed
    case Persistent

    /// POA is destroyed when all objects are deactivated
    case transient
}

/// Where servant `dispatch` runs for objects activated on a POA.
///
/// - `inlineOnTransportThread`: default — SHM ring / transport thread
///   (lowest latency; not MainActor-safe).
/// - `main`: SHM ring fire-and-forgets the whole request onto
///   `DispatchQueue.main` (no Asio hop). Servant + reply run on main.
/// - `queue`: same for a custom **serial** `DispatchQueue` (required for
///   FIFO SHM reply matching).
/// - `loop`: same for a thread that is not a `DispatchQueue` at all — see
///   `PoaExecutor`.
///
/// Avoid blocking main on NPRPC work that itself waits on main.
public enum PoaDispatchExecutor: Sendable {
    case inlineOnTransportThread
    case main
    case queue(DispatchQueue)
    case loop(any PoaExecutor)
}

/// A serial execution context that is a **thread with its own event loop**
/// rather than a `DispatchQueue`.
///
/// `.main` and `.queue` cover everything GCD owns, but they cannot express the
/// case they are most wanted for: a thread that blocks in someone else's
/// `wait` — a GLFW/X11 pump, an epoll loop, a game loop — and that owns state
/// which may only be touched from *that thread*. GCD cannot pin a serial queue
/// to a chosen thread (only `.main` is pinned, and draining it means the loop
/// can never block), so such a loop can neither be a hop target nor be woken by
/// one.
///
/// Conforming types supply the two things the C `DispatchExecutor` needs:
///
/// ```swift
/// final class RenderLoop: PoaExecutor {
///     func post(_ work: @escaping @Sendable () -> Void) {
///         queue.append(work)     // drained once per loop iteration
///         wakeTheLoop()          // must unblock the loop's wait
///     }
///     var isRunningOnExecutor: Bool { Thread.current === loopThread }
/// }
/// ```
///
/// **`post` must wake the loop.** The ring fires and forgets: nothing else will
/// tell the loop that work is waiting, so a `post` that only enqueues leaves
/// the servant unrun until the loop happens to wake for its own reasons — on an
/// idle process, never.
///
/// **The loop must be serial and must drain in FIFO order**, for the same
/// reason `.queue` demands a serial queue: a shared-memory session matches
/// replies by ring slot order.
///
/// `isRunningOnExecutor` keeps `invoke_sync` from posting to a loop it is
/// already on, which would wait for a drain that cannot happen until it
/// returns.
public protocol PoaExecutor: AnyObject, Sendable {
    /// Enqueue `work` on the loop **and wake it**.
    func post(_ work: @escaping @Sendable () -> Void)
    /// True when the calling thread is the loop's own.
    var isRunningOnExecutor: Bool { get }
}

/// Holds a `DispatchQueue` for the C `DispatchExecutor` callbacks, plus the
/// specific key `is_running_on` tests. See `Poa.executorBox` for who owns it.
fileprivate final class DispatchExecutorBox: @unchecked Sendable {
    let queue: DispatchQueue
    let specificKey = DispatchSpecificKey<UInt8>()

    init(_ queue: DispatchQueue) {
        self.queue = queue
        // Any block running on this queue sees the key — used by is_running_on.
        queue.setSpecific(key: specificKey, value: 1)
    }
}

/// Holds a `PoaExecutor` for the C `DispatchExecutor` callbacks. Same role as
/// `DispatchExecutorBox`, for a hop target GCD does not own.
fileprivate final class LoopExecutorBox: @unchecked Sendable {
    let executor: any PoaExecutor
    init(_ executor: any PoaExecutor) { self.executor = executor }
}

/// Carries opaque C tokens across a `@Sendable` Dispatch hop.
/// Used for both fire-and-forget SHM offload and invoke_sync post+wait.
fileprivate struct UncheckedWork: @unchecked Sendable {
    let fn: (@convention(c) (UnsafeMutableRawPointer?) -> Void)
    let arg: UnsafeMutableRawPointer?
}

// C-compatible trampolines for nprpc::DispatchExecutor.
// Equivalent to dispatch_async_f(queue, arg, fn) — ring posts here and returns.
private let poaDispatchPost: @convention(c) (
    UnsafeMutableRawPointer?,
    (@convention(c) (UnsafeMutableRawPointer?) -> Void)?,
    UnsafeMutableRawPointer?
) -> Void = { ctx, fn, arg in
    guard let ctx, let fn else { return }
    let box = Unmanaged<DispatchExecutorBox>.fromOpaque(ctx).takeUnretainedValue()
    let work = UncheckedWork(fn: fn, arg: arg)
    box.queue.async {
        work.fn(work.arg)
    }
}

private let poaDispatchIsRunningOn: @convention(c) (UnsafeMutableRawPointer?) -> Bool = { ctx in
    guard let ctx else { return false }
    let box = Unmanaged<DispatchExecutorBox>.fromOpaque(ctx).takeUnretainedValue()
    return DispatchQueue.getSpecific(key: box.specificKey) != nil
}

// Same two trampolines for a `PoaExecutor`. The C side cannot tell the
// difference: it holds an opaque ctx and two function pointers either way.
private let poaLoopPost: @convention(c) (
    UnsafeMutableRawPointer?,
    (@convention(c) (UnsafeMutableRawPointer?) -> Void)?,
    UnsafeMutableRawPointer?
) -> Void = { ctx, fn, arg in
    guard let ctx, let fn else { return }
    let box = Unmanaged<LoopExecutorBox>.fromOpaque(ctx).takeUnretainedValue()
    let work = UncheckedWork(fn: fn, arg: arg)
    box.executor.post {
        work.fn(work.arg)
    }
}

private let poaLoopIsRunningOn: @convention(c) (UnsafeMutableRawPointer?) -> Bool = { ctx in
    guard let ctx else { return false }
    return Unmanaged<LoopExecutorBox>.fromOpaque(ctx)
        .takeUnretainedValue().executor.isRunningOnExecutor
}

// MARK: - Global dispatch function for C++ callback
/// This is a C-compatible function that C++ calls when dispatching to Swift servants.
/// It extracts the NPRPCServant from the opaque pointer and calls its dispatch method.
private let globalServantDispatch: @convention(c) (UnsafeMutableRawPointer?, UnsafeMutableRawPointer?, UnsafeMutableRawPointer?, UnsafeMutableRawPointer?, UnsafeMutableRawPointer?) -> Void = { servantPtr, rxBuffer, txBuffer, endpointPtr, sessionCtxPtr in
    guard let servantPtr = servantPtr else { return }
    guard let rxBuffer = rxBuffer else { return }
    guard let txBuffer = txBuffer else { return }

    // Reconstruct servant (don't consume - just borrow)
    let servant = Unmanaged<NPRPCServant>.fromOpaque(servantPtr).takeUnretainedValue()

    // Store session context for streaming operations
    servant.sessionContext = sessionCtxPtr
    defer { servant.sessionContext = nil }

    // Shared-memory sessions hand a zero-copy *view* into the receive ring.
    // Generated servants overwrite that same buffer with the response and may
    // grow it (exception strings, variable outputs). Growing a view reallocates
    // into heap ownership and invalidates any previously captured data pointer
    // (exception marshalling captures `exData` *before* marshal, so size/msg_id
    // would land in the ring while the body lives on the heap). Copying also
    // gives a naturally aligned heap base for Swift's typed loads.
    //
    // TCP/WS/QUIC already own a heap buffer — wrap it in place, no copy.
    let buffer: FlatBuffer
    if nprpc_flatbuffer_is_view(rxBuffer) {
        let requestSize = nprpc_flatbuffer_size(rxBuffer)
        let owned = FlatBuffer()
        if requestSize > 0 {
            owned.prepare(requestSize)
            owned.commit(requestSize)
            if let srcData = nprpc_flatbuffer_cdata(rxBuffer),
               let dstData = owned.data {
                memcpy(dstData, srcData, requestSize)
            }
        }
        buffer = owned
    } else {
        buffer = FlatBuffer(wrapping: rxBuffer)
    }

    // Extract endpoint from C++ EndPoint pointer
    let endpoint: NPRPCEndpoint
    if let endpointPtr = endpointPtr {
        let typeValue = UInt32(nprpc_endpoint_get_type(endpointPtr))
        let hostname = String(cString: nprpc_endpoint_get_hostname(endpointPtr))
        let port = nprpc_endpoint_get_port(endpointPtr)
        endpoint = NPRPCEndpoint(type: EndPointType(rawValue: typeValue) ?? .Tcp, hostname: hostname, port: port)
    } else {
        // Fallback - shouldn't happen in normal operation
        endpoint = NPRPCEndpoint(type: .Tcp, hostname: "localhost", port: 0)
    }

    // Call servant dispatch - it writes the response into `buffer`
    servant.dispatch(buffer: buffer, remoteEndpoint: endpoint)

    // Copy response into C++ tx_buffer (session owns the send path).
    // For the owned/TCP wrap path, buffer.handle is rxBuffer itself.
    let responseSize = buffer.size
    if responseSize > 0 {
        let txSizeBefore = nprpc_flatbuffer_size(txBuffer)
        nprpc_flatbuffer_consume(txBuffer, txSizeBefore)
        nprpc_flatbuffer_prepare(txBuffer, responseSize)
        nprpc_flatbuffer_commit(txBuffer, responseSize)

        if let srcData = buffer.constData,
           let dstData = nprpc_flatbuffer_data(txBuffer) {
            memcpy(dstData, srcData, responseSize)
        }
    }
}

/// Portable Object Adapter - manages servant lifecycle
///
/// POAs are containers for servants (object implementations). They:
/// - Assign object IDs to servants
/// - Route incoming requests to the correct servant
/// - Control which transports servants are accessible on
/// - Manage servant lifecycle (activation/deactivation)
///
/// Example:
/// ```swift
/// // Background / high-throughput servants (default)
/// let poa = try rpc.createPoa(maxObjects: 100)
///
/// // UI servants — dispatch lands on the main queue
/// let uiPoa = try rpc.createPoa(
///     maxObjects: 32,
///     dispatch: .main
/// )
///
/// let servant = MyServantImpl()
/// let objectId = try uiPoa.activateObject(servant, flags: .networkOnly)
/// ```
public final class Poa {
    /// Opaque handle to C++ nprpc::Poa
    internal let handle: UnsafeMutableRawPointer

    /// The hop target — a `DispatchQueue` box or a `PoaExecutor` box.
    ///
    /// Held here for reflection and for symmetry, but *not* what keeps it
    /// alive: the C++ POA outlives this wrapper (there is no `destroyPoa`, and
    /// callers routinely let the returned `Poa` go out of scope after
    /// activating), and it dereferences the box on every request. So the box
    /// is retained into the C context instead, deliberately for the life of
    /// the process. At most `max_poa_objects` of them can ever exist.
    fileprivate let executorBox: AnyObject?

    fileprivate init(handle: UnsafeMutableRawPointer, executorBox: AnyObject? = nil) {
        self.handle = handle
        self.executorBox = executorBox
    }

    /// POA index
    public var index: UInt16 {
        return nprpc_poa_get_index(handle)
    }

    /// Activate a servant in this POA
    /// - Parameters:
    ///   - servant: The servant to activate
    ///   - flags: Transport flags controlling how the object is accessible
    ///   - sessionContext: Optional session context for session-specific activation
    /// - Returns: ObjectId data for the activated object
    /// - Throws: RuntimeError if activation fails
    public func activateObject(
        _ servant: NPRPCServant,
        flags: ObjectActivationFlags,
        sessionContext: UnsafeMutableRawPointer? = nil
    ) throws -> detail.ObjectId {
        // Get unmanaged pointer to servant (we need to prevent Swift from deallocating it)
        let unmanagedServant = Unmanaged.passRetained(servant)
        let servantPtr = unmanagedServant.toOpaque()

        guard let oidPtr = nprpc_poa_activate_swift_servant(
            handle,
            servantPtr,
            servant.getClass(),
            flags.rawValue,
            globalServantDispatch,
            sessionContext
        ) else {
            // Release the retained servant since activation failed
            unmanagedServant.release()
            throw RuntimeError(message: "Failed to activate servant")
        }

        // Read ObjectId from the returned pointer
        let objectId = detail.ObjectId(
            object_id: nprpc_objectid_get_object_id(oidPtr),
            poa_idx: nprpc_objectid_get_poa_idx(oidPtr),
            flags: nprpc_objectid_get_flags(oidPtr),
            origin: Array(UnsafeBufferPointer(
                start: nprpc_objectid_get_origin(oidPtr),
                count: 16
            )),
            class_id: String(cString: nprpc_objectid_get_class_id(oidPtr)),
            urls: String(cString: nprpc_objectid_get_urls(oidPtr))
        )

        // Free the ObjectId allocated by C++
        nprpc_objectid_destroy(oidPtr)

        return objectId
    }

    public func activateObjectWithId(
        objectId: UInt64,
        servant: NPRPCServant,
        flags: ObjectActivationFlags,
        sessionContext: UnsafeMutableRawPointer? = nil
    ) throws -> detail.ObjectId {
        // Get unmanaged pointer to servant (we need to prevent Swift from deallocating it)
        let unmanagedServant = Unmanaged.passRetained(servant)
        let servantPtr = unmanagedServant.toOpaque()

        guard let oidPtr = nprpc_poa_activate_swift_servant_with_id(
            handle,
            servantPtr,
            servant.getClass(),
            flags.rawValue,
            globalServantDispatch,
            sessionContext,
            objectId
        ) else {
            // Release the retained servant since activation failed
            unmanagedServant.release()
            throw RuntimeError(message: "Failed to activate servant")
        }

        // Read ObjectId from the returned pointer
        let objectId = detail.ObjectId(
            object_id: nprpc_objectid_get_object_id(oidPtr),
            poa_idx: nprpc_objectid_get_poa_idx(oidPtr),
            flags: nprpc_objectid_get_flags(oidPtr),
            origin: Array(UnsafeBufferPointer(
                start: nprpc_objectid_get_origin(oidPtr),
                count: 16
            )),
            class_id: String(cString: nprpc_objectid_get_class_id(oidPtr)),
            urls: String(cString: nprpc_objectid_get_urls(oidPtr))
        )

        // Free the ObjectId allocated by C++
        nprpc_objectid_destroy(oidPtr)

        return objectId
    }

    /// Deactivate an object by its ID
    /// - Parameter objectId: The object ID to deactivate
    public func deactivateObject(_ objectId: UInt64) {
        nprpc_poa_deactivate_object(handle, objectId)
    }
}

// MARK: - Rpc extension for POA management
extension Rpc {
    /// Create a new POA with specified configuration
    /// - Parameters:
    ///   - maxObjects: Maximum number of objects (0 = unlimited)
    ///   - lifetime: Lifetime policy
    ///   - idPolicy: Object ID assignment policy
    ///   - dispatch: Thread/queue where servant `dispatch` runs
    /// - Returns: New POA instance
    /// - Throws: RuntimeError if creation fails
    public func createPoa(
        maxObjects: UInt32 = 0,
        lifetime: PoaLifetime = .Persistent,
        idPolicy: ObjectIdPolicy = .systemGenerated,
        dispatch: PoaDispatchExecutor = .inlineOnTransportThread
    ) throws -> Poa {
        guard self.isInitialized, let rpcHandle = self.handle else {
            throw RuntimeError(message: "Rpc not initialized")
        }

        let lifespanValue: UInt32 = (lifetime == .Persistent) ? 0 : 1
        let idPolicyValue: UInt32 = (idPolicy == .systemGenerated) ? 0 : 1
        let rawHandle = UnsafeMutableRawPointer(rpcHandle)

        switch dispatch {
        case .inlineOnTransportThread:
            guard let poaHandle = nprpc_rpc_create_poa(
                rawHandle, maxObjects, lifespanValue, idPolicyValue
            ) else {
                throw RuntimeError(message: "Failed to create POA")
            }
            return Poa(handle: poaHandle)

        case .main:
            return try createPoaWithQueue(
                rawHandle: rawHandle,
                maxObjects: maxObjects,
                lifespanValue: lifespanValue,
                idPolicyValue: idPolicyValue,
                queue: .main
            )

        case .queue(let queue):
            return try createPoaWithQueue(
                rawHandle: rawHandle,
                maxObjects: maxObjects,
                lifespanValue: lifespanValue,
                idPolicyValue: idPolicyValue,
                queue: queue
            )

        case .loop(let executor):
            let box = LoopExecutorBox(executor)
            // Retained for the life of the C++ POA, which outlives this
            // wrapper — see `Poa.executorBox`.
            let ctx = Unmanaged.passRetained(box).toOpaque()

            guard let poaHandle = nprpc_rpc_create_poa_with_executor(
                rawHandle,
                maxObjects,
                lifespanValue,
                idPolicyValue,
                poaLoopPost,
                poaLoopIsRunningOn,
                ctx
            ) else {
                throw RuntimeError(message: "Failed to create POA with loop executor")
            }
            return Poa(handle: poaHandle, executorBox: box)
        }
    }

    private func createPoaWithQueue(
        rawHandle: UnsafeMutableRawPointer,
        maxObjects: UInt32,
        lifespanValue: UInt32,
        idPolicyValue: UInt32,
        queue: DispatchQueue
    ) throws -> Poa {
        let box = DispatchExecutorBox(queue)
        // Retained for the life of the C++ POA, which outlives this wrapper —
        // see `Poa.executorBox`.
        let ctx = Unmanaged.passRetained(box).toOpaque()

        guard let poaHandle = nprpc_rpc_create_poa_with_executor(
            rawHandle,
            maxObjects,
            lifespanValue,
            idPolicyValue,
            poaDispatchPost,
            poaDispatchIsRunningOn,
            ctx
        ) else {
            throw RuntimeError(message: "Failed to create POA with dispatch executor")
        }

        return Poa(handle: poaHandle, executorBox: box)
    }
}
