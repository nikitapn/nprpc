// Generated types only - for testing marshalling without C++ dependencies

public enum Color: Int, Sendable {
  case red
  case green
  case blue
}

public struct Point: Sendable {
  public var x: Int32
  public var y: Int32

  public init(x: Int32, y: Int32) {
    self.x = x
    self.y = y
  }
}

public struct Rectangle: Sendable {
  public var topLeft: Point
  public var bottomRight: Point
  public var color: Color

  public init(topLeft: Point, bottomRight: Point, color: Color) {
    self.topLeft = topLeft
    self.bottomRight = bottomRight
    self.color = color
  }
}

// MARK: - Marshal Point

func marshal_Point(buffer: UnsafeMutableRawPointer, offset: Int, data: Point) {
  buffer.storeBytes(of: data.x, toByteOffset: offset + 0, as: Int32.self)
  buffer.storeBytes(of: data.y, toByteOffset: offset + 4, as: Int32.self)
}

// MARK: - Unmarshal Point

func unmarshal_Point(buffer: UnsafeRawPointer, offset: Int) -> Point {
  return Point(
    x: buffer.load(fromByteOffset: offset + 0, as: Int32.self),
    y: buffer.load(fromByteOffset: offset + 4, as: Int32.self)
  )
}

// MARK: - Marshal Rectangle

func marshal_Rectangle(buffer: UnsafeMutableRawPointer, offset: Int, data: Rectangle) {
  marshal_Point(buffer: buffer, offset: offset + 0, data: data.topLeft)
  marshal_Point(buffer: buffer, offset: offset + 8, data: data.bottomRight)
  buffer.storeBytes(of: Int32(data.color.rawValue), toByteOffset: offset + 16, as: Int32.self)
}

// MARK: - Unmarshal Rectangle

func unmarshal_Rectangle(buffer: UnsafeRawPointer, offset: Int) -> Rectangle {
  return Rectangle(
    topLeft: unmarshal_Point(buffer: buffer, offset: offset + 0),
    bottomRight: unmarshal_Point(buffer: buffer, offset: offset + 8),
    color: Color(rawValue: Int(buffer.load(fromByteOffset: offset + 16, as: Int32.self)))!
  )
}

// MARK: - Protocols (for servant tests)

public protocol CalculatorProtocol {
  func add(a: Int32, b: Int32) throws -> Int32
  func divide(numerator: Double, denominator: Double) throws -> Double
}

public protocol ShapeServiceProtocol {
  func getRectangle(id: UInt32) throws -> Rectangle
  func setRectangle(id: UInt32, rect: Rectangle) throws
}

// Servant base for Calculator
open class CalculatorServant: CalculatorProtocol {
  public init() {}

  open func add(a: Int32, b: Int32) throws -> Int32 {
    fatalError("Subclass must implement add")
  }

  open func divide(numerator: Double, denominator: Double) throws -> Double {
    fatalError("Subclass must implement divide")
  }
}

// Servant base for ShapeService
open class ShapeServiceServant: ShapeServiceProtocol {
  public init() {}

  open func getRectangle(id: UInt32) throws -> Rectangle {
    fatalError("Subclass must implement getRectangle")
  }

  open func setRectangle(id: UInt32, rect: Rectangle) throws {
    fatalError("Subclass must implement setRectangle")
  }
}
