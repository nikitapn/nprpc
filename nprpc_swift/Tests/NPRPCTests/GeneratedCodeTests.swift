import XCTest

// Test the generated Swift code from test_swift_gen.npidl
final class GeneratedCodeTests: XCTestCase {
    
    // MARK: - Struct Tests
    
    func testPointCreation() {
        let point = Point(x: 10, y: 20)
        XCTAssertEqual(point.x, 10)
        XCTAssertEqual(point.y, 20)
    }
    
    func testRectangleCreation() {
        let topLeft = Point(x: 0, y: 0)
        let bottomRight = Point(x: 100, y: 100)
        let rect = Rectangle(topLeft: topLeft, bottomRight: bottomRight, color: .red)
        
        XCTAssertEqual(rect.topLeft.x, 0)
        XCTAssertEqual(rect.topLeft.y, 0)
        XCTAssertEqual(rect.bottomRight.x, 100)
        XCTAssertEqual(rect.bottomRight.y, 100)
        XCTAssertEqual(rect.color, .red)
    }
    
    func testColorEnum() {
        XCTAssertEqual(Color.red.rawValue, 0)
        XCTAssertEqual(Color.green.rawValue, 1)
        XCTAssertEqual(Color.blue.rawValue, 2)
        
        XCTAssertEqual(Color(rawValue: 0), .red)
        XCTAssertEqual(Color(rawValue: 1), .green)
        XCTAssertEqual(Color(rawValue: 2), .blue)
    }
    
    // MARK: - Marshal/Unmarshal Tests
    
    func testPointMarshalUnmarshal() {
        let original = Point(x: 42, y: 73)
        
        // Allocate buffer
        let bufferSize = 8  // 2 * Int32
        let buffer = UnsafeMutableRawPointer.allocate(byteCount: bufferSize, alignment: 4)
        defer { buffer.deallocate() }
        
        // Marshal
        marshal_Point(buffer: buffer, offset: 0, data: original)
        
        // Unmarshal
        let restored = unmarshal_Point(buffer: buffer, offset: 0)
        
        // Verify
        XCTAssertEqual(restored.x, original.x)
        XCTAssertEqual(restored.y, original.y)
    }
    
    func testRectangleMarshalUnmarshal() {
        let original = Rectangle(
            topLeft: Point(x: 10, y: 20),
            bottomRight: Point(x: 100, y: 200),
            color: .blue
        )
        
        // Allocate buffer (2 Points + 1 Color enum)
        let bufferSize = 20  // 4 + 4 + 4 + 4 + 4
        let buffer = UnsafeMutableRawPointer.allocate(byteCount: bufferSize, alignment: 4)
        defer { buffer.deallocate() }
        
        // Marshal
        marshal_Rectangle(buffer: buffer, offset: 0, data: original)
        
        // Unmarshal
        let restored = unmarshal_Rectangle(buffer: buffer, offset: 0)
        
        // Verify
        XCTAssertEqual(restored.topLeft.x, original.topLeft.x)
        XCTAssertEqual(restored.topLeft.y, original.topLeft.y)
        XCTAssertEqual(restored.bottomRight.x, original.bottomRight.x)
        XCTAssertEqual(restored.bottomRight.y, original.bottomRight.y)
        XCTAssertEqual(restored.color, original.color)
    }
    
    func testAlignedMemoryAccess() {
        // Ensure aligned access works correctly
        let point = Point(x: -100, y: 100)
        
        let bufferSize = 16  // Extra space to test alignment
        let buffer = UnsafeMutableRawPointer.allocate(byteCount: bufferSize, alignment: 4)
        defer { buffer.deallocate() }
        
        // Marshal at offset 0 (aligned)
        marshal_Point(buffer: buffer, offset: 0, data: point)
        let restored1 = unmarshal_Point(buffer: buffer, offset: 0)
        XCTAssertEqual(restored1.x, point.x)
        XCTAssertEqual(restored1.y, point.y)
        
        // Marshal at offset 8 (also aligned for Int32)
        marshal_Point(buffer: buffer, offset: 8, data: point)
        let restored2 = unmarshal_Point(buffer: buffer, offset: 8)
        XCTAssertEqual(restored2.x, point.x)
        XCTAssertEqual(restored2.y, point.y)
    }
    
    func testEnumMarshalInStruct() {
        // Test that enum values marshal correctly
        for color in [Color.red, Color.green, Color.blue] {
            let rect = Rectangle(
                topLeft: Point(x: 0, y: 0),
                bottomRight: Point(x: 1, y: 1),
                color: color
            )
            
            let buffer = UnsafeMutableRawPointer.allocate(byteCount: 20, alignment: 4)
            defer { buffer.deallocate() }
            
            marshal_Rectangle(buffer: buffer, offset: 0, data: rect)
            let restored = unmarshal_Rectangle(buffer: buffer, offset: 0)
            
            XCTAssertEqual(restored.color, color, "Color \(color) should roundtrip correctly")
        }
    }
    
    // MARK: - Servant Tests
    
    func testCalculatorServantSubclass() {
        class TestCalculator: CalculatorServant {
            override func add(a: Int32, b: Int32) throws -> Int32 {
                return a + b
            }
            
            override func divide(numerator: Double, denominator: Double) throws -> Double {
                guard denominator != 0 else {
                    throw NSError(domain: "DivideByZero", code: 1)
                }
                return numerator / denominator
            }
        }
        
        let calc = TestCalculator()
        
        XCTAssertEqual(try? calc.add(a: 10, b: 20), 30)
        XCTAssertEqual(try? calc.add(a: -5, b: 5), 0)
        
        XCTAssertEqual(try? calc.divide(numerator: 10.0, denominator: 2.0), 5.0)
        XCTAssertThrowsError(try calc.divide(numerator: 10.0, denominator: 0.0))
    }
    
    func testShapeServiceServantSubclass() {
        class TestShapeService: ShapeServiceServant {
            var storage: [UInt32: Rectangle] = [:]
            
            override func getRectangle(id: UInt32) throws -> Rectangle {
                guard let rect = storage[id] else {
                    throw NSError(domain: "NotFound", code: 404)
                }
                return rect
            }
            
            override func setRectangle(id: UInt32, rect: Rectangle) throws {
                storage[id] = rect
            }
        }
        
        let service = TestShapeService()
        
        let rect = Rectangle(
            topLeft: Point(x: 0, y: 0),
            bottomRight: Point(x: 50, y: 50),
            color: .green
        )
        
        // Set
        XCTAssertNoThrow(try service.setRectangle(id: 1, rect: rect))
        
        // Get
        let retrieved = try? service.getRectangle(id: 1)
        XCTAssertNotNil(retrieved)
        XCTAssertEqual(retrieved?.color, .green)
        XCTAssertEqual(retrieved?.bottomRight.x, 50)
        
        // Not found
        XCTAssertThrowsError(try service.getRectangle(id: 999))
    }
}
