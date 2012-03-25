#include "http.h"
#include "file.h"
#include "time.h"
#include <sys/socket.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <fcntl.h>
#include <poll.h>

/// Socket

bool Socket::connect(const string& host, const string& service) {
    addrinfo* ai=0; getaddrinfo(strz(host).data(), strz(service).data(), 0, &ai);
    if(!ai) warn(service,"on"_,host,"not found"_);
    fd = socket(ai->ai_family,ai->ai_socktype,ai->ai_protocol);
    fcntl(fd,F_SETFL,O_NONBLOCK); //allow to timeout connections
    ::connect(fd, ai->ai_addr, ai->ai_addrlen);
    pollfd poll = {fd,POLLOUT,0};
    ::poll(&poll,1, 500); //timeout after 500ms
    if(!poll.revents) { fd=0; return false; }
    fcntl(fd,F_SETFL,0);
    freeaddrinfo(ai);
    return true;
}
Socket::~Socket() { if(fd) close( fd ); fd=0; }
uint Socket::available(uint need) {
    if(need==uint(-1)) buffer<<this->read(4096);
    else while(need>Buffer::available(need)) buffer<<this->read(need-Buffer::available(need));
    return Buffer::available(need);
}

/// SSLSocket

declare(static void ssl_init(), constructor) {
      SSL_library_init();
}
bool SSLSocket::connect(const string& host, const string& service, bool secure) {
    if(!Socket::connect(host,service)) return false;
    if(secure) {
        static SSL_CTX* ctx=SSL_CTX_new(TLSv1_client_method());
        ssl = SSL_new(ctx);
        SSL_set_fd(ssl,fd);
        SSL_connect(ssl);
    }
    return true;
}
SSLSocket::~SSLSocket() { if(ssl) SSL_shutdown(ssl); }
array<byte> SSLSocket::read(int size) {
    array<byte> buffer(size);
    if(ssl) size=SSL_read(ssl, buffer.data(), size); else size=::read(fd,buffer.data(),size);
    buffer.setSize(size);
    return buffer;
}
void SSLSocket::write(const array<byte>& buffer) {
    if(ssl) SSL_write(ssl,buffer.data(),buffer.size());
    else ::write(fd,buffer.data(),buffer.size());
}

/// Base64

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

/// URL

URL::URL(const string& url) {
    assert(url);
    TextBuffer s(copy(url));
    if(contains(url,"://"_)) scheme = s.until("://"_); else scheme="http"_;
    string domain = s.untilAny("/?"_); if(s.buffer[s.index-1]=='?') s.index--;
    host = section(domain,'@',-2,-1);
    if(contains(domain,'@')) authorization = base64(section(domain,'@'));
    if(!contains(host,"."_)) { path=move(host); path<<"/"_; }
    path << s.until("#"_);
    fragment = s.readAll();
}
URL URL::relative(URL&& url) const {
    if(url.scheme) return move(url);
    if(url.host) url.path=url.host+"/"_+url.path;
    if(!url.host) url.host=copy(host);
    if(!contains(url.host,"."_) || url.host=="."_) error(host,path,url.host,url.path);
    return move(url);
}
string str(const URL& url) {
    return url.scheme+"://"_+(url.authorization?url.authorization+"@"_:""_)+url.host+"/"_+url.path+(url.fragment?"#"_+url.fragment:""_);
}

/// HTTP

array<byte> http(const string& host, const string& path, bool secure,
                 const array<string>& headers, const string& method, const string &content) {
    TextSocket http;
    if(!http.connect(host, secure?"https"_/*443*/:"http"_/*80*/, secure)) return ""_;

    string request = method+" /"_+path+" HTTP/1.1\r\nHost: "_+host+"\r\n"_; //TODO: Accept-Encoding: gzip,deflate
    if(content) request << "Content-Length: "_+dec(content.size())+"\r\n"_;
    for(const string& header: headers) request << header+"\r\n"_;
    http.write( request+"\r\n"_+content );
    uint contentLength=0;

    int status;
    if(http.match("HTTP/1.1 200 OK\r\n"_)) status=200;
    else if(http.match("HTTP/1.1 301 Moved Permanently\r\n"_)) { log("301 Moved Permanently"); status=301; }
    else if(http.match("HTTP/1.1 302 Found\r\n"_)) { log("302 Found"); status=302; }
    else if(http.match("HTTP/1.1 302 Moved Temporarily\r\n"_)) { log("302 Moved Temporarily"); status=302; }
    else if(http.match("HTTP/1.1 304 Not Modified\r\n"_)) { log("304 Not Modified"); return ""_; }
    else { log(http.until("\r\n\r\n"_)); error("Unhandled response for",host+"/"_+path,headers); }
    bool chunked=false;
    for(;;) { //parse header
        if(http.match("\r\n"_)) break;
        string key = http.until(": "_); assert(key,http.buffer);
        string value=http.until("\r\n"_); assert(value,http.buffer);
        if(key=="Content-Length"_) contentLength=toInteger(value);
        else if(key=="Transfer-Encoding"_ && value=="chunked"_) chunked=true;
        else if(key=="Location"_ && (status==301||status==302)) {
            URL url = URL(host).relative(value);
            return ::http(url.host,url.path,secure,headers,method,content);
        }
    }

    array<byte> data;
    if(contentLength) data=http.DataStream::read<byte>(contentLength);
    else if(chunked) {
        for(;;) {
            http.match("\r\n"_);
            int contentLength = toInteger(http.until("\r\n"_),16);
            if(contentLength == 0) break;
            data << http.DataStream::read<byte>( contentLength );
        }
    } else assert(data,"Missing content",http.buffer);
    assert(data,"Empty content",(string&)http.buffer);
    log("request",data.size()/1024,"KB");
    return data;
}

array<byte> getURL(const URL& url) {
    static int cache=openFolder(strz(getenv("HOME"))+"/.cache"_);
    string name = replace(url.path,"/"_,"."_);
    if(!name || name=="."_) name="index.htm"_;
    string file = url.host+"/"_+name;
    //TODO: remove old files
    array<byte> content;
    if(exists(file,cache)) {
        long modified = modifiedTime(file,cache);
        if(currentTime()-modified < 3*60*60) { //less than 3 hours old
            array<byte> data = readFile(file,cache);
            if(data) return data;
        }
        array<string> headers;
        if(url.authorization) headers<< "Authorization: Basic "_+url.authorization;
        headers<< "If-Modified-Since: "_+date("ddd, dd MMM yyyy hh:mm:ss TZD"_,date(modified));
        content = http(url.host,url.path,url.scheme=="https"_,headers);
        if(!content) content = readFile(file,cache); //304 Not Modified //TODO: touch instead of rewriting
    }
    if(!exists(url.host,cache)) createFolder(url.host,cache);
    if(!content) {
        array<string> headers;
        if(url.authorization) headers<< "Authorization: Basic "_+url.authorization;
        content = http(url.host,url.path,url.scheme=="https"_,headers);
    }
    writeFile(file,content,cache,true);
    return content;
}
