#pragma once
#include "file.h"
#include "process.h"
#include "map.h"
#include "stream.h"
#include "signal.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

/// Align array size appending default elements
void align(array<byte>& a, int width) { int s=a.size(), n=align(width, s); if(n>s) grow(a, n); }
/// Align the stream position to the next \a width
void align(Stream& s, int width) { s.index=align(width, s.index); }

/// D-Bus

enum Message { InvalidType, MethodCall, MethodReturn, Error, Signal };
struct Header {
    byte endianness='l';
    ubyte message;
    ubyte flag = 0;
    ubyte version = 1;
    uint32 length;
    uint32 serial;
    uint32 fieldsSize;
    Header(){}
    Header(byte message, uint32 serial):message(message),serial(serial){}
};
enum Field { InvalidField, Path, Interface, Member, ErrorName, ReplySerial, Destination, Sender, Signature };

template <class T> struct variant : T { variant(){} variant(T&& t):T(move(t)){} operator const T&() const { return *this; } };
template <> struct variant<int> { int t; variant(){} variant(int t):t(t){} operator const int&() const { return t; } };
template<class... Args> struct signature { operator bool() { return true; } };
template<> struct signature<> { operator bool() { return false; } };
struct ObjectPath : string {};

//TODO: tuples
struct DBusIcon {
    int width;
    int height;
    array<byte> data;
};

// D-Bus signature serializer
template<class Arg, class... Args> void sign(string& s) { sign<Arg>(s); sign<Args...>(s); }
//template<class A> void sign(string&) { static_assert(sizeof(A) && 0,"No signature defined for type"); } //function template partial specialization
template<> void sign<string>(string& s) { s<<"s"_; }
template<> void sign<int>(string& s) { s<<"i"_; }
template<> void sign<uint>(string& s) { s<<"u"_; }
template<> void sign<ObjectPath>(string& s) { s<<"o"_; }
//template<class T> void sign< signature<T> >(string& s) { s<<"g"_; } //function template partial specialization is not allowed
//template<class T> void sign< variant<T> >(string& s) { s<<"v"_; sign<T>(s); } //function template partial specialization is not allowed
template<>void sign< variant<string> >(string& s) { s<<"v"_; }
template<>void sign< variant<int> >(string& s) { s<<"v"_; }

//  D-Bus arguments serializer
template<class A> void write(array<byte>& s, const A&) { static_assert(sizeof(A) && 0,"No serializer defined for type"); }
template<class Arg, class... Args> void write(array<byte>& s, const Arg& arg, const Args&... args) { write(s,arg); write(s, args i(...)); }
void write(array<byte>&) {}

void write(array<byte>& s, const string& input) { align(s, 4); s << raw(input.size()) << input << 0; }
void write(array<byte>& s, ubyte byte) { s << byte; }
void write(array<byte>& s, int integer) { align(s, 4); s << raw(integer); }
void write(array<byte>& s, uint integer) { align(s, 4); s << raw(integer); }
template<class T> void write(array<byte>& s, const variant<T>& variant) { write(s, signature<T>()); write(s,(T&)variant); }
template<class... Args> void write(array<byte>& s, signature<Args...>) {
    string signature; sign<Args...>(signature); assert(signature); s << (ubyte)signature.size() << move(signature) << 0;
}
template<> void write(array<byte>& s, signature<>) { s << 0 << 0; }

// D-Bus argument parsers
template<class T> void read(Stream& s, variant<T>& output) {
    uint8 size = s.read(); s += size+1; //TODO: type checking
    read(s,(T&)output);
}
template<class T> void read(Stream& s, array<T>& output) {
    align(s, 4); uint length = s.read();
    for(uint start=s.index;s.index<start+length;) { T e; read(s,e); output << move(e); }
}
void read(Stream& s, string& output) { align(s, 4); output = copy(s.readArray<byte>()); s++;/*0*/ }
void read(Stream&) {}
void read(Stream& s, DBusIcon& output) { align(s, 8); output.width=s.read(); output.height=s.read(); output.data=copy(s.readArray<byte>()); }
template<class Arg, class... Args> void read(Stream& s, Arg& arg, Args&... args) { read(s,arg); read(s, args i(...)); }

struct DBus : Poll {
    int fd = 0; // Session bus socket
    string name; // Instance name given by DBus
    uint32 serial = 0; // Serial number to match messages and their replies
    /// DBus read loop is able to dynamically dispatch notifications on signals and methods call
    /// \note the necessary IPC wrappers are generated using template metaprogramming when registering delegates
    map<string, delegate<void, uint32, string, array<byte> > > methods; //wrappers to parse arguments, call methods and serialize reply
    map<string, signal< string, array<byte> > > signals_; //wrappers to parse arguments and emit signals
    map<string, delegate<void> > delegates; //actual methods/signals implementations by delegates

    /// Read messages and parse \a outputs from first reply (without type checking)
    template<class... Outputs> void read(uint32 serial, Outputs&... outputs) {
        for(;;) {
            auto header = ::read<Header>(fd);
            if(header.message == MethodCall) assert(header.flag==0);
            uint32 replySerial=0;
            string name;
            Stream s( ::read(fd, align(4,header.fieldsSize)+header.length) );
            for(;s.index<header.fieldsSize;){
                byte field = s.read();
                uint8 size = s.read(); s+=size+1;
                if(field==Destination||field==Path||field==Interface||field==Member||field==Sender||field==ErrorName) {
                    string value; ::read(s, value);
                    if(field==Member) name=move(value);
                    else if(field==ErrorName) log(value);
                } else if(field==ReplySerial) {
                    replySerial=s.read();
                } else if(field==Signature) {
                    uint8 size = s.read(); s+=size;
                } else error("Unknown field"_,field);
                align(s, 8);
            }
            if(header.length) {
                if(header.message == MethodReturn && replySerial==serial) {
                    ::read(s, outputs i(...));
                    return;
                } else if(header.message == Signal) {
                    auto signal = signals_.find(name);
                    if(signal) signal->emit(move(name),s.readAll<byte>());
                    if(!serial) return;
                } else if(header.message == MethodCall) {
                    auto method = methods.at(name);
                    method(header.serial,move(name),s.readAll<byte>());
                    if(!serial) return;
                } else fail();
            } else if(header.message == MethodReturn && replySerial==serial) return;
        }
    }

    /// Write a message of type \a type using \a Args as arguments
    template<class... Args> uint32 write(Message type, int32 replySerial, const string& target, const string& object,
                                           const string& interface, const string& member, const Args&... args) {
        array<byte> out;
        //Header
        Header header(type,++serial);
        if(replySerial==-2) header.flag=1; //NoReply
        out << raw(header);
        //Fields
        // Path
        assert( !(type==MethodCall||type==Signal) || object );
#define field(type, field, value) if(value) { align(out, 8); ::write(out, (ubyte)field); ::write(out, signature<type>()); ::write(out, value); }
        field(ObjectPath,Path,object);
        // Interface
        assert( !(type==Signal) || interface );
        field(string,Interface,interface);
        // Member
        assert( !(type==MethodCall||type==Signal) || member );
        field(string,Member,member);
        // ReplySerial
        assert( (type==MethodReturn) ^ (replySerial<0) );
        if(replySerial>=0) { align(out, 8); ::write(out, (ubyte)ReplySerial); ::write(out, signature<uint>()); ::write(out, replySerial); }
        // Destination
        field(string,Destination,target);
        // signature
        if(signature<Args...>()) { align(out, 8); ::write(out, (ubyte)Signature); out<<1<<"g"_<<0; ::write(out, signature<Args...>()); }
        //Fields size
        *(uint32*)(&out.at(offsetof(Header,fieldsSize))) = out.size()-sizeof(Header);
        // Body
        align(out, 8);
        int bodyStart = out.size();
        ::write(out, args i(...));
        *(uint32*)(&out.at(offsetof(Header,length))) = out.size()-bodyStart;
        //dump(out);
        ::write(fd,out);
        return serial;
    }

    struct Reply {
        DBus* d; uint32 serial;

        Reply(DBus* d, uint32 serial):d(d),serial(serial){}
        no_copy(Reply) Reply(Reply&& o):d(o.d),serial(o.serial){o.d=0;}

        /// Read the reply and return its value
        template<class T> operator T() {
            assert(d,"Reply read twice"_);
            T t;
            d->read<T>(serial,t);
            d=0; return move(t);
        }
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
        template<class... Args> Reply operator ()(const string& method, const Args&... args) {
            string interface = section(method,'.',0,-2), member=section(method,'.',-2,-1);
            return Reply(d,d->write(MethodCall,-1,target,object,interface,member,args i(...)));
        }
        //Reply operator [](const string& property) { return (*this)("org.freedesktop.DBus.Properties.Get"_,""_,property); }
        template<class T> T get(const string& property) {
            d->write(MethodCall,-1,target,object,"org.freedesktop.DBus.Properties"_,"Get"_,""_,property);
            variant<T> t; d->read(d->serial,t); return move(t);
        }
        template<class... Args> void noreply(const string& method, const Args&... args) {
            string interface = section(method,'.',0,-2), member=section(method,'.',-2,-1);
            d->write(MethodCall,-2,target,object,interface,member,args i(...));
        }
    };

    /// Get a handle to a D-Bus object
    Object operator ()(const string& object) { return Object(this,section(object,'/'),"/"_+section(object,'/',-2,-1)); }

    /// MethodCall method delegate and return empty reply
    void methodWrapper(uint32 serial, string name, array<byte>) {
        delegates.at(name)();
        write(MethodReturn,serial,""_,""_,""_,""_);
    }
    /*/// MethodCall method delegate and return reply
    template<class R> void methodWrapper(uint32 serial, string name, array<byte> data) {
        Stream in(move(data));
        R r = (*(delegate<R>*)&delegates.at(name))();
        write(MethodReturn,serial,""_,""_,""_,""_,r);
    }*/
    /// Unpack one argument, call method delegate and return empty reply
    template<class A> void methodWrapper(uint32 serial, string name, array<byte> data) {
        Stream in(move(data));
        A a; ::read(in,a);
        (*(delegate<void,A>*)&delegates.at(name))(move(a));
        write(MethodReturn,serial,""_,""_,""_,""_);
    }
    /// Unpack one argument and call method delegate and return reply
    template<class R, class A> void methodWrapper(uint32 serial, string name, array<byte> data) {
        Stream in(move(data));
        A a; ::read(in,a);
        R r = (*(delegate<R,A>*)&delegates.at(name))(move(a));
        write(MethodReturn,serial,""_,""_,""_,""_,r);
    }
    /// Unpack two arguments and call the method delegate and return reply
    template<class R, class A, class B> void methodWrapper(uint32 serial, string name, array<byte> data) {
        Stream in(move(data));
        A a; B b; ::read(in,a,b);
        R r = (*(delegate<R,A,B>*)&delegates.at(name))(move(a),move(b));
        write(MethodReturn,serial,""_,""_,""_,""_,r);
    }

    /// Generates an IPC wrapper for \a method and bind it to \a name
    template<class C, class R, class... Args> void bind(const string& name, C* object, R (C::*method)(Args...) ) {
        methods.insert(copy(name), delegate<void, uint32, string, array<byte> >(this,&DBus::methodWrapper<R,Args...>));
        delegates.insert(copy(name), delegate<void>(object, (void (C::*)())method));
    }
    template<class C, class... Args> void bind(const string& name, C* object, void (C::*method)(Args...) ) {
        methods.insert(copy(name), delegate<void, uint32, string, array<byte> >(this,&DBus::methodWrapper<Args...>));
        delegates.insert(copy(name), delegate<void>(object, (void (C::*)())method));
    }

    /// Unpack arguments and call the delegate
    template<class... Args> void signalMethodCall(string name, Stream& in, Args... args) {
        assert(!in);
        (*(delegate<void,Args...>*)&delegates.at(name))(move(args) i(...));
    }
    template<class U, class... Unpack, class... Args> void signalMethodCall(string name, Stream& in, Args... args) {
        U u; ::read(in,u);
        signalMethodCall<Unpack...>(move(name), in, move(u), args i(...));
    }
    template<class... Unpack> void signalWrapper(string name, array<byte> data) {
        Stream in(move(data));
        signalMethodCall<Unpack...>(move(name), in);
    }

    /// Generates an IPC wrapper for \a signal and connect it to \a name
    template<class C, class... Args> void connect(const string& name, C* object, void (C::*signal)(Args...) ) {
        signals_[copy(name)].connect(this,&DBus::signalWrapper<Args...>);
        delegates.insert(copy(name), delegate<void>(object, (void (C::*)())signal) );
    }

    DBus() {
        fd = socket(PF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
        sockaddr_un addr; clear(addr);
        addr.sun_family = AF_UNIX;
        string path = section(section(strz(getenv("DBUS_SESSION_BUS_ADDRESS")),'=',1,2),',');
        addr.sun_path[0]=0; copy(addr.sun_path+1,path.data(),path.size());
        if(::connect(fd,(sockaddr*)&addr,3+path.size())) fail();
        ::write(fd,"\0AUTH EXTERNAL 30\r\n"_); ::read(fd,37);/*OK*/ ::write(fd,"BEGIN \r\n"_);
        name = Object(this,"org.freedesktop.DBus"_,"/org/freedesktop/DBus"_)("org.freedesktop.DBus.Hello"_);
        registerPoll({fd, POLLIN});
    }
    void event(pollfd) { read(0); }
};
