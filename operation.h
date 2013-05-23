#pragma once
/// \file operation.h Abstract interface for operations run by a process manager
#include "array.h"
#include "map.h"
#include "string.h"
#include "file.h"
#include <typeinfo>
#include "data.h"

/// Reference counter to be inherited by shared objects
struct shareable {
    virtual void addUser() { ++userCount; }
    virtual uint removeUser() { return --userCount; }
    uint userCount = 1;
};

/// Abstract factory pattern (allows construction of class by names)
template <class I> struct Interface {
    struct AbstractFactory {
        virtual unique<I> constructNewInstance() abstract;
    };
    static map<ref<byte>, AbstractFactory*> factories;
    template <class C> struct Factory : AbstractFactory {
        unique<I> constructNewInstance() override { return unique<C>(); }
        Factory() { TextData s (str(typeid(C).name())); s.integer(); factories.insert(s.word(), this); }
        static Factory registerFactory;
    };
    static unique<I> instance(const ref<byte>& name) { return factories.at(name)->constructNewInstance(); }
};
template <class I> map<ref<byte>,typename Interface<I>::AbstractFactory*> Interface<I>::factories __attribute((init_priority(1000)));
template <class I> template <class C> typename Interface<I>::template Factory<C> Interface<I>::Factory<C>::registerFactory __attribute((init_priority(1001)));
#define class(C,I) \
    struct C; \
    template struct Interface<I>::Factory<C>; \
    struct C : virtual I

/// Dynamic-typed value
struct Variant : string {
    Variant(const ref<byte>& s) : string(s) {}
    Variant(int integer) : string(dec(integer)){}
    operator int() const { return toInteger(*this); }
    operator const string&() const { return *this; }
};
template<> inline Variant copy(const Variant& o) { return Variant((ref<byte>)o); }
template<> inline string str(const Variant& o) { return copy((const string&)o); }

/// Intermediate result
struct Result : shareable {
    Result(const ref<byte>& name, long timestamp, const ref<byte>& arguments, const ref<byte>& metadata, buffer<byte>&& data)
        : name(name), timestamp(timestamp), arguments(arguments), metadata(metadata), data(move(data)) {}
    string name;
    long timestamp; //TODO: hash
    string arguments;
    string metadata; //FIXME: allow Operation to construct derived result
    buffer<byte> data;
};
inline bool operator==(const Result& a, const ref<byte>& b) { return a.name == b; }
template<> inline string str(const Result& o) { return copy(o.name); }

 /// Executes an operation using inputs to compute outputs (of given sample sizes)
struct Operation {
    /// Returns which parameters affects this operation output
    virtual ref<ref<byte>> parameters() const { return {}; }
    /// Returns the desired intermediate data size in bytes for each outputs
    virtual uint64 outputSize(const map<ref<byte>, Variant>& args, const ref<shared<Result>>& inputs, uint index) abstract;
    /// Executes the operation using inputs to compute outputs
    virtual void execute(const map<ref<byte>, Variant>& args, array<shared<Result>>& outputs, const ref<shared<Result>>& inputs) abstract;
};
