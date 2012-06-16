#include "dbus.h"
#include "file.h"
#include "stream.h"
#include <sys/socket.h>
#include <sys/un.h>

#include "array.cc"
Array(DBusIcon)
Array(DBus::RawMethod)
Array(DBus::RawSignal)

/// Align array size appending default elements
void align(array<byte>& a, int width) { int s=a.size(), n=align(width, s); if(n>s) grow(a, n); }
/// Align the stream position to the next \a width
void align(Buffer& s, int width) { s.index=align(width, s.index); }

template<class T> T read(int fd) {
    T t;
    int unused size = read(fd,(byte*)&t,sizeof(T));
    assert(size==sizeof(T),size,sizeof(T));
    return t;
}

/// Definitions

enum Message { InvalidType, MethodCall, MethodReturn, Error, Signal };
struct Header {
    byte endianness;
    ubyte message;
    ubyte flag;
    ubyte version;
    uint32 length;
    uint32 serial;
    uint32 fieldsSize;
};
enum Field { InvalidField, Path, Interface, Member, ErrorName, ReplySerial, Destination, Sender, Signature };

template<class... Args> struct signature { operator bool() { return true; } };
template<> struct signature<> { operator bool() { return false; } };
struct ObjectPath : string {};

/// Parser

// D-Bus argument parsers
template<class T> void read(DataBuffer& s, variant<T>& output) {
    uint8 size = s.read(); s.advance(size+1); //TODO: type checking
    read(s,(T&)output);
}
template<class T> void read(DataBuffer& s, array<T>& output) {
    align(s, 4); uint length = s.read();
    for(uint start=s.index;s.index<start+length;) { T e; read(s,e); output << move(e); }
}
void read(DataBuffer& s, string& output) { align(s, 4); output = copy(s.readArray()); s.advance(1);/*0*/ }
void read(DataBuffer&) {}
void read(DataBuffer& s, DBusIcon& output) { align(s, 8); output.width=s.read(); output.height=s.read(); output.data=copy(s.readArray()); }
template<class Arg, class... Args> void read(DataBuffer& s, Arg& arg, Args&... args) { read(s,arg); read(s, args ___); }

template<class... Outputs> void DBus::read(uint32 serial, Outputs&... outputs) {
    if(!fd) { warn("Not connected to D-BUS"); return; }
    for(;;) {
        auto header = ::read<Header>(fd);
        if(header.message == MethodCall) assert(header.flag==0);
        uint32 replySerial=0;
        string name;
        DataBuffer s( ::read(fd, align(8,header.fieldsSize)+header.length) );
        for(;s.index<header.fieldsSize;){
            ubyte field = s.read();
            uint8 size = s.read(); s.advance(size+1);
            if(field==Path||field==Interface||field==Member||field==ErrorName||field==Destination||field==Sender) {
                string value; ::read(s, value);
                if(field==Member) name=move(value);
                else if(field==ErrorName) log(value);
            } else if(field==ReplySerial) {
                replySerial=s.read();
            } else if(field==Signature) {
                uint8 size = s.read(); s.advance(size);
            } else warn("Unknown field"_,field);
            align(s, 8);
        }
        if(header.length) {
            if(header.message == MethodReturn) {
                if(replySerial==serial) {
                    ::read(s, outputs ___);
                    return;
                }
            } else if(header.message == Signal) {
                auto signal = signals_.find(name);
                if(signal) signal->emit(move(name),s.readAll());
            } else if(header.message == MethodCall) {
                auto method = methods.at(name);
                method(header.serial,move(name),s.readAll());
            } else error("Unknown message",header.message);
        } else {
            if(header.message == MethodReturn && replySerial==serial) return;
            warn("Unhandled message",header.message);
        }
        if(!serial) return;
        if(replySerial>=serial) { error("replySerial>=serial",replySerial,serial); return; }
    }
}

/// Serializer

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
template<class A> void write(array<byte>&, const A&) { static_assert(sizeof(A) & 0,"No serializer defined for type"); }
template<class Arg, class... Args> void write(array<byte>& s, const Arg& arg, const Args&... args) { write(s,arg); write(s, args ___); }
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

template<class... Args> uint32 DBus::write(int type, int32 replySerial, const string& target, const string& object,
                                           const string& interface, const string& member, const Args&... args) {
    if(!fd) { warn("Not connected to D-BUS"); return 0; }
    array<byte> out;
    out << raw( Header{ 'l', type, replySerial==-2, 1, 0, ++serial, 0 } );
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
    ::write(out, args ___);
    *(uint32*)(&out.at(offsetof(Header,length))) = out.size()-bodyStart;
    //dump(out);
    ::write(fd,out);
    return serial;
}

/// Reply / Object

template<class T> DBus::Reply::operator T() {
    assert(d,"Reply read twice"_);
    T t;
    d->read<T>(serial,t);
    d=0; return move(t);
}
/*template<class... Args>DBus:: Reply DBus::Object::operator ()(const string& method, const Args&... args) { //FIXME: find out how to explicitly instantiate
    string interface = section(method,'.',0,-2), member=section(method,'.',-2,-1);
    return Reply(d,d->write(MethodCall,-1,target,object,interface,member,args ___));
}*/
DBus:: Reply DBus::Object::operator ()(const string& method) {
    string interface = section(method,'.',0,-2), member=section(method,'.',-2,-1);
    return Reply(d,d->write(MethodCall,-1,target,object,interface,member));
}
template<class A> DBus::Reply DBus::Object::operator()(const string& method, const A& a) {
    string interface = section(method,'.',0,-2), member=section(method,'.',-2,-1);
    return Reply(d,d->write(MethodCall,-1,target,object,interface,member, a));
}
template<class A, class B> DBus::Reply DBus::Object::operator()(const string& method, const A& a, const B& b) {
    string interface = section(method,'.',0,-2), member=section(method,'.',-2,-1);
    return Reply(d,d->write(MethodCall,-1,target,object,interface,member, a, b));
}
template<class A> void DBus::Object::noreply(const string& method, const A& a) {
    string interface = section(method,'.',0,-2), member=section(method,'.',-2,-1);
    d->write(MethodCall,-2,target,object,interface,member, a);
}
template<class A, class B> void DBus::Object::noreply(const string& method, const A& a, const B& b) {
    string interface = section(method,'.',0,-2), member=section(method,'.',-2,-1);
    d->write(MethodCall,-2,target,object,interface,member, a, b);
}
template<class T> T DBus::Object::get(const string& property) {
    d->write(MethodCall,-1,target,object,"org.freedesktop.DBus.Properties"_,"Get"_,""_,property);
    variant<T> t; d->read(d->serial,t); return move(t);
}

DBus::Object DBus::operator ()(const string& object) { return Object(this,section(object,'/'),"/"_+section(object,'/',-2,-1)); }

/// Methods

void DBus::methodWrapper(uint32 serial, string name, array<byte>) {
    delegates.at(name)();
    write(MethodReturn,serial,""_,""_,""_,""_);
}
/// Unpack one argument, call method delegate and return empty reply
template<class A> void DBus::methodWrapper(uint32 serial, string name, array<byte> data) {
    DataBuffer in(move(data));
    A a; ::read(in,a);
    (*(delegate<void(A)>*)&delegates.at(name))(move(a));
    write(MethodReturn,serial,""_,""_,""_,""_);
}
/// Unpack one argument and call method delegate and return reply
template<class R, class A> void DBus::methodWrapper(uint32 serial, string name, array<byte> data) {
    DataBuffer in(move(data));
    A a; ::read(in,a);
    R r = (*(delegate<R(A)>*)&delegates.at(name))(move(a));
    write(MethodReturn,serial,""_,""_,""_,""_,r);
}
/// Unpack two arguments and call the method delegate and return reply
template<class R, class A, class B> void DBus::methodWrapper(uint32 serial, string name, array<byte> data) {
    DataBuffer in(move(data));
    A a; B b; ::read(in,a,b);
    R r = (*(delegate<R(A,B)>*)&delegates.at(name))(move(a),move(b));
    write(MethodReturn,serial,""_,""_,""_,""_,r);
}

/// Signals

void DBus::signalWrapper(string name, array<byte>) {
    delegates.at(name)();
}
template<class A> void DBus::signalWrapper(string name, array<byte> data) {
    DataBuffer in(move(data));
    A a; ::read(in,a);
    (*(delegate<void(A)>*)&delegates.at(name))(move(a));
}
template<class A, class B> void DBus::signalWrapper(string name, array<byte> data) {
    DataBuffer in(move(data));
    A a; B b; ::read(in,a,b);
    (*(delegate<void(A,B)>*)&delegates.at(name))(move(a),move(b));
}
template<class A, class B, class C> void DBus::signalWrapper(string name, array<byte> data) {
    DataBuffer in(move(data));
    A a; B b; C c; ::read(in,a,b,c);
    (*(delegate<void(A,B,C)>*)&delegates.at(name))(move(a),move(b),move(c));
}

/// Connection

DBus::DBus() {
     fd = socket(PF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
     sockaddr_un addr; clear(addr);
     addr.sun_family = AF_UNIX;
     string path = section(section(strz(getenv("DBUS_SESSION_BUS_ADDRESS")),'=',1,2),',');
     addr.sun_path[0]=0; copy((byte*)addr.sun_path+1,path.data(),path.size());
     if(::connect(fd,(sockaddr*)&addr,3+path.size())) { fd=0; warn("Couldn't connect to D-Bus"); return; }
     ::write(fd,"\0AUTH EXTERNAL 30\r\n"_); ::read(fd,37);/*OK*/ ::write(fd,"BEGIN \r\n"_);
     name = Object(this,"org.freedesktop.DBus"_,"/org/freedesktop/DBus"_)("org.freedesktop.DBus.Hello"_);
     registerPoll({fd, POLLIN});
 }
 void DBus::event(pollfd) { read(0); }

 /// Explicit template instanciations

 template DBus::Reply::operator uint();
template DBus::Reply::operator array<string>();
template void DBus::Object::noreply(const string&, const string&);
template void DBus::Object::noreply(const string&, const string&, const uint&);
template void DBus::Object::noreply(const string&, const int&, const int&);
template void DBus::Object::noreply(const string&, const int&, const string&);
template string DBus::Object::get(const string&);
template uint32 DBus::Object::get(const string&);
template array<DBusIcon> DBus::Object::get(const string&);

template void DBus::methodWrapper<variant<int>, string, string>(unsigned int, string, array<byte>);
template void DBus::methodWrapper<string>(unsigned int, string, array<byte>);
template void DBus::signalWrapper<string, string, string>(string, array<byte>);
