#pragma once
#include "array.h"
#include "string.h"
#pragma GCC diagnostic ignored "-Wpmf-conversions"

template<typename R, typename... Args> struct delegate {
    void* _this=0;
    R (*method)(void*, Args...)=0;
    delegate(){}
    template <class C> delegate(C* _this, R (C::*method)(Args...)) : _this((void*)_this), method((R(*)(void*, Args...))method) {}
    R operator ()(Args... args) const { assert(this); return method(_this, move(args)...); }
};
template<typename... Args> bool operator ==(const delegate<Args...>& a, const delegate<Args...>& b) {
    return a._this==b._this && a.method==b.method;
}

template<class T> struct array;
template<typename... Args> struct signal {
    array< delegate<void> > slots;
    void emit(Args... args) { for(delegate<void>& slot: slots) (*(delegate<void,Args...>*)&slot)(copy(args)...);  }
    template <class C> void connect(C* _this, void (C::*method)(Args...)) {
        slots.append( delegate<void>(_this, (void (C::*)())method) );
    }
};
