#include "test.hpp"
#include <nprpc/impl/nprpc_impl.hpp>

void test_throw_exception(::nprpc::flat_buffer& buf);

namespace {
struct test_M1 {
  ::nprpc::flat::Boolean _1;
};

class test_M1_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<test_M1*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const test_M1*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  test_M1_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const ::nprpc::flat::Boolean& _1() const noexcept { return base()._1;}
  ::nprpc::flat::Boolean& _1() noexcept { return base()._1;}
};

struct test_M2 {
  uint32_t _1;
  ::nprpc::flat::Boolean _2;
  ::nprpc::flat::Vector<uint8_t> _3;
};

class test_M2_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<test_M2*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const test_M2*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  test_M2_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  const uint32_t& _1() const noexcept { return base()._1;}
  uint32_t& _1() noexcept { return base()._1;}
  const ::nprpc::flat::Boolean& _2() const noexcept { return base()._2;}
  ::nprpc::flat::Boolean& _2() noexcept { return base()._2;}
  void _3(std::uint32_t elements_size) { new (&base()._3) ::nprpc::flat::Vector<uint8_t>(buffer_, elements_size); }
  auto _3_d() noexcept { return ::nprpc::flat::Vector_Direct1<uint8_t>(buffer_, offset_ + offsetof(test_M2, _3)); }
  auto _3() noexcept { return (::nprpc::flat::Span<uint8_t>)base()._3; }
};

struct test_M3 {
  ::nprpc::flat::Optional<uint32_t> _1;
};

class test_M3_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<test_M3*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const test_M3*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  test_M3_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  auto _1() noexcept { return ::nprpc::flat::Optional_Direct<uint32_t,void>(buffer_, offset_ + offsetof(test_M3, _1));  }
};

struct test_M4 {
  ::nprpc::flat::Optional<uint32_t> _1;
  ::nprpc::flat::Optional<test::flat::AAA> _2;
};

class test_M4_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<test_M4*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const test_M4*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  test_M4_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  auto _1() noexcept { return ::nprpc::flat::Optional_Direct<uint32_t,void>(buffer_, offset_ + offsetof(test_M4, _1));  }
  auto _2() noexcept { return ::nprpc::flat::Optional_Direct<test::flat::AAA,test::flat::AAA_Direct>(buffer_, offset_ + offsetof(test_M4, _2));  }
};

struct test_M5 {
  ::nprpc::flat::Optional<test::flat::BBB> _1;
};

class test_M5_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<test_M5*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const test_M5*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  test_M5_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  auto _1() noexcept { return ::nprpc::flat::Optional_Direct<test::flat::BBB,test::flat::BBB_Direct>(buffer_, offset_ + offsetof(test_M5, _1));  }
};

struct test_M6 {
  test::flat::DDD _1;
};

class test_M6_Direct {
  ::nprpc::flat_buffer& buffer_;
  const std::uint32_t offset_;

  auto& base() noexcept { return *reinterpret_cast<test_M6*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }
  auto const& base() const noexcept { return *reinterpret_cast<const test_M6*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }
public:
  uint32_t offset() const noexcept { return offset_; }
  void* __data() noexcept { return (void*)&base(); }
  test_M6_Direct(::nprpc::flat_buffer& buffer, std::uint32_t offset)
    : buffer_(buffer)
    , offset_(offset)
  {
  }
  auto _1() noexcept { return test::flat::DDD_Direct(buffer_, offset_ + offsetof(test_M6, _1)); }
};


bool check_1DDD_1(nprpc::flat_buffer& buf, test_M6_Direct& ia) {
  if (static_cast<std::uint32_t>(buf.size()) < ia.offset() + 12) goto check_failed;
  {
    {
      if(!ia._1().a_d()._check_size_align(static_cast<std::uint32_t>(buf.size()))) goto check_failed;
      {
        auto span = ia._1().a_d()();
        for (auto e : span) {
  if (static_cast<std::uint32_t>(buf.size()) < e.offset() + 20) goto check_failed;
      {
        if(!e.b_d()._check_size_align(static_cast<std::uint32_t>(buf.size()))) goto check_failed;
      }
      {
        if(!e.c_d()._check_size_align(static_cast<std::uint32_t>(buf.size()))) goto check_failed;
      }
        }
      }
    }
    {
      if(!ia._1().b()._check_size_align(static_cast<std::uint32_t>(buf.size()))) goto check_failed;
      if ( ia._1().b().has_value() ) {
        auto value = ia._1().b().value();
      if(!value._check_size_align(static_cast<std::uint32_t>(buf.size()))) goto check_failed;
      {
        auto span = value();
        for (auto e : span) {
  if (static_cast<std::uint32_t>(buf.size()) < e.offset() + 20) goto check_failed;
      {
        if(!e.a_d()._check_size_align(static_cast<std::uint32_t>(buf.size()))) goto check_failed;
      }
      {
        if(!e.b_d()._check_size_align(static_cast<std::uint32_t>(buf.size()))) goto check_failed;
      }
      {
        if(!e.c()._check_size_align(static_cast<std::uint32_t>(buf.size()))) goto check_failed;
      }
        }
      }
      }
    }
  }
  return true;
check_failed:
  nprpc::impl::make_simple_answer(buf, nprpc::impl::MessageId::Error_BadInput);
  return false;
}
} // 

namespace test { 
bool test::TestBasic::ReturnBoolean() {
  ::nprpc::flat_buffer buf;
  {
    auto mb = buf.prepare(32);
    buf.commit(32);
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_type = ::nprpc::impl::MessageType::Request;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));
  __ch.object_id() = this->_data().object_id;
  __ch.poa_idx() = this->_data().poa_idx;
  __ch.interface_idx() = interface_idx_;
  __ch.function_idx() = 0;
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  ::nprpc::impl::g_orb->call(this->get_endpoint(), buf, this->get_timeout());
  auto std_reply = nprpc::impl::handle_standart_reply(buf);
  if (std_reply != -1) {
    std::cerr << "received an unusual reply for function with output arguments\n";
    throw nprpc::Exception("Unknown Error");
  }
  test_M1_Direct out(buf, sizeof(::nprpc::impl::Header));
    bool __ret_value;
    __ret_value = (bool)out._1();
  return __ret_value;
}

bool test::TestBasic::In(/*in*/uint32_t a, /*in*/bool b, /*in*/::nprpc::flat::Span<const uint8_t> c) {
  ::nprpc::flat_buffer buf;
  {
    auto mb = buf.prepare(176);
    buf.commit(48);
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_type = ::nprpc::impl::MessageType::Request;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));
  __ch.object_id() = this->_data().object_id;
  __ch.poa_idx() = this->_data().poa_idx;
  __ch.interface_idx() = interface_idx_;
  __ch.function_idx() = 1;
  test_M2_Direct _(buf,32);
  _._1() = a;
  _._2() = b;
  _._3(static_cast<uint32_t>(c.size()));
  memcpy(_._3().data(), c.data(), c.size() * 1);
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  ::nprpc::impl::g_orb->call(this->get_endpoint(), buf, this->get_timeout());
  auto std_reply = nprpc::impl::handle_standart_reply(buf);
  if (std_reply != -1) {
    std::cerr << "received an unusual reply for function with output arguments\n";
    throw nprpc::Exception("Unknown Error");
  }
  test_M1_Direct out(buf, sizeof(::nprpc::impl::Header));
    bool __ret_value;
    __ret_value = (bool)out._1();
  return __ret_value;
}

void test::TestBasic::Out(/*out*/uint32_t& a, /*out*/bool& b, /*out*/std::vector<uint8_t>& c) {
  ::nprpc::flat_buffer buf;
  {
    auto mb = buf.prepare(32);
    buf.commit(32);
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_type = ::nprpc::impl::MessageType::Request;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));
  __ch.object_id() = this->_data().object_id;
  __ch.poa_idx() = this->_data().poa_idx;
  __ch.interface_idx() = interface_idx_;
  __ch.function_idx() = 2;
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  ::nprpc::impl::g_orb->call(this->get_endpoint(), buf, this->get_timeout());
  auto std_reply = nprpc::impl::handle_standart_reply(buf);
  if (std_reply != -1) {
    std::cerr << "received an unusual reply for function with output arguments\n";
    throw nprpc::Exception("Unknown Error");
  }
  test_M2_Direct out(buf, sizeof(::nprpc::impl::Header));
    a = out._1();
    b = (bool)out._2();
    {
      auto span = out._3();
      c.resize(span.size());
      memcpy(c.data(), span.data(), 1 * span.size());
    }
}

void test::ITestBasic_Servant::dispatch(nprpc::Buffers& bufs, nprpc::EndPoint remote_endpoint, bool from_parent, nprpc::ReferenceList& ref_list) {
  nprpc::impl::flat::CallHeader_Direct __ch(bufs(), sizeof(::nprpc::impl::Header));
  switch(__ch.function_idx()) {
    case 0: {
      bool __ret_val;
      __ret_val = ReturnBoolean();
      auto& obuf = bufs();
      obuf.consume(obuf.size());
      obuf.prepare(17);
      obuf.commit(17);
      test_M1_Direct oa(obuf,16);
      oa._1() = __ret_val;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::BlockResponse;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;
      break;
    }
    case 1: {
      test_M2_Direct ia(bufs(), 32);
      bool __ret_val;
      __ret_val = In(ia._1(), ia._2(), ia._3());
      auto& obuf = bufs();
      obuf.consume(obuf.size());
      obuf.prepare(17);
      obuf.commit(17);
      test_M1_Direct oa(obuf,16);
      oa._1() = __ret_val;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::BlockResponse;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;
      break;
    }
    case 2: {
      auto& obuf = bufs.flip();
      obuf.consume(obuf.size());
      obuf.prepare(160);
      obuf.commit(32);
      test_M2_Direct oa(obuf,16);
      Out(oa._1(), oa._2(), oa._3_d());
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::BlockResponse;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;
      break;
    }
    default:
      nprpc::impl::make_simple_answer(bufs(), nprpc::impl::MessageId::Error_UnknownFunctionIdx);
  }
}

bool test::TestOptional::InEmpty(/*in*/const std::optional<uint32_t>& a) {
  ::nprpc::flat_buffer buf;
  {
    auto mb = buf.prepare(164);
    buf.commit(36);
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_type = ::nprpc::impl::MessageType::Request;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));
  __ch.object_id() = this->_data().object_id;
  __ch.poa_idx() = this->_data().poa_idx;
  __ch.interface_idx() = interface_idx_;
  __ch.function_idx() = 0;
  test_M3_Direct _(buf,32);
  if (a) {
    _._1().alloc();
    _._1().value() = a.value();
  } else { 
    _._1().set_nullopt();
  }
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  ::nprpc::impl::g_orb->call(this->get_endpoint(), buf, this->get_timeout());
  auto std_reply = nprpc::impl::handle_standart_reply(buf);
  if (std_reply != -1) {
    std::cerr << "received an unusual reply for function with output arguments\n";
    throw nprpc::Exception("Unknown Error");
  }
  test_M1_Direct out(buf, sizeof(::nprpc::impl::Header));
    bool __ret_value;
    __ret_value = (bool)out._1();
  return __ret_value;
}

bool test::TestOptional::In(/*in*/const std::optional<uint32_t>& a, /*in*/const std::optional<test::AAA>& b) {
  ::nprpc::flat_buffer buf;
  {
    auto mb = buf.prepare(168);
    buf.commit(40);
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_type = ::nprpc::impl::MessageType::Request;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));
  __ch.object_id() = this->_data().object_id;
  __ch.poa_idx() = this->_data().poa_idx;
  __ch.interface_idx() = interface_idx_;
  __ch.function_idx() = 1;
  test_M4_Direct _(buf,32);
  if (a) {
    _._1().alloc();
    _._1().value() = a.value();
  } else { 
    _._1().set_nullopt();
  }
  if (b) {
    _._2().alloc();
    auto value = _._2().value();
    value.a() = b.value().a;
    value.b(b.value().b);
    value.c(b.value().c);
  } else { 
    _._2().set_nullopt();
  }
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  ::nprpc::impl::g_orb->call(this->get_endpoint(), buf, this->get_timeout());
  auto std_reply = nprpc::impl::handle_standart_reply(buf);
  if (std_reply != -1) {
    std::cerr << "received an unusual reply for function with output arguments\n";
    throw nprpc::Exception("Unknown Error");
  }
  test_M1_Direct out(buf, sizeof(::nprpc::impl::Header));
    bool __ret_value;
    __ret_value = (bool)out._1();
  return __ret_value;
}

void test::TestOptional::OutEmpty(/*out*/std::optional<uint32_t>& a) {
  ::nprpc::flat_buffer buf;
  {
    auto mb = buf.prepare(32);
    buf.commit(32);
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_type = ::nprpc::impl::MessageType::Request;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));
  __ch.object_id() = this->_data().object_id;
  __ch.poa_idx() = this->_data().poa_idx;
  __ch.interface_idx() = interface_idx_;
  __ch.function_idx() = 2;
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  ::nprpc::impl::g_orb->call(this->get_endpoint(), buf, this->get_timeout());
  auto std_reply = nprpc::impl::handle_standart_reply(buf);
  if (std_reply != -1) {
    std::cerr << "received an unusual reply for function with output arguments\n";
    throw nprpc::Exception("Unknown Error");
  }
  test_M3_Direct out(buf, sizeof(::nprpc::impl::Header));
    {
      auto opt = out._1();
      if (opt.has_value()) {
        a = std::decay<decltype(a)>::type::value_type{};
        auto& value_to = a.value();
        value_to = opt.value();
      } else { 
        a = std::nullopt;
      }
    }
}

void test::TestOptional::Out(/*out*/std::optional<uint32_t>& a) {
  ::nprpc::flat_buffer buf;
  {
    auto mb = buf.prepare(32);
    buf.commit(32);
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_type = ::nprpc::impl::MessageType::Request;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));
  __ch.object_id() = this->_data().object_id;
  __ch.poa_idx() = this->_data().poa_idx;
  __ch.interface_idx() = interface_idx_;
  __ch.function_idx() = 3;
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  ::nprpc::impl::g_orb->call(this->get_endpoint(), buf, this->get_timeout());
  auto std_reply = nprpc::impl::handle_standart_reply(buf);
  if (std_reply != -1) {
    std::cerr << "received an unusual reply for function with output arguments\n";
    throw nprpc::Exception("Unknown Error");
  }
  test_M3_Direct out(buf, sizeof(::nprpc::impl::Header));
    {
      auto opt = out._1();
      if (opt.has_value()) {
        a = std::decay<decltype(a)>::type::value_type{};
        auto& value_to = a.value();
        value_to = opt.value();
      } else { 
        a = std::nullopt;
      }
    }
}

void test::ITestOptional_Servant::dispatch(nprpc::Buffers& bufs, nprpc::EndPoint remote_endpoint, bool from_parent, nprpc::ReferenceList& ref_list) {
  nprpc::impl::flat::CallHeader_Direct __ch(bufs(), sizeof(::nprpc::impl::Header));
  switch(__ch.function_idx()) {
    case 0: {
      test_M3_Direct ia(bufs(), 32);
      bool __ret_val;
      __ret_val = InEmpty(ia._1());
      auto& obuf = bufs();
      obuf.consume(obuf.size());
      obuf.prepare(17);
      obuf.commit(17);
      test_M1_Direct oa(obuf,16);
      oa._1() = __ret_val;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::BlockResponse;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;
      break;
    }
    case 1: {
      test_M4_Direct ia(bufs(), 32);
      bool __ret_val;
      __ret_val = In(ia._1(), ia._2());
      auto& obuf = bufs();
      obuf.consume(obuf.size());
      obuf.prepare(17);
      obuf.commit(17);
      test_M1_Direct oa(obuf,16);
      oa._1() = __ret_val;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::BlockResponse;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;
      break;
    }
    case 2: {
      auto& obuf = bufs.flip();
      obuf.consume(obuf.size());
      obuf.prepare(148);
      obuf.commit(20);
      test_M3_Direct oa(obuf,16);
      OutEmpty(oa._1());
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::BlockResponse;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;
      break;
    }
    case 3: {
      auto& obuf = bufs.flip();
      obuf.consume(obuf.size());
      obuf.prepare(148);
      obuf.commit(20);
      test_M3_Direct oa(obuf,16);
      Out(oa._1());
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::BlockResponse;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;
      break;
    }
    default:
      nprpc::impl::make_simple_answer(bufs(), nprpc::impl::MessageId::Error_UnknownFunctionIdx);
  }
}

void test::TestNested::Out(/*out*/std::optional<test::BBB>& a) {
  ::nprpc::flat_buffer buf;
  {
    auto mb = buf.prepare(32);
    buf.commit(32);
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_type = ::nprpc::impl::MessageType::Request;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));
  __ch.object_id() = this->_data().object_id;
  __ch.poa_idx() = this->_data().poa_idx;
  __ch.interface_idx() = interface_idx_;
  __ch.function_idx() = 0;
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  ::nprpc::impl::g_orb->call(this->get_endpoint(), buf, this->get_timeout());
  auto std_reply = nprpc::impl::handle_standart_reply(buf);
  if (std_reply != -1) {
    std::cerr << "received an unusual reply for function with output arguments\n";
    throw nprpc::Exception("Unknown Error");
  }
  test_M5_Direct out(buf, sizeof(::nprpc::impl::Header));
    {
      auto opt = out._1();
      if (opt.has_value()) {
        a = std::decay<decltype(a)>::type::value_type{};
        auto& value_to = a.value();
        auto value_from = opt.value();
        {
          auto span = value_from.a();
          value_to.a.resize(span.size());
          auto it = std::begin(value_to.a);
          for (auto e : span) {
            (*it).a = e.a();
            (*it).b = (std::string_view)e.b();
            (*it).c = (std::string_view)e.c();
            ++it;
          }
        }
        {
          auto span = value_from.b();
          value_to.b.resize(span.size());
          auto it = std::begin(value_to.b);
          for (auto e : span) {
            (*it).a = (std::string_view)e.a();
            (*it).b = (std::string_view)e.b();
            {
              auto opt = e.c();
              if (opt.has_value()) {
                (*it).c = std::decay<decltype((*it).c)>::type::value_type{};
                auto& value_to = (*it).c.value();
                value_to = (bool)opt.value();
              } else { 
                (*it).c = std::nullopt;
              }
            }
            ++it;
          }
        }
      } else { 
        a = std::nullopt;
      }
    }
}

void test::ITestNested_Servant::dispatch(nprpc::Buffers& bufs, nprpc::EndPoint remote_endpoint, bool from_parent, nprpc::ReferenceList& ref_list) {
  nprpc::impl::flat::CallHeader_Direct __ch(bufs(), sizeof(::nprpc::impl::Header));
  switch(__ch.function_idx()) {
    case 0: {
      auto& obuf = bufs.flip();
      obuf.consume(obuf.size());
      obuf.prepare(148);
      obuf.commit(20);
      test_M5_Direct oa(obuf,16);
      Out(oa._1());
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::BlockResponse;
      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;
      break;
    }
    default:
      nprpc::impl::make_simple_answer(bufs(), nprpc::impl::MessageId::Error_UnknownFunctionIdx);
  }
}

void test::TestBadInput::In(/*in*/const test::DDD& a) {
  ::nprpc::flat_buffer buf;
  {
    auto mb = buf.prepare(172);
    buf.commit(44);
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;
    static_cast<::nprpc::impl::Header*>(mb.data())->msg_type = ::nprpc::impl::MessageType::Request;
  }
  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));
  __ch.object_id() = this->_data().object_id;
  __ch.poa_idx() = this->_data().poa_idx;
  __ch.interface_idx() = interface_idx_;
  __ch.function_idx() = 0;
  test_M6_Direct _(buf,32);
  _._1().a(static_cast<uint32_t>(a.a.size()));
  {
    auto span = _._1().a();
    auto it = a.a.begin();
    for (auto e : span) {
      auto __ptr = ::nprpc::make_wrapper1(*it);
        e.a() = __ptr->a;
        e.b(__ptr->b);
        e.c(__ptr->c);
      ++it;
    }
  }
  /*
  if (a.b) {
    _._1().b().alloc();
    auto value = _._1().b().value();
    value.length(static_cast<uint32_t>(a.b.value().size()));
    {
      auto span = value();
      auto it = a.b.value().begin();
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
    }
  } else { 
    _._1().b().set_nullopt();
  }*/
  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);
  ::nprpc::impl::g_orb->call(this->get_endpoint(), buf, this->get_timeout());
  auto std_reply = nprpc::impl::handle_standart_reply(buf);
  if (std_reply != 0) {
    std::cerr << "received an unusual reply for function with no output arguments\n";
  }
}

void test::ITestBadInput_Servant::dispatch(nprpc::Buffers& bufs, nprpc::EndPoint remote_endpoint, bool from_parent, nprpc::ReferenceList& ref_list) {
  nprpc::impl::flat::CallHeader_Direct __ch(bufs(), sizeof(::nprpc::impl::Header));
  switch(__ch.function_idx()) {
    case 0: {
      test_M6_Direct ia(bufs(), 32);
      if ( !check_1DDD_1(bufs(), ia) ) break;
      In(ia._1());
      nprpc::impl::make_simple_answer(bufs(), nprpc::impl::MessageId::Success);
      break;
    }
    default:
      nprpc::impl::make_simple_answer(bufs(), nprpc::impl::MessageId::Error_UnknownFunctionIdx);
  }
}

} // namespace test

