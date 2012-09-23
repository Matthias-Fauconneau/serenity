#pragma once
#include "array.h"

/// functor abstract interface
template<class R, class... Args> struct functor;
template<class R, class... Args> struct functor<R(Args...)> { virtual R operator()(Args... args) const=0; };
/// functor template specialization for lambdas
template<class F, class R, class... Args> struct lambda;
template<class F, class R, class... Args> struct lambda<F, R(Args...)> : functor<R(Args...)> {
    F f;
    lambda(F&& f):f(move(f)){}
    R operator()(Args... args) const { return f(forward<Args>(args)___); }
};
/// functor template specialization for methods
template<class O, class R, class... Args> struct method;
template<class O, class R, class... Args> struct method<O, R(Args...)> : functor<R(Args...)> {
    O* object;
    R (O::*pmf)(Args...);
    method(O* object, R (O::*pmf)(Args...)): object(object), pmf(pmf){}
    R operator ()(Args... args) const { return (object->*pmf)(forward<Args>(args)___); }
};
/// functor template specialization for const methods
template<class O, class R, class... Args> struct const_method;
template<class O, class R, class... Args> struct const_method<O, R(Args...)> : functor<R(Args...)> {
    const O* object;
    R (O::*pmf)(Args...) const;
    const_method(const O* object, R (O::*pmf)(Args...) const): object(object), pmf(pmf){}
    R operator ()(Args... args) const { return (object->*pmf)(forward<Args>(args)___); }
};

template<class R, class... Args> struct function;
template<class R, class... Args> struct function<R(Args...)> {
    long any[6]; //always store functor inline
    template<class F> function(F f) {
        static_assert(sizeof(lambda<F,R(Args...)>)<=sizeof(any),"");
        new (any) lambda<F,R(Args...)>(move(f));
    }
    template<class O> function(O* object, R (O::*pmf)(Args...)) {
        static_assert(sizeof(method<O,R(Args...)>)<=sizeof(any),"");
        new (any) method<O,R(Args...)>(object, pmf);
    }
    template<class O> function(const O* object, R (O::*pmf)(Args...) const) {
        static_assert(sizeof(const_method<O,R(Args...)>)<=sizeof(any),"");
        new (any) const_method<O,R(Args...)>(object, pmf);
    }
#pragma GCC system_header //-Wstrict-aliasing
    R operator()(Args... args) const { return ((functor<R(Args...)>&)any)(forward<Args>(args)___); }
};

template<class... Args> struct signal {
    array< function<void(Args...)> > delegates;
    void operator()(Args... args) const { for(const auto& delegate: delegates) delegate(args ___); }
    template<class F> void connect(F f) { delegates<< f; }
    template<class C, class B, predicate(__is_base_of(B,C))>
    void connect(C* object, void (B::*pmf)(Args...)) { delegates<< function<void(Args...)>(static_cast<B*>(object),pmf); }
    template<class C, class B, predicate(__is_base_of(B,C))>
    void connect(const C* object, void (B::*pmf)(Args...) const) { delegates<< function<void(Args...)>(static_cast<const B*>(object),pmf); }
    explicit operator bool() { return delegates.size(); }
    operator function<void(Args...)>(){ return __(this,&signal<Args...>::operator()); }
};
template<class... Args> signal<Args...> copy(const signal<Args...>& b) { signal<Args...> a; a.delegates=copy(b.delegates); return a; }
