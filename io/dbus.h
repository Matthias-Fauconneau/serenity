#pragma once
#include "thread.h"
#include "map.h"
#include "function.h"
#include "data.h"
#include <typeinfo>

/// Aligns array size appending default elements
inline void align(array<byte>& a, int width) { int s=a.size, n=align(width, s); if(n>s) a.grow(n); }

/// D-Bus

template<Type T> struct variant : T { variant(){} variant(T&& t):T(move(t)){} operator const T&() const { return *this; } };
template<> struct variant<int> { int t; variant(){} variant(int t):t(t){} operator const int&() const { return t; } };
template<> struct variant<uint> { uint t; variant(){} variant(uint t):t(t){} operator const uint&() const { return t; } };
template<> struct variant<String> { String t; variant(){} variant(String&& t):t(move(t)){} operator const String&() const { return t; } };

// D-Bus argument parsers
template<Type T> void read(BinaryData& s, variant<T>& output) {
    uint8 size = s.read(); s.advance(size+1); //TODO: type checking
    read(s, (T&)output);
}
template<Type T> void read(BinaryData& s, array<T>& output) {
    s.align(4); uint size = s.read();
    for(uint start=s.index;s.index<start+size;) { T e; read(s,e); output.append(move(e)); }
}
inline void read(BinaryData& s, buffer<char>& output) { s.align(4); uint size=s.read(); output = copyRef(s.Data::read(size)); s.advance(1);/*0*/ }
template<Type T> void readRaw(BinaryData& s, T& output) { s.align(alignof(T)); output = s.read<T>(); }
inline void read(BinaryData& s, int& output) { readRaw(s, output); }
inline void read(BinaryData& s, uint& output) { readRaw(s, output); }
inline void read(BinaryData&) {}
generic void read(BinaryData&, T&) { static_assert(0&&sizeof(T),"No overload for read(BinaryData&, T&)"); }
template<Type Arg, Type... Args> void read(BinaryData& s, Arg& arg, Args&... args) { read(s, arg); read(s, args ...); }

// D-Bus signature serializer
template<Type... Args> struct Signature { operator bool() { return true; } };
template<> struct Signature<> { operator bool() { return false; } };
struct ObjectPath : String {};
template<Type K, Type V> struct DictEntry { K k; V v; };

template<Type A> void sign() { static_assert(sizeof(A) & 0,"No signature serializer defined for type"); }
template<Type Arg, Type... Args> void sign(array<char>& s) { sign<Arg>(s); sign<Args...>(s); }

template<> inline void sign<int>(array<char>& s) { s.append('i'); }
template<> inline void sign<uint>(array<char>& s) { s.append('u'); }

template<> inline void sign<string>(array<char>& s) { s.append('s'); }
template<> inline void sign<String>(array<char>& s) { s.append('s'); }

template<> inline void sign<ObjectPath>(array<char>& s) { s.append('o'); }

//template<Type T> void sign< Signature<T> >(array<char>& s) { s.append('g'); } // Function template partial specialization is not allowed

//template<Type T> void sign< variant<T> >(array<char>& s) { s.append('v'); sign<T>(s); } // Function template partial specialization is not allowed
template<> inline void sign< variant<String> >(array<char>& s) { s.append('v'); }
template<> inline void sign< variant<int> >(array<char>& s) { s.append('v'); }

//template<Type K, Type V> inline void sign<DictEntry<K,V>>(array<char>& s) { s.append('{'); sign<K>(s); sign<V>(s); s.append("}"); } // Function template partial specialization is not allowed
template<> inline void sign<DictEntry<String,variant<String>>>(array<char>& s) { s.append('{'); sign<String>(s); sign<variant<String>>(s); s.append('}'); }

//template<Type T> inline void sign<ref<T>>(array<char>& s) { s.append('a'); sign<T>(s); } // Function template partial specialization is not allowed
template<> inline void sign<ref<int>>(array<char>& s) { s.append('a'); sign<int>(s); }
template<> inline void sign<ref<string>>(array<char>& s) { s.append('a'); sign<string>(s); }
template<> inline void sign<ref<DictEntry<String,variant<String>>>>(array<char>& s) { s.append('a'); sign<DictEntry<String,variant<String>>>(s); }
typedef array<DictEntry<String,variant<String>>> Dict;

//  D-Bus arguments serializer
template<Type A> void serialize(array<byte>&, const A&) { static_assert(sizeof(A) & 0,"No argument serializer defined for type"); }
template<Type Arg, Type... Args> void serialize(array<byte>& s, const Arg& arg, const Args&... args) { serialize(s,arg); serialize(s, args ...); }
inline void serialize(array<byte>&) {}

inline void serialize(array<byte>& s, uint8 byte) { s .append(byte); }
inline void serialize(array<byte>& s, int integer) { align(s, 4); s.append(raw<int>(integer)); }
inline void serialize(array<byte>& s, uint integer) { align(s, 4); s.append(raw<uint>(integer)); }

template<Type T> void serialize(array<byte>& s, const variant<T>& variant) { serialize(s, Signature<T>()); serialize(s,(T&)variant); }
template<Type... Args> void serialize(array<byte>& s, Signature<Args...>) {
    array<char> signature; sign<Args...>(signature); assert(signature); s.append((uint8)signature.size); s.append(move(signature)); s.append(0);
}
template<> inline void serialize(array<byte>& s, Signature<>) { s.append(0); s.append(0); }

template<Type K, Type V> void serialize(array<byte>& s, const DictEntry<K,V>& entry) { align(s, 8); serialize(s, entry.k); serialize(s, entry.v); }

template<Type T> void serialize(array<byte>& s, const ref<T>& array) { serialize(s, uint(s.size*sizeof(T))); align(s, alignof(T)); for(const auto& e: array) serialize(s, e); }
template<Type T> void serialize(array<byte>& s, const array<T>& array) { serialize(s, (ref<T>)array); }

inline void serialize(array<byte>& s, const string input) { serialize(s, uint(input.size)); s.append(input); s.append(0); }
inline void serialize(array<byte>& s, const String& input) { serialize(s, (string)input ); }

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
    template<Type... Outputs> void readMessage(uint32 serial, Outputs&... outputs) {
        readMessage(serial,  [&outputs...](BinaryData& s){::read(s, outputs...); });
    }


    /// Writes a message of type \a type using \a Args as arguments
    uint32 writeSerializedMessage(uint8 type, int32 replySerial, const string target, const string object,
                        const string interface, const string member,  const ref<byte>& signature, const ref<byte>& arguments);
    template<Type... Args> uint32 writeMessage(uint8 type, int32 replySerial, const string target, const string object,
                                           const string interface, const string member, const Args&... arguments) {
        ::Signature<Args...> signature; array<byte> serializedSignature; if(signature) serialize(serializedSignature, signature);
        array<byte> serializedArguments; serialize(serializedArguments, arguments...);
        return writeSerializedMessage(type, replySerial, target, object, interface, member, serializedSignature, serializedArguments);
    }

    struct Reply {
        handle<DBus*> dbus; uint32 serial;

        /// Read the reply and return its value
        template<Type T> operator T() {//;//generic DBus::Reply::operator T() {
            assert(dbus,"Reply read twice"_);
            T t;
            dbus->readMessage<T>(serial, t);
            dbus=0; return move(t);
        }
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
        template<Type... Args> DBus::Reply operator ()(const string method, const Args&... args) {
            string interface = method.contains('.')?section(method,'.',0,-2):target, member=section(method,'.',-2,-1);
            return {dbus, dbus->writeMessage(MethodCall, -1, target, object, interface, member, args ...)};
        }
        template<Type A> void noreply(const string method, const A&);
        template<Type A, Type B> void noreply(const string method, const A&, const B&);
        template<Type T> T get(const string property)  {
            string interface = section(property,'.',0,-2)?:target, member=section(property,'.',-2,-1);
            uint32 serial = dbus->writeMessage(MethodCall,-1, target, object, "org.freedesktop.DBus.Properties"_, "Get"_, interface, member);
            variant<T> t; dbus->readMessage(serial, t); return move(t);
        }
        template<Type T> void set(const string property, variant<T> t)  {
            string interface = section(property,'.',0,-2)?:target, member=section(property,'.',-2,-1);
            dbus->writeMessage(MethodCall,-1, target, object, "org.freedesktop.DBus.Properties"_, "Set"_, interface, member, t);
        }
        array<String> children();
        Object node(string name);
    };

    /// Gets a handle to a D-Bus object
    Object operator()(const string object);

    /// Calls method delegate and returns empty reply
    void methodWrapper(uint32 serial, string name, ref<byte>);
    /// Unpacks one argument, calls method delegate and returns empty reply
    template<Type A> void methodWrapper(uint32 serial, string name, ref<byte> data);
    /// Unpacks one argument and calls method delegate and returns reply
    template<Type R, Type A> void methodWrapper(uint32 serial, string name, ref<byte> data);
    /// Unpacks two arguments and calls the method delegate and returns reply
    template<Type R, Type A, Type B> void methodWrapper(uint32 serial, string name, ref<byte> data);

    /// Generates an IPC wrapper for \a method and binds it to \a name
    template<Type C, Type R, Type... Args> void bind(const string name, C* object, R (C::*method)(Args...) ) {
        methods.insert(name, {this,&DBus::methodWrapper<R,Args...>});
        delegates.insert(name, {object, (void (C::*)())method});
    }
    template<Type C, Type... Args> void bind(const string name, C* object, void (C::*method)(Args...) ) {
        methods.insert(name, {this,&DBus::methodWrapper<Args...>});
        delegates.insert(name, {object, (void (C::*)())method});
    }

    /// Calls signal delegate
#if 1
    //template<Type... Args> void signalWrapper(string name, ref<byte> data); // Function template partial specialization is not allowed
    generic T readValue(BinaryData& in) { T t; read(in, t); return t; }
    template<Type... Args> void signalWrapper(string name, ref<byte> data) {
        BinaryData in(move(data));
        (*(function<void(Args...)>*)&delegates.at(name))(readValue<Args>(in)...);
    }
#else
    void signalWrapper(string name, ref<byte>);
    /// Unpacks one argument and calls signal delegate
    template<class A> void signalWrapper(string name, ref<byte> data);
    /// Unpacks two arguments and calls signal delegate
    template<class A, class B> void signalWrapper(string name, ref<byte> data);
    /// Unpacks three arguments and calls signal delegate
    template<class A, class B, class C> void signalWrapper(string name, ref<byte> data);
#endif

    /// Generates an IPC wrapper for \a signal and connect it to \a name
    template<Type C, Type... Args> void connect(const string name, C* object, void (C::*signal)(Args...) ) {
        signals_[copyRef(name)].connect(this,&DBus::signalWrapper<Args...>);
        delegates.insert(copyRef(name), {object, (void (C::*)())signal});
    }

    enum Scope { Session, System };
    DBus(Scope scope=Session);
    void event() override;
};

inline String str(const DBus::Object& o) { return str(o.target, o.object); }
