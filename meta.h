#pragma once
/// \file meta.h Perfect forwarding
#include "core.h"
namespace std {
template<bool Cond, typename Iftrue, typename Iffalse> struct conditional { typedef Iftrue type; };
template<typename Iftrue, typename Iffalse> struct conditional<false, Iftrue, Iffalse> { typedef Iffalse type; };
template<typename...> struct or_;
template<> struct or_<> : public false_type {};
template<typename _B1> struct or_<_B1> : public _B1 { };
template<typename B1, typename B2> struct or_<B1, B2> : public conditional<B1::value, B1, B2>::type {};
template<typename B1, typename B2, typename B3, typename... Bn> struct or_<B1, B2, B3, Bn...>
        : public conditional<B1::value, B1, or_<B2, B3, Bn...> >::type {};
template<typename...> struct and_;
template<> struct and_<> : public true_type {};
template<typename B1> struct and_<B1> : public B1 {};
template<typename B1, typename B2> struct and_<B1, B2> : public conditional<B1::value, B2, B1>::type {};
template<typename B1, typename B2, typename B3, typename... Bn> struct and_<B1, B2, B3, Bn...>
  : public conditional<B1::value, and_<B2, B3, Bn...>, B1>::type {};
template<typename P> struct not_ : public integral_constant<bool, !P::value> {};

template<typename> struct is_void_helper : public false_type {};
template<> struct is_void_helper<void> : public true_type {};
template<typename T> struct remove_const { typedef T type; };
template<typename T> struct remove_const<T const> { typedef T type; };
template<typename T> struct is_void : public integral_constant<bool,is_void_helper<typename remove_const<T>::type>::value> {};

template<typename> struct is_function : public false_type {};
template<typename R, typename... Args> struct is_function<R(Args...)> : public true_type {};
template<typename R, typename... Args> struct is_function<R(Args...) const> : public true_type {};

template<typename> struct is_array : public false_type {};
template<typename T, uint Size> struct is_array<T[Size]> : public true_type {};
template<typename T> struct is_array<T[]> : public true_type {};

template<typename From, typename To, bool = or_<is_void<From>, is_function<To>, is_array<To> >::value> struct is_convertible_helper
  { static constexpr bool value = is_void<To>::value; };
template<typename> struct is_rvalue_reference : public false_type { };
template<typename T> struct is_rvalue_reference<T&&> : public true_type { };
template<typename T> struct is_reference : public or_<is_lvalue_reference<T>, is_rvalue_reference<T> >::type {};
template<typename T, bool = and_<not_<is_reference<T>>, not_<is_void<T> > >::value> struct add_rvalue_reference_helper
  { typedef T type; };
template<typename T> struct add_rvalue_reference_helper<T, true> { typedef T&& type; };
template<typename T> struct add_rvalue_reference : public add_rvalue_reference_helper<T> {};
template<typename T> typename add_rvalue_reference<T>::type declval();
struct sfinae_types { typedef char one; typedef struct { char arr[2]; } two; };
template<typename From, typename To> struct is_convertible_helper<From, To, false> : public sfinae_types {
    template<typename To1> static void test_aux(To1);
    template<typename From1, typename To1> static decltype(test_aux<To1>(declval<From1>()), one()) test(int);
    template<typename, typename> static two test(...);
    static constexpr bool value = sizeof(test<From, To>(0)) == 1;
};
template<typename F, typename T> struct is_convertible : public integral_constant<bool, is_convertible_helper<F, T>::value> {};
}
#define is_convertible(F, T) std::is_convertible<F, T>::value
#define can_forward(T) is_convertible(remove_reference(T), remove_reference(T##f))
#define perfect(T) class T##f, predicate(can_forward(T))
#define perfect2(T,U) class T##f, class U##f, predicate(can_forward(T)), predicate1(can_forward(U))
