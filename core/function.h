#pragma once
/// \file function.h Equivalent to std::function
#include "array.h"

// functor abstract interface
template<Type R, Type... Args> struct functor;
template<Type R, Type... Args> struct functor<R(Args...)> {
    virtual ~functor() {}
    virtual R operator()(Args... args) const abstract;
};
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
    virtual R operator()(Args... args) const override { return (object->*pmf)(forward<Args>(args)...); }
};
// functor template specialization for const methods
template<Type O, Type R, Type... Args> struct const_method;
template<Type O, Type R, Type... Args> struct const_method<O, R(Args...)> : functor<R(Args...)> {
    const O* object;
    R (O::*pmf)(Args...) const;
    const_method(const O* object, R (O::*pmf)(Args...) const): object(object), pmf(pmf){}
    virtual R operator()(Args... args) const override { return (object->*pmf)(forward<Args>(args)...); }
};

template<Type R, Type... Args> struct function;
/// Provides a common interface to store functions, methods (delegates) and anonymous functions (lambdas)
template<Type R, Type... Args> struct function<R(Args...)> : functor<R(Args...)> {
    long any[32]; // Always store functor inline
    function() : any{0}{} // Invalid function (segfaults)
    /// Wraps an anonymous function (or a a function pointer)
    template<Type F> function(F f) {
        static_assert(sizeof(anonymous_function<F,R(Args...)>)<=sizeof(any),"");
        new (any) anonymous_function<F,R(Args...)>(f);
    }
    /// Wraps a method
    template<Type O> function(O* object, R (O::*pmf)(Args...)) {
        static_assert(sizeof(method<O,R(Args...)>)<=sizeof(any),"");
        new (any) method<O,R(Args...)>(object, pmf);
    }
    /// Wraps a const method
    template<Type O> function(const O* object, R (O::*pmf)(Args...) const) {
        static_assert(sizeof(const_method<O,R(Args...)>)<=sizeof(any),"");
        new (any) const_method<O,R(Args...)>(object, pmf);
    }
    virtual R operator()(Args... args) const override { assert(any[0]); return ((functor<R(Args...)>&)any)(forward<Args>(args)...); }
    explicit operator bool() { return any[0]; }
};

/// Helps modularization by binding unrelated objects through persistent connections
template<Type... Args> struct signal {
    array<function<void(Args...)>> delegates;

    /// Connects a delegate
    template<Type... A> void connect(A&&... args) { delegates.append( forward<A>(args)... ); }
    /// Returns the emit operator as an anonymous function
    operator function<void(Args...)>() { return {this, &signal<Args...>::operator()}; }
    /// Emits the signal to all registered delegates
    void operator()(Args... args) const { for(const function<void(Args...)>& delegate: delegates) delegate(args...); }
};
template<Type... Args> signal<Args...> copy(const signal<Args...>& b) { signal<Args...> a; a.delegates=copy(b.delegates); return a; }

// Sort using anonymous comparison function
generic uint partition(function<bool(const T&, const T&)> less, const mref<T>& at, size_t left, size_t right, size_t pivotIndex) {
    swap(at[pivotIndex], at[right]);
    const T& pivot = at[right];
    uint storeIndex = left;
    for(uint i: range(left,right)) {
        if(less(pivot, at[i])) {
            swap(at[i], at[storeIndex]);
            storeIndex++;
        }
    }
    swap(at[storeIndex], at[right]);
    return storeIndex;
}

generic void quicksort(function<bool(const T&, const T&)> less, const mref<T>& at, int left, int right) {
    if(left < right) { // If the list has 2 or more items
        int pivotIndex = partition(less, at, left, right, (left + right)/2);
        if(pivotIndex) quicksort(less, at, left, pivotIndex-1);
        quicksort(less, at, pivotIndex+1, right);
    }
}
/// Quicksorts the array in-place
generic const mref<T>& sort(function<bool(const T&, const T&)> less, const mref<T>& at) {
    if(at.size) quicksort(less, at, 0, at.size-1); return at;
}
