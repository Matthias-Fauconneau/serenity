#pragma once
#include "array.h"
#pragma GCC diagnostic ignored "-Wpmf-conversions"

template<typename... Args> struct delegate {
    void* _this=0;
    void (*method)(void*, Args...)=0;
    delegate(){}
    template <class C> delegate(C* _this, void (C::*method)(Args...)) : _this((void*)_this), method((void(*)(void*, Args...))method) {}
    void operator ()(Args... args) { debug(if(!_this)fail();) method(_this, args...); }
};
template<typename... Args> bool operator ==(const delegate<Args...>& a, const delegate<Args...>& b) {
    return a._this==b._this && a.method==b.method;
}

template<class T> struct array;
template<typename... Args> struct signal {
    array< delegate<> > slots;
    void emit(Args... args) { for(delegate<>& slot: slots) (*(delegate<Args...>*)&slot)(args...);  }
    template <class C> void connect(C* _this, void (C::*method)(Args...)) {
        slots.append( delegate<>(_this, (void (C::*)())method) );
    }
};
