// objects.inl
class SimpleObjectImpl : public nprpc::test::ISimpleObject_Servant {
  uint32_t value_ = 0;
public:
  void SetValue (uint32_t a) override {
    value_ = a;
  }
};

class TestObjectsImpl : public nprpc::test::ITestObjects_Servant {
  SimpleObjectImpl simple_object_;
  nprpc::Poa* poa_;
  nprpc::ObjectPtr<nprpc::test::SimpleObject> received_object_;


public:
  TestObjectsImpl(nprpc::Poa* poa)
    : poa_(poa)
  {
    // poa_->activate_object(&simple_object_, nprpc::ObjectActivationFlags::all, nullptr);
  }

  void SendObject (nprpc::Object* o) override {
    auto simple_obj = nprpc::narrow<nprpc::test::SimpleObject>(o);
    if (!simple_obj) {
      throw nprpc::test::AssertionFailed{"Invalid object type passed to SendObject"};
    }

    // Store the object for later release
    received_object_ = nprpc::ObjectPtr<nprpc::test::SimpleObject>(simple_obj);
    auto obj = received_object_.get();
    nprpc::spawn_task(nprpc::get_io_context(),
      [obj]() -> boost::asio::awaitable<void> {
        co_await nprpc::as_awaitable(obj->SetValueAsync(42));
      }
    );
  }

  void ReleaseReceivedObject() override {
    if (!received_object_) {
      throw nprpc::test::AssertionFailed{"No object was received yet"};
    }

    received_object_.reset();
  }

  void SendNestedObjects (nprpc::test::flat::NestedObjects_Direct o) override {
    // Get the current session context from thread-local storage
    auto& ctx = nprpc::get_context();

    // Create Object proxies from the ObjectIds using the session's remote endpoint
    auto* obj1_raw = nprpc::impl::create_object_from_flat(o.object1(), ctx.remote_endpoint);
    auto* obj2_raw = nprpc::impl::create_object_from_flat(o.object2(), ctx.remote_endpoint);

    // Narrow to the specific interface and transfer ownership into ObjectPtr
    nprpc::ObjectPtr<nprpc::test::SimpleObject> o1(
      nprpc::narrow<nprpc::test::SimpleObject>(obj1_raw));
    nprpc::ObjectPtr<nprpc::test::SimpleObject> o2(
      nprpc::narrow<nprpc::test::SimpleObject>(obj2_raw));

    if (!o1 || !o2) {
      throw nprpc::test::AssertionFailed{"Invalid object types in NestedObjects"};
    }


    // SetValue RPCs go back to the caller; schedule after dispatch returns.
    nprpc::spawn_task(nprpc::get_io_context(),
      [o1 = std::move(o1), o2 = std::move(o2)]() mutable -> boost::asio::awaitable<void> {
        co_await nprpc::as_awaitable(o1->SetValueAsync(100));
        co_await nprpc::as_awaitable(o2->SetValueAsync(200));
      }
    );
  }

};
