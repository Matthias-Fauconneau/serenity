#include "http.h"
#include "file.h"
#include "time.h"
#include "process.h"
#include "linux.h"
#include "map.h"
#include "memory.h"
#include "debug.h"
#include "process.h"

/// TCPSocket

uint32 ip(TextData& s) {
    int a=s.number(), b=(s.match('.'),s.number()), c=(s.match('.'),s.number()), d=(s.match('.'),s.number());
    return (d<<24)|(c<<16)|(b<<8)|a;
}

bool TCPSocket::connect(const ref<byte>& host, const ref<byte>& service) {
    disconnect();
    static File dnsCache = File("dns"_,cache(),File::ReadWrite|File::Create|File::Append);
    static Map dnsMap = dnsCache;
    uint ip=-1;
    for(TextData s(dnsMap);s;s.until('\n')) { if(s.match(host)) { s.match(' '); ip=::ip(s); break; } } //TODO: binary search (on fixed length lines)
    if(!ip) ip=-1;//return false; //negative entry
    if(ip==uint(-1)) {
        static Socket dns=0;
        if(!dns) {
            dns = Socket(PF_INET,SOCK_DGRAM);
            TextData s=readFile("etc/resolv.conf"_);
            s.until("nameserver "_);
            uint a=s.number(), b=(s.match("."_),s.number()), c=(s.match("."_),s.number()), d=(s.match("."_),s.number());
            sockaddr addr = {PF_INET, big16(53), (d<<24)|(c<<16)|(b<<8)|a};
            check_( ::connect(dns.fd, &addr, sizeof(addr)) );
        }
        array<byte> query;
        struct Header { uint16 id=big16(currentTime()); uint16 flags=1; uint16 qd=big16(1), an=0, ns=0, ar=0; } packed header;
        query << raw(header);
        for(TextData s(host);s;) { //QNAME
            ref<byte> label = s.until('.');
            query << label.size << label;
        }
        query << 0 << 0 << 1 << 0 << 1;
        dns.write(query);
        ::write(string(host+" "_));
        pollfd fd(dns.fd,POLLIN); if(!::poll(&fd,1,1000)){log("DNS query timed out, retrying... "); dns.write(query); if(!::poll(&fd,1,1000)){log("giving up"); return false; } }
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
            log(dec(raw(ip),'.'));
            dnsCache.write(entry); //add new entry
            dnsMap = dnsCache; //remap cache
            break;
        }
        if(ip==uint(-1)) {
            log("unknown");
            dnsCache.write(string(host+" 0.0.0.0\n"_)); // add negative entry
            dnsMap = dnsCache; //remap cache
            return false;
        }
    }

    fd = socket(PF_INET,SOCK_STREAM|O_NONBLOCK);
    sockaddr addr = {PF_INET,big16(service=="https"_?443:80),ip};
    ::connect(fd, &addr, sizeof(addr));
    fcntl(fd,F_SETFL,0);
    registerPoll(POLLOUT);
    return true;
}
void TCPSocket::disconnect() { unregisterPoll(); }

uint TCPSocket::available(uint need) {
    while(need>InputData::available(need) && poll()) {
        array<byte> packet=readUpTo(max(4096u,need-InputData::available(need)));
        if(!packet) { warn("Empty packet",InputData::available(need),need); break; }
        (array<byte>&)buffer<<packet;
    }
    return InputData::available(need);
}

/// SSLSocket

extern "C" {
 int SSL_library_init();
 struct SSLContext* SSL_CTX_new(const struct SSLMethod* method);
 const SSLMethod* TLSv1_client_method();
 SSL* SSL_new(SSLContext *ctx);
 int SSL_set_fd(SSL*, int fd);
 int SSL_connect(SSL*);
 int SSL_shutdown(SSL*);
 int SSL_read(SSL*,void *buf,int num);
 int SSL_write(SSL*,const void *buf,int num);
}
bool SSLSocket::connect(const ref<byte>& host, const ref<byte>& service) {
    if(!TCPSocket::connect(host,service)) return false;
    if(service=="https"_) {
        static SSLContext* ctx=(SSL_library_init(), SSL_CTX_new(TLSv1_client_method()));
        ssl = SSL_new(ctx);
        SSL_set_fd(ssl,fd);
        SSL_connect(ssl);
    }
    return true;
}
SSLSocket::~SSLSocket() { if(ssl) SSL_shutdown(ssl); }
uint SSLSocket::available(uint need) {
    if(!ssl) return TCPSocket::available(need);
    while(need>InputData::available(need)) {
        if(poll(this,1,10)!=1 || !(revents&POLLIN)) { log(revents&POLLHUP?"Connection broken":"Waiting for more data"); break; }
        uint size = max(4096u,need-InputData::available(need));
        array<byte> packet(size);
        if(ssl) size=SSL_read(ssl, packet.data(), size);
        if(int(size)<0) { warn("SSL error"); break; }
        packet.setSize(size);
        if(!packet) { warn("Empty packet",InputData::available(need),need); break; }
        (array<byte>&)buffer<<packet;
    }
    return InputData::available(need);
}
void SSLSocket::write(const ref<byte>& buffer) {
    if(!ssl) return TCPSocket::write(buffer);
    assert(SSL_write(ssl,buffer.data,buffer.size)==(int)buffer.size);
}

/// Base64

string base64(const ref<byte>& input) {
    string output(input.size*4/3+1);
    for(uint j=0;j<input.size;) {
        byte block[3]={};
        uint i=0; while(i<3 && j<input.size) block[i++] = input[j++];
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

URL::URL(const ref<byte>& url) {
    if(!url) { warn("Empty URL"); return; }
    TextData s(url);
    if(find(url,"://"_)) scheme = string(s.until("://"_));
    else s.match("//"_); //net_path
    ref<byte> domain = s.untilAny("/?"_); if(s.buffer[s.index-1]=='?') s.index--;
    host = string(section(domain,'@',-2,-1));
    if(domain.contains('@')) authorization = base64(section(domain,'@'));
    if(!host.contains('.')) { path=move(host); path<<"/"_; }
    else if(host=="."_) host.clear();
    path << s.until('#');
    fragment = string(s.untilEnd());
}
URL URL::relative(URL&& url) const {
    if(url.scheme) { assert(url.host.size()>1); return move(url); } //already complete URL
    if(url.host && url.path) { assert(url.host.size()>1); url.scheme=copy(scheme); return move(url); } //missing only scheme
    if(url.host) { //relative path URLs could be misparsed into host field if first path element contains dots
        swap(url.path,url.host);
        if(url.host) {
            if(!startsWith(url.host,"/"_)) url.path<<"/"_;
            url.path << move(url.host);
        }
    }
    if(!url.host) url.host=copy(host);
    if(!url.host.contains('.') || url.host=="."_) error(host,path,url.host,url.path);
    if(startsWith(url.path,"."_)) url.path.removeAt(0);
    while(startsWith(url.path,"/"_)) url.path.removeAt(0);
    return move(url);
}
string str(const URL& url) {
    return url.scheme+(url.scheme?"://"_:""_)
            +url.authorization+(url.authorization?"@"_:""_)
            +url.host+"/"_+url.path
            +(url.fragment?"#"_:""_)+url.fragment;
}

/// HTTP

string cacheFile(const URL& url) {
    string name = replace(url.path,"/"_,"."_);
    if(!name || name=="."_) name=string("index.htm"_);
    assert(!url.host.contains('/'));
    return url.host+"/"_+name;
}

HTTP::HTTP(const URL& url, Handler handler, array<string>&& headers, const ref<byte>& method)
    : url(str(url)), headers(move(headers)), method(method), handler(handler) {
    if(!connect(url.host, url.scheme)) { free(this); return; } state=Request;
}
void HTTP::request() {
    string request = method+" /"_+url.path+" HTTP/1.1\r\nHost: "_+url.host+"\r\nUser-Agent: Browser\r\n"_; //TODO: Accept-Encoding: gzip,deflate
    for(const string& header: headers) request << header+"\r\n"_;
    write(string(request+"\r\n"_)); state=Header;
}
void HTTP::header() {
    string file = cacheFile(url);
    // Status
    if(!match("HTTP/1.1 "_)&&!match("HTTP/1.0 "_)) { log((string&)buffer); log("No HTTP",url); state=Done; free(this); return; }
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
    else if(status==504) log("Gateway timeout"_);
    else { log((string&)buffer); warn("Unhandled status",status,"from",url); state=Done; free(this); return; }

    // Headers
    for(;;) {
        if(match("\r\n"_)) break;
        ref<byte> key = until(": "_); assert(key,buffer);
        ref<byte> value=until("\r\n"_);
        if(key=="Content-Length"_) contentLength=toInteger(value);
        else if(key=="Transfer-Encoding"_ && value=="chunked"_) chunked=true;
        else if((key=="Location"_ && (status==301||status==302)) || key=="Refresh"_) {
            if(startsWith(value,"0;URL="_)) value=value.slice(6);
            URL next = url.relative(value);
            assert(!url.host.contains('/') && !next.host.contains('/'),url,next);
            redirect << file;
            ((array<byte>&)buffer).clear(); index=0; contentLength=0; chunked=false; disconnect(); //state=Connect;
            url=move(next);
            if(!connect(url.host, url.scheme)) { free(this); return; } state=Connect;
            return;
        }
    }
    state = Data;
}
void HTTP::event() {
    if((revents&POLLHUP) && state <= Data) { log("Connection broken",url); state=Done; free(this); return; } //TODO: also cleanup never established connections
    if(state == Request) { fcntl(fd,F_SETFL,0); events=POLLIN|POLLHUP; request(); return; }
    if(state == Header) { header(); }
    if(state == Data) {
        if(contentLength) {
            if(available(contentLength)<contentLength) return;
            content << InputData::read(contentLength);
            state=Cache;
        }
        else if(chunked) {
            do {
                uint chunkSize = number(16); match("\r\n"_);
                if(chunkSize==0) { state=Cache; break; }
                uint unused packetSize=available(chunkSize); assert(packetSize>=chunkSize);
                content << InputData::read(chunkSize); match("\r\n"_);
            } while(InputData::available(3)>=3);
        }
        else state=Cache;
    }
    if(state == Cache) {
        if(!content) log("Missing content",(string&)buffer);
        if(content.size()>1024) log("Downloaded",url,content.size()/1024,"KB"); else log("Downloaded",url,content.size(),"B");
        redirect << cacheFile(url);
        for(const string& file: redirect) writeFile(file,content,cache());
        state=Handle; wait(); return;  //Cache other outstanding requests before handling this one
    }
    if(state==Handle) {
        handler(url,Map(cacheFile(url),cache()));
        state=Done;
        free(this);
        return;
    }
}

void getURL(const URL &url, Handler handler, int maximumAge) {
    string file = cacheFile(url);
    array<string> headers;
    if(url.authorization) headers<< "Authorization: Basic "_+url.authorization;
    // Check if cached
    if(existsFile(file,cache())) {
        long modified = modifiedTime(file,cache());
        if(currentTime()-modified < maximumAge*60) {
            debug( log("Cached",url); )
            handler(url,Map(file,cache()));
            return;
        }
        headers<< "If-Modified-Since: "_+str(date(modified),"ddd, dd MMM yyyy hh:mm:ss TZD"_);
    }
    heap<HTTP>(url,handler,move(headers));
}
