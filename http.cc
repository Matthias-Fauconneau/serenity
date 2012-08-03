#include "http.h"
#include "file.h"
#include "time.h"
#include "process.h"
#include "linux.h"
#include "map.h"
#include "memory.h"
#include "array.cc"

/// Socket

uint32 ip(TextStream& s) {
    int a=s.number(), b=(s.match('.'),s.number()), c=(s.match('.'),s.number()), d=(s.match('.'),s.number());
    return (d<<24)|(c<<16)|(b<<8)|a;
}

bool Socket::connect(const ref<byte>& host, const ref<byte>& /*service*/) {
    disconnect();
    static int dnsCache = appendFile("cache/dns"_);
    static Map dnsMap = mapFile(dnsCache);
    uint ip=-1;
    for(TextStream s(dnsMap);s;s.until('\n')) { if(s.match(host)) { s.match(" "_); ip=::ip(s); break; } } //TODO: binary search (on fixed length lines)
    if(!ip) ip=-1;//return false; //negative entry
    if(ip==uint(-1)) {
        static int dns;
        if(!dns) {
            dns = socket(PF_INET,SOCK_DGRAM,0);
            assert(dns>0,dns);
            TextStream s(readFile("etc/resolv.conf"_));
            s.until("nameserver "_);
            uint a=s.number(), b=(s.match("."_),s.number()), c=(s.match("."_),s.number()), d=(s.match("."_),s.number());
            sockaddr addr = {PF_INET, swap16(53), (d<<24)|(c<<16)|(b<<8)|a};
            int unused e= check( ::connect(dns, &addr, sizeof(addr)) );
        }
        array<byte> query;
        struct Header { uint16 id=swap16(currentTime()); uint16 flags=1; uint16 qd=swap16(1), an=0, ns=0, ar=0; } packed header;
        query << raw(header);
        for(TextStream s(host);s;) { //QNAME
            ref<byte> label = s.until('.');
            query << label.size << label;
        }
        query << 0 << 0 << 1 << 0 << 1;
        ::write(dns,query);
        ::write(1,string("query "_+host+" "_));
        DataStream s(readUpTo(dns,4096), true);
        header = s.read<Header>();
        for(int i=0;i<swap16(header.qd);i++) { for(ubyte n;(n=s.read());) s.advance(n); s.advance(4); } //skip any query headers
        for(int i=0;i<swap16(header.an);i++) {
            for(ubyte n;(n=s.read());) { if(n>=0xC0) { s.advance(1); break; } s.advance(n); } //skip name
            uint16 type=s.read(), unused class_=s.read(); uint32 unused ttl=s.read(); uint16 unused size=s.read();
            if(type!=1) { s.advance(size); continue; }
            assert(type=1/*A*/); assert(class_==1/*INET*/);
            ip = s.read<uint>(); //IP (no swap)
            string entry = host+" "_+str(raw(ip),"."_)+"\n"_;
            log(str(raw(ip),"."_));
            ::write(dnsCache,entry); // add new entry
            dnsMap = mapFile(dnsCache); //remap cache
            break;
        }
        if(ip==uint(-1)) {
            log("unknown");
            ::write(dnsCache,string(host+" 0.0.0.0\n"_)); // add negative entry
            dnsMap = mapFile(dnsCache); //remap cache
            return false;
        }
    }

    fd = socket(PF_INET,SOCK_STREAM|O_NONBLOCK,0);
    sockaddr addr = {PF_INET,swap16(80),ip}; //http
    ::connect(fd, &addr, sizeof(addr));
    return true;
}
void Socket::disconnect() { if(fd) close( fd ); fd=0; }

array<byte> Socket::receive(uint size) { return readUpTo(fd,size); }
void Socket::write(const ref<byte>& buffer) { ::write(fd,buffer); }

uint Socket::available(uint need) {
    if(need==uint(-1)) buffer<<this->receive(4096);
    else while(need>Stream::available(need)) buffer<<this->receive(max(4096u,need-Stream::available(need)));
    return Stream::available(need);
}
ref<byte> Socket::get(uint size) {
    while(size>Stream::available(size)) buffer<<this->receive(max(4096u,size-Stream::available(size)));
    return Stream::get(size);
}

/// Base64

string base64(const ref<byte>& input) {
    string output(input.size*4/3+1);
    for(uint j=0;j<input.size;) {
        ubyte block[3];
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
    if(!url) { trace(); warn("Empty URL"); return; }
    TextStream s(url);
    if(find(url,"://"_)) scheme = string(s.until("://"_));
    else s.match("//"_); //net_path
    ref<byte> domain = s.untilAny("/?"_); if(s.buffer[s.index-1]=='?') s.index--;
    host = string(section(domain,'@',-2,-1));
    if(contains(domain,byte('@'))) authorization = base64(section(domain,'@'));
    if(!contains(host,byte('.'))) { path=move(host); path<<"/"_; }
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
    if(!contains<byte>(url.host,'.') || url.host=="."_) error(host,path,url.host,url.path);
    if(startsWith(url.path,"."_)) url.path=string(slice(url.path,1));
    while(startsWith(url.path,"/"_)) url.path=string(slice(url.path,1));
    return move(url);
}
string str(const URL& url) {
    return url.scheme+(url.scheme?"://"_:""_)
            +url.authorization+(url.authorization?"@"_:""_)
            +url.host+"/"_+url.path
            +(url.fragment?"#"_:""_)+url.fragment;
}

/// HTTP

int cache;
string cacheFile(const URL& url) {
    if(!cache) cache=openFolder("cache"_);
    string name = replace(url.path,"/"_,"."_);
    if(!name || name=="."_) name=string("index.htm"_);
    assert(!contains<byte>(url.host,'/'));
    return url.host+"/"_+name;
}

HTTP::HTTP(const URL& url, function<void(const URL&, array<byte>&&)> handler, array<string>&& headers, const ref<byte>& method)
    : url(str(url)), headers(move(headers)), method(method), handler(handler) {
    debug( log("Request",url); )
    if(!connect(url.host, url.scheme)) { free(this); return; }
    registerPoll(i({fd, POLLIN|POLLOUT}));
}
void HTTP::request() {
    state=Request;
    string request = method+" /"_+url.path+" HTTP/1.1\r\nHost: "_+url.host+"\r\nUser-Agent: Browser\r\n"_; //TODO: Accept-Encoding: gzip,deflate
    for(const string& header: headers) request << header+"\r\n"_;
    write( string(request+"\r\n"_) );
}
void HTTP::header() {
    string file = cacheFile(url);
    assert(state==Request);
    state=Header;
    // Status
    if(!match("HTTP/1.1 "_)&&!match("HTTP/1.0 "_)) { log((string&)buffer, index); warn("No HTTP",url); state=Done; free(this); return; }
    int status = toInteger(until(" "_));
    until("\r\n"_);
    if(status==200||status==301||status==302) {}
    else if(status==304) { //Not Modified
        assert(exists(file,cache));
        content = readFile(file,cache);
        assert(content);
        writeFile(file,content,cache); //TODO: touch instead of rewriting
        debug( log("Not Modified",url); )
        state = Handle;
        return;
    } else if(status==404||status==408) {
        warn(status==404?"Not Found"_:"Request timeout"_,url);
        state=Done; free(this); return;
    } else {
        log(until("\r\n\r\n"_)); warn("Unhandled status",status,"from",url);
        state=Done; free(this); return;
    }

    // Headers
    for(;;) {
        if(match("\r\n"_)) break;
        ref<byte> key = until(": "_); assert(key,buffer);
        ref<byte> value=until("\r\n"_);
        if(key=="Content-Length"_) {
            contentLength=toInteger(value);
            if(contentLength==0) { state=Done; free(this); return; }
        }
        else if(key=="Transfer-Encoding"_ && value=="chunked"_) chunked=true;
        else if((key=="Location"_ && (status==301||status==302)) || key=="Refresh"_) {
            if(startsWith(value,"0;URL="_)) value=::slice(value,6);
            URL next = url.relative(value);
            assert(!contains<byte>(url.host,'/'),url);
            assert(!contains<byte>(next.host,'/'),url,next);
            redirect << file;
            buffer.clear(); index=0; contentLength=0; chunked=false; unregisterPoll(); disconnect(); state=Connect;
            url=move(next);
            if(!connect(url.host, url.scheme)) free(this); else registerPoll(i({fd, POLLIN|POLLOUT}));
            return;
        } //else if(key=="Set-Cookie"_) log("Set-Cookie"_,value); //ignored
    }
    state = Data;
}
void HTTP::event(pollfd poll) {
    assert(fd); assert(state>=Connect && state <=Done, int(state));
    if(poll.revents&POLLHUP) { log("Connection broken",poll.revents&POLLIN,url); state=Done; free(this); return; }
    if(state == Connect) {
        if(!poll.revents) { log("Connection timeout",url); state=Done; free(this); return; }
        fcntl(fd,F_SETFL,0);
        request();
        return;
    }
    if(state == Request) {
        header();
        if(state==Data && contentLength) {
            content = array<byte>(Stream::read(min(contentLength,Stream::available(0))));//Read all currently buffered data (without blocking on socket)
            if(content.size()!=contentLength) return; else state=Cache;
        }
    }
    if(state == Data) {
        if(contentLength) {
            content << receive(contentLength-content.size());
            if(content.size()>=contentLength) state=Cache;
        }
        else if(chunked) {
            int contentLength = number(16); match("\r\n"_);
            if(contentLength) { content << read(contentLength); match("\r\n"_); }
            else state=Cache;
        }
    }
    if(state == Cache) {
        if(!content) { log("Missing content",(string&)buffer); free(this); return; }
        log("Downloaded",url,content.size()/1024,"KB");

        redirect << cacheFile(url);
        for(const string& file: redirect) {
            if(!exists(section(file,'/'),cache)) createFolder(section(file,'/'),cache);
            writeFile(file,content,cache);
        }
        state=Handle; //wait(); return;  //Cache other outstanding requests before handling this one (i.e cache them even if this handler exits/crash)
    }
    if(state==Handle) {
        handler(url,move(content));
        state=Done;
        free(this);
        return;
    }
}

void getURL(const URL &url, function<void(const URL&, array<byte>&&)> handler, int maximumAge) {
    string file = cacheFile(url);
    array<string> headers;
    if(url.authorization) headers<< "Authorization: Basic "_+url.authorization;
    // Check if cached
    if(exists(file,cache)) {
        long modified = modifiedTime(file,cache);
        if(currentTime()-modified < maximumAge*60) {
            debug( log("Cached",url); )
            array<byte> content = readFile(file,cache);
            assert(content,file);
            handler(url,move(content));
            return;
        }
        headers<< "If-Modified-Since: "_+str(date(modified),"ddd, dd MMM yyyy hh:mm:ss TZD"_);
    }
    alloc<HTTP>(url,handler,move(headers));
}
