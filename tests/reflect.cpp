import cppx.reflect;
import std;

// --- Test structs (aggregate, flat) ---

struct One { int a; };
struct Two { int a; double b; };
struct Three { std::string s; int i; bool b; };
struct WithOpt { int x; std::optional<int> y; };
struct Sixteen {
    int a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p;
};

// --- Test helpers ---

int failed = 0;

void check(bool cond, std::string_view msg) {
    if (!cond) {
        std::println(std::cerr, "FAIL: {}", msg);
        ++failed;
    }
}

// --- Tests ---

void test_tuple_size() {
    static_assert(cppx::reflect::tuple_size_v<One> == 1);
    static_assert(cppx::reflect::tuple_size_v<Two> == 2);
    static_assert(cppx::reflect::tuple_size_v<Three> == 3);
    static_assert(cppx::reflect::tuple_size_v<WithOpt> == 2);
    static_assert(cppx::reflect::tuple_size_v<Sixteen> == 16);
    check(true, "tuple_size_v");
}

void test_get_mutable() {
    Two t{1, 2.5};
    check(cppx::reflect::get<0>(t) == 1, "get<0> read");
    check(cppx::reflect::get<1>(t) == 2.5, "get<1> read");
    cppx::reflect::get<0>(t) = 42;
    check(t.a == 42, "get<0> mutable");
    cppx::reflect::get<1>(t) = 3.25;
    check(t.b == 3.25, "get<1> mutable");
}

void test_get_const() {
    Three const t{"hi", 7, true};
    check(cppx::reflect::get<0>(t) == "hi", "const get<0>");
    check(cppx::reflect::get<1>(t) == 7, "const get<1>");
    check(cppx::reflect::get<2>(t) == true, "const get<2>");
}

void test_name_of() {
    static_assert(cppx::reflect::name_of<One, 0>() == "a");
    static_assert(cppx::reflect::name_of<Two, 0>() == "a");
    static_assert(cppx::reflect::name_of<Two, 1>() == "b");
    static_assert(cppx::reflect::name_of<Three, 0>() == "s");
    static_assert(cppx::reflect::name_of<Three, 1>() == "i");
    static_assert(cppx::reflect::name_of<Three, 2>() == "b");
    static_assert(cppx::reflect::name_of<WithOpt, 0>() == "x");
    static_assert(cppx::reflect::name_of<WithOpt, 1>() == "y");
    static_assert(cppx::reflect::name_of<Sixteen, 0>() == "a");
    static_assert(cppx::reflect::name_of<Sixteen, 15>() == "p");
    check(true, "name_of");
}

void test_concept() {
    static_assert(cppx::reflect::Reflectable<One>);
    static_assert(cppx::reflect::Reflectable<Two>);
    static_assert(cppx::reflect::Reflectable<Sixteen>);
    static_assert(!cppx::reflect::Reflectable<int>);
    static_assert(!cppx::reflect::Reflectable<std::string>);
    check(true, "Reflectable concept");
}

int main() {
    test_tuple_size();
    test_get_mutable();
    test_get_const();
    test_name_of();
    test_concept();

    if (failed > 0) {
        std::println(std::cerr, "\n{} test(s) failed", failed);
        return 1;
    }
    std::println("all cppx.reflect tests passed");
    return 0;
}
