#pragma once
#include "array.h"
#include "string.h"
#pragma GCC diagnostic ignored "-Wpmf-conversions"
#define is_base_of(B,D) std::is_base_of<B,D>::value

template<typename R, typename... Args> struct delegate {
    void* _this=0;
    R (*method)(void*, Args...)=0;
    delegate(){}
    template<class C, class B, predicate(is_base_of(B,C))> delegate(C* _this, R (B::*method)(Args...))
        : _this((void*)(B*)_this), method((R(*)(void*, Args...))method) {}
    R operator ()(Args... args) const { assert(this); return method(_this, move(args)...); }
};
template<typename... Args> bool operator ==(const delegate<Args...>& a, const delegate<Args...>& b) {
    return a._this==b._this && a.method==b.method;
}

template<class T> struct array;
template<typename... Args> struct signal {
    array< delegate<void> > slots;
    void emit(Args... args) { for(delegate<void>& slot: slots) (*(delegate<void,Args...>*)&slot)(copy(args)...);  }
    template<class C, class B, predicate(is_base_of(B,C))> void connect(C* _this, void (B::*method)(Args...)) {
        slots.append( delegate<void>(_this, (void (C::*)())method) );
    }
    void connect(signal<Args...>* signal) { connect(signal, &signal::emit); }
    template<class C> bool disconnect(C* _this) {
        for(uint i=0;i<slots.size();i++) if(slots[i]._this==_this) { slots.removeAt(i); return true; }
        return false;
    }
};
