#include "test_swift_gen.hpp"
#include <nprpc/impl/nprpc_impl.hpp>

void test_swift_gen_throw_exception(::nprpc::flat_buffer& buf);

namespace Test {

namespace {
struct test_swift_gen_M1 {
  int32_t _1;
  int32_t _2;
};

class test_swift_gen_M1_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<test_swift_gen_M1*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const test_swift_gen_M1*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  test_swift_gen_M1_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const int32_t& _1() const noexcept { return base()._1;}
  int32_t& _1() noexcept { return base()._1;}
  const int32_t& _2() const noexcept { return base()._2;}
  int32_t& _2() noexcept { return base()._2;}
};

struct test_swift_gen_M2 {
  int32_t _1;
};

class test_swift_gen_M2_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<test_swift_gen_M2*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const test_swift_gen_M2*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  test_swift_gen_M2_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const int32_t& _1() const noexcept { return base()._1;}
  int32_t& _1() noexcept { return base()._1;}
};

struct test_swift_gen_M3 {
  double _1;
  double _2;
};

class test_swift_gen_M3_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<test_swift_gen_M3*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const test_swift_gen_M3*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  test_swift_gen_M3_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const double& _1() const noexcept { return base()._1;}
  double& _1() noexcept { return base()._1;}
  const double& _2() const noexcept { return base()._2;}
  double& _2() noexcept { return base()._2;}
};

struct test_swift_gen_M4 {
  double _1;
};

class test_swift_gen_M4_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<test_swift_gen_M4*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const test_swift_gen_M4*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  test_swift_gen_M4_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const double& _1() const noexcept { return base()._1;}
  double& _1() noexcept { return base()._1;}
};

struct test_swift_gen_M5 {
  uint32_t _1;
};

class test_swift_gen_M5_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<test_swift_gen_M5*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const test_swift_gen_M5*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  test_swift_gen_M5_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint32_t& _1() const noexcept { return base()._1;}
  uint32_t& _1() noexcept { return base()._1;}
};

struct test_swift_gen_M6 {
  ::Test::flat::Rectangle _1;
};

class test_swift_gen_M6_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<test_swift_gen_M6*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const test_swift_gen_M6*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  test_swift_gen_M6_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  auto _1() noexcept { return ::Test::flat::Rectangle_Direct(buffer_, offset_ + offsetof(test_swift_gen_M6, _1)); }
};

struct test_swift_gen_M7 {
  uint32_t _1;
  ::Test::flat::Rectangle _2;
};

class test_swift_gen_M7_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<test_swift_gen_M7*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const test_swift_gen_M7*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  test_swift_gen_M7_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint32_t& _1() const noexcept { return base()._1;}
  uint32_t& _1() noexcept { return base()._1;}
  auto _2() noexcept { return ::Test::flat::Rectangle_Direct(buffer_, offset_ + offsetof(test_swift_gen_M7, _2)); }
};


} // 

#ifdef NPRPC_SWIFT_BRIDGE
// C++ marshal/unmarshal for Swift bridge
static void marshal_Point(void* buffer, int offset, const Point& data) {
  auto* ptr = static_cast<std::byte*>(buffer) + offset;
  *reinterpret_cast<int32_t*>(ptr + 0) = data.x;
  *reinterpret_cast<int32_t*>(ptr + 4) = data.y;
}

static Point unmarshal_Point(const void* buffer, int offset) {
  const auto* ptr = static_cast<const std::byte*>(buffer) + offset;
  return Point{
    /*.x = */ *reinterpret_cast<const int32_t*>(ptr + 0),
    /*.y = */ *reinterpret_cast<const int32_t*>(ptr + 4)
  };
}
#endif // NPRPC_SWIFT_BRIDGE

#ifdef NPRPC_SWIFT_BRIDGE
// C++ marshal/unmarshal for Swift bridge
static void marshal_Rectangle(void* buffer, int offset, const Rectangle& data) {
  auto* ptr = static_cast<std::byte*>(buffer) + offset;
  marshal_Point(buffer, offset + 0, data.topLeft);
  marshal_Point(buffer, offset + 8, data.bottomRight);
  *reinterpret_cast<int32_t*>(ptr + 16) = static_cast<int32_t>(data.color);
}

static Rectangle unmarshal_Rectangle(const void* buffer, int offset) {
  const auto* ptr = static_cast<const std::byte*>(buffer) + offset;
  return Rectangle{
    /*.topLeft = */ unmarshal_Point(buffer, offset + 0),
    /*.bottomRight = */ unmarshal_Point(buffer, offset + 8),
    /*.color = */ static_cast<Color>(*reinterpret_cast<const int32_t*>(ptr + 16))
  };
}
#endif // NPRPC_SWIFT_BRIDGE

void Calculator::add(int32_t a, int32_t b, int32_t& result) {
  ::nprpc::flat_buffer buf;
  auto session = ::nprpc::impl::g_rpc->get_session(this->get_endpoint());
  if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(session->ctx(), buf, 40))
    buf.prepare(40);
  {
    buf.commit(40);
    static_cast<::nprpc::impl::Header*>(buf.data().data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;
  static_cast<::nprpc::impl::Header*>(buf.data().data())->msg_type =::nprpc::impl::MessageType::Request;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));
  __ch.object_id() = this->object_id();
  __ch.poa_idx() = this->poa_idx();
  __ch.interface_idx() = interface_idx_;
  __ch.function_idx() = 0;
  test_swift_gen_M1_Direct _(buf,32);
  _._1() = a;
  _._2() = b;
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  session->send_receive(buf, this->get_timeout());
  auto std_reply = ::nprpc::impl::handle_standart_reply(buf);
  if (std_reply != -1) {
    throw ::nprpc::Exception("Unknown Error");
  }
  test_swift_gen_M2_Direct out(buf, sizeof(::nprpc::impl::Header));
    result = out._1();
}

void Calculator::divide(double numerator, double denominator, double& result) {
  ::nprpc::flat_buffer buf;
  auto session = ::nprpc::impl::g_rpc->get_session(this->get_endpoint());
  if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(session->ctx(), buf, 48))
    buf.prepare(48);
  {
    buf.commit(48);
    static_cast<::nprpc::impl::Header*>(buf.data().data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;
  static_cast<::nprpc::impl::Header*>(buf.data().data())->msg_type =::nprpc::impl::MessageType::Request;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));
  __ch.object_id() = this->object_id();
  __ch.poa_idx() = this->poa_idx();
  __ch.interface_idx() = interface_idx_;
  __ch.function_idx() = 1;
  test_swift_gen_M3_Direct _(buf,32);
  _._1() = numerator;
  _._2() = denominator;
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  session->send_receive(buf, this->get_timeout());
  auto std_reply = ::nprpc::impl::handle_standart_reply(buf);
  if (std_reply != -1) {
    throw ::nprpc::Exception("Unknown Error");
  }
  test_swift_gen_M4_Direct out(buf, sizeof(::nprpc::impl::Header));
    result = out._1();
}

void ICalculator_Servant::dispatch(::nprpc::SessionContext& ctx, [[maybe_unused]] bool from_parent) {
  assert(ctx.rx_buffer != nullptr);
  auto* header = static_cast<::nprpc::impl::Header*>(ctx.rx_buffer->data().data());
  if (header->msg_id == ::nprpc::impl::MessageId::StreamInitialization) {
    ::nprpc::impl::flat::StreamInit_Direct init(*ctx.rx_buffer, sizeof(::nprpc::impl::Header));
    switch(init.func_idx()) {
      default:
        // Error
        break;
    }
    return;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(*ctx.rx_buffer, sizeof(::nprpc::impl::Header));
  switch(__ch.function_idx()) {
    case 0: {
      assert(ctx.rx_buffer != nullptr);
      test_swift_gen_M1_Direct ia(*ctx.rx_buffer, 32);
      int32_t _out_1;
      add(ia._1(), ia._2(), _out_1);
      assert(ctx.tx_buffer != nullptr);
      auto& obuf = *ctx.tx_buffer;
      obuf.consume(obuf.size());
      if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(ctx, obuf, 20))
        obuf.prepare(20);
      obuf.commit(20);
      test_swift_gen_M2_Direct oa(obuf,16);
      oa._1() = _out_1;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::BlockResponse;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;
      break;
    }
    case 1: {
      assert(ctx.rx_buffer != nullptr);
      test_swift_gen_M3_Direct ia(*ctx.rx_buffer, 32);
      double _out_1;
      divide(ia._1(), ia._2(), _out_1);
      assert(ctx.tx_buffer != nullptr);
      auto& obuf = *ctx.tx_buffer;
      obuf.consume(obuf.size());
      if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(ctx, obuf, 24))
        obuf.prepare(24);
      obuf.commit(24);
      test_swift_gen_M4_Direct oa(obuf,16);
      oa._1() = _out_1;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::BlockResponse;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;
      break;
    }
    default:
      ::nprpc::impl::make_simple_answer(ctx, ::nprpc::impl::MessageId::Error_UnknownFunctionIdx);
  }
}


#ifdef NPRPC_SWIFT_BRIDGE
// Swift bridge implementation for Calculator
void Test::Calculator_SwiftBridge::add(int32_t a, int32_t b, int32_t& result) {
  add_swift_trampoline(swift_servant_, a, b, &result);
}

void Test::Calculator_SwiftBridge::divide(double numerator, double denominator, double& result) {
  divide_swift_trampoline(swift_servant_, numerator, denominator, &result);
}

#endif // NPRPC_SWIFT_BRIDGE

void ShapeService::getRectangle(uint32_t id, Rectangle& rect) {
  ::nprpc::flat_buffer buf;
  auto session = ::nprpc::impl::g_rpc->get_session(this->get_endpoint());
  if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(session->ctx(), buf, 36))
    buf.prepare(36);
  {
    buf.commit(36);
    static_cast<::nprpc::impl::Header*>(buf.data().data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;
  static_cast<::nprpc::impl::Header*>(buf.data().data())->msg_type =::nprpc::impl::MessageType::Request;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));
  __ch.object_id() = this->object_id();
  __ch.poa_idx() = this->poa_idx();
  __ch.interface_idx() = interface_idx_;
  __ch.function_idx() = 0;
  test_swift_gen_M5_Direct _(buf,32);
  _._1() = id;
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  session->send_receive(buf, this->get_timeout());
  auto std_reply = ::nprpc::impl::handle_standart_reply(buf);
  if (std_reply != -1) {
    throw ::nprpc::Exception("Unknown Error");
  }
  test_swift_gen_M6_Direct out(buf, sizeof(::nprpc::impl::Header));
    memcpy(&rect, out._1().__data(), 20);
}

void ShapeService::setRectangle(uint32_t id, const Rectangle& rect) {
  ::nprpc::flat_buffer buf;
  auto session = ::nprpc::impl::g_rpc->get_session(this->get_endpoint());
  if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(session->ctx(), buf, 56))
    buf.prepare(56);
  {
    buf.commit(56);
    static_cast<::nprpc::impl::Header*>(buf.data().data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;
  static_cast<::nprpc::impl::Header*>(buf.data().data())->msg_type =::nprpc::impl::MessageType::Request;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));
  __ch.object_id() = this->object_id();
  __ch.poa_idx() = this->poa_idx();
  __ch.interface_idx() = interface_idx_;
  __ch.function_idx() = 1;
  test_swift_gen_M7_Direct _(buf,32);
  _._1() = id;
  memcpy(_._2().__data(), &rect, 20);
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  session->send_receive(buf, this->get_timeout());
  auto std_reply = ::nprpc::impl::handle_standart_reply(buf);
  if (std_reply != 0) {
    throw ::nprpc::Exception("Unknown Error");
  }
}

void IShapeService_Servant::dispatch(::nprpc::SessionContext& ctx, [[maybe_unused]] bool from_parent) {
  assert(ctx.rx_buffer != nullptr);
  auto* header = static_cast<::nprpc::impl::Header*>(ctx.rx_buffer->data().data());
  if (header->msg_id == ::nprpc::impl::MessageId::StreamInitialization) {
    ::nprpc::impl::flat::StreamInit_Direct init(*ctx.rx_buffer, sizeof(::nprpc::impl::Header));
    switch(init.func_idx()) {
      default:
        // Error
        break;
    }
    return;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(*ctx.rx_buffer, sizeof(::nprpc::impl::Header));
  switch(__ch.function_idx()) {
    case 0: {
      assert(ctx.rx_buffer != nullptr);
      test_swift_gen_M5_Direct ia(*ctx.rx_buffer, 32);
      Rectangle _out_1;
      getRectangle(ia._1(), _out_1);
      assert(ctx.tx_buffer != nullptr);
      auto& obuf = *ctx.tx_buffer;
      obuf.consume(obuf.size());
      if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(ctx, obuf, 36))
        obuf.prepare(36);
      obuf.commit(36);
      test_swift_gen_M6_Direct oa(obuf,16);
      memcpy(oa._1().__data(), &_out_1, 20);
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::BlockResponse;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;
      break;
    }
    case 1: {
      assert(ctx.rx_buffer != nullptr);
      test_swift_gen_M7_Direct ia(*ctx.rx_buffer, 32);
      setRectangle(ia._1(), ia._2());
      ::nprpc::impl::make_simple_answer(ctx, nprpc::impl::MessageId::Success);
      break;
    }
    default:
      ::nprpc::impl::make_simple_answer(ctx, ::nprpc::impl::MessageId::Error_UnknownFunctionIdx);
  }
}


#ifdef NPRPC_SWIFT_BRIDGE
// Swift bridge implementation for ShapeService
void Test::ShapeService_SwiftBridge::getRectangle(uint32_t id, Rectangle& rect) {
  alignas(4) std::byte __rect_buf[20];
  getRectangle_swift_trampoline(swift_servant_, id, __rect_buf);
  rect = unmarshal_Rectangle(__rect_buf, 0);
}

void Test::ShapeService_SwiftBridge::setRectangle(uint32_t id, Rectangle const& rect) {
  alignas(4) std::byte __rect_buf[20];
  marshal_Rectangle(__rect_buf, 0, rect);
  setRectangle_swift_trampoline(swift_servant_, id, __rect_buf);
}

#endif // NPRPC_SWIFT_BRIDGE

} // module Test
