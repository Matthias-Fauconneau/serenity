#pragma once
#include "thread.h"
#include "map.h"
#include "function.h"
#include "data.h"
#include <typeinfo>

/// Aligns array size appending default elements
inline void align(array<byte>& a, int width) { int s=a.size, n=align(width, s); if(n>s) a.grow(n); }

/// D-Bus

template<class T> struct variant : T { variant(){} variant(T&& t):T(move(t)){} operator const T&() const { return *this; } };
template<> struct variant<int> { int t; variant(){} variant(int t):t(t){} operator const int&() const { return t; } };
template<> struct variant<uint> { uint t; variant(){} variant(uint t):t(t){} operator const uint&() const { return t; } };

// D-Bus argument parsers
template<class T> void read(BinaryData& s, variant<T>& output) {
    uint8 size = s.read(); s.advance(size+1); //TODO: type checking
    read(s, (T&)output);
}
template<class T> void read(BinaryData& s, array<T>& output) {
    s.align(4); uint size = s.read();
    for(uint start=s.index;s.index<start+size;) { T e; read(s,e); output << move(e); }
}
inline void read(BinaryData& s, String& output) { s.align(4); uint size=s.read(); output = String(s.Data::read(size)); s.advance(1);/*0*/ }
template<Type T> void readRaw(BinaryData& s, T& output) { s.align(alignof(T)); output = s.read<T>(); }
inline void read(BinaryData& s, int& output) { readRaw(s, output); }
inline void read(BinaryData& s, uint& output) { readRaw(s, output); }
inline void read(BinaryData&) {}
template<class Arg, class... Args> void read(BinaryData& s, Arg& arg, Args&... args) { read(s,arg); read(s, args ...); }

// D-Bus signature serializer
template<class... Args> struct Signature { operator bool() { return true; } };
template<> struct Signature<> { operator bool() { return false; } };
struct ObjectPath : String {};
template<Type K, Type V> struct DictEntry { K k; V v; };

template<class A> void sign() { static_assert(sizeof(A) & 0,"No signature serializer defined for type"); }
template<class Arg, class... Args> void sign(String& s) { sign<Arg>(s); sign<Args...>(s); }

template<> inline void sign<int>(String& s) { s<<'i'; }
template<> inline void sign<uint>(String& s) { s<<'u'; }

template<> inline void sign<string>(String& s) { s<<'s'; }
template<> inline void sign<String>(String& s) { s<<'s'; }

template<> inline void sign<ObjectPath>(String& s) { s<<'o'; }

//template<class T> void sign< Signature<T> >(String& s) { s<<'g'; } //function template partial specialization is not allowed

//template<class T> void sign< variant<T> >(String& s) { s<<'v'; sign<T>(s); } //function template partial specialization is not allowed
template<> inline void sign< variant<String> >(String& s) { s<<'v'; }
template<> inline void sign< variant<int> >(String& s) { s<<'v'; }

//template<Type K, Type V> inline void sign<DictEntry<K,V>>(String& s) { s<<'{'; sign<K>(s); sign<V>(s); s<<"}"; } //function template partial specialization is not allowed
template<> inline void sign<DictEntry<String,variant<String>>>(String& s) { s<<'{'; sign<String>(s); sign<variant<String>>(s); s<<'}'; }

//template<Type T> inline void sign<array<T>>(String& s) { s<<'a'; sign<T>(s); } //function template partial specialization is not allowed
template<> inline void sign<array<DictEntry<String,variant<String>>>>(String& s) { s<<'a'; sign<DictEntry<String,variant<String>>>(s); }
typedef array<DictEntry<String,variant<String>>> Dict;

//  D-Bus arguments serializer
template<class A> void serialize(array<byte>&, const A&) { static_assert(sizeof(A) & 0,"No argument serializer defined for type"); }
template<class Arg, class... Args> void serialize(array<byte>& s, const Arg& arg, const Args&... args) { serialize(s,arg); serialize(s, args ...); }
inline void serialize(array<byte>&) {}

inline void serialize(array<byte>& s, uint8 byte) { s << byte; }
inline void serialize(array<byte>& s, int integer) { align(s, 4); s << raw<int>(integer); }
inline void serialize(array<byte>& s, uint integer) { align(s, 4); s << raw<uint>(integer); }

template<class T> void serialize(array<byte>& s, const variant<T>& variant) { serialize(s, Signature<T>()); serialize(s,(T&)variant); }
template<class... Args> void serialize(array<byte>& s, Signature<Args...>) {
    String signature; sign<Args...>(signature); assert(signature); s << (uint8)signature.size << move(signature) << 0;
}
template<> inline void serialize(array<byte>& s, Signature<>) { s << 0 << 0; }

template<class K, class V> void serialize(array<byte>& s, const DictEntry<K,V>& entry) { align(s, 8); serialize(s, entry.k); serialize(s, entry.v); }

template<class T> void serialize(array<byte>& s, const ref<T>& array) { serialize(s, uint(s.size*sizeof(T))); align(s, alignof(T)); for(const auto& e: array) serialize(s, e); }
template<class T> void serialize(array<byte>& s, const array<T>& array) { serialize(s, uint(s.size*sizeof(T))); align(s, alignof(T)); for(const auto& e: array) serialize(s, e); }

inline void serialize(array<byte>& s, const string& input) { serialize(s, uint(input.size)); s << input << 0; }
inline void serialize(array<byte>& s, const String& input) { serialize(s, uint(input.size)); s << input << 0; }

/// Session bus
struct DBus : Socket, Poll {
    enum Message { InvalidType, MethodCall, MethodReturn, Error, Signal };

    String name; // Instance name given by DBus
    uint32 serial = 0; // Serial number to match messages and their replies
    /// DBus read loop is able to dynamically dispatch notifications on signals and methods call
    /// \note the necessary IPC wrappers are generated using template metaprogramming when registering delegates
    typedef function<void(uint32, string, ref<byte>)> RawMethod;
    map<String, RawMethod> methods; // Wrappers to parse arguments, call methods and serialize reply
    typedef signal<string, ref<byte>> RawSignal;
    map<String, RawSignal> signals_; // Wrappers to parse arguments and emit signals
    map<String, function<void()> > delegates; // Actual methods/signals implementations by delegates

    /// Reads messages and parse \a outputs from first reply (without type checking)
    void readMessage(uint32 serial, function<void(BinaryData&)> readOutputs);
    template<class... Outputs> void readMessage(uint32 serial, Outputs&... outputs) {
        readMessage(serial,  [&outputs...](BinaryData& s){::read(s, outputs...); });
    }


    /// Writes a message of type \a type using \a Args as arguments
    uint32 writeSerializedMessage(uint8 type, int32 replySerial, const string& target, const string& object,
                        const string& interface, const string& member,  const ref<byte>& signature, const ref<byte>& arguments);
    template<class... Args> uint32 writeMessage(uint8 type, int32 replySerial, const string& target, const string& object,
                                           const string& interface, const string& member, const Args&... arguments) {
        ::Signature<Args...> signature; array<byte> serializedSignature; if(signature) serialize(serializedSignature, signature);
        array<byte> serializedArguments; serialize(serializedArguments, arguments...);
        return writeSerializedMessage(type, replySerial, target, object, interface, member, serializedSignature, serializedArguments);
    }

    struct Reply {
        handle<DBus*> dbus; uint32 serial;

        /// Read the reply and return its value
        template<class T> operator T();
        /// Read empty replies
        /// \note \a Reply must be read before interleaving any call
        ~Reply() { if(dbus) dbus->read(serial); dbus=0; }
    };

    struct Object {
        DBus* dbus;
        String target;
        String object;
        /// MethodCall \a method with \a args. Returns a handle to read the reply.
        /// \note \a Reply must be read before interleaving any call
        template<class... Args> DBus::Reply operator ()(const string& method, const Args&... args) {
            string interface = section(method,'.',0,-2)?:target, member=section(method,'.',-2,-1);
            return {dbus, dbus->writeMessage(MethodCall, -1, target, object, interface, member, args ...)};
        }
        template<class A> void noreply(const string& method, const A&);
        template<class A, class B> void noreply(const string& method, const A&, const B&);
        template<class T> T get(const string& property)  {
            string interface = section(property,'.',0,-2)?:target, member=section(property,'.',-2,-1);
            uint32 serial = dbus->writeMessage(MethodCall,-1, target, object, "org.freedesktop.DBus.Properties"_, "Get"_, interface, member);
            variant<T> t; dbus->readMessage(serial, t); return move(t);
        }
        array<String> children();
        Object node(string name);
    };

    /// Gets a handle to a D-Bus object
    Object operator()(const string& object);

    /// Calls method delegate and returns empty reply
    void methodWrapper(uint32 serial, string name, ref<byte>);
    /// Unpacks one argument, calls method delegate and returns empty reply
    template<class A> void methodWrapper(uint32 serial, string name, ref<byte> data);
    /// Unpacks one argument and calls method delegate and returns reply
    template<class R, class A> void methodWrapper(uint32 serial, string name, ref<byte> data);
    /// Unpacks two arguments and calls the method delegate and returns reply
    template<class R, class A, class B> void methodWrapper(uint32 serial, string name, ref<byte> data);

    /// Generates an IPC wrapper for \a method and binds it to \a name
    template<class C, class R, class... Args> void bind(const string& name, C* object, R (C::*method)(Args...) ) {
        methods.insert(name, {this,&DBus::methodWrapper<R,Args...>});
        delegates.insert(name, {object, (void (C::*)())method});
    }
    template<class C, class... Args> void bind(const string& name, C* object, void (C::*method)(Args...) ) {
        methods.insert(name, {this,&DBus::methodWrapper<Args...>});
        delegates.insert(name, {object, (void (C::*)())method});
    }

    /// Calls signal delegate
    void signalWrapper(string name, ref<byte>);
    /// Unpacks one argument and calls signal delegate
    template<class A> void signalWrapper(string name, ref<byte> data);
    /// Unpacks two arguments and calls signal delegate
    template<class A, class B> void signalWrapper(string name, ref<byte> data);
    /// Unpacks three arguments and calls signal delegate
    template<class A, class B, class C> void signalWrapper(string name, ref<byte> data);

    /// Generates an IPC wrapper for \a signal and connect it to \a name
    template<class C, class... Args> void connect(const string& name, C* object, void (C::*signal)(Args...) ) {
        signals_[name].connect(this,&DBus::signalWrapper<Args...>);
        delegates.insert(name, {object, (void (C::*)())signal});
    }

    enum Scope { Session, System };
    DBus(Scope scope=Session);
    void event() override;
};

inline String str(const DBus::Object& o) { return str(o.target, o.object); }
