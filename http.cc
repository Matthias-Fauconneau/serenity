#include "http.h"
#include "file.h"
#include "time.h"
#include "process.h"
#include "linux.h"
#include "map.h"
#include "process.h"
#include "string.h"

struct sockaddress { uint16 family; uint16 port; uint host; int pad[2]; };

// UDPSocket
struct UDPSocket : Socket {
    UDPSocket(uint host, uint16 port) : Socket(PF_INET,SOCK_DGRAM) {
        sockaddress addr = {PF_INET, big16(port), host}; check_(::connect(fd, (const sockaddr*)&addr, sizeof(addr)));
    }
};
// TCPSocket
TCPSocket::TCPSocket(uint host, uint16 port) : Socket(PF_INET,SOCK_STREAM|O_NONBLOCK) {
    if(host==uint(-1)) { if(fd>0) close(fd); fd=0; return; }
    sockaddress addr = {PF_INET,big16(port),host}; connect(Socket::fd, (const sockaddr*)&addr, sizeof(addr));
    fcntl(Socket::fd,F_SETFL,0);
}
// SSLSocket
extern "C" {
 struct SSL;
 int SSL_library_init();
 struct SSLContext* SSL_CTX_new(const struct SSLMethod* method);
 const SSLMethod* TLSv1_client_method();
 SSL* SSL_new(SSLContext *ctx);
 int SSL_set_fd(SSL*, int fd);
 int SSL_state(SSL*);
 int SSL_connect(SSL*);
 int SSL_shutdown(SSL*);
 int SSL_read(SSL*,void *buf,int num);
 int SSL_write(SSL*,const void *buf,int num);
}
SSLSocket::SSLSocket(uint host, uint16 port, bool secure) : TCPSocket(host,port) {
    if(secure && fd) {
        static SSLContext* ctx=(SSL_library_init(), SSL_CTX_new(TLSv1_client_method()));
        ssl = SSL_new(ctx);
        SSL_set_fd(ssl, fd);
        SSL_connect(ssl);
    }
}
SSLSocket::~SSLSocket() { if(ssl) SSL_shutdown(ssl); }
array<byte> SSLSocket::readUpTo(int size) {
    if(!ssl) return TCPSocket::readUpTo(size);
    array<byte> buffer(size);
    size=SSL_read(ssl, buffer.data, size);
    if(size>=0) buffer.size=size; else warn("SSL error",size);
    return buffer;
}
void SSLSocket::write(const ref<byte>& buffer) {
    if(!ssl) return TCPSocket::write(buffer);
    int unused size=SSL_write(ssl,buffer.data,buffer.size); assert(size==(int)buffer.size);
}

// DNS
uint ip(TextData& s) { int a=s.integer(), b=(s.match('.'),s.integer()), c=(s.match('.'),s.integer()), d=(s.match('.'),s.integer()); return (d<<24)|(c<<16)|(b<<8)|a; }
uint nameserver() { TextData s=readFile("/etc/resolv.conf"_); s.until("nameserver "_); return ip(s); }
uint resolve(const ref<byte>& host) {
    static File dnsCache = File("dns"_,cache(),ReadWrite|Create|Append);
    static Map dnsMap = dnsCache;
    uint ip=-1;
    for(TextData s(dnsMap);s;s.line()) { if(s.match(host)) { s.match(' '); ip=::ip(s); break; } } //TODO: binary search (on fixed length lines)
    bool negativeEntry=false; if(!ip) ip=-1, negativeEntry=true; //return false; //try to resolve negative entries again
    if(ip==uint(-1)) {
        static UDPSocket dns = UDPSocket(nameserver(), 53);
        array<byte> query;
        struct Header { uint16 id=big16(currentTime()); uint16 flags=1; uint16 qd=big16(1), an=0, ns=0, ar=0; } packed header;
        query << raw(header);
        for(TextData s(host);s;) { //QNAME
            ref<byte> label = s.until('.');
            query << label.size << label;
        }
        query << 0 << 0 << 1 << 0 << 1;
        dns.write(query);
        if(!dns.poll(1000)){log("DNS query timed out, retrying... "); dns.write(query); if(!dns.poll(1000)){log("giving up"); return -1; }}
        BinaryData s(dns.readUpTo(4096), true);
        header = s.read<Header>();
        for(int i=0;i<big16(header.qd);i++) { for(uint8 n;(n=s.read());) s.advance(n); s.advance(4); } //skip any query headers
        for(int i=0;i<big16(header.an);i++) {
            for(uint8 n;(n=s.read());) { if(n>=0xC0) { s.advance(1); break; } s.advance(n); } //skip name
            uint16 type=s.read(), unused class_=s.read(); uint32 unused ttl=s.read(); uint16 unused size=s.read();
            if(type!=1) { s.advance(size); continue; }
            assert(type=1/*A*/); assert(class_==1/*INET*/);
            ip = s.read<uint>(); //IP (no swap)
            string entry = host+" "_+dec(raw(ip),'.')+"\n"_;
            log(host,dec(raw(ip),'.'));
            dnsCache.write(entry); //add new entry
            dnsMap = dnsCache; //remap cache
            break;
        }
        if(ip==uint(-1) && !negativeEntry) {
            log("unknown",host);
            dnsCache.write(string(host+" 0.0.0.0\n"_)); // add negative entry
            dnsMap = dnsCache; //remap cache
        }
    }
    return ip;
}

// URL
string base64(const ref<byte>& input) {
    string output(input.size*4/3+1);
    for(uint j=0;j<input.size;) {
        byte block[3]={}; uint i=0; while(i<3 && j<input.size) block[i++] = input[j++];
        const char cb64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        output << cb64[ block[0] >> 2 ] << cb64[ ((block[0] & 0x03) << 4) | ((block[1] & 0xf0) >> 4) ]
               << (i > 1 ? cb64[ ((block[1] & 0x0f) << 2) | ((block[2] & 0xc0) >> 6) ] : '=') << (i > 2 ? cb64[ block[2] & 0x3f ] : '=');
    }
    return output;
}
URL::URL(const ref<byte>& url) {
    if(!url) { log("Empty URL"); return; }
    TextData s(url);
    if(find(url,"://"_)) scheme = string(s.until("://"_));
    else s.match("//"_); //net_path
    ref<byte> domain = s.whileNo("/?"_);
    host = string(section(domain,'@',-2,-1));
    if(!host.contains('.')) path=move(host);
    if(domain.contains('@')) authorization = base64(section(domain,'@'));
    path << s.until('#');
    fragment = string(s.untilEnd());
}
URL URL::relative(URL&& url) const {
    if(!url.scheme) {
        if(!url.path && url.host) url.path=move(url.host);
        if(!url.host && !startsWith(url.path,"/"_)) url.path = section(path,'/',0,-2)+"/"_+url.path;
        url.scheme=copy(scheme);
    }
    if(!url.host) url.host=copy(host);
    if(startsWith(url.path,"."_)) url.path.removeAt(0);
    assert(!host.contains('/'));
    return move(url);
}
string str(const URL& url) {
    return url.scheme+(url.scheme?"://"_:""_)+url.authorization+(url.authorization?"@"_:""_)+url.host+url.path+(url.fragment?"#"_:""_)+url.fragment;
}

template<class T> uint DataStream<T>::available(uint need) {
    while(need>Data::available(need) && T::poll()) {
        array<byte> chunk = T::readUpTo(max(4096u,need-Data::available(need)));
        if(!chunk) { log("Empty chunk",Data::available(need),need); break; }
        buffer << chunk;
    }
    return Data::available(need);
}

// HTTP
string cacheFile(const URL& url) {
    string name = replace(url.path,"/"_,"."_);
    if(startsWith(name,"."_)) name.removeAt(0);
    if(!name) name=string("index.htm"_);
    assert(url.host && !url.host.contains('/'));
    return url.host+"/"_+name;
}
HTTP::HTTP(URL&& url, Handler handler, array<string>&& headers, const ref<byte>& method)
    : DataStream<SSLSocket>(resolve(url.host),url.scheme=="https"_?443:80,url.scheme=="https"_), Poll(Socket::fd,POLLOUT),
      url(move(url)), headers(move(headers)), method(method), handler(handler) {
    if(!Socket::fd) { warn("Unknown host",this->url.host); state=Done; free(this); return; }
    registerPoll();
}
void HTTP::request() {
    string request = method+" "_+(startsWith(url.path,"/"_)?""_:"/"_)+url.path+" HTTP/1.1\r\nHost: "_+url.host+"\r\n"_; //User-Agent: Browser\r\n
    for(const string& header: headers) request << header+"\r\n"_;
    write(string(request+"\r\n"_)); state=Header;
}
#if 0
GET /ggmain.rss HTTP/1.1[CRLF]
GET /ggmain.rss HTTP/1.1
Host: www.girlgeniusonline.com[CRLF]
Host: www.girlgeniusonline.com
Connection: close[CRLF]
Connection: close
Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8[CRLF]
Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8
Accept-Language: en-US, *[CRLF]
Accept-Language: en-US, *
Accept-Charset: ISO-8859-1,UTF-8;q=0.7,*;q=0.7[CRLF]
Accept-Charset: ISO-8859-1,UTF-8;q=0.7,*;q=0.7
Cache-Control: no-cache[CRLF]
Cache-Control: no-cache
Referer: http://web-sniffer.net/[CRLF]

Referer: http://web-sniffer.net/
#endif

void HTTP::header() {
    string file = cacheFile(url);
    // Status
    if(!match("HTTP/1.1 "_)&&!match("HTTP/1.0 "_)) { log("No HTTP",url); state=Done; free(this); return; }
    int status = toInteger(until(" "_));
    until("\r\n"_);
    if(status==200||status==301||status==302) {}
    else if(status==304) { //Not Modified
        assert(existsFile(file,cache()));
        touchFile(file,cache());
        log("Not Modified",url);
        state = Handle;
        return;
    }
    else if(status==400) log("Bad Request"_,url); //cache reply anyway to avoid repeating bad requests
    else if(status==404) log("Not Found"_,url);
    else if(status==408) log("Request timeout"_);
    else if(status==500) log("Internal Server Error"_,untilEnd());
    else if(status==502) log("Bad gateway"_);
    else if(status==504) log("Gateway timeout"_);
    else { log("Unhandled status",status,"from",url); state=Done; free(this); return; }

    // Headers
    while(!match("\r\n"_)) {
        ref<byte> key = until(": "_); assert(key,buffer);
        ref<byte> value=until("\r\n"_);
        if(key=="Content-Length"_) contentLength=toInteger(value);
        else if(key=="Transfer-Encoding"_ && value=="chunked"_) chunked=true;
        else if((key=="Location"_ && status!=200) || key=="Refresh"_) {
            if(startsWith(value,"0;URL="_)) value=value.slice(6);
            url = url.relative(value);
            uint ip = resolve(url.host);
            if(ip==uint(-1)) { log("Unknown host",url); free(this); return; }
            SSLSocket::operator=(SSLSocket(ip, url.scheme=="https"_?443:80, url.scheme=="https"_)); Poll::fd=SSLSocket::fd;
            index=0; buffer=array<byte>(); contentLength=chunked=0;
            redirect << file;
            state=Request; events=POLLOUT;
            return;
        }
    }
    state = Content;
}
void HTTP::event() {
    if((revents&POLLHUP) && state <= Content) { log("Connection broken",url); state=Done; free(this); return; }
    if(state == Request) { events=POLLIN; request(); return; }
    if(state == Header) { header(); }
    if(state == Content) {
        if(contentLength) {
            if(available(contentLength)<contentLength) return;
            content << Data::read(contentLength);
            state=Cache;
        }
        else if(chunked) {
            do {
                if(chunkSize<=0) { chunkSize = hexadecimal(); match("\r\n"_); } //else already parsed
                if(chunkSize==0) { state=Cache; break; }
                if(chunkSize<0 || available(chunkSize+2)<uint(chunkSize+2)) return;
                content << Data::read(chunkSize); match("\r\n"_);
                chunkSize=0;
            } while(Data::available(3)>=3);
        }
        else state=Cache;
    }
    if(state == Cache) {
        if(!content) { log("Missing content",buffer); state=Done; free(this); return; }
        if(content.size>1024) log("Downloaded",url,content.size/1024,"KB"); else log("Downloaded",url,content.size,"B");
        redirect << cacheFile(url);
        for(const string& file: redirect) {Folder(section(file,'/'),cache(),true); writeFile(file,content,cache());}
        state=Handle; queue(); return;  //Cache other outstanding requests before handling this one
    }
    if(state==Handle) {
        handler(url,Map(cacheFile(url),cache()));
        state=Done;
        free(this);
        return;
    }
}

void getURL(URL&& url, Handler handler, int maximumAge) {
    assert(url.host,url);
    string file = cacheFile(url);
    array<string> headers;
    if(url.authorization) headers<< "Authorization: Basic "_+url.authorization;
    // Check if cached
    if(existsFile(file,cache())) {
        long modified = modifiedTime(file,cache());
        if(currentTime()-modified < maximumAge*60) {
            log("Cached", url);
            handler(url,Map(file,cache()));
            return;
        }
        headers<< "If-Modified-Since: "_+str(Date(modified),"ddd, dd MMM yyyy hh:mm:ss TZD"_);
    }
    heap<HTTP>(move(url),handler,move(headers));
}
