#pragma once
#include "core.h"
#include "debug.h"

/// functor abstract interface
template<class R, class... Args> struct functor;
template<class R, class... Args> struct functor<R(Args...)> { virtual R operator()(Args... args) const=0; };
/// functor template specialization for lambdas
template<class F, class R, class... Args> struct lambda;
template<class F, class R, class... Args> struct lambda<F, R(Args...)> : functor<R(Args...)> {
    F f;
    lambda(F&& f):f(move(f)){}
    R operator()(Args... args) const { return f(forward<Args>(args)...); }
};
/// functor template specialization for methods
template<class O, class R, class... Args> struct method;
template<class O, class R, class... Args> struct method<O, R(Args...)> : functor<R(Args...)> {
    O* object;
    R (O::*pmf)(Args...);
    method(O* object, R (O::*pmf)(Args...)): object(object), pmf(pmf){}
    R operator ()(Args... args) const { return (object->*pmf)(forward<Args>(args)...); }
};

template<class R, class... Args> struct function;
template<class R, class... Args> struct function<R(Args...)> {
    uint any[4]; //store any functor on stack
    template<class F> function(F f) {
        assert_(sizeof(lambda<F,R(Args...)>)<=sizeof(any));
        new (any) lambda<F,R(Args...)>(move(f));
    }
    template<class O> function(O* object, void (O::*pmf)(Args...)) {
        assert(sizeof(method<O,R(Args...)>)<=sizeof(any), sizeof(method<O,R(Args...)>), sizeof(any));
        new (any) method<O,R(Args...)>(object, pmf);
    }
    R operator()(Args... args) const { return ((functor<R(Args...)>&)any)(forward<Args>(args)...); }
};
