#include "CppUnitTest.h"

#include <iostream>
#include <chrono>
#include <numeric>
#include "proxy/test.hpp"
#include <nprpc/nprpc_nameserver.hpp>
#include <nplib/utils/thread_pool.hpp>
#include <nplib/utils/utf8.h>

#include <boost/range/algorithm_ext/push_back.hpp> 
#include <boost/range/irange.hpp>
#include <boost/asio/thread_pool.hpp>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std::string_literals;

using nplib::utf8::wide;

namespace nprpctest {

using thread_pool = nplib::thread_pool_1;

nprpc::Rpc* rpc;
nprpc::Poa* poa;


TEST_MODULE_INITIALIZE(InitRPC)
{
	try {
		nprpc::Config rpc_cfg;
		rpc_cfg.debug_level = nprpc::DebugLevel::DebugLevel_Critical;
		rpc_cfg.port = 22222;
		rpc_cfg.websocket_port = 0;

		rpc = nprpc::init(thread_pool::get_instance().ctx(), std::move(rpc_cfg));
		
		auto policy = std::make_unique<nprpc::Policy_Lifespan>(nprpc::Policy_Lifespan::Persistent);
		poa = rpc->create_poa(128, { policy.get() });
	} catch (nprpc::Exception& ex) {
		Assert::Fail(wide(ex.what()).c_str());
	}
}

TEST_MODULE_CLEANUP(DestroyRPC)
{
//	thread_pool::get_instance().stop();
	rpc->destroy();
}


#define ASSERT_EQUAL(x, y) if ( (x) != (y) ) return false


	TEST_CLASS(nprpctest)
	{
		template<typename T>
		auto make_stuff_happen(T::servant_t& servant) {
			static const std::string object_name = "nprpc_test_object";

			auto nameserver = rpc->get_nameserver("192.168.1.2");
			auto oid = poa->activate_object(&servant);
			nameserver->Bind(oid, object_name);
			
			nprpc::Object* raw;
			Assert::IsTrue(nameserver->Resolve(object_name, raw));
			
			auto obj = nprpc::narrow<T>(raw);
			obj->add_ref();
			
			return nprpc::ObjectPtr(obj);
		}
	public:
		TEST_METHOD(TestBasic)
		{
			class TestBasicImpl
				: public test::ITestBasic_Servant {
			public:
				virtual bool ReturnBoolean() {
					return true;
				}

				virtual bool In(uint32_t a, ::nprpc::flat::Boolean b, ::nprpc::flat::Span<uint8_t> c) {
					ASSERT_EQUAL(a, 100);
					ASSERT_EQUAL(b.get(), true);

					uint8_t ix = 0;
					for (auto i : c) {
						ASSERT_EQUAL(ix++, i);
					}

					return true;
				}
				
				virtual void Out(uint32_t& a, ::nprpc::flat::Boolean& b, ::nprpc::flat::Vector_Direct1<uint8_t> c) {
					a = 100;
					b = true;

					c.length(256);
					auto span = c();
					std::iota(std::begin(span), std::end(span), 0);
				}
			} servant;

			try {
				auto obj = make_stuff_happen<test::TestBasic>(servant);

				Assert::IsTrue(obj->ReturnBoolean());

				std::vector<uint8_t> ints;
				ints.reserve(256);
				boost::push_back(ints, boost::irange(0, 255));

				Assert::IsTrue(obj->In(100, true, nprpc::flat::make_read_only_span(ints)));

				uint32_t a;
				bool b;
				
				obj->Out(a, b, ints);

				Assert::AreEqual(a, static_cast<uint32_t>(100));
				Assert::AreEqual(b, true);

				uint8_t ix = 0;
				for (auto i : ints) Assert::AreEqual(ix++, i);
			} catch (nprpc::Exception& ex) {
				Assert::Fail(wide(ex.what()).c_str());
			}
		}

		TEST_METHOD(TestOptional)
		{
			class TestOptionalImpl
				: public test::ITestOptional_Servant {
			public:
				virtual bool InEmpty(::nprpc::flat::Optional_Direct<uint32_t> a) {
					ASSERT_EQUAL(a.has_value(), false);
					return true;
				}

				virtual bool In(::nprpc::flat::Optional_Direct<uint32_t> a, ::nprpc::flat::Optional_Direct<test::flat::AAA, test::flat::AAA_Direct> b) {
					ASSERT_EQUAL(a.has_value(), true);
					ASSERT_EQUAL(a.value(), 100);
					ASSERT_EQUAL(b.has_value(), true);

					auto const& value = b.value();

					ASSERT_EQUAL(value.a(), 100);
					ASSERT_EQUAL((std::string_view)value.b(), "test_b");
					ASSERT_EQUAL((std::string_view)value.c(), "test_c");

					return true;
				}

				virtual void OutEmpty(::nprpc::flat::Optional_Direct<uint32_t> a) {
					a.set_nullopt();
				}

				virtual void Out(::nprpc::flat::Optional_Direct<uint32_t> a) {
					a.alloc();
					a.value() = 100;
				}
			} servant;

			try {
				auto obj = make_stuff_happen<test::TestOptional>(servant);

				Assert::IsTrue(obj->InEmpty(std::nullopt));
				Assert::IsTrue(obj->In(100, test::AAA{ 100u, "test_b"s, "test_c"s }));

				std::optional<uint32_t> a;

				obj->OutEmpty(a);
				Assert::IsFalse(a.has_value());

				obj->Out(a);
				Assert::IsTrue(a.has_value());
				Assert::IsTrue(a.value() == 100);

			} catch (nprpc::Exception& ex) {
				Assert::Fail(wide(ex.what()).c_str());
			}
		}
		TEST_METHOD(TestContext)
		{
			
		}
	};
}
