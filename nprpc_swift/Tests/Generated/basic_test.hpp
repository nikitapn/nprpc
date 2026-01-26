#ifndef __NPRPC_BASIC_TEST_HPP__
#define __NPRPC_BASIC_TEST_HPP__

#include <nprpc/flat.hpp>
#include <nprpc/nprpc.hpp>
#include <nprpc/stream_writer.hpp>
#include <nprpc/stream_reader.hpp>

// Module export macro
#ifdef NPRPC_EXPORTS
#  define BASIC_TEST_API NPRPC_EXPORT_ATTR
#else
#  define BASIC_TEST_API NPRPC_IMPORT_ATTR
#endif

namespace swift::test {

class TestException : public ::nprpc::Exception {
public:
  std::string message;
  uint32_t code;

  TestException() : ::nprpc::Exception("TestException") {} 
  TestException(std::string _message, uint32_t _code)
    : ::nprpc::Exception("TestException")
    , message(_message)
    , code(_code)
  {
  }
};

namespace flat {
struct TestException {
  uint32_t __ex_id;
  ::nprpc::flat::String message;
  uint32_t code;
};

class TestException_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<TestException*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const TestException*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  TestException_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint32_t& __ex_id() const noexcept { return base().__ex_id;}
  uint32_t& __ex_id() noexcept { return base().__ex_id;}
  void message(const char* str) { new (&base().message) ::nprpc::flat::String(buffer_, str); }
  void message(const std::string& str) { new (&base().message) ::nprpc::flat::String(buffer_, str); }
  auto message() noexcept { return (::nprpc::flat::Span<char>)base().message; }
  auto message() const noexcept { return (::nprpc::flat::Span<const char>)base().message; }
  auto message_d() noexcept { return ::nprpc::flat::String_Direct1(buffer_, offset_ + offsetof(TestException, message)); }
  const uint32_t& code() const noexcept { return base().code;}
  uint32_t& code() noexcept { return base().code;}
};
} // namespace flat

struct Point {
  float x;
  float y;
};

namespace flat {
struct Point {
  float x;
  float y;
};

class Point_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<Point*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const Point*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  Point_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const float& x() const noexcept { return base().x;}
  float& x() noexcept { return base().x;}
  const float& y() const noexcept { return base().y;}
  float& y() noexcept { return base().y;}
};
} // namespace flat

struct Rectangle {
  Point top_left;
  float width;
  float height;
};

namespace flat {
struct Rectangle {
  flat::Point top_left;
  float width;
  float height;
};

class Rectangle_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<Rectangle*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const Rectangle*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  Rectangle_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  auto top_left() noexcept { return flat::Point_Direct(buffer_, offset_ + offsetof(Rectangle, top_left)); }
  const float& width() const noexcept { return base().width;}
  float& width() noexcept { return base().width;}
  const float& height() const noexcept { return base().height;}
  float& height() noexcept { return base().height;}
};
} // namespace flat

class BASIC_TEST_API ICalculator_Servant
  : public virtual ::nprpc::ObjectServant
{
public:
  static std::string_view _get_class() noexcept { return "basic_test/swift.test.Calculator"; }
  std::string_view get_class() const noexcept override { return ICalculator_Servant::_get_class(); }
  void dispatch(::nprpc::SessionContext& ctx, [[maybe_unused]] bool from_parent) override;
  virtual uint32_t Add (uint32_t a, uint32_t b) = 0;
  virtual uint32_t Multiply (uint32_t a, uint32_t b) = 0;
  virtual void Divide (uint32_t numerator, uint32_t denominator, float& result) = 0;
};

class BASIC_TEST_API Calculator
  : public virtual ::nprpc::Object
{
  const uint8_t interface_idx_;
public:
  using servant_t = ICalculator_Servant;

  Calculator(uint8_t interface_idx) : interface_idx_(interface_idx) {}
  uint32_t Add (uint32_t a, uint32_t b);
  uint32_t Multiply (uint32_t a, uint32_t b);
  void Divide (uint32_t numerator, uint32_t denominator, float& result);
};


#ifdef NPRPC_SWIFT_BRIDGE
// Swift servant bridge for Calculator
class Calculator_SwiftBridge : public ICalculator_Servant {
  void* swift_servant_;
public:
  Calculator_SwiftBridge(void* swift_servant) : swift_servant_(swift_servant) {}

  void Add(uint32_t a, uint32_t b) override;
  void Multiply(uint32_t a, uint32_t b) override;
  void Divide(uint32_t numerator, uint32_t denominator, float& result) override;
};

extern "C" {
  void Add_swift_trampoline(void* swift_servant, uint32_t a, uint32_t b);
  void Multiply_swift_trampoline(void* swift_servant, uint32_t a, uint32_t b);
  void Divide_swift_trampoline(void* swift_servant, uint32_t numerator, uint32_t denominator, float* result);
}
#endif // NPRPC_SWIFT_BRIDGE

class BASIC_TEST_API IGeometry_Servant
  : public virtual ::nprpc::ObjectServant
{
public:
  static std::string_view _get_class() noexcept { return "basic_test/swift.test.Geometry"; }
  std::string_view get_class() const noexcept override { return IGeometry_Servant::_get_class(); }
  void dispatch(::nprpc::SessionContext& ctx, [[maybe_unused]] bool from_parent) override;
  virtual float CalculateArea (flat::Rectangle_Direct rect) = 0;
  virtual void GetBounds (flat::Rectangle_Direct rect, float& area, float& perimeter) = 0;
};

class BASIC_TEST_API Geometry
  : public virtual ::nprpc::Object
{
  const uint8_t interface_idx_;
public:
  using servant_t = IGeometry_Servant;

  Geometry(uint8_t interface_idx) : interface_idx_(interface_idx) {}
  float CalculateArea (const Rectangle& rect);
  void GetBounds (const Rectangle& rect, float& area, float& perimeter);
};


#ifdef NPRPC_SWIFT_BRIDGE
// Swift servant bridge for Geometry
class Geometry_SwiftBridge : public IGeometry_Servant {
  void* swift_servant_;
public:
  Geometry_SwiftBridge(void* swift_servant) : swift_servant_(swift_servant) {}

  void CalculateArea(Rectangle const& rect) override;
  void GetBounds(Rectangle const& rect, float& area, float& perimeter) override;
};

extern "C" {
  void CalculateArea_swift_trampoline(void* swift_servant, void* rect);
  void GetBounds_swift_trampoline(void* swift_servant, void* rect, float* area, float* perimeter);
}
#endif // NPRPC_SWIFT_BRIDGE

namespace helper {
inline void assign_from_flat_CalculateArea_rect(::swift::test::flat::Rectangle_Direct& src, ::swift::test::Rectangle& dest) {
    memcpy(&dest, src.__data(), 16);
}
} // namespace basic_test::helper
namespace helpers {
inline void assign_from_flat_Rectangle(::swift::test::flat::Rectangle_Direct& src, ::swift::test::Rectangle& dest) {
    memcpy(&dest, src.__data(), 16);
}
inline void assign_from_cpp_Rectangle(::swift::test::flat::Rectangle_Direct& dest, const ::swift::test::Rectangle& src) {
    memcpy(dest.__data(), &src, 16);
}
} // namespace test::flat
namespace helpers {
inline void assign_from_flat_Point(::swift::test::flat::Point_Direct& src, ::swift::test::Point& dest) {
    memcpy(&dest, src.__data(), 8);
}
inline void assign_from_cpp_Point(::swift::test::flat::Point_Direct& dest, const ::swift::test::Point& src) {
    memcpy(dest.__data(), &src, 8);
}
} // namespace test::flat
} // module swift::test

#endif