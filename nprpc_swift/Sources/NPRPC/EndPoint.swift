// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Swift wrapper for NPRPC network endpoints

import CNprpc

/// Transport protocol for NPRPC communication
public enum TransportType: UInt8, Sendable {
    case unknown = 0
    case tcp = 1
    case webSocket = 2
    case http = 3
    case quic = 4
    case udp = 5
    case sharedMemory = 6
    
    /// Initialize from C++ EndPointType
    init(from cxxType: nprpc_swift.EndPointType) {
        self = TransportType(rawValue: cxxType.rawValue) ?? .unknown
    }
    
    /// Convert to C++ EndPointType
    var cxxType: nprpc_swift.EndPointType {
        return nprpc_swift.EndPointType(rawValue: self.rawValue) ?? .Unknown
    }
    
    /// Human-readable name
    public var description: String {
        switch self {
        case .unknown: return "Unknown"
        case .tcp: return "TCP"
        case .webSocket: return "WebSocket"
        case .http: return "HTTP"
        case .quic: return "QUIC"
        case .udp: return "UDP"
        case .sharedMemory: return "SharedMemory"
        }
    }
    
    /// URL scheme for this transport
    public var scheme: String {
        switch self {
        case .unknown: return "unknown"
        case .tcp: return "tcp"
        case .webSocket: return "ws"
        case .http: return "http"
        case .quic: return "quic"
        case .udp: return "udp"
        case .sharedMemory: return "shm"
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
            throw InvalidEndpointError(message: "Failed to parse URL", url: url)
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
        return EndPoint(type: .tcp, hostname: host, port: port)
    }
    
    public static func webSocket(host: String, port: UInt16, path: String = "/nprpc") -> EndPoint {
        return EndPoint(type: .webSocket, hostname: host, port: port, path: path)
    }
    
    public static func http(host: String, port: UInt16, path: String = "/nprpc") -> EndPoint {
        return EndPoint(type: .http, hostname: host, port: port, path: path)
    }
    
    public static func quic(host: String, port: UInt16) -> EndPoint {
        return EndPoint(type: .quic, hostname: host, port: port)
    }
    
    public static func udp(host: String, port: UInt16) -> EndPoint {
        return EndPoint(type: .udp, hostname: host, port: port)
    }
    
    public static func sharedMemory(uuid: String) -> EndPoint {
        return EndPoint(type: .sharedMemory, hostname: uuid, port: 0)
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
