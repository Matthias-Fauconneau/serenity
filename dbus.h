#pragma once
#include "process.h"
#include "map.h"
#include "signal.h"

/// D-Bus

template <class T> struct variant : T { variant(){} variant(T&& t):T(move(t)){} operator const T&() const { return *this; } };
template <> struct variant<int> { int t; variant(){} variant(int t):t(t){} operator const int&() const { return t; } };

//TODO: tuples
struct DBusIcon {
    int width;
    int height;
    array<byte> data;
};

struct DBus : Poll {
    int fd = 0; // Session bus socket
    string name; // Instance name given by DBus
    uint32 serial = 0; // Serial number to match messages and their replies
    /// DBus read loop is able to dynamically dispatch notifications on signals and methods call
    /// \note the necessary IPC wrappers are generated using template metaprogramming when registering delegates
    map<string, delegate<void(uint32, string, array<byte>)> > methods; //wrappers to parse arguments, call methods and serialize reply
    map<string, signal< string, array<byte> > > signals_; //wrappers to parse arguments and emit signals
    map<string, delegate<void()> > delegates; //actual methods/signals implementations by delegates

    /// Read messages and parse \a outputs from first reply (without type checking)
    template<class... Outputs> void read(uint32 serial, Outputs&... outputs);

    /// Write a message of type \a type using \a Args as arguments
    template<class... Args> uint32 write(int type, int32 replySerial, const string& target, const string& object,
                                           const string& interface, const string& member, const Args&... args);

    struct Reply {
        DBus* d; uint32 serial;

        Reply(DBus* d, uint32 serial):d(d),serial(serial){}
        no_copy(Reply) Reply(Reply&& o):d(o.d),serial(o.serial){o.d=0;}

        /// Read the reply and return its value
        template<class T> operator T();
        /// Read empty replies
        /// \note \a Reply must be read before interleaving any call
        ~Reply() { if(d) d->read(serial); d=0; }
    };

    struct Object {
        DBus* d;
        string target;
        string object;
        Object(DBus* d,string target,string object):d(d),target(move(target)),object(move(object)){} //QtCreator doesn't like initializer
        /// MethodCall \a method with \a args. Return a handle to read the reply.
        /// \note \a Reply must be read before interleaving any call
        //template<class... Args> Reply operator ()(const string& method, const Args&... args);
        Reply operator ()(const string& method);
        template<class A> Reply operator ()(const string& method, const A& a);
        template<class A, class B> Reply operator()(const string& method, const A&, const B&);
        template<class T> T get(const string& property);
    };

    /// Get a handle to a D-Bus object
    Object operator ()(const string& object);

    /// call method delegate and return empty reply
    void methodWrapper(uint32 serial, string name, array<byte>);
    /// Unpack one argument, call method delegate and return empty reply
    template<class A> void methodWrapper(uint32 serial, string name, array<byte> data);
    /// Unpack one argument and call method delegate and return reply
    template<class R, class A> void methodWrapper(uint32 serial, string name, array<byte> data);
    /// Unpack two arguments and call the method delegate and return reply
    template<class R, class A, class B> void methodWrapper(uint32 serial, string name, array<byte> data);

    /// Generates an IPC wrapper for \a method and bind it to \a name
    template<class C, class R, class... Args> void bind(const string& name, C* object, R (C::*method)(Args...) ) {
        methods.insert(copy(name), delegate<void(uint32, string, array<byte>)>(this,&DBus::methodWrapper<R,Args...>));
        delegates.insert(copy(name), delegate<void()>(object, (void (C::*)())method));
    }
    template<class C, class... Args> void bind(const string& name, C* object, void (C::*method)(Args...) ) {
        methods.insert(copy(name), delegate<void(uint32, string, array<byte>)>(this,&DBus::methodWrapper<Args...>));
        delegates.insert(copy(name), delegate<void()>(object, (void (C::*)())method));
    }

    /// call signal delegate
    void signalWrapper(string name, array<byte>);
    /// Unpack one argument and call signal delegate
    template<class A> void signalWrapper(string name, array<byte> data);
    /// Unpack two arguments and call signal delegate
    template<class A, class B> void signalWrapper(string name, array<byte> data);
    /// Unpack three arguments and call signal delegate
    template<class A, class B, class C> void signalWrapper(string name, array<byte> data);

    /// Generates an IPC wrapper for \a signal and connect it to \a name
    template<class C, class... Args> void connect(const string& name, C* object, void (C::*signal)(Args...) ) {
        signals_[copy(name)].connect(this,&DBus::signalWrapper<Args...>);
        delegates.insert(copy(name), delegate<void()>(object, (void (C::*)())signal) );
    }

    DBus();
    void event(pollfd) override;
};
