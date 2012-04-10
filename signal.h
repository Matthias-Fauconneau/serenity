#pragma once
#include "array.h"
#include "string.h"
#include "debug.h"
#define is_base_of(B,D) std::is_base_of<B,D>::value

template<class T> class delegate {};
class VoidClass {};
template<class R, class... Args> struct delegate<R(Args...)> {
    VoidClass* _this;
    union {
        R (VoidClass::*member)(Args...);
        R (*method)(void*, Args...);
    };
    delegate():_this(0),member(0){}
    template<class C, class B, predicate(is_base_of(B,C))> delegate(C* _this, R (B::*method)(Args...))
        : _this((VoidClass*)(B*)_this), member((R(VoidClass::*)(Args...))method) {}
    R operator ()(Args... args) const { assert(this); return method(_this,move(args)...); }
};
template<class... Args> bool operator ==(const delegate<Args...>& a, const delegate<Args...>& b) {
    return a._this==b._this && a.method==b.method;
}

template<class T> struct array;
template<class... Args> struct signal {
    array< delegate<void()> > slots;
    void emit(Args... args) { for(delegate<void()>& slot: slots) (*(delegate<void(Args...)>*)&slot)(copy(args)...);  }
    template<class C, class B, predicate(is_base_of(B,C))> void connect(C* _this, void (B::*method)(Args...)) {
        slots.append( delegate<void()>(_this, (void (C::*)())method) );
    }
    void connect(signal<Args...>* signal) { connect(signal, &signal::emit); }
    operator delegate<void()>() { return delegate<void()>(this, &signal::emit); }
    template<class C> bool disconnect(C* _this) {
        for(uint i=0;i<slots.size();i++) if(slots[i]._this==(VoidClass*)_this) { slots.removeAt(i); return true; }
        return false;
    }
};
