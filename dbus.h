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
enum Field { Path=1, Interface, Member, ErrorName, ReplySerial, Target, Sender, SignatureField };
template <Field type> struct FieldHeader {
    byte field = type;
    Signature<type==SignatureField?'g':type==Path?'o':'s'> s;
};

// Template metaprogramming for static dispatch of D-Bus outputs parsers

template <class T> struct Variant : T {
    operator T&() const { return *this; }
};
template<class T> void read(Stream& s, Variant<T>& output) {
    uint8 size = s.read(); s += size+1; //TODO: type checking
    read(s,(T&)output);
}
template<class T> void read(Stream& s, array<T>& output) {
    align(s, 4); int32 length = s.read();
    //FIXME: wrong if T has align(8) (structs)
    for(int i=0;i<length;i++) { T e; read(s,e); output << move(e); }
}
void read(Stream& s, string& output) { align(s, 4); output = copy(s.readArray<byte>()); s++;/*0*/ }
void read(Stream&) {}

struct DBusIcon {
    int width;
    int height;
    array<byte> data;
};
void read(Stream& s, DBusIcon& output) { align(s, 8); output.width=s.read(); output.height=s.read(); output.data=copy(s.readArray<byte>()); }

// Template metaprogramming for static dispatch of D-Bus signature serializer

template<class Input, class... Inputs> string sign() { return sign<Input>()+sign<Inputs...>(); }
template<> string sign<string>() { return "s"_; }
template<> string sign<int>() { return "i"_; }

template<class... Inputs> void writeSignature(array<byte>& s) {
    string signature=sign<Inputs...>();
    align(s, 8); s << raw(FieldHeader<SignatureField>()); s << signature.size(); s << move(signature); s << 0;
}
template<> void writeSignature(array<byte>&) {}

// Template metaprogramming for static dispatch of D-Bus inputs serializer

void write(array<byte>& s, const string& input) {
    align(s, 4);
    s << raw(input.size()) << input << 0;
}
void write(array<byte>& s, int integer) { align(s, 4); s << raw(integer); }
void write(array<byte>&) {}
template<class Input, class... Inputs> void write(array<byte>& s, const Input& input, const Inputs&... inputs) {
    write(s,input); write(s,inputs...);
}

template <Field type> void writeField(array<byte>& s, const string& field) {
    align(s, 8);
    s << raw(FieldHeader<type>());
    write(s,field);
}

struct DBus : Poll {
    int fd = 0; // Session bus socket
    string name; // Instance name given by DBus
    uint32 serial = 0; // Serial number to match messages and their replies
    map<string, signal<> > signals;

    /// Read messages and parse \a outputs from first reply (without type checking)
    template<class... Outputs> void read(uint32, Outputs&... outputs) {
        for(;;) {
            array<byte> buffer = ::read(fd,sizeof(MessageHeader<ReplyMessage>)+4);
            auto header = *(MessageHeader<ReplyMessage>*)buffer.data();
            uint32 fieldsSize = align(8, *(uint32*)&buffer.at(sizeof(MessageHeader<ReplyMessage>)));
            string signalName;
            Stream s( ::read(fd, fieldsSize) );
            for(;s;){
                byte field = s.read();
                uint8 size = s.read();
                s += size+1;
                if(field==Target||field==Path||field==Interface||field==Member||field==Sender) {
                    align(s, 4);
                    string name = s.readArray<byte>(); s++;
                    if(field==Member) signalName=move(name);
                } else if(field==ReplySerial) { align(s, 4); s.read<uint32>(); /*uint32 replySerial=s.read(); assert(replySerial==serial);*/ }
                else if(field==SignatureField) { uint8 size = s.read(); s+=size; }
                align(s, 4);
            }
            if(header.length) {
                Stream s( ::read(fd,header.length) );
                if(header.message == ReplyMessage) ::read(s,outputs...);
                else if(header.message == Signal) {
                    auto signal = signals.find(signalName);
                    if(signal) signal->emit();
                    return;
                }
            }
            if(header.message == ReplyMessage) return;
        }
    }

    /// Write a message of type \a type using \a inputs as arguments
    template<Message type, class... Inputs>
    uint32 write(const string& target, const string& object, const string& interface, const string& member, const Inputs&... inputs) {
        array<byte> body; ::write(body,inputs...);
        array<byte> s;
        s << raw(MessageHeader<type>(body.size(),++serial));
        int length = s.size(); s<<0; align(s, 4); int begin=s.size();
        ::writeField<Path>(s,object);
        if(interface) ::writeField<Interface>(s,interface);
        ::writeField<Member>(s,member);
        ::writeField<Target>(s,target);
        ::writeSignature<Inputs...>(s);
        *(uint32*)(&s.at(length)) = s.size()-begin;
        align(s, 8);
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
            string interface = section(method,'.',0,-2), member=section(method,'.',-2,-1);
            return Reply(d,d->write<Call>(target,object,interface,member,inputs...));
        }
        Reply operator [](const string& property) {
            return (*this)("org.freedesktop.DBus.Properties.Get"_,""_,property);
        }
    };

    /// Get a handle to a D-Bus object
    template<class... Inputs> Object operator ()(const string& object, const Inputs&... inputs) {
        return {this,section(object,'/'),"/"_+section(object,'/',-2,-1)};
    }

    DBus() {
        fd = socket(PF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
        sockaddr_un addr; clear(addr);
        addr.sun_family = AF_UNIX;
        string path = section(section(strz(getenv("DBUS_SESSION_BUS_ADDRESS")),'=',1,2),',');
        addr.sun_path[0]=0; copy(addr.sun_path+1,path.data(),path.size());
        if(connect(fd,(sockaddr*)&addr,3+path.size())) fail();
        ::write(fd,"\0AUTH EXTERNAL 30\r\n"_); ::read(fd,37);/*OK*/ ::write(fd,"BEGIN \r\n"_);
        name = operator ()("org.freedesktop.DBus/org/freedesktop/DBus"_)("org.freedesktop.DBus.Hello"_);
        registerPoll({fd, POLLIN});
    }
    void event(pollfd) { read(0); }
};
