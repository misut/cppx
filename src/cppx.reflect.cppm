// Minimal aggregate reflection (self-contained, Clang / MSVC).
// Provides field count, field access, and field name extraction for
// C++23 aggregate types. Designed to be replaced by std::meta (C++26).
//
// Limitations:
//   - Aggregates with nested aggregates may overcount due to brace elision;
//     use an explicit descriptor override for such types.
//   - Max 64 direct fields.
//   - Clang (__PRETTY_FUNCTION__) / MSVC (__FUNCSIG__).
//   - Requires T to be default-constructible (for name extraction).

export module cppx.reflect;
import std;

namespace cppx::reflect::detail {

// Implicitly convertible to anything. Used to probe aggregate-init arity.
struct any_type {
    template<typename T>
    constexpr operator T() const noexcept;  // declared, never defined
};

// True when T can be aggregate-initialized with N any_type values.
template<typename T, std::size_t... I>
concept initializable_with = requires { T{ (static_cast<void>(I), any_type{})... }; };

template<typename T, std::size_t N>
consteval bool is_n_initializable() {
    return []<std::size_t... I>(std::index_sequence<I...>) {
        return initializable_with<T, I...>;
    }(std::make_index_sequence<N>{});
}

inline constexpr std::size_t kMaxFields = 64;

// Count fields: largest N in [0, kMaxFields] for which T{any, ..., any} compiles.
template<typename T>
consteval std::size_t field_count() {
    std::size_t count = 0;
    [&]<std::size_t... N>(std::index_sequence<N...>) {
        ((is_n_initializable<T, N>() ? (count = N) : count), ...);
    }(std::make_index_sequence<kMaxFields + 1>{});
    return count;
}

} // namespace cppx::reflect::detail

export namespace cppx::reflect {

template<typename T>
struct is_reflectable : std::bool_constant<
    std::is_aggregate_v<std::remove_cvref_t<T>> &&
    !std::is_union_v<std::remove_cvref_t<T>> &&
    !std::is_array_v<std::remove_cvref_t<T>>
> {};

template<typename T>
inline constexpr bool is_reflectable_v = is_reflectable<T>::value;

template<typename T>
concept Reflectable = is_reflectable_v<T>
    && (detail::field_count<std::remove_cvref_t<T>>() > 0)
    && (detail::field_count<std::remove_cvref_t<T>>() <= detail::kMaxFields);

template<Reflectable T>
inline constexpr std::size_t tuple_size_v =
    detail::field_count<std::remove_cvref_t<T>>();

} // namespace cppx::reflect

namespace cppx::reflect::detail {

// Decompose an aggregate via structured bindings, return a tie-tuple.
template<typename T>
constexpr auto to_tuple(T&& obj) noexcept {
    using U = std::remove_cvref_t<T>;
    constexpr auto N = field_count<U>();
    if constexpr (N == 1) { auto& [a] = obj; return std::tie(a); }
    else if constexpr (N == 2)  { auto& [a,b] = obj; return std::tie(a,b); }
    else if constexpr (N == 3)  { auto& [a,b,c] = obj; return std::tie(a,b,c); }
    else if constexpr (N == 4)  { auto& [a,b,c,d] = obj; return std::tie(a,b,c,d); }
    else if constexpr (N == 5)  { auto& [a,b,c,d,e] = obj; return std::tie(a,b,c,d,e); }
    else if constexpr (N == 6)  { auto& [a,b,c,d,e,f] = obj; return std::tie(a,b,c,d,e,f); }
    else if constexpr (N == 7)  { auto& [a,b,c,d,e,f,g] = obj; return std::tie(a,b,c,d,e,f,g); }
    else if constexpr (N == 8)  { auto& [a,b,c,d,e,f,g,h] = obj; return std::tie(a,b,c,d,e,f,g,h); }
    else if constexpr (N == 9)  { auto& [a,b,c,d,e,f,g,h,i] = obj; return std::tie(a,b,c,d,e,f,g,h,i); }
    else if constexpr (N == 10) { auto& [a,b,c,d,e,f,g,h,i,j] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j); }
    else if constexpr (N == 11) { auto& [a,b,c,d,e,f,g,h,i,j,k] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k); }
    else if constexpr (N == 12) { auto& [a,b,c,d,e,f,g,h,i,j,k,l] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l); }
    else if constexpr (N == 13) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m); }
    else if constexpr (N == 14) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n); }
    else if constexpr (N == 15) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o); }
    else if constexpr (N == 16) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p); }
    else if constexpr (N == 17) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q); }
    else if constexpr (N == 18) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r); }
    else if constexpr (N == 19) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s); }
    else if constexpr (N == 20) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t); }
    else if constexpr (N == 21) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u); }
    else if constexpr (N == 22) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v); }
    else if constexpr (N == 23) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w); }
    else if constexpr (N == 24) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x); }
    else if constexpr (N == 25) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y); }
    else if constexpr (N == 26) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z); }
    else if constexpr (N == 27) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A); }
    else if constexpr (N == 28) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B); }
    else if constexpr (N == 29) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C); }
    else if constexpr (N == 30) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D); }
    else if constexpr (N == 31) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E); }
    else if constexpr (N == 32) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F); }
    else if constexpr (N == 33) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G); }
    else if constexpr (N == 34) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H); }
    else if constexpr (N == 35) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I); }
    else if constexpr (N == 36) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J); }
    else if constexpr (N == 37) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K); }
    else if constexpr (N == 38) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L); }
    else if constexpr (N == 39) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M); }
    else if constexpr (N == 40) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_); }
    else if constexpr (N == 41) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O); }
    else if constexpr (N == 42) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P); }
    else if constexpr (N == 43) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q); }
    else if constexpr (N == 44) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R); }
    else if constexpr (N == 45) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S); }
    else if constexpr (N == 46) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_); }
    else if constexpr (N == 47) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U); }
    else if constexpr (N == 48) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V); }
    else if constexpr (N == 49) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W); }
    else if constexpr (N == 50) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X); }
    else if constexpr (N == 51) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y); }
    else if constexpr (N == 52) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z); }
    else if constexpr (N == 53) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa); }
    else if constexpr (N == 54) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb); }
    else if constexpr (N == 55) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc); }
    else if constexpr (N == 56) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc,dd] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc,dd); }
    else if constexpr (N == 57) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc,dd,ee] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc,dd,ee); }
    else if constexpr (N == 58) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc,dd,ee,ff] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc,dd,ee,ff); }
    else if constexpr (N == 59) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc,dd,ee,ff,gg] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc,dd,ee,ff,gg); }
    else if constexpr (N == 60) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc,dd,ee,ff,gg,hh] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc,dd,ee,ff,gg,hh); }
    else if constexpr (N == 61) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc,dd,ee,ff,gg,hh,ii] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc,dd,ee,ff,gg,hh,ii); }
    else if constexpr (N == 62) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc,dd,ee,ff,gg,hh,ii,jj] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc,dd,ee,ff,gg,hh,ii,jj); }
    else if constexpr (N == 63) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc,dd,ee,ff,gg,hh,ii,jj,kk] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc,dd,ee,ff,gg,hh,ii,jj,kk); }
    else if constexpr (N == 64) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc,dd,ee,ff,gg,hh,ii,jj,kk,ll] = obj; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,I,J,K,L,M,N_,O,P,Q,R,S,T_,U,V,W,X,Y,Z,aa,bb,cc,dd,ee,ff,gg,hh,ii,jj,kk,ll); }
    else { static_assert(N <= 64, "cppx.reflect: field count out of range"); }
}

// Fake static instance used for taking compile-time pointers to fields.
// Uses a union with an explicit no-op destructor so that T's default
// constructor is NEVER called: this avoids tripping MSVC's constexpr
// heap-allocation tracker for T's whose default ctor it (sometimes
// incorrectly) flags as allocating, e.g. std::string.
//
// We never read `value` — the only operations are forming references
// to its subobjects via structured bindings, which decay to addresses
// the compiler resolves symbolically (no runtime touch).
template<typename T>
union storage {
    char _;
    T value;
    constexpr storage() noexcept : _{} {}
    constexpr ~storage() noexcept {}
};

template<typename T>
inline constexpr storage<T> external_storage{};

template<typename T>
inline constexpr T const& external = external_storage<T>.value;

// When instantiated with a concrete pointer NTTP, the compiler's
// function-signature string embeds the pointer's expression:
//   Clang:  "...pretty_ptr() [P = &...external_storage<T>.value.NAME]"
//   MSVC:   "...pretty_ptr<T,I,&...external_storage<struct T>->value->NAME>(void) noexcept"
// MSVC folds pointer NTTPs that share the same type and offset across
// different storage objects, producing incorrect names. Extra T and I
// template parameters force distinct instantiations and correct output.
//
// The NTTP parameter MUST be named (P) — clang does not emit `[<unnamed> = ...]`
// for anonymous template parameters, so without the name the pointer value
// is missing from __PRETTY_FUNCTION__ and the parser below cannot find it.
#if defined(_MSC_VER)
template<typename, std::size_t, auto* P>
#else
template<auto* P>
#endif
consteval std::string_view pretty_ptr() noexcept {
#if defined(_MSC_VER)
    return std::string_view{__FUNCSIG__};
#else
    return std::string_view{__PRETTY_FUNCTION__};
#endif
}

template<typename T, std::size_t I>
consteval std::string_view field_name_impl() noexcept {
    constexpr auto tup = to_tuple(external<T>);
    constexpr auto ptr = &std::get<I>(tup);
#if defined(_MSC_VER)
    constexpr auto sv = pretty_ptr<T, I, ptr>();
    // MSVC: "...->value->FIELD>(void) noexcept"
    constexpr auto end = sv.rfind(">(void)");
    static_assert(end != std::string_view::npos, "cppx.reflect: no '>(void)' in __FUNCSIG__");
    constexpr auto arrow = sv.rfind("->", end);
    static_assert(arrow != std::string_view::npos, "cppx.reflect: no '->' in __FUNCSIG__");
    return sv.substr(arrow + 2, end - arrow - 2);
#else
    constexpr auto sv = pretty_ptr<ptr>();
    // Clang: find the closing ']' of the NTTP list and the last '.' before it.
    constexpr auto rbr = sv.rfind(']');
    static_assert(rbr != std::string_view::npos, "cppx.reflect: no ']' in __PRETTY_FUNCTION__");
    constexpr auto dot = sv.rfind('.', rbr);
    static_assert(dot != std::string_view::npos, "cppx.reflect: no '.' in __PRETTY_FUNCTION__");
    return sv.substr(dot + 1, rbr - dot - 1);
#endif
}

} // namespace cppx::reflect::detail

export namespace cppx::reflect {

// Access field I as an lvalue reference (const-propagating).
template<std::size_t I, Reflectable T>
constexpr auto&& get(T&& obj) noexcept {
    return std::get<I>(detail::to_tuple(std::forward<T>(obj)));
}

// Compile-time field name for field I of T.
template<typename T, std::size_t I>
    requires Reflectable<T> && (I < tuple_size_v<T>)
consteval std::string_view name_of() noexcept {
    return detail::field_name_impl<std::remove_cvref_t<T>, I>();
}

} // namespace cppx::reflect
