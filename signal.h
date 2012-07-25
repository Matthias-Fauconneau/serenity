#pragma once
#include "array.h"
#include "string.h"
#include "function.h"

template<class... Args> struct signal {
    array< function<void(Args...)> > slots;
    void operator()(Args... args) const { for(const auto& slot: slots) slot(args...); }
    template<class F> void connect(F f) { slots<< f; }
    template<class C, class B, predicate(__is_base_of(B,C))>
    void connect(C* object, void (B::*pmf)(Args...)) { slots<< function<void(Args...)>(static_cast<B*>(object),pmf); }
    //operator function<void(Args...)>() { return function<void(Args...)>(this, &signal::emit); }
};
