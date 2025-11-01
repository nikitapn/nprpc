// objects.inl
class SimpleObjectImpl : public test::ISimpleObject_Servant {
  uint32_t value_ = 0;
public:
  void SetValue (uint32_t a) override {
    value_ = a;
  }
};

class TestObjectsImpl : public test::ITestObjects_Servant {
  SimpleObjectImpl simple_object_;
  nprpc::Poa* poa_;
  nprpc::ObjectPtr<test::SimpleObject> received_object_;
public:
  TestObjectsImpl(nprpc::Poa* poa)
    : poa_(poa)
  {
    // poa_->activate_object(&simple_object_, nprpc::ObjectActivationFlags::ALLOW_ALL, nullptr);
  }

  void SendObject (nprpc::Object* o) override {
    auto simple_obj = nprpc::narrow<test::SimpleObject>(o);
    if (!simple_obj) {
      throw test::AssertionFailed{"Invalid object type passed to SendObject"};
    }
    
    // Store the object for later release
    received_object_ = nprpc::ObjectPtr<test::SimpleObject>(simple_obj);
    received_object_->SetValue({}, 42);
  }

  void ReleaseReceivedObject() override {
    if (!received_object_) {
      throw test::AssertionFailed{"No object was received yet"};
    }

    received_object_->release();
  }

  void SendNestedObjects (test::flat::NestedObjects_Direct o) override {
    // Get the current session context from thread-local storage
    auto& ctx = nprpc::get_context();

    // Create Object proxies from the ObjectIds using the session's remote endpoint
    auto* obj1_raw = nprpc::impl::create_object_from_flat(o.object1(), ctx.remote_endpoint);
    auto* obj2_raw = nprpc::impl::create_object_from_flat(o.object2(), ctx.remote_endpoint);

    // Narrow to the specific interface
    auto obj1 = nprpc::narrow<test::SimpleObject>(obj1_raw);
    auto obj2 = nprpc::narrow<test::SimpleObject>(obj2_raw);

    if (!obj1 || !obj2) {
      throw test::AssertionFailed{"Invalid object types in NestedObjects"};
    }

    // Now you can call methods on the objects
    obj1->SetValue({}, 100);
    obj2->SetValue({}, 200);
    
    // Release references when done
    obj1->release();
    obj2->release();
  }

};