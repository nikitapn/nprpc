// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Swift wrapper for NPRPC network endpoints

import CNprpc

/// Transport protocol for NPRPC communication
public enum TransportType: UInt8, Sendable {
    case Unknown = 0
    case Tcp = 1
    case WebSocket = 2
    case Http = 3
    case Quic = 4
    case SharedMemory = 6

    /// Initialize from C++ EndPointType
    init(from cxxType: nprpc_swift.EndPointType) {
        self = TransportType(rawValue: cxxType.rawValue) ?? .Unknown
    }

    /// Convert to C++ EndPointType
    var cxxType: nprpc_swift.EndPointType {
        return nprpc_swift.EndPointType(rawValue: self.rawValue) ?? .Unknown
    }
    
    /// Human-readable name
    public var description: String {
        switch self {
        case .Unknown: return "Unknown"
        case .Tcp: return "TCP"
        case .WebSocket: return "WebSocket"
        case .Http: return "HTTP"
        case .Quic: return "QUIC"
        case .SharedMemory: return "SharedMemory"
        }
    }
    
    /// URL scheme for this transport
    public var scheme: String {
        switch self {
        case .Unknown: return "unknown"
        case .Tcp: return "tcp"
        case .WebSocket: return "ws"
        case .Http: return "http"
        case .Quic: return "quic"
        case .SharedMemory: return "shm"
        }
    }
}

/// Network endpoint identifying a remote NPRPC object
public struct EndPoint: Sendable {
    public let type: TransportType
    public let hostname: String
    public let port: UInt16
    public let path: String
    
    /// Create an endpoint with explicit parameters
    public init(type: TransportType, hostname: String, port: UInt16, path: String = "") {
        self.type = type
        self.hostname = hostname
        self.port = port
        self.path = path
    }
    
    /// Parse endpoint from URL string
    /// - Parameter url: URL in format "scheme://host:port/path"
    /// - Returns: Parsed endpoint, or nil if invalid
    public static func parse(_ url: String) throws -> EndPoint {
        let cxxUrl = std.string(url)
        guard let info = nprpc_swift.EndPointInfo.parse(cxxUrl).value else {
            throw RuntimeError(message: "Failed to parse URL: \(url)")
        }
        
        return EndPoint(
            type: TransportType(from: info.type),
            hostname: String(info.hostname),
            port: info.port,
            path: String(info.path)
        )
    }
    
    /// Convert endpoint to URL string
    public func toURL() -> String {
        var info = nprpc_swift.EndPointInfo()
        info.type = type.cxxType
        info.hostname = std.string(hostname)
        info.port = port
        info.path = std.string(path)
        return String(info.to_url())
    }
    
    /// Convenience initializers for common transports
    public static func tcp(host: String, port: UInt16) -> EndPoint {
        return EndPoint(type: .Tcp, hostname: host, port: port)
    }
    
    public static func webSocket(host: String, port: UInt16, path: String = "/nprpc") -> EndPoint {
        return EndPoint(type: .WebSocket, hostname: host, port: port, path: path)
    }
    
    public static func http(host: String, port: UInt16, path: String = "/nprpc") -> EndPoint {
        return EndPoint(type: .Http, hostname: host, port: port, path: path)
    }
    
    public static func quic(host: String, port: UInt16) -> EndPoint {
        return EndPoint(type: .Quic, hostname: host, port: port)
    }

    public static func sharedMemory(uuid: String) -> EndPoint {
        return EndPoint(type: .SharedMemory, hostname: uuid, port: 0)
    }
}

// MARK: - CustomStringConvertible
extension EndPoint: CustomStringConvertible {
    public var description: String {
        return toURL()
    }
}

// MARK: - Equatable
extension EndPoint: Equatable {
    public static func == (lhs: EndPoint, rhs: EndPoint) -> Bool {
        return lhs.type == rhs.type &&
               lhs.hostname == rhs.hostname &&
               lhs.port == rhs.port &&
               lhs.path == rhs.path
    }
}

// MARK: - Hashable
extension EndPoint: Hashable {
    public func hash(into hasher: inout Hasher) {
        hasher.combine(type)
        hasher.combine(hostname)
        hasher.combine(port)
        hasher.combine(path)
    }
}
