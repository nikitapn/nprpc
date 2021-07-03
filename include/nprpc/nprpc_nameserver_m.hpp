#ifndef NPRPC_NAMESERVER_M_
#define NPRPC_NAMESERVER_M_

struct NPRPC_NAMESERVER__M1 {
  nprpc::ObjectId _1;
  std::string _2;
};

namespace flat {
struct NPRPC_NAMESERVER__M1 {
  nprpc::detail::flat::ObjectId _1;
  ::flat::String _2;
};

class NPRPC_NAMESERVER__M1_Direct {
  boost::beast::flat_buffer& buffer_;
  const size_t offset_;

  auto& base() noexcept { return *reinterpret_cast<NPRPC_NAMESERVER__M1*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const NPRPC_NAMESERVER__M1*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  void* __data() noexcept { return (void*)&base(); }
  NPRPC_NAMESERVER__M1_Direct(boost::beast::flat_buffer& buffer, size_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  auto _1() noexcept { return nprpc::detail::flat::ObjectId_Direct(buffer_, offset_ + offsetof(NPRPC_NAMESERVER__M1, _1)); }
  void _2(const char* str) { new (&base()._2) ::flat::String(buffer_, str); }
  void _2(const std::string& str) { new (&base()._2) ::flat::String(buffer_, str); }
  auto _2() noexcept { return (::flat::Span<char>)base()._2; }
};
} // namespace flat

struct NPRPC_NAMESERVER__M2 {
  std::string _1;
};

namespace flat {
struct NPRPC_NAMESERVER__M2 {
  ::flat::String _1;
};

class NPRPC_NAMESERVER__M2_Direct {
  boost::beast::flat_buffer& buffer_;
  const size_t offset_;

  auto& base() noexcept { return *reinterpret_cast<NPRPC_NAMESERVER__M2*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const NPRPC_NAMESERVER__M2*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  void* __data() noexcept { return (void*)&base(); }
  NPRPC_NAMESERVER__M2_Direct(boost::beast::flat_buffer& buffer, size_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  void _1(const char* str) { new (&base()._1) ::flat::String(buffer_, str); }
  void _1(const std::string& str) { new (&base()._1) ::flat::String(buffer_, str); }
  auto _1() noexcept { return (::flat::Span<char>)base()._1; }
};
} // namespace flat

struct NPRPC_NAMESERVER__M3 {
  bool _1;
  nprpc::ObjectId _2;
};

namespace flat {
struct NPRPC_NAMESERVER__M3 {
  bool _1;
  nprpc::detail::flat::ObjectId _2;
};

class NPRPC_NAMESERVER__M3_Direct {
  boost::beast::flat_buffer& buffer_;
  const size_t offset_;

  auto& base() noexcept { return *reinterpret_cast<NPRPC_NAMESERVER__M3*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const NPRPC_NAMESERVER__M3*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  void* __data() noexcept { return (void*)&base(); }
  NPRPC_NAMESERVER__M3_Direct(boost::beast::flat_buffer& buffer, size_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const bool& _1() const noexcept { return base()._1;}
  bool& _1() noexcept { return base()._1;}
  auto _2() noexcept { return nprpc::detail::flat::ObjectId_Direct(buffer_, offset_ + offsetof(NPRPC_NAMESERVER__M3, _2)); }
};
} // namespace flat


#endif