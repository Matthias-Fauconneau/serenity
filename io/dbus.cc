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
enum class Field { InvalidField, Path, Interface, Member, ErrorName, ReplySerial, Destination, Sender, SignatureField };

/// Parser

void DBus::readMessage(uint32 serial, function<void(BinaryData&)> readOutputs) {
    for(;;) {
        Header header = read<Header>();
        log(header.message, header.flag, header.version, header.length, header.serial, header.fieldsSize);
        if(header.message == MethodCall) assert(header.flag==0, header.flag);
        uint32 replySerial=0;
        String name;
        BinaryData s = read(align(8,header.fieldsSize)+header.length);
        while(s.index<header.fieldsSize) {
            Field field = (Field)s.read8();
            uint8 size = s.read(); s.advance(size+1);
            if(field==Field::Path||field==Field::Interface||field==Field::Member||field==Field::ErrorName||field==Field::Destination||field==Field::Sender) {
                String value; ::read(s, value);
                if(field==Field::Member) name=move(value);
                else if(field==Field::ErrorName) log(value);
            } else if(field==Field::ReplySerial) {
                replySerial=s.read();
            } else if(field==Field::SignatureField) {
                uint8 size = s.read(); s.advance(size);
            } else error("Unknown field"_, int(field));
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
                else log("Unconnected signal", name);
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

uint32 DBus::writeSerializedMessage(uint8 type, int32 replySerial, const string target, const string object,
                                           const string interface, const string member, const ref<byte>& signature, const ref<byte>& arguments) {
    array<byte> out;
    out.append( raw(Header{'l', type, replySerial==-2, 1, 0, ++serial, 0 }));
    // Fields
    // Path
    assert( !(type==MethodCall||type==Signal) || (object && !endsWith(object,"/"_)), object);
#define field(type, field, value) if(value) { align(out, 8); serialize(out, uint8(field)); serialize(out, Signature<type>()); serialize(out, value); }
    field(ObjectPath, Field::Path, object);
    // Interface
    assert( !(type==Signal) || interface );
    field(String, Field::Interface, interface);
    // Member
    assert( !(type==MethodCall||type==Signal) || member );
    field(String, Field::Member, member);
    // ReplySerial
    assert( (type==MethodReturn) ^ (replySerial<0) );
    if(replySerial>=0) { align(out, 8); serialize(out, uint8(Field::ReplySerial)); serialize(out, Signature<uint>()); serialize(out, replySerial); }
    // Destination
    if(target=="org.freedesktop.DBus"_ || interface!=target) field(String, Field::Destination, target);
    // Signature
    if(signature) { align(out, 8); serialize(out, (uint8)Field::SignatureField); out.append(1); out.append('g'); out.append(0); out.append(signature); }
    //Fields size
    *(uint32*)(&out.at(__builtin_offsetof(Header,fieldsSize))) = out.size-sizeof(Header);
    // Body
    align(out, 8);
    int bodyStart = out.size;
    out.append(arguments);
    *(uint32*)(&out.at(__builtin_offsetof(Header,length))) = out.size-bodyStart;
    write(out);
    return serial;
}

/// Reply / Object

template<class A> void DBus::Object::noreply(const string method, const A& a) {
    string interface = section(method,'.',0,-2), member=section(method,'.',-2,-1);
    dbus->writeMessage(MethodCall,-2, target, object,interface,member, a);
}
template<class A, class B> void DBus::Object::noreply(const string method, const A& a, const B& b) {
    string interface = section(method,'.',0,-2), member=section(method,'.',-2,-1);
    dbus->writeMessage(MethodCall,-2, target, object,interface,member, a, b);
}
array<String> DBus::Object::children() {
    uint32 serial = dbus->writeMessage(MethodCall,-1, target, object,"org.freedesktop.DBus.Introspectable"_,"Introspect"_);
    String xml; dbus->readMessage(serial, xml);
    Element root = parseXML(xml);
    array<String> names;
    for(const auto& e: root("node"_).children) if(e->attributes.contains("name"_)) names.append(copyRef(e->attribute("name"_)));
    return names;
}
DBus::Object DBus::Object::node(string name) {
    return DBus::Object{dbus,copy(target),object+"/"_+name};
}

DBus::Object DBus::operator ()(const string object) { return {this, copyRef(section(object,'/')), "/"_+section(object,'/',-2,-1)}; }

/// Methods

void DBus::methodWrapper(uint32 serial, string name, ref<byte>) {
    delegates.at(name)();
    writeMessage(MethodReturn,serial,""_,""_,""_,""_);
}
/// Unpacks one argument, calls method delegate and return empty reply
template<Type A> void DBus::methodWrapper(uint32 serial, string name, ref<byte> data) {
    BinaryData in(move(data));
    A a; ::read(in, a);
    (*(function<void(A)>*)&delegates.at(name))(move(a));
    writeMessage(MethodReturn,serial,""_,""_,""_,""_);
}
/// Unpacks one argument and calls method delegate and return reply
template<Type R, Type A> void DBus::methodWrapper(uint32 serial, string name, ref<byte> data) {
    BinaryData in(move(data));
    A a; ::read(in,a);
    R r = (*(function<R(A)>*)&delegates.at(name))(move(a));
    writeMessage(MethodReturn,serial,""_,""_,""_,""_,r);
}
/// Unpacks two arguments and calls the method delegate and return reply
template<Type R, Type A, Type B> void DBus::methodWrapper(uint32 serial, string name, ref<byte> data) {
    BinaryData in(move(data));
    A a; B b; ::read(in,a,b);
    R r = (*(function<R(A,B)>*)&delegates.at(name))(move(a),move(b));
    writeMessage(MethodReturn,serial,""_,""_,""_,""_,r);
}

/// Signals

#if 0
#if 1
generic T readValue(BinaryData& in) { T t; read(in, t); return t; }
template<Type... Args> void DBus::signalWrapper(string name, ref<byte> data) {
    BinaryData in(move(data));
    (*(function<void(Args...)>*)&delegates.at(name))(readValue<Args>(in)...);
}
#else
void DBus::signalWrapper(string name, ref<byte>) {
    delegates.at(name)();
}
template<Type A> void DBus::signalWrapper(string name, ref<byte> data) {
    BinaryData in(move(data));
    A a; ::read(in,a);
    (*(function<void(A)>*)&delegates.at(name))(move(a));
}
template<Type A, Type B> void DBus::signalWrapper(string name, ref<byte> data) {
    BinaryData in(move(data));
    A a; B b; ::read(in, a, b);
    (*(function<void(A,B)>*)&delegates.at(name))(move(a),move(b));
}
template<Type A, Type B, Type C> void DBus::signalWrapper(string name, ref<byte> data) {
    BinaryData in(move(data));
    A a; B b; C c; ::read(in, a, b, c);
    (*(function<void(A,B,C)>*)&delegates.at(name))(move(a),move(b),move(c));
}
#endif
#endif

/// Connection

DBus::DBus(Scope scope) : Socket(PF_LOCAL,SOCK_STREAM), Poll(Socket::fd) {
 string path;
 if(scope == Session) path = section(section(environmentVariable("DBUS_SESSION_BUS_ADDRESS"_),'=',1,2),',');
 else if(scope == System) path = "/var/run/dbus/system_bus_socket"_;
 else error("Unknown scope"_);
 if(find(environmentVariable("DBUS_SESSION_BUS_ADDRESS"_),"abstract"_)) {
  sockaddr_un addr = {};
  addr.sun_family = AF_UNIX;
  addr.sun_path[0]=0;
  mref<byte>((byte*)addr.sun_path+1,path.size+1).copy(strz(path));
  check( ::connect(Socket::fd,(sockaddr*)&addr,3+path.size), path);
 } else {
  sockaddr_un addr = {};
  addr.sun_family = AF_LOCAL;
  mref<byte>((byte*)addr.sun_path,path.size+1).copy(strz(path));
  check( ::connect(Socket::fd,(sockaddr*)&addr,2+path.size), path);
 }
 write("\0AUTH EXTERNAL 31303030\r\n"_); String status=read(37); assert(startsWith(status, "OK"_)); write("BEGIN \r\n"_);
 name = Object{this, "org.freedesktop.DBus"__, "/org/freedesktop/DBus"__}("org.freedesktop.DBus.Hello"_);
}
void DBus::event() { readMessage(0); }

/// Explicit template instanciations

template DBus::Reply::operator uint();
//template void DBus::Object::noreply(const string, const string);
//template void DBus::Object::noreply(const string, const string, const uint&);
//template void DBus::Object::noreply(const string, const int&, const int&);
//template void DBus::Object::noreply(const string, const int&, const string);

template void DBus::methodWrapper<variant<int>, String, String>(unsigned int, string, ref<byte>);
template void DBus::methodWrapper<String>(unsigned int, string, ref<byte>);
