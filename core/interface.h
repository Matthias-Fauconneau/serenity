#pragma once
/// \file interface.h Abstract factory pattern (allows construction of registered class by names)
#include <typeinfo>
#include "data.h"
#include "map.h"

/// Abstract factory pattern (allows construction of class by names)
template <class I> struct Interface {
    struct AbstractFactory {
        /// Returns the version of this implementation
        virtual string version() abstract;
        virtual unique<I> constructNewInstance() abstract;
    };
    static map<string, AbstractFactory*> factories;
    template <class C> struct Factory : AbstractFactory {
        string version() override { return __DATE__ " " __TIME__ ""_; }
        unique<I> constructNewInstance() override { return unique<C>(); }
        Factory() { TextData s (str(typeid(C).name())); s.integer(); factories.insert(s.identifier(), this); }
        static Factory registerFactory;
    };
    static string version(const string& name) { return factories.at(name)->version(); }
    static unique<I> instance(const string& name) { return factories.at(name)->constructNewInstance(); }
};
template <class I> map<string,typename Interface<I>::AbstractFactory*> Interface<I>::factories;
template <class I> template <class C> typename Interface<I>::template Factory<C> Interface<I>::Factory<C>::registerFactory;
