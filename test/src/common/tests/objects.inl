
class SimpleObjectImpl : public test::ISimpleObject_Servant {
  uint32_t value_ = 0;
public:
  uint32_t GetValue () override {
    return value_;
  }
  void SetValue (uint32_t a) override {
    value_ = a;
  }
};


class TestObjectsImpl : public test::ITestObjects_Servant {
  SimpleObjectImpl simple_object_;
  nprpc::Poa* poa_;
public:
  TestObjectsImpl(nprpc::Poa* poa)
    : poa_(poa)
  {
    // poa_->activate_object(&simple_object_, nprpc::ObjectActivationFlags::ALLOW_ALL, nullptr);
  }

  void SendObject (nprpc::Object* o) override {
    auto simple_obj = nprpc::narrow<test::SimpleObject>(o);
    if (!simple_obj) {
      throw nprpc::Exception("Invalid object type passed to SendObject");
    }
    // uint32_t val = simple_obj->GetValue();
    // std::cout << "Received SimpleObject with value: " << val << std::endl;
    // simple_obj->SetValue(val + 10);
  }
};