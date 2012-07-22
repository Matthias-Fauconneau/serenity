#pragma once
#include "array.h"
#include "string.h"
#include "function.h"

template<class... Args> struct signal {
    array< function<void(Args...)> > slots;
    void operator()(Args... args) const { for(const auto& slot: slots) slot(args...); }
    //void operator()(Args... args) const { for(auto& slot: slots) slot(copy(args)...);  }
    //void emit(Args... args) const { for(auto& slot: slots) slot(copy(args)...);  }
    template<class F> void connect(F f) { slots<< f; }
    template<class O> void connect(O* object, void (O::*pmf)(Args...)) { slots<< function<void(Args...)>(object,pmf); }
    //operator function<void(Args...)>() { return function<void(Args...)>(this, &signal::emit); }
    /*template<class C> bool disconnect(C* _this) {
        for(uint i=0;i<slots.size();i++) if(slots.at(i)._this==(VoidClass*)_this) { slots.removeAt(i); return true; }
        return false;
    }*/
};
