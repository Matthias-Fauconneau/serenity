#pragma once
#include "array.h"
#pragma GCC diagnostic ignored "-Wpmf-conversions"

template<typename... Args> struct delegate {
    void* _this=0;
    void (*method)(void*, Args...)=0;
    delegate(){}
    template <class C> delegate(C* _this, void (C::*method)(Args...)) : _this((void*)_this), method((void(*)(void*, Args...))method) {}
};
template<class T> struct array;
template<typename... Args> struct signal : array< delegate<Args...> > {
    void emit(Args... args) { for(auto slot: *this) slot.method(slot._this, args...);  }
    template <class C> void connect(C* _this, void (C::*method)(Args...)) {
        *this << delegate<Args...>(_this, method);
    }
};
