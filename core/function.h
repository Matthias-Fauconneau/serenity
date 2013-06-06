#pragma once
/// \file function.h Equivalent to std::function
#include "array.h"

// functor abstract interface
template<Type R, Type... Args> struct functor;
template<Type R, Type... Args> struct functor<R(Args...)> { virtual R operator()(Args... args) const=0; };
// functor template specialization for anonymous functions
template<Type F, Type R, Type... Args> struct anonymous_function;
template<Type F, Type R, Type... Args> struct anonymous_function<F, R(Args...)> : functor<R(Args...)> {
    F f;
    anonymous_function(F f):f(f){}
    virtual R operator()(Args... args) const override { return f(forward<Args>(args)...); }
};
// functor template specialization for methods
template<Type O, Type R, Type... Args> struct method;
template<Type O, Type R, Type... Args> struct method<O, R(Args...)> : functor<R(Args...)> {
    O* object;
    R (O::*pmf)(Args...);
    method(O* object, R (O::*pmf)(Args...)): object(object), pmf(pmf){}
    virtual R operator ()(Args... args) const override { return (object->*pmf)(forward<Args>(args)...); }
};
// functor template specialization for const methods
template<Type O, Type R, Type... Args> struct const_method;
template<Type O, Type R, Type... Args> struct const_method<O, R(Args...)> : functor<R(Args...)> {
    const O* object;
    R (O::*pmf)(Args...) const;
    const_method(const O* object, R (O::*pmf)(Args...) const): object(object), pmf(pmf){}
    virtual R operator ()(Args... args) const override { return (object->*pmf)(forward<Args>(args)...); }
};

template<Type R, Type... Args> struct function;
/// Provides a common interface to store functions, methods (delegates) and anonymous functions (lambdas)
template<Type R, Type... Args> struct function<R(Args...)> : functor<R(Args...)> {
    long any[22]; //always store functor inline
    template<Type F> function(F f) {
        static_assert(sizeof(anonymous_function<F,R(Args...)>)<=sizeof(any),"");
        new (any) anonymous_function<F,R(Args...)>(f);
    }
    template<Type O> function(O* object, R (O::*pmf)(Args...)) {
        static_assert(sizeof(method<O,R(Args...)>)<=sizeof(any),"");
        new (any) method<O,R(Args...)>(object, pmf);
    }
    template<Type O> function(const O* object, R (O::*pmf)(Args...) const) {
        static_assert(sizeof(const_method<O,R(Args...)>)<=sizeof(any),"");
        new (any) const_method<O,R(Args...)>(object, pmf);
    }
#pragma GCC system_header //-Wstrict-aliasing
    virtual R operator()(Args... args) const override { return ((functor<R(Args...)>&)any)(forward<Args>(args)...); }
};

/// Helps modularization by binding unrelated objects through persistent connections
template<Type... Args> struct signal {
    array<function<void(Args...)>> delegates;
    /// Emits the signal to all registered delegates
    void operator()(Args... args) const { for(const function<void(Args...)>& delegate: delegates) delegate(args...); }
    /// Connects an anonymous function
    template<Type F> void connect(F f) { delegates<< f; }
    /// Connects a function
    void connect(void (*pf)(Args...)) { delegates<< function<void(Args...)>(pf); }
    /// Connects a method
    template<class C, class B>
    void connect(C* object, void (B::*pmf)(Args...)) { delegates<< function<void(Args...)>(static_cast<B*>(object),pmf); }
    /// Connects a const method
    template<class C, class B>
    void connect(const C* object, void (B::*pmf)(Args...) const) { delegates<< function<void(Args...)>(static_cast<const B*>(object),pmf); }
    /// Returns whether this signal is connected to any delegate
    explicit operator bool() { return delegates.size; }
    /// Returns the emit operator as an anonymous function
    operator function<void(Args...)>(){ return {this,&signal<Args...>::operator()}; }
    /// Clears all connections
    void clear() { delegates.clear(); }
};
template<Type... Args> signal<Args...> copy(const signal<Args...>& b) { signal<Args...> a; a.delegates=copy(b.delegates); return a; }
