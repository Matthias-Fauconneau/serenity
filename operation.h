#pragma once
/// \file operation.h Abstract interface for operations run by a process manager
#include "array.h"
#include "map.h"
#include "string.h"
#include "file.h"
#include <typeinfo>

/// Abstract factory pattern (allows construction of class by names)
template <class I> struct Interface {
    struct AbstractFactory {
        virtual I& constructNewInstance();
    };
    static map<ref<byte>, AbstractFactory*> factories;
    template <class C> struct Factory : AbstractFactory {
        I& constructNewInstance() override { return *(new C()); }
        Factory() { factories.insert(str(typeid(C).name()+1), this); }
        static Factory registerFactory;
    };
    static I& instance(const ref<byte>& name) { return factories.at(name)->constructNewInstance(); }
};
template <class I> map<ref<byte>,typename Interface<I>::AbstractFactory*> Interface<I>::factories;
template <class I> template <class C> typename Interface<I>::template Factory<C> Interface<I>::Factory<C>::registerFactory;
#define class(C,I) \
    struct C; \
    template struct Interface<I>::Factory<C>; \
    struct C : virtual I

/// Dynamic-typed value
struct Variant : string {
    Variant(const ref<byte>& s) : string(s) {}
    Variant(int integer):string(dec(integer)){}
    operator int() { return toInteger(*this); }
};

/// Intermediate result
struct Result : shareable {
    Result(const ref<byte>& name, long timestamp, const ref<byte>& metadata) : name(name), timestamp(timestamp), metadata(metadata) {}
    string name;
    long timestamp; //TODO: hash
    string metadata;
    array<byte> data;
};
bool operator==(const Result& a, const ref<byte>& b) { return a.name == b; }
template<> string str(const Result& o) { return copy(o.name); }

 /// Executes an operation using inputs to compute outputs (of given sample sizes)
struct Operation {
    /// Returns the desired intermediate data size in bytes for each outputs
    virtual uint64 outputSize(map<ref<byte>, Variant>& args, const ref<shared<Result>>& inputs, uint index) abstract;
    /// Executes the operation using inputs to compute outputs
    virtual void execute(map<ref<byte>, Variant>& args, const ref<shared<Result>>& outputs, const ref<shared<Result>>& inputs) abstract;
};

/// Convenience class to define a single input, single output operation
template<Type I, Type O> struct Pass : virtual Operation {
    virtual uint64 outputSize(ref<const Result*> inputs, uint index) abstract;
    virtual void execute(Result& target, const Result& source, map<ref<byte>, Variant>& args) abstract;
    virtual void execute(ref<Result*> outputs, ref<const Result*> inputs, map<ref<byte>, Variant>& args) override { execute(*outputs[0], *inputs[0], args); }
};
