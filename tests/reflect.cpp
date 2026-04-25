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
struct ThirtyTwo {
    int a01,a02,a03,a04,a05,a06,a07,a08;
    int a09,a10,a11,a12,a13,a14,a15,a16;
    int a17,a18,a19,a20,a21,a22,a23,a24;
    int a25,a26,a27,a28,a29,a30,a31,a32;
};
struct FiftyFour {
    int f01,f02,f03,f04,f05,f06,f07,f08,f09,f10;
    int f11,f12,f13,f14,f15,f16,f17,f18,f19,f20;
    int f21,f22,f23,f24,f25,f26,f27,f28,f29,f30;
    int f31,f32,f33,f34,f35,f36,f37,f38,f39,f40;
    int f41,f42,f43,f44,f45,f46,f47,f48,f49,f50;
    int f51,f52,f53,f54;
};
struct SixtyFour {
    int g01,g02,g03,g04,g05,g06,g07,g08,g09,g10;
    int g11,g12,g13,g14,g15,g16,g17,g18,g19,g20;
    int g21,g22,g23,g24,g25,g26,g27,g28,g29,g30;
    int g31,g32,g33,g34,g35,g36,g37,g38,g39,g40;
    int g41,g42,g43,g44,g45,g46,g47,g48,g49,g50;
    int g51,g52,g53,g54,g55,g56,g57,g58,g59,g60;
    int g61,g62,g63,g64;
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
    static_assert(cppx::reflect::tuple_size_v<ThirtyTwo> == 32);
    static_assert(cppx::reflect::tuple_size_v<FiftyFour> == 54);
    static_assert(cppx::reflect::tuple_size_v<SixtyFour> == 64);
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
    static_assert(cppx::reflect::name_of<ThirtyTwo, 0>() == "a01");
    static_assert(cppx::reflect::name_of<ThirtyTwo, 31>() == "a32");
    static_assert(cppx::reflect::name_of<FiftyFour, 0>() == "f01");
    static_assert(cppx::reflect::name_of<FiftyFour, 53>() == "f54");
    static_assert(cppx::reflect::name_of<SixtyFour, 0>() == "g01");
    static_assert(cppx::reflect::name_of<SixtyFour, 63>() == "g64");
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
