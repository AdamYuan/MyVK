#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../../include/myvk_rg/interface/Key.hpp"
#include "../../include/myvk_rg/interface/Pool.hpp"

TEST_CASE("Test Key") {
	using myvk_rg::interface::GlobalKey;
	using myvk_rg::interface::PoolKey;
	PoolKey key0 = {"pass", 0};
	PoolKey key1 = {"albedo_tex"};
	GlobalKey global_key0 = GlobalKey{GlobalKey{key0}, key1};
	GlobalKey global_key1 = GlobalKey{GlobalKey{key0}, key1};
	CHECK_EQ(global_key0.Format(), "pass:0.albedo_tex");
	CHECK_EQ(key0.Format(), "pass:0");
	CHECK((key0 != key1));
	CHECK((GlobalKey{global_key0, key0} != global_key0));
	CHECK((global_key0 == global_key1));
}

TEST_CASE("Test Pool Data") {
	using myvk_rg::interface::Wrapper;
	Wrapper<int> int_wrapper;
	CHECK_EQ(*int_wrapper.Construct<int>(10), 10);
	CHECK_EQ(*int_wrapper.Get<int>(), 10);

	Wrapper<std::istream> ifs_wrapper;
	CHECK(ifs_wrapper.Construct<std::ifstream>());
	CHECK_EQ(ifs_wrapper.Get<std::istringstream>(), nullptr);
	CHECK(ifs_wrapper.Get<std::istream>());
	CHECK(ifs_wrapper.Get<std::ifstream>());

	Wrapper<std::variant<int, double, std::istream>> var_wrapper;
	CHECK(var_wrapper.Construct<std::ifstream>());
	CHECK_EQ(var_wrapper.Get<int>(), nullptr);
	CHECK_EQ(var_wrapper.Get<std::istringstream>(), nullptr);
	CHECK(var_wrapper.Get<std::istream>());
	CHECK(var_wrapper.Get<std::ifstream>());

	CHECK_EQ(*var_wrapper.Construct<int>(2), 2);
	CHECK_EQ(var_wrapper.Get<std::istream>(), nullptr);
	CHECK_EQ(var_wrapper.Get<double>(), nullptr);
	CHECK_EQ(*var_wrapper.Get<int>(), 2);
	*var_wrapper.Get<int>() = 3;
	CHECK_EQ(*var_wrapper.Get<int>(), 3);
}
