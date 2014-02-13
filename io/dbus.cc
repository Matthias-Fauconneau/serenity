#include "dbus.h"
#include "file.h"
#include "data.h"
#include <sys/socket.h>
#include <sys/un.h>

/// Aligns array size appending default elements
void align(array<byte>& a, int width) { int s=a.size, n=align(width, s); if(n>s) a.grow(n); }

/// Definitions

enum Message { InvalidType, MethodCall, MethodReturn, Error, Signal };
struct Header {
    byte endianness;
    uint8 message;
    uint8 flag;
    uint8 version;
    uint32 length;
    uint32 serial;
    uint32 fieldsSize;
};
enum Field { InvalidField, Path, Interface, Member, ErrorName, ReplySerial, Destination, Sender, Signature };

template<class... Args> struct signature { operator bool() { return true; } };
template<> struct signature<> { operator bool() { return false; } };
struct ObjectPath : String {};

/// Parser

// D-Bus argument parsers
template<class T> void read(BinaryData& s, variant<T>& output) {
    uint8 size = s.read(); s.advance(size+1); //TODO: type checking
    read(s,(T&)output);
}
template<class T> void read(BinaryData& s, array<T>& output) {
    s.align(4); uint size = s.read();
    for(uint start=s.index;s.index<start+size;) { T e; read(s,e); output << move(e); }
}
void read(BinaryData& s, String& output) { s.align(4); uint size=s.read(); output = String(s.Data::read(size)); s.advance(1);/*0*/ }
void read(BinaryData&) {}
template<class Arg, class... Args> void read(BinaryData& s, Arg& arg, Args&... args) { read(s,arg); read(s, args ...); }

template<class... Outputs> void DBus::readMessage(uint32 serial, Outputs&... outputs) {
    for(;;) {
        Header header = read<Header>();
        if(header.message == MethodCall) assert(header.flag==0);
        uint32 replySerial=0;
        String name;
        BinaryData s = read(align(8,header.fieldsSize)+header.length);
        for(;s.index<header.fieldsSize;){
            uint8 field = s.read();
            uint8 size = s.read(); s.advance(size+1);
            if(field==Path||field==Interface||field==Member||field==ErrorName||field==Destination||field==Sender) {
                String value; ::read(s, value);
                if(field==Member) name=move(value);
                else if(field==ErrorName) log(value);
            } else if(field==ReplySerial) {
                replySerial=s.read();
            } else if(field==Signature) {
                uint8 size = s.read(); s.advance(size);
            } else error("Unknown field"_,field);
            s.align(8);
        }
        if(header.length) {
            if(header.message == MethodReturn) {
                if(replySerial==serial) {
                    ::read(s, outputs ...);
                    return;
                }
            } else if(header.message == Signal) {
                RawSignal* signal = signals_.find(name);
                if(signal) (*signal)(name, s.untilEnd());
            } else if(header.message == MethodCall) {
                RawMethod method = methods.at(name);
                method(header.serial, name, s.untilEnd());
            } else error("Unknown message",header.message);
        } else {
            if(header.message == MethodReturn && replySerial==serial) return;
            error("Unhandled message",header.message);
        }
        if(!serial) return;
        if(replySerial>=serial) { error("replySerial>=serial",replySerial,serial); return; }
    }
}

/// Serializer

// D-Bus signature serializer
template<class Arg, class... Args> void sign(String& s) { sign<Arg>(s); sign<Args...>(s); }
//template<class A> void sign(String&) { static_assert(sizeof(A) && 0,"No signature defined for type"); } //function template partial specialization
template<> void sign<string>(String& s) { s<<"s"_; }
template<> void sign<String>(String& s) { s<<"s"_; }
template<> void sign<int>(String& s) { s<<"i"_; }
template<> void sign<uint>(String& s) { s<<"u"_; }
template<> void sign<ObjectPath>(String& s) { s<<"o"_; }
//template<class T> void sign< signature<T> >(String& s) { s<<"g"_; } //function template partial specialization is not allowed
//template<class T> void sign< variant<T> >(String& s) { s<<"v"_; sign<T>(s); } //function template partial specialization is not allowed
template<>void sign< variant<String> >(String& s) { s<<"v"_; }
template<>void sign< variant<int> >(String& s) { s<<"v"_; }

//  D-Bus arguments serializer
template<class A> void write(array<byte>&, const A&) { static_assert(sizeof(A) & 0,"No serializer defined for type"); }
template<class Arg, class... Args> void write(array<byte>& s, const Arg& arg, const Args&... args) { write(s,arg); write(s, args ...); }
void write(array<byte>&) {}

void write(array<byte>& s, string input) { align(s, 4); s << raw(input.size) << input << 0; }
void write(array<byte>& s, uint8 byte) { s << byte; }
void write(array<byte>& s, int integer) { align(s, 4); s << raw(integer); }
void write(array<byte>& s, uint integer) { align(s, 4); s << raw(integer); }
template<class T> void write(array<byte>& s, const variant<T>& variant) { write(s, signature<T>()); write(s,(T&)variant); }
template<class... Args> void write(array<byte>& s, signature<Args...>) {
    String signature; sign<Args...>(signature); assert(signature); s << (uint8)signature.size << move(signature) << 0;
}
template<> void write(array<byte>& s, signature<>) { s << 0 << 0; }

template<class... Args> uint32 DBus::writeMessage(uint8 type, int32 replySerial, const string& target, const string& object,
                                           const string& interface, const string& member, const Args&... args) {
    array<byte> out;
    out << raw( Header{ 'l', type, replySerial==-2, 1, 0, ++serial, 0 } );
    //Fields
    // Path
    assert( !(type==MethodCall||type==Signal) || object );
#define field(type, field, value) if(value) { align(out, 8); ::write(out, (uint8)field); ::write(out, signature<type>()); ::write(out, value); }
    field(ObjectPath,Path,object);
    // Interface
    assert( !(type==Signal) || interface );
    field(String,Interface,interface);
    // Member
    assert( !(type==MethodCall||type==Signal) || member );
    field(String,Member,member);
    // ReplySerial
    assert( (type==MethodReturn) ^ (replySerial<0) );
    if(replySerial>=0) { align(out, 8); ::write(out, (uint8)ReplySerial); ::write(out, signature<uint>()); ::write(out, replySerial); }
    // Destination
    field(String,Destination,target);
    // signature
    if(signature<Args...>()) { align(out, 8); ::write(out, (uint8)Signature); out<<1<<"g"_<<0; ::write(out, signature<Args...>()); }
    //Fields size
    *(uint32*)(&out.at(offsetof(Header,fieldsSize))) = out.size-sizeof(Header);
    // Body
    align(out, 8);
    int bodyStart = out.size;
    ::write(out, args ...);
    *(uint32*)(&out.at(offsetof(Header,length))) = out.size-bodyStart;
    //dump(out);
    write(out);
    return serial;
}

/// Reply / Object

template<class T> DBus::Reply::operator T() {
    assert(dbus,"Reply read twice"_);
    T t;
    dbus->readMessage<T>(serial,t);
    dbus=0; return move(t);
}
template<class... Args> DBus::Reply DBus::Object::operator ()(const string& method, const Args&... args) { //FIXME: find out how to explicitly instantiate
    string interface = section(method,'.',0,-2), member=section(method,'.',-2,-1);
    return {dbus.pointer, dbus->writeMessage(MethodCall, -1, target, object, interface, member, args ...)};
}
/*DBus:: Reply DBus::Object::operator ()(const string& method) {
    string interface = section(method,'.',0,-2), member=section(method,'.',-2,-1);
    return Reply(dbus,dbus->writeMessage(MethodCall,-1,target,object,interface,member));
}
template<class A> DBus::Reply DBus::Object::operator()(const string& method, const A& a) {
    string interface = section(method,'.',0,-2), member=section(method,'.',-2,-1);
    return Reply(dbus,dbus->writeMessage(MethodCall,-1,target,object,interface,member, a));
}
template<class A, class B> DBus::Reply DBus::Object::operator()(const string& method, const A& a, const B& b) {
    string interface = section(method,'.',0,-2), member=section(method,'.',-2,-1);
    return Reply(dbus,dbus->writeMessage(MethodCall,-1,target,object,interface,member, a, b));
}*/
template<class A> void DBus::Object::noreply(const string& method, const A& a) {
    string interface = section(method,'.',0,-2), member=section(method,'.',-2,-1);
    dbus->writeMessage(MethodCall,-2,target,object,interface,member, a);
}
template<class A, class B> void DBus::Object::noreply(const string& method, const A& a, const B& b) {
    string interface = section(method,'.',0,-2), member=section(method,'.',-2,-1);
    dbus->writeMessage(MethodCall,-2,target,object,interface,member, a, b);
}
template<class T> T DBus::Object::get(const string& property) {
    dbus->writeMessage(MethodCall,-1,target,object,"org.freedesktop.DBus.Properties"_,"Get"_,""_,property);
    variant<T> t; dbus->readMessage(dbus->serial,t); return move(t);
}

DBus::Object DBus::operator ()(const string& object) { return {this, String(section(object,'/')), "/"_+section(object,'/',-2,-1)}; }

/// Methods

void DBus::methodWrapper(uint32 serial, string name, ref<byte>) {
    delegates.at(name)();
    writeMessage(MethodReturn,serial,""_,""_,""_,""_);
}
/// Unpacks one argument, calls method delegate and return empty reply
template<class A> void DBus::methodWrapper(uint32 serial, string name, ref<byte> data) {
    BinaryData in(move(data));
    A a; ::read(in,a);
    (*(function<void(A)>*)&delegates.at(name))(move(a));
    writeMessage(MethodReturn,serial,""_,""_,""_,""_);
}
/// Unpacks one argument and calls method delegate and return reply
template<class R, class A> void DBus::methodWrapper(uint32 serial, string name, ref<byte> data) {
    BinaryData in(move(data));
    A a; ::read(in,a);
    R r = (*(function<R(A)>*)&delegates.at(name))(move(a));
    writeMessage(MethodReturn,serial,""_,""_,""_,""_,r);
}
/// Unpacks two arguments and calls the method delegate and return reply
template<class R, class A, class B> void DBus::methodWrapper(uint32 serial, string name, ref<byte> data) {
    BinaryData in(move(data));
    A a; B b; ::read(in,a,b);
    R r = (*(function<R(A,B)>*)&delegates.at(name))(move(a),move(b));
    writeMessage(MethodReturn,serial,""_,""_,""_,""_,r);
}

/// Signals

void DBus::signalWrapper(string name, ref<byte>) {
    delegates.at(name)();
}
template<class A> void DBus::signalWrapper(string name, ref<byte> data) {
    BinaryData in(move(data));
    A a; ::read(in,a);
    (*(function<void(A)>*)&delegates.at(name))(move(a));
}
template<class A, class B> void DBus::signalWrapper(string name, ref<byte> data) {
    BinaryData in(move(data));
    A a; B b; ::read(in,a,b);
    (*(function<void(A,B)>*)&delegates.at(name))(move(a),move(b));
}
template<class A, class B, class C> void DBus::signalWrapper(string name, ref<byte> data) {
    BinaryData in(move(data));
    A a; B b; C c; ::read(in,a,b,c);
    (*(function<void(A,B,C)>*)&delegates.at(name))(move(a),move(b),move(c));
}

/// Connection

DBus::DBus() : Socket(PF_UNIX,SOCK_STREAM|SOCK_CLOEXEC), Poll(Socket::fd) {
     sockaddr_un addr = {};
     addr.sun_family = AF_UNIX;
     string path = section(section(strz(getenv("DBUS_SESSION_BUS_ADDRESS"_)),'=',1,2),',');
     addr.sun_path[0]=0; copy(mref<byte>((byte*)addr.sun_path+1,path.size), path);
     if(::connect(Socket::fd,(sockaddr*)&addr,3+path.size)) error("Couldn't connect to D-Bus");
     write("\0AUTH EXTERNAL 30\r\n"_); read(37);/*OK*/ write("BEGIN \r\n"_);
     name = Object{this,String("org.freedesktop.DBus"_),String("/org/freedesktop/DBus"_)}("org.freedesktop.DBus.Hello"_);
 }
 void DBus::event() { readMessage(0); }

 /// Explicit template instanciations

 template DBus::Reply::operator uint();
template DBus::Reply::operator array<string>();
template void DBus::Object::noreply(const string&, const string&);
template void DBus::Object::noreply(const string&, const string&, const uint&);
template void DBus::Object::noreply(const string&, const int&, const int&);
template void DBus::Object::noreply(const string&, const int&, const string&);
template string DBus::Object::get(const string&);
template uint32 DBus::Object::get(const string&);

template void DBus::methodWrapper<variant<int>, string, string>(unsigned int, string, ref<byte>);
template void DBus::methodWrapper<string>(unsigned int, string, ref<byte>);
template void DBus::signalWrapper<string, string, string>(string, ref<byte>);
