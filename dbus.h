#pragma once
#include "file.h"

#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>

#ifdef DEBUG
inline void dump(const array<byte>& s) {
    string str; for(int i=0;i<s.size;i++) { char c = s[i]; if(c>=' '&&c<='~') str<<c; else str<<'\\'<<toString((ubyte)c,8); }
    log(str);
}
#endif

/// D-Bus

//static Message Header
enum Message : byte { Call=1, ReplyMessage, Error, Signal };
template <Message type> struct MessageHeader {
    byte endianness='l';
    Message message = type;
    byte flag = 0;
    byte version = 1;
    uint32 length;
    uint32 serial;
    MessageHeader(int length, int serial):length(length),serial(serial){}
};

//static Signature
template <char... str> struct Signature {
    uint8 size = sizeof(str);
    char signature[sizeof...(str)+1] = { str..., 0 };
};

//static Field Header
enum Field : byte { Path=1, Interface, Member, ErrorName, ReplySerial, Target, Sender, SignatureField };
template <Field type> struct FieldHeader {
    Field field = type;
    Signature<type==SignatureField?'g':type==Path?'o':'s'> s;
};

// Template metaprogramming for static dispatch of D-Bus outputs parsers

template <class T> struct Variant : T {
    operator T&() const { return *this; }
};
template<class T> void read(Stream<>& s, Variant<T>& output) {
    uint8 size = s.read(); s += size+1; //TODO: type checking
    read(s,(T&)output);
}
template<class T> void read(Stream<>& s, array<T>& output) {
    s.align<4>(); int32 length = s.read();
    uint begin=s.pos; //FIXME: wrong if T has align(8) (structs)
    while(s.pos<begin+length) { T e; read(s,e); output << move(e); }
}
void read(Stream<>& s, string& output) { s.align<4>(); output = s.readArray().copy(); s++;/*0*/ }
void read(Stream<>&) {}

struct DBusIcon {
    int width;
    int height;
    array<byte> data;
};
void read(Stream<>& s, DBusIcon& output) { s.align<8>(); output.width=s.read(); output.height=s.read(); output.data=s.readArray().copy(); }

// Template metaprogramming for static dispatch of D-Bus signature serializer

template<class Input, class... Inputs> void sign(array<byte>& s) { sign<Input>(s); sign<Inputs...>(s); }
template<> void sign<string>(array<byte>& s) { s<<"s"_; }
template<> void sign<int>(array<byte>& s) { s<<"i"_; }

template<class... Inputs> void writeSignature(array<byte>& s) {
    string signature; sign<Inputs...>(signature);
    s.align<8>(); s << raw(FieldHeader<SignatureField>()); s << signature.size; s << move(signature); s << 0;
}
template<> void writeSignature(array<byte>&) {}

// Template metaprogramming for static dispatch of D-Bus inputs serializer

void write(array<byte>& s, const string& input) {
    s.align<4>();
    s << raw(input.size);
    s << input;
    s << 0;
}
void write(array<byte>& s, int integer) { s.align<4>(); s << raw(integer); }
void write(array<byte>&) {}
template<class Input, class... Inputs> void write(array<byte>& s, const Input& input, const Inputs&... inputs) {
    write(s,input); write(s,inputs...);
}

template <Field type> void writeField(array<byte>& s, const string& field) {
    s.align<8>();
    s << raw(FieldHeader<type>());
    write(s,field);
}

struct DBus {
    int fd = 0; // Session bus socket
    string name; // Instance name given by DBus
    uint32 serial = 0; // Serial number to match messages and their replies

    /// Read messages and parse \a outputs from first reply (without type checking)
    template<class... Outputs> void read(uint32, Outputs&... outputs) {
        for(;;) {
            array<byte> buffer(sizeof(MessageHeader<ReplyMessage>)+4);
            ::read(fd,buffer); assert(buffer.size==buffer.capacity,buffer.size,"Connection closed"_);
            auto header = *(MessageHeader<ReplyMessage>*)&buffer;
            uint32 fieldsSize = align<8>(*(uint32*)&buffer.at(sizeof(MessageHeader<ReplyMessage>)));
            array<byte> fields(fieldsSize); ::read(fd,fields);
            string signal;
            for(Stream<> s(fields);s;) {
                Field field = s.read();
                uint8 size = s.read();
                s += size+1;
                if(field==Target||field==Path||field==Interface||field==Member||field==Sender) {
                    s.align<4>();
                    string name = s.readArray(); s+=1;
                    if(field==Member) signal=move(name);
                } else if(field==ReplySerial) { s.align<4>(); s.read<uint32>(); /*uint32 replySerial=s.read(); assert(replySerial==serial);*/ }
                else if(field==SignatureField) { uint8 size = s.read(); s+=size; }
                s.align<4>();
            }
            if(header.length) {
                array<byte> data(header.length); ::read(fd,data); Stream<> s(data);
                //dump(data);
                if(header.message == ReplyMessage) ::read(s,outputs...);
                //else if(header.message == Signal) log("signal"_,signal);
            }
            if(header.message == ReplyMessage) return;
        }
    }

    /// Write a message of type \a type using \a inputs as arguments
    template<Message type, class... Inputs>
    uint32 write(const string& target, const string& object, const string& interface, const string& member, const Inputs&... inputs) {
        array<byte> body; ::write(body,inputs...);
        array<byte> s;
        s << raw(MessageHeader<type>(body.size,++serial));
        int length = s.size; s.skip(4); s.align<4>(); int begin=s.size;
        ::writeField<Path>(s,object);
        if(interface) ::writeField<Interface>(s,interface);
        ::writeField<Member>(s,member);
        ::writeField<Target>(s,target);
        ::writeSignature<Inputs...>(s);
        *(uint32*)(&s.at(length)) = s.size-begin;
        s.align<8>();
        s << move(body);
        //dump(s); log(R"()"_);
        ::write(fd,s);
        return serial;
    }

    struct Reply {
        DBus* d; uint32 serial;

        Reply(DBus* d, int serial):d(d),serial(serial){}
        no_copy(Reply) Reply(Reply&& o):d(o.d),serial(o.serial){o.d=0;}

        /// Read the reply and return its value
        template<class T> operator T() { assert(d,"Reply read twice"_); T t; d->read<T>(serial,t); d=0; return t; }
        /// Read empty replies
        /// \note \a Reply must be read before interleaving any call
        ~Reply() { if(d) d->read(serial); d=0; }
    };

    struct Object {
        DBus* d;
        string target;
        string object;
        /// Call \a method with \a inputs. Return a handle to read the reply.
        /// \note \a Reply must be read before interleaving any call
        template<class... Inputs> Reply operator ()(const string& method, const Inputs&... inputs) {
            string interface = /*method.contains('.')?*/section(method,'.',0,-2)/*:""_*/, member=section(method,'.',-2,-1);
            return Reply(d,d->write<Call>(target,object,interface,member,inputs...));
        }
        Reply operator [](const string& property) {
            return (*this)("org.freedesktop.DBus.Properties.Get"_,""_,property);
        }
        Object(DBus* d,string&& target, string&& object):d(d),target(move(target)),object(move(object)){}
    };

    /// Get a handle to a D-Bus object
    template<class... Inputs> Object operator ()(const string& object, const Inputs&... inputs) {
        return Object(this,section(object,'/').copy(),"/"_+section(object,'/',-2,-1).copy());
    }

    DBus() {
        fd = socket(PF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
        sockaddr_un addr; clear(addr);
        addr.sun_family = AF_UNIX;
        string path = section(section(strz(getenv("DBUS_SESSION_BUS_ADDRESS")),'=',1,2),',');
        addr.sun_path[0]=0; path.copy(addr.sun_path+1);
        if(connect(fd,(sockaddr*)&addr,3+path.size)) fail();
        ::write(fd,"\0AUTH EXTERNAL 30\r\n"_);
        char buf[256]; ::read(fd,buf,sizeof(buf)); //OK
        ::write(fd,"BEGIN \r\n"_);
        name = (*this)("org.freedesktop.DBus/org/freedesktop/DBus"_)("org.freedesktop.DBus.Hello"_);
    }
};
