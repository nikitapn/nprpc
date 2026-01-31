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

enum class Color : uint32_t {
  Red,
  Green,
  Blue
};
struct Point {
  int32_t x;
  int32_t y;
};

namespace flat {
struct Point {
  int32_t x;
  int32_t y;
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
  const int32_t& x() const noexcept { return base().x;}
  int32_t& x() noexcept { return base().x;}
  const int32_t& y() const noexcept { return base().y;}
  int32_t& y() noexcept { return base().y;}
};
} // namespace flat

struct Rectangle {
  Point topLeft;
  Point bottomRight;
  Color color;
};

namespace flat {
struct Rectangle {
  flat::Point topLeft;
  flat::Point bottomRight;
  Color color;
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
  auto topLeft() noexcept { return flat::Point_Direct(buffer_, offset_ + offsetof(Rectangle, topLeft)); }
  auto bottomRight() noexcept { return flat::Point_Direct(buffer_, offset_ + offsetof(Rectangle, bottomRight)); }
  const Color& color() const noexcept { return base().color;}
  Color& color() noexcept { return base().color;}
};
} // namespace flat

class BASIC_TEST_API IShapeService_Servant
  : public virtual ::nprpc::ObjectServant
{
public:
  static std::string_view _get_class() noexcept { return "basic_test/swift.test.ShapeService"; }
  std::string_view get_class() const noexcept override { return IShapeService_Servant::_get_class(); }
  void dispatch(::nprpc::SessionContext& ctx, [[maybe_unused]] bool from_parent) override;
  virtual void getRectangle (uint32_t id, flat::Rectangle_Direct rect) = 0;
  virtual void setRectangle (uint32_t id, flat::Rectangle_Direct rect) = 0;
};

class BASIC_TEST_API ShapeService
  : public virtual ::nprpc::Object
{
  const uint8_t interface_idx_;
public:
  using servant_t = IShapeService_Servant;

  ShapeService(uint8_t interface_idx) : interface_idx_(interface_idx) {}
  void getRectangle (uint32_t id, Rectangle& rect);
  void setRectangle (uint32_t id, const Rectangle& rect);
};


#ifdef NPRPC_SWIFT_BRIDGE
// Swift servant bridge for ShapeService
class ShapeService_SwiftBridge : public IShapeService_Servant {
  void* swift_servant_;
public:
  ShapeService_SwiftBridge(void* swift_servant) : swift_servant_(swift_servant) {}

  void getRectangle(uint32_t id, Rectangle& rect) override;
  void setRectangle(uint32_t id, Rectangle const& rect) override;
};

extern "C" {
  void getRectangle_swift_trampoline(void* swift_servant, uint32_t id, void* rect);
  void setRectangle_swift_trampoline(void* swift_servant, uint32_t id, void* rect);
}
#endif // NPRPC_SWIFT_BRIDGE

namespace helper {
inline void assign_from_cpp_getRectangle_rect(::swift::test::flat::Rectangle_Direct& dest, const ::swift::test::Rectangle& src) {
  memcpy(dest.__data(), &src, 20);
}
inline void assign_from_flat_setRectangle_rect(::swift::test::flat::Rectangle_Direct& src, ::swift::test::Rectangle& dest) {
  memcpy(&dest, src.__data(), 20);
}
} // namespace basic_test::helper
} // module swift::test

#endif