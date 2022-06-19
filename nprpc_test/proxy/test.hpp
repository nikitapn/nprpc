#ifndef TEST_
#define TEST_

#include <nprpc/flat.hpp>
#include <nprpc/nprpc.hpp>

namespace test { 
struct AAA {
  uint32_t a;
  std::string b;
  std::string c;
};

namespace flat {
struct AAA {
  uint32_t a;
  ::nprpc::flat::String b;
  ::nprpc::flat::String c;
};

class AAA_Direct {
  ::nprpc::flat_buffer& buffer_;
  const size_t offset_;

  auto& base() noexcept { return *reinterpret_cast<AAA*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const AAA*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  void* __data() noexcept { return (void*)&base(); }
  AAA_Direct(::nprpc::flat_buffer& buffer, size_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint32_t& a() const noexcept { return base().a;}
  uint32_t& a() noexcept { return base().a;}
  void b(const char* str) { new (&base().b) ::nprpc::flat::String(buffer_, str); }
  void b(const std::string& str) { new (&base().b) ::nprpc::flat::String(buffer_, str); }
  auto b() noexcept { return (::nprpc::flat::Span<char>)base().b; }
  auto b() const noexcept { return (::nprpc::flat::Span<const char>)base().b; }
  auto b_d() noexcept {     return ::nprpc::flat::String_Direct1(buffer_, offset_ + offsetof(AAA, b));  }
  void c(const char* str) { new (&base().c) ::nprpc::flat::String(buffer_, str); }
  void c(const std::string& str) { new (&base().c) ::nprpc::flat::String(buffer_, str); }
  auto c() noexcept { return (::nprpc::flat::Span<char>)base().c; }
  auto c() const noexcept { return (::nprpc::flat::Span<const char>)base().c; }
  auto c_d() noexcept {     return ::nprpc::flat::String_Direct1(buffer_, offset_ + offsetof(AAA, c));  }
};
} // namespace flat

class ITestBasic_Servant
  : public virtual nprpc::ObjectServant
{
public:
  static std::string_view _get_class() noexcept { return "test/test.TestBasic"; }
  std::string_view get_class() const noexcept override { return ITestBasic_Servant::_get_class(); }
  void dispatch(nprpc::Buffers& bufs, nprpc::EndPoint remote_endpoint, bool from_parent, nprpc::ReferenceList& ref_list) override;
  virtual bool ReturnBoolean () = 0;
  virtual bool In (uint32_t a, ::nprpc::flat::Boolean b, ::nprpc::flat::Span<uint8_t> c) = 0;
  virtual void Out (uint32_t& a, ::nprpc::flat::Boolean& b, /*out*/::nprpc::flat::Vector_Direct1<uint8_t> c) = 0;
};

class TestBasic
  : public virtual nprpc::Object
{
  const uint8_t interface_idx_;
public:
  using servant_t = ITestBasic_Servant;

  TestBasic(uint8_t interface_idx) : interface_idx_(interface_idx) {}
  bool ReturnBoolean ();
  bool In (/*in*/uint32_t a, /*in*/bool b, /*in*/::nprpc::flat::Span<const uint8_t> c);
  void Out (/*out*/uint32_t& a, /*out*/bool& b, /*out*/std::vector<uint8_t>& c);
};

class ITestOptional_Servant
  : public virtual nprpc::ObjectServant
{
public:
  static std::string_view _get_class() noexcept { return "test/test.TestOptional"; }
  std::string_view get_class() const noexcept override { return ITestOptional_Servant::_get_class(); }
  void dispatch(nprpc::Buffers& bufs, nprpc::EndPoint remote_endpoint, bool from_parent, nprpc::ReferenceList& ref_list) override;
  virtual bool InEmpty (::nprpc::flat::Optional_Direct<uint32_t> a) = 0;
  virtual bool In (::nprpc::flat::Optional_Direct<uint32_t> a, ::nprpc::flat::Optional_Direct<test::flat::AAA, test::flat::AAA_Direct> b) = 0;
  virtual void OutEmpty (::nprpc::flat::Optional_Direct<uint32_t> a) = 0;
  virtual void Out (::nprpc::flat::Optional_Direct<uint32_t> a) = 0;
};

class TestOptional
  : public virtual nprpc::Object
{
  const uint8_t interface_idx_;
public:
  using servant_t = ITestOptional_Servant;

  TestOptional(uint8_t interface_idx) : interface_idx_(interface_idx) {}
  bool InEmpty (/*in*/const std::optional<uint32_t>& a);
  bool In (/*in*/const std::optional<uint32_t>& a, /*in*/const std::optional<test::AAA>& b);
  void OutEmpty (/*out*/std::optional<uint32_t>& a);
  void Out (/*out*/std::optional<uint32_t>& a);
};

struct CCC {
  std::string a;
  std::string b;
  std::optional<bool> c;
};

namespace flat {
struct CCC {
  ::nprpc::flat::String a;
  ::nprpc::flat::String b;
  ::nprpc::flat::Optional<::nprpc::flat::Boolean> c;
};

class CCC_Direct {
  ::nprpc::flat_buffer& buffer_;
  const size_t offset_;

  auto& base() noexcept { return *reinterpret_cast<CCC*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const CCC*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  void* __data() noexcept { return (void*)&base(); }
  CCC_Direct(::nprpc::flat_buffer& buffer, size_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  void a(const char* str) { new (&base().a) ::nprpc::flat::String(buffer_, str); }
  void a(const std::string& str) { new (&base().a) ::nprpc::flat::String(buffer_, str); }
  auto a() noexcept { return (::nprpc::flat::Span<char>)base().a; }
  auto a() const noexcept { return (::nprpc::flat::Span<const char>)base().a; }
  auto a_d() noexcept {     return ::nprpc::flat::String_Direct1(buffer_, offset_ + offsetof(CCC, a));  }
  void b(const char* str) { new (&base().b) ::nprpc::flat::String(buffer_, str); }
  void b(const std::string& str) { new (&base().b) ::nprpc::flat::String(buffer_, str); }
  auto b() noexcept { return (::nprpc::flat::Span<char>)base().b; }
  auto b() const noexcept { return (::nprpc::flat::Span<const char>)base().b; }
  auto b_d() noexcept {     return ::nprpc::flat::String_Direct1(buffer_, offset_ + offsetof(CCC, b));  }
  auto c() noexcept { return ::nprpc::flat::Optional_Direct<::nprpc::flat::Boolean>(buffer_, offset_ + offsetof(CCC, c));  }
};
} // namespace flat

struct BBB {
  std::vector<AAA> a;
  std::vector<CCC> b;
};

namespace flat {
struct BBB {
  ::nprpc::flat::Vector<test::flat::AAA> a;
  ::nprpc::flat::Vector<test::flat::CCC> b;
};

class BBB_Direct {
  ::nprpc::flat_buffer& buffer_;
  const size_t offset_;

  auto& base() noexcept { return *reinterpret_cast<BBB*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const BBB*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  void* __data() noexcept { return (void*)&base(); }
  BBB_Direct(::nprpc::flat_buffer& buffer, size_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  void a(size_t elements_size) { new (&base().a) ::nprpc::flat::Vector<test::flat::AAA>(buffer_, elements_size); }
  auto a_d() noexcept { return ::nprpc::flat::Vector_Direct2<test::flat::AAA, test::flat::AAA_Direct>(buffer_, offset_ + offsetof(BBB, a)); }
  auto a() noexcept { return ::nprpc::flat::Span_ref<test::flat::AAA, test::flat::AAA_Direct>(buffer_, base().a.range(buffer_.data().data())); }
  void b(size_t elements_size) { new (&base().b) ::nprpc::flat::Vector<test::flat::CCC>(buffer_, elements_size); }
  auto b_d() noexcept { return ::nprpc::flat::Vector_Direct2<test::flat::CCC, test::flat::CCC_Direct>(buffer_, offset_ + offsetof(BBB, b)); }
  auto b() noexcept { return ::nprpc::flat::Span_ref<test::flat::CCC, test::flat::CCC_Direct>(buffer_, base().b.range(buffer_.data().data())); }
};
} // namespace flat

class ITestNested_Servant
  : public virtual nprpc::ObjectServant
{
public:
  static std::string_view _get_class() noexcept { return "test/test.TestNested"; }
  std::string_view get_class() const noexcept override { return ITestNested_Servant::_get_class(); }
  void dispatch(nprpc::Buffers& bufs, nprpc::EndPoint remote_endpoint, bool from_parent, nprpc::ReferenceList& ref_list) override;
  virtual void Out (::nprpc::flat::Optional_Direct<test::flat::BBB, test::flat::BBB_Direct> a) = 0;
};

class TestNested
  : public virtual nprpc::Object
{
  const uint8_t interface_idx_;
public:
  using servant_t = ITestNested_Servant;

  TestNested(uint8_t interface_idx) : interface_idx_(interface_idx) {}
  void Out (/*out*/std::optional<test::BBB>& a);
};

} // namespace test

namespace test::helper {
template<::nprpc::IterableCollection T>
void assign_from_cpp_Out_a(::nprpc::flat::Optional_Direct<test::flat::BBB, test::flat::BBB_Direct>& dest, const T & src) {
    if (src) {
      dest.alloc();
      auto value = dest.value();
      value.a(src.value().a.size());
        auto span = value.a();
        auto it = src.value().a.begin();
        for (auto e : span) {
        auto __ptr = ::nprpc::make_wrapper1(*it);
          e.a() = __ptr->a;
          e.b(__ptr->b);
          e.c(__ptr->c);
        ++it;
        }
        value.b(src.value().b.size());
          auto span = value.b();
          auto it = src.value().b.begin();
          for (auto e : span) {
          auto __ptr = ::nprpc::make_wrapper1(*it);
            e.a(__ptr->a);
            e.b(__ptr->b);
          if (__ptr->c) {
              e.c().alloc();
              e.c().value() = __ptr->c.value();
          } else { 
              e.c().set_nullopt();
          }
          ++it;
          }
    } else { 
          dest.set_nullopt();
    }
}
} // namespace test::helper

#endif