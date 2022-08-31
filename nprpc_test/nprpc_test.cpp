#include "CppUnitTest.h"
#include "data.hpp"

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

		return nprpc::ObjectPtr(nprpc::narrow<T>(raw));
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

	TEST_METHOD(TestNested)
	{
		class TestNestedlImpl
			: public test::ITestNested_Servant {
		public:
			virtual void Out(::nprpc::flat::Optional_Direct<test::flat::BBB, test::flat::BBB_Direct> a) {
				a.alloc();
				auto value = a.value();

				value.a(1024);

				{
					auto span = value.a();
					std::uint32_t ix = 0;
					for (auto i : span) {
						i.a() = ix++;
						i.b(tstr1);
						i.c(tstr2);
					}
				}

				value.b(2048);

				auto span = value.b();
				bool b = false;
				for (auto i : span) {
					i.a(tstr1);
					i.b(tstr2);
					i.c().alloc();
					i.c().value() = (b ^= 1);
				}

			}
		} servant;

		try {
			auto obj = make_stuff_happen<test::TestNested>(servant);

			std::optional<test::BBB> a;

			obj->Out(a);

			Assert::IsTrue(a.has_value());
			auto& value = a.value();

			Assert::AreEqual(value.a.size(), 1024ull);

			std::uint32_t ix = 0;
			for (auto& i : value.a) {
				Assert::AreEqual(i.a, ix++);
				Assert::AreEqual(std::string_view(i.b), std::string_view(tstr1));
				Assert::AreEqual(std::string_view(i.c), std::string_view(tstr2));
			}

			Assert::AreEqual(value.b.size(), 2048ull);

			bool b = false;
			for (auto& i : value.b) {
				Assert::AreEqual(std::string_view(i.a), std::string_view(tstr1));
				Assert::AreEqual(std::string_view(i.b), std::string_view(tstr2));
				Assert::IsTrue(i.c.has_value());
				Assert::AreEqual(i.c.value(), b ^= 1);
			}
		} catch (nprpc::Exception& ex) {
			Assert::Fail(wide(ex.what()).c_str());
		}

	}

	TEST_METHOD(TestBadInput)
	{
		class TestBadInputlImpl
			: public test::ITestBadInput_Servant {
		public:
			virtual void In(test::flat::DDD_Direct a) {}
		} servant;

		try {
			auto obj = make_stuff_happen<test::TestBadInput>(servant);

			test::DDD a;
			//a.b.push_back(test::DDD{});
			//a.b[0].a = "string";

			obj->In(a);

			Assert::Fail(L"Check did not happen");
		} catch (nprpc::ExceptionBadInput&) {
			// ok
		} catch (nprpc::Exception& ex) {
			Assert::Fail(wide(ex.what()).c_str());
		}

	}
};
}
