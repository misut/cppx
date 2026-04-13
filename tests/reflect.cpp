import cppx.reflect;
import cppx.test;
import std;

// --- Test structs (aggregate, flat) ---

struct One { int a; };
struct Two { int a; double b; };
struct Three { std::string s; int i; bool b; };
struct WithOpt { int x; std::optional<int> y; };
struct Sixteen {
    int a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p;
};
struct Eighteen {
    int a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r;
};
struct TwentyFour {
    int a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x;
};

cppx::test::context tc;

// --- Tests ---

void test_tuple_size() {
    static_assert(cppx::reflect::tuple_size_v<One> == 1);
    static_assert(cppx::reflect::tuple_size_v<Two> == 2);
    static_assert(cppx::reflect::tuple_size_v<Three> == 3);
    static_assert(cppx::reflect::tuple_size_v<WithOpt> == 2);
    static_assert(cppx::reflect::tuple_size_v<Sixteen> == 16);
    static_assert(cppx::reflect::tuple_size_v<Eighteen> == 18);
    static_assert(cppx::reflect::tuple_size_v<TwentyFour> == 24);
    tc.check(true, "tuple_size_v");
}

void test_get_mutable() {
    Two t{1, 2.5};
    tc.check(cppx::reflect::get<0>(t) == 1, "get<0> read");
    tc.check(cppx::reflect::get<1>(t) == 2.5, "get<1> read");
    cppx::reflect::get<0>(t) = 42;
    tc.check(t.a == 42, "get<0> mutable");
    cppx::reflect::get<1>(t) = 3.25;
    tc.check(t.b == 3.25, "get<1> mutable");
}

void test_get_const() {
    Three const t{"hi", 7, true};
    tc.check(cppx::reflect::get<0>(t) == "hi", "const get<0>");
    tc.check(cppx::reflect::get<1>(t) == 7, "const get<1>");
    tc.check(cppx::reflect::get<2>(t) == true, "const get<2>");
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
    static_assert(cppx::reflect::name_of<Eighteen, 17>() == "r");
    static_assert(cppx::reflect::name_of<TwentyFour, 23>() == "x");
    tc.check(true, "name_of");
}

void test_concept() {
    static_assert(cppx::reflect::Reflectable<One>);
    static_assert(cppx::reflect::Reflectable<Two>);
    static_assert(cppx::reflect::Reflectable<Sixteen>);
    static_assert(!cppx::reflect::Reflectable<int>);
    static_assert(!cppx::reflect::Reflectable<std::string>);
    tc.check(true, "Reflectable concept");
}

int main() {
    test_tuple_size();
    test_get_mutable();
    test_get_const();
    test_name_of();
    test_concept();
    return tc.summary("cppx.reflect");
}
