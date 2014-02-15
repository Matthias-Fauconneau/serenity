#include "dbus.h"
#include "file.h"
#include "xml.h"
#include <sys/socket.h>
#include <sys/un.h>

/// Definitions

struct Header {
    byte endianness;
    uint8 message;
    uint8 flag;
    uint8 version;
    uint32 length;
    uint32 serial;
    uint32 fieldsSize;
};
enum Field { InvalidField, Path, Interface, Member, ErrorName, ReplySerial, Destination, Sender, SignatureField };

/// Parser

void DBus::readMessage(uint32 serial, function<void(BinaryData&)> readOutputs) {
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
            } else if(field==SignatureField) {
                uint8 size = s.read(); s.advance(size);
            } else error("Unknown field"_,field);
            s.align(8);
        }
        if(header.length) {
            if(header.message == MethodReturn) {
                if(replySerial==serial) {
                    readOutputs(s);
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

uint32 DBus::writeSerializedMessage(uint8 type, int32 replySerial, const string& target, const string& object,
                                           const string& interface, const string& member, const ref<byte>& signature, const ref<byte>& arguments) {
    array<byte> out;
    out << raw(Header{'l', type, replySerial==-2, 1, 0, ++serial, 0 });
    // Fields
    // Path
    assert( !(type==MethodCall||type==Signal) || (object && !endsWith(object,"/"_)), object);
#define field(type, field, value) if(value) { align(out, 8); serialize(out, uint8(field)); serialize(out, Signature<type>()); serialize(out, value); }
    field(ObjectPath, Path, object);
    // Interface
    assert( !(type==Signal) || interface );
    field(String, Interface, interface);
    // Member
    assert( !(type==MethodCall||type==Signal) || member );
    field(String, Member, member);
    // ReplySerial
    assert( (type==MethodReturn) ^ (replySerial<0) );
    if(replySerial>=0) { align(out, 8); serialize(out, uint8(ReplySerial)); serialize(out, Signature<uint>()); serialize(out, replySerial); }
    // Destination
    if(target=="org.freedesktop.DBus"_ || interface!=target) field(String, Destination, target);
    // Signature
    if(signature) { align(out, 8); serialize(out, (uint8)SignatureField); out<<1<<'g'<<0<<signature; }
    //Fields size
    *(uint32*)(&out.at(offsetof(Header,fieldsSize))) = out.size-sizeof(Header);
    // Body
    align(out, 8);
    int bodyStart = out.size;
    out << arguments;
    *(uint32*)(&out.at(offsetof(Header,length))) = out.size-bodyStart;
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
template<class A> void DBus::Object::noreply(const string& method, const A& a) {
    string interface = section(method,'.',0,-2), member=section(method,'.',-2,-1);
    dbus->writeMessage(MethodCall,-2, target, object,interface,member, a);
}
template<class A, class B> void DBus::Object::noreply(const string& method, const A& a, const B& b) {
    string interface = section(method,'.',0,-2), member=section(method,'.',-2,-1);
    dbus->writeMessage(MethodCall,-2, target, object,interface,member, a, b);
}
array<String> DBus::Object::children() {
    uint32 serial = dbus->writeMessage(MethodCall,-1, target, object,"org.freedesktop.DBus.Introspectable"_,"Introspect"_);
    String xml; dbus->readMessage(serial, xml);
    Element root = parseXML(xml);
    array<String> names;
    for(const auto& e: root("node"_).children) if(e->attributes.contains("name"_)) names << String(e->attribute("name"_));
    return names;
}
DBus::Object DBus::Object::node(string name) {
    return DBus::Object{dbus,copy(target),object+"/"_+name};
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

DBus::DBus(Scope scope) : Socket(PF_LOCAL,SOCK_STREAM), Poll(Socket::fd) {
     sockaddr_un addr = {};
     addr.sun_family = AF_LOCAL;
     string path;
     if(scope == Session) path = section(section(getenv("DBUS_SESSION_BUS_ADDRESS"_),'=',1,2),',');
     else if(scope == System) path = "/var/run/dbus/system_bus_socket"_;
     else error("Unknown scope"_);
     copy(mref<byte>((byte*)addr.sun_path,path.size+1), strz(path));
     check_( ::connect(Socket::fd,(sockaddr*)&addr,2+path.size) );
     write("\0AUTH EXTERNAL 31303030\r\n"_); String status=read(37); assert(startsWith(status, "OK"_)); write("BEGIN \r\n"_);
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

template void DBus::methodWrapper<variant<int>, string, string>(unsigned int, string, ref<byte>);
template void DBus::methodWrapper<string>(unsigned int, string, ref<byte>);
template void DBus::signalWrapper<string, string, string>(string, ref<byte>);
