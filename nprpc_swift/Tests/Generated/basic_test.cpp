#include "basic_test.hpp"
#include <nprpc/impl/nprpc_impl.hpp>

void basic_test_throw_exception(::nprpc::flat_buffer& buf);

namespace swift::test {

namespace {
struct basic_test_M1 {
  uint32_t _1;
  uint32_t _2;
};

class basic_test_M1_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<basic_test_M1*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const basic_test_M1*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  basic_test_M1_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint32_t& _1() const noexcept { return base()._1;}
  uint32_t& _1() noexcept { return base()._1;}
  const uint32_t& _2() const noexcept { return base()._2;}
  uint32_t& _2() noexcept { return base()._2;}
};

struct basic_test_M2 {
  uint32_t _1;
};

class basic_test_M2_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<basic_test_M2*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const basic_test_M2*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  basic_test_M2_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint32_t& _1() const noexcept { return base()._1;}
  uint32_t& _1() noexcept { return base()._1;}
};

struct basic_test_M3 {
  float _1;
};

class basic_test_M3_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<basic_test_M3*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const basic_test_M3*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  basic_test_M3_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const float& _1() const noexcept { return base()._1;}
  float& _1() noexcept { return base()._1;}
};

struct basic_test_M4 {
  ::swift::test::flat::Rectangle _1;
};

class basic_test_M4_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<basic_test_M4*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const basic_test_M4*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  basic_test_M4_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  auto _1() noexcept { return ::swift::test::flat::Rectangle_Direct(buffer_, offset_ + offsetof(basic_test_M4, _1)); }
};

struct basic_test_M5 {
  float _1;
  float _2;
};

class basic_test_M5_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<basic_test_M5*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const basic_test_M5*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  basic_test_M5_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const float& _1() const noexcept { return base()._1;}
  float& _1() noexcept { return base()._1;}
  const float& _2() const noexcept { return base()._2;}
  float& _2() noexcept { return base()._2;}
};


} // 

#ifdef NPRPC_SWIFT_BRIDGE
// C++ marshal/unmarshal for Swift bridge
static void marshal_Point(void* buffer, int offset, const Point& data) {
  auto* ptr = static_cast<std::byte*>(buffer) + offset;
  *reinterpret_cast<float*>(ptr + 0) = data.x;
  *reinterpret_cast<float*>(ptr + 4) = data.y;
}

static Point unmarshal_Point(const void* buffer, int offset) {
  const auto* ptr = static_cast<const std::byte*>(buffer) + offset;
  return Point{
    /*.x = */ *reinterpret_cast<const float*>(ptr + 0),
    /*.y = */ *reinterpret_cast<const float*>(ptr + 4)
  };
}
#endif // NPRPC_SWIFT_BRIDGE

#ifdef NPRPC_SWIFT_BRIDGE
// C++ marshal/unmarshal for Swift bridge
static void marshal_Rectangle(void* buffer, int offset, const Rectangle& data) {
  auto* ptr = static_cast<std::byte*>(buffer) + offset;
  marshal_Point(buffer, offset + 0, data.top_left);
  *reinterpret_cast<float*>(ptr + 8) = data.width;
  *reinterpret_cast<float*>(ptr + 12) = data.height;
}

static Rectangle unmarshal_Rectangle(const void* buffer, int offset) {
  const auto* ptr = static_cast<const std::byte*>(buffer) + offset;
  return Rectangle{
    /*.top_left = */ unmarshal_Point(buffer, offset + 0),
    /*.width = */ *reinterpret_cast<const float*>(ptr + 8),
    /*.height = */ *reinterpret_cast<const float*>(ptr + 12)
  };
}
#endif // NPRPC_SWIFT_BRIDGE

uint32_t Calculator::Add(uint32_t a, uint32_t b) {
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
  basic_test_M1_Direct _(buf,32);
  _._1() = a;
  _._2() = b;
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  session->send_receive(buf, this->get_timeout());
  auto std_reply = ::nprpc::impl::handle_standart_reply(buf);
  if (std_reply != -1) {
    throw ::nprpc::Exception("Unknown Error");
  }
  basic_test_M2_Direct out(buf, sizeof(::nprpc::impl::Header));
    uint32_t __ret_value;
    __ret_value = out._1();
  return __ret_value;
}

uint32_t Calculator::Multiply(uint32_t a, uint32_t b) {
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
  __ch.function_idx() = 1;
  basic_test_M1_Direct _(buf,32);
  _._1() = a;
  _._2() = b;
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  session->send_receive(buf, this->get_timeout());
  auto std_reply = ::nprpc::impl::handle_standart_reply(buf);
  if (std_reply != -1) {
    throw ::nprpc::Exception("Unknown Error");
  }
  basic_test_M2_Direct out(buf, sizeof(::nprpc::impl::Header));
    uint32_t __ret_value;
    __ret_value = out._1();
  return __ret_value;
}

void Calculator::Divide(uint32_t numerator, uint32_t denominator, float& result) {
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
  __ch.function_idx() = 2;
  basic_test_M1_Direct _(buf,32);
  _._1() = numerator;
  _._2() = denominator;
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  session->send_receive(buf, this->get_timeout());
  auto std_reply = ::nprpc::impl::handle_standart_reply(buf);
  if (std_reply == 1) basic_test_throw_exception(buf);
  if (std_reply != -1) {
    throw ::nprpc::Exception("Unknown Error");
  }
  basic_test_M3_Direct out(buf, sizeof(::nprpc::impl::Header));
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
      basic_test_M1_Direct ia(*ctx.rx_buffer, 32);
      uint32_t __ret_val;
      __ret_val = Add(ia._1(), ia._2());
      assert(ctx.tx_buffer != nullptr);
      auto& obuf = *ctx.tx_buffer;
      obuf.consume(obuf.size());
      if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(ctx, obuf, 20))
        obuf.prepare(20);
      obuf.commit(20);
      basic_test_M2_Direct oa(obuf,16);
      oa._1() = __ret_val;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::BlockResponse;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;
      break;
    }
    case 1: {
      assert(ctx.rx_buffer != nullptr);
      basic_test_M1_Direct ia(*ctx.rx_buffer, 32);
      uint32_t __ret_val;
      __ret_val = Multiply(ia._1(), ia._2());
      assert(ctx.tx_buffer != nullptr);
      auto& obuf = *ctx.tx_buffer;
      obuf.consume(obuf.size());
      if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(ctx, obuf, 20))
        obuf.prepare(20);
      obuf.commit(20);
      basic_test_M2_Direct oa(obuf,16);
      oa._1() = __ret_val;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::BlockResponse;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;
      break;
    }
    case 2: {
      assert(ctx.rx_buffer != nullptr);
      basic_test_M1_Direct ia(*ctx.rx_buffer, 32);
      float _out_1;
      try {
        Divide(ia._1(), ia._2(), _out_1);
      }
      catch(::swift::test::TestException& e) {
        assert(ctx.tx_buffer != nullptr);
        auto& obuf = *ctx.tx_buffer;
        obuf.consume(obuf.size());
        if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(ctx, obuf, 32))
          obuf.prepare(32);
        obuf.commit(32);
        ::swift::test::flat::TestException_Direct oa(obuf,16);
        oa.__ex_id() = 0;
        oa.message(e.message);
        oa.code() = e.code;
        static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);
        static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::Exception;
        static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;
        return;
      }
      assert(ctx.tx_buffer != nullptr);
      auto& obuf = *ctx.tx_buffer;
      obuf.consume(obuf.size());
      if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(ctx, obuf, 20))
        obuf.prepare(20);
      obuf.commit(20);
      basic_test_M3_Direct oa(obuf,16);
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
void swift.test::Calculator_SwiftBridge::Add(uint32_t a, uint32_t b) {
  Add_swift_trampoline(swift_servant_, a, b);
}

void swift.test::Calculator_SwiftBridge::Multiply(uint32_t a, uint32_t b) {
  Multiply_swift_trampoline(swift_servant_, a, b);
}

void swift.test::Calculator_SwiftBridge::Divide(uint32_t numerator, uint32_t denominator, float& result) {
  Divide_swift_trampoline(swift_servant_, numerator, denominator, &result);
}

#endif // NPRPC_SWIFT_BRIDGE

float Geometry::CalculateArea(const Rectangle& rect) {
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
  __ch.function_idx() = 0;
  basic_test_M4_Direct _(buf,32);
  memcpy(_._1().__data(), &rect, 16);
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  session->send_receive(buf, this->get_timeout());
  auto std_reply = ::nprpc::impl::handle_standart_reply(buf);
  if (std_reply != -1) {
    throw ::nprpc::Exception("Unknown Error");
  }
  basic_test_M3_Direct out(buf, sizeof(::nprpc::impl::Header));
    float __ret_value;
    __ret_value = out._1();
  return __ret_value;
}

void Geometry::GetBounds(const Rectangle& rect, float& area, float& perimeter) {
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
  basic_test_M4_Direct _(buf,32);
  memcpy(_._1().__data(), &rect, 16);
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  session->send_receive(buf, this->get_timeout());
  auto std_reply = ::nprpc::impl::handle_standart_reply(buf);
  if (std_reply != -1) {
    throw ::nprpc::Exception("Unknown Error");
  }
  basic_test_M5_Direct out(buf, sizeof(::nprpc::impl::Header));
    area = out._1();
    perimeter = out._2();
}

void IGeometry_Servant::dispatch(::nprpc::SessionContext& ctx, [[maybe_unused]] bool from_parent) {
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
      basic_test_M4_Direct ia(*ctx.rx_buffer, 32);
      float __ret_val;
      __ret_val = CalculateArea(ia._1());
      assert(ctx.tx_buffer != nullptr);
      auto& obuf = *ctx.tx_buffer;
      obuf.consume(obuf.size());
      if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(ctx, obuf, 20))
        obuf.prepare(20);
      obuf.commit(20);
      basic_test_M3_Direct oa(obuf,16);
      oa._1() = __ret_val;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::BlockResponse;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;
      break;
    }
    case 1: {
      assert(ctx.rx_buffer != nullptr);
      basic_test_M4_Direct ia(*ctx.rx_buffer, 32);
      float _out_1;
      float _out_2;
      GetBounds(ia._1(), _out_1, _out_2);
      assert(ctx.tx_buffer != nullptr);
      auto& obuf = *ctx.tx_buffer;
      obuf.consume(obuf.size());
      if (!::nprpc::impl::g_rpc->prepare_zero_copy_buffer(ctx, obuf, 24))
        obuf.prepare(24);
      obuf.commit(24);
      basic_test_M5_Direct oa(obuf,16);
      oa._1() = _out_1;
      oa._2() = _out_2;
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
// Swift bridge implementation for Geometry
void swift.test::Geometry_SwiftBridge::CalculateArea(Rectangle const& rect) {
  alignas(4) std::byte __rect_buf[16];
  marshal_Rectangle(__rect_buf, 0, rect);
  CalculateArea_swift_trampoline(swift_servant_, __rect_buf);
}

void swift.test::Geometry_SwiftBridge::GetBounds(Rectangle const& rect, float& area, float& perimeter) {
  alignas(4) std::byte __rect_buf[16];
  marshal_Rectangle(__rect_buf, 0, rect);
  GetBounds_swift_trampoline(swift_servant_, __rect_buf, &area, &perimeter);
}

#endif // NPRPC_SWIFT_BRIDGE

} // module swift::test

void basic_test_throw_exception(::nprpc::flat_buffer& buf) { 
  switch(*(uint32_t*)( (char*)buf.data().data() + sizeof(::nprpc::impl::Header)) ) {
  case 0:
  {
    ::swift::test::flat::TestException_Direct ex_flat(buf, sizeof(::nprpc::impl::Header));
    ::swift::test::TestException ex;
    ex.message = (std::string_view)ex_flat.message();
    ex.code = ex_flat.code();
    throw ex;
  }
  default:
    throw std::runtime_error("unknown rpc exception");
  }
}
