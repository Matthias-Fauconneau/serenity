#pragma once
#include "thread.h"
#include "map.h"
#include "function.h"

/// D-Bus

template<class T> struct variant : T { variant(){} variant(T&& t):T(move(t)){} operator const T&() const { return *this; } };
template<> struct variant<int> { int t; variant(){} variant(int t):t(t){} operator const int&() const { return t; } };
template<> struct variant<uint> { uint t; variant(){} variant(uint t):t(t){} operator const uint&() const { return t; } };

/// Session bus
struct DBus : Socket, Poll {
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
    template<class... Outputs> void readMessage(uint32 serial, Outputs&... outputs);

    /// Writes a message of type \a type using \a Args as arguments
    template<class... Args> uint32 writeMessage(uint8 type, int32 replySerial, const string& target, const string& object,
                                           const string& interface, const string& member, const Args&... args);

    struct Reply {
        handle<DBus*> dbus; uint32 serial;

        /// Read the reply and return its value
        template<class T> operator T();
        /// Read empty replies
        /// \note \a Reply must be read before interleaving any call
        ~Reply() { if(dbus) dbus->read(serial); dbus=0; }
    };

    struct Object {
        handle<DBus*> dbus;
        String target;
        String object;
        /// MethodCall \a method with \a args. Returns a handle to read the reply.
        /// \note \a Reply must be read before interleaving any call
        template<class... Args> Reply operator ()(const string& method, const Args&... args);
        /*Reply operator ()(const string& method);
        template<class A> Reply operator ()(const string& method, const A& a);
        template<class A, class B> Reply operator()(const string& method, const A&, const B&);*/
        template<class A> void noreply(const string& method, const A&);
        template<class A, class B> void noreply(const string& method, const A&, const B&);
        template<class T> T get(const string& property);
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

    DBus();
    void event() override;
};
