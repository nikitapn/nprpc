constexpr auto nested_test_str1 = "34k535n3jknjknfdjgndfgnj4kjn545kr3k4n3k45j n34k kgfdg334r34rnkfndkgn[34l5k32;45l324lk;5j324k5j32lk4;5j34lknmt3l2k4ng54;nt295t0-2jt2ithnjkgnsfdgmndf;gkj3m450934j5234k5n345n345lkjn643kj6n4kljnkbvnvcb456456456gfhfgh56y";
constexpr auto nested_test_str2 = "d34234k24j1;lk24j12lk341234n45n345n3kj45n2345jn34lk5jn34kj5n3lk5jn2354lk3j2n4532-49503245i32k4095i34rjjngdfn;dfgns;kgfsd;lfkgsd;lgfkmgo4523[459j324509345i302945hjnkjngdgmsfg,msdb'sdf'3;4l5k32[59j2345igfgdsgfdg34345345bbdfgdsgdgdsfgdfvcbxcvbdfg34trjg";

class TestNestedImpl : public nprpc::test::ITestNested_Servant {
public:
    void Out(::nprpc::flat::Optional_Direct<nprpc::test::flat::BBB, nprpc::test::flat::BBB_Direct> a) override {
        a.alloc();
        auto value = a.value();
        value.a(1024);
        {
            auto span = value.a();
            std::uint32_t ix = 0;
            for (auto i : span) {
                i.a() = ix++;
                i.b(nested_test_str1);
                i.c(nested_test_str2);
            }
        }

        value.b(2048);
        auto span = value.b();
        bool b = false;
        for (auto i : span) {
            i.a(nested_test_str1);
            i.b(nested_test_str2);
            i.c().alloc();
            i.c().value() = (b ^= 1);
        }
    }

    nprpc::test::TopLevel ReturnNested() override {
        nprpc::test::TopLevel top {
            .x = "top_level_string",
            .y = {
                .x = "level1_string",
                .y = {
                    .x = "level2_string",
                    .y = {1,2,3,4,5,6,7,8,9,10},
                    .z = 3ull
                },
                .z = 2ull
            },
            .z = 1ull
        };
        return top;
    }
};