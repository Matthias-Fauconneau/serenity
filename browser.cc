#include "process.h"
#include "file.h"
#include "xml.h"

#include <sys/socket.h>
#include <netdb.h>

template<class T> inline array<string> hex(array<T> a) { return apply<string>(a,[](const T& b){return hex(b);}); }
#define check( expression) ({ int error=expression; if(error) error(#expression,error); })

/// \a Socket is a network socket
struct Socket : Buffer {
    int fd=0;
    /// Connects to \a service on \a host
    void connect(const string& host, const string& service) {
        addrinfo* ai=0; getaddrinfo(strz(host).data(), strz(service).data(), 0, &ai);
        //for(addrinfo* i=ai; i; i=i->ai_next) log(join(hex(array<ushort>((ushort*)(i->ai_addr+8),6)),":"_));
        if(!ai) log(service,"on"_,host,"not found"_);
        fd = socket(ai->ai_family,ai->ai_socktype,ai->ai_protocol);
        check( ::connect(fd, ai->ai_addr, ai->ai_addrlen) );
        freeaddrinfo(ai);
    }
    ~Socket() { if(fd) close( fd ); fd=0; }
    virtual array<byte> read(int size) { return ::read(fd,size); }
    virtual void write(const array<byte>& buffer) { ::write(fd,buffer); }

    uint available(uint need) override {
        if(need==uint(-1)) buffer<<this->read(4096);
        else if(need>Buffer::available(need)) buffer<<this->read(need-Buffer::available(need));
        return Buffer::available(need);
    }
};

#include <openssl/ssl.h>
declare(static void ssl_init(), constructor) {
      SSL_library_init();
}
struct SSLSocket : Socket {
    SSL* ssl=0;
    void connect(const string& host, const string& service, bool secure=false) {
        Socket::connect(host,service);
        if(secure) {
            static SSL_CTX* ctx=SSL_CTX_new(TLSv1_client_method());
            ssl = SSL_new(ctx);
            SSL_set_fd(ssl,fd);
            SSL_connect(ssl);
        }
    }
    ~SSLSocket() { if(ssl) SSL_shutdown(ssl); }
    array<byte> read(int size) override {
        array<byte> buffer(size);
        if(ssl) size=SSL_read(ssl, buffer.data(), size); else size=::read(fd,buffer.data(),size);
        buffer.setSize(size);
        return buffer;
    }
    void write(const array<byte>& buffer) override {
        if(ssl) SSL_write(ssl,buffer.data(),buffer.size());
        else ::write(fd,buffer.data(),buffer.size());
    }
};

struct TextSocket : TextStream, SSLSocket {};

//base64 encode a stream adding padding
string base64(const string& input) {
    string output(input.size()*4/3+1);
    for(uint j=0;j<input.size();) {
        ubyte block[3];
        uint i=0; while(i<3 && j<input.size()) block[i++] = input[j++];
        assert(i);
        //encode 3 8-bit binary bytes as 4 '6-bit' characters
        const char cb64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        output
                << cb64[ block[0] >> 2 ]
                << cb64[ ((block[0] & 0x03) << 4) | ((block[1] & 0xf0) >> 4) ]
                << (i > 1 ? cb64[ ((block[1] & 0x0f) << 2) | ((block[2] & 0xc0) >> 6) ] : '=')
                << (i > 2 ? cb64[ block[2] & 0x3f ] : '=');
    }
    return output;
}

struct HTTP {
    TextSocket http;
    string host;
    string authorization;
    bool chunked=false;

    /// Create an HTTP connection to \a host
    HTTP(string&& host, bool secure=false, string&& authorization=""_) : host(move(host)), authorization(move(authorization)) {
        http.connect(this->host, secure?"https"_/*443*/:"http"_/*80*/, secure);
    }
    array<byte> request(const string& path, const string& method, const string& post) {
        string request = method+" /"_+path+" HTTP/1.1\r\nHost: "_+host+"\r\n"_; //TODO: Accept-Encoding: gzip,deflate
        if(authorization) request << "Authorization: Basic "_+authorization+"\r\n"_;
        if(post) request << "Content-Length: "_+dec(post.size())+"\r\n"_;
        http.write( request+"\r\n"_+post );
        uint contentLength=0;
        if(http.match("HTTP/1.1 200 OK\r\n"_));
        else error(http.until("\r\n"_));
        for(;;) { //parse header
            if(http.match("\r\n"_)) break;
            string key = http.until(": "_); string value=http.until("\r\n"_);
            if(key=="Content-Length"_) contentLength=toInteger(value);
            //if(http.match("Transfer-Encoding: chunked"_) ) chunked=true;
        }
        if(contentLength) return http.DataStream::read<byte>(contentLength);
        /*else if( chunked ) {
            array<byte> data;
            for(;;) {
                http.match("\r\n"_);
                int contentLength = http.hex();
                http.match("\r\n"_);
                if(contentLength == 0) return data;
                data << http.read( contentLength );
            }
        }*/ else error("Missing content",http.buffer);
    }
    array<byte> get(const string& path) { return request(path,"GET"_,""_); }
    static array<byte> getURL(const string& url) {
        TextBuffer s(copy(url));
        bool secure;
        if(s.match("http://"_)) secure=false;
        else if(s.match("https://"_)) secure=true;
        else error(url);
        string host = s.until("/"_);
        string path = s.readAll();
        string content = HTTP(section(host,'@',-2,-1),secure,base64(section(host,'@'))).get(path);
        return move(content);
    }
    array<byte> post(const string& path, const string& content) { return request(path,"POST"_,content); }
};

struct Browser : Application {
    void start(array<string>&& arguments) {
        array<string> feeds = apply<string>( Element(mapFile(arguments.first()))("opml/body/outline/outline"_),
                                             [](const Element* const& e){return (*e)["xmlUrl"_];});
    for(const string& url: feeds) {
        log((const string&)HTTP::getURL(url));
        break;
    }
}
} browser;
