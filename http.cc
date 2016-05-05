#include "http.h"
#include "file.h"
#include "time.h"
#include "thread.h"
#include "map.h"
#include "thread.h"
#include "string.h"
#include <sys/socket.h>
#include <fcntl.h>

// Sockets

struct sockaddress { uint16 family; uint16 port; uint host; int pad[2]; };

struct UDPSocket : Socket {
 UDPSocket(uint host, uint16 port) : Socket(PF_INET,SOCK_DGRAM) {
  sockaddress addr = {PF_INET, big16(port), host, {0,0}}; check(::connect(fd, (const sockaddr*)&addr, sizeof(addr)));
 }
};

TCPSocket::TCPSocket(uint host, uint16 port) { connect(host, port); }
void TCPSocket::connect(uint host, uint16 port) {
 assert_(!fd);
 fd = check(socket(PF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0));
 if(host==uint(-1)) { close(); return; }
 sockaddress addr = {PF_INET, big16(port), host, {0,0}}; ::connect(Socket::fd, (const sockaddr*)&addr, sizeof(addr));
 fcntl(Socket::fd, F_SETFL, 0);
}

#include <openssl/ssl.h> // ssl
SSLSocket::SSLSocket(uint host, uint16 port, bool secure) { connect(host, port, secure); }
SSLSocket::~SSLSocket() { disconnect(); }

void SSLSocket::connect(uint host, uint16 port, bool secure) {
 TCPSocket::connect(host,port);
 if(secure && fd) {
  static auto* ctx=(SSL_library_init(), SSL_CTX_new(TLSv1_client_method()));
  ssl = SSL_new(ctx);
  SSL_set_fd(ssl, fd);
  SSL_connect(ssl);
 }
}
/// Reads up to \a size bytes into \a buffer
int64 SSLSocket::readUpTo(mref<byte> target) {
 if(ssl) return SSL_read(ssl, target.begin(), target.size);
 else return TCPSocket::readUpTo(target);
}
buffer<byte> SSLSocket::readUpTo(int64 capacity) {
 assert_(capacity > 0);
 ::buffer<byte> buffer(capacity, 0);
 int64 size = readUpTo(mref<byte>(buffer.begin(), capacity));
 if(size<0) error("SSL error", size);
 buffer.size = size;
 return buffer;
}
void SSLSocket::write(const ref<byte>& buffer) {
 if(!ssl) { TCPSocket::write(buffer); return; }
 int unused size=SSL_write(ssl,buffer.data,buffer.size); assert(size==(int)buffer.size);
}
void SSLSocket::disconnect() { if(ssl) { SSL_shutdown(ssl); ssl=0; } close(); }


template<class T> size_t DataStream<T>::available(size_t need) {
 while((int64)need>(int64)Data::available(need) && T::poll(1000)) {
  ::buffer<byte> chunk = T::readUpTo(max(4096ll, int64(need)-(int64)Data::available(need)));
  if(!chunk) { error("Empty chunk: already buffered ", Data::available(need), "but need", need); break; }
  buffer.append( chunk ); data=buffer;
 }
 return Data::available(need);
}

// Cache
static const Folder& cache() { static Folder cache(".cache", home()); return cache; }

// DNS

uint ip(TextData& s) { int a=s.integer(), b=(s.match('.'),s.integer()), c=(s.match('.'),s.integer()), d=(s.match('.'),s.integer()); return (d<<24)|(c<<16)|(b<<8)|a; }
uint nameserver() { static uint ip = ({ auto data = readFile("/etc/resolv.conf"_); TextData s (data); s.until("nameserver "_); ::ip(s); }); return ip; }
uint resolve(const ref<byte>& host) {
 static File dnsCache("dns"_, cache(), Flags(ReadWrite|Create|Append));
 static Map dnsMap (dnsCache);
 uint ip=-1;
 for(TextData s(dnsMap);s;s.line()) { if(s.match(host)) { s.match(' '); ip=::ip(s); break; } } //TODO: binary search (on fixed length lines)
 bool negativeEntry=false; if(!ip) ip=-1, negativeEntry=true; //return false; //try to resolve negative entries again
 if(ip==uint(-1)) {
  static UDPSocket dns(nameserver(), 53);
  array<byte> query;
  struct Header { uint16 id=big16(currentTime()); uint16 flags=1; uint16 qd=big16(1), an=0, ns=0, ar=0; } packed header;
  query.append( raw(header) );
  for(TextData s(host);s;) { //QNAME
   ref<byte> label = s.until('.');
   query.append(label.size);
   query.append(label);
  }
  query.append(ref<byte>{0,0,1,0,1});
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
   String entry = host+" "_+str(uint8(raw(ip)[0]))+"."_+str(uint8(raw(ip)[1]))+"."_+str(uint8(raw(ip)[2]))+"."_+str(uint8(raw(ip)[3]))+"\n"_;
   log_(entry);
   dnsCache.write(entry); //add new entry
   dnsMap = Map(dnsCache); //remap cache
   break;
  }
  if(ip==uint(-1) && !negativeEntry) {
   log("unknown",host);
   dnsCache.write(host+" 0.0.0.0\n"_); // add negative entry
   dnsMap = Map(dnsCache); //remap cache
  }
 }
 return ip;
}

// URL

String base64(ref<byte> input) {
 String output(input.size*4/3+1, 0);
 for(uint j=0;j<input.size;) {
  byte block[3]={}; uint i=0; while(i<3 && j<input.size) block[i++] = input[j++];
  const char cb64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  output.append(ref<byte>{cb64[ block[0] >> 2 ], cb64[ ((block[0] & 0x03) << 4) | ((block[1] & 0xf0) >> 4) ],
                          (i > 1 ? cb64[ ((block[1] & 0x0f) << 2) | ((block[2] & 0xc0) >> 6) ] : '='), (i > 2 ? cb64[ block[2] & 0x3f ] : '=')});
 }
 return output;
}
URL::URL(string url) {
 if(!url) { log("Empty URL"); return; }
 TextData s(url);
 if(find(url,"://"_)) scheme = copyRef(s.until("://"_));
 else s.match("//"_); //net_path
 ref<byte> domain = s.whileNo("/?"_);
 host = copyRef(section(domain,'@',-2,-1));
 if(!host.contains('.')) path=move(host);
 if(domain.contains('@')) authorization = base64(section(domain,'@'));
 path= path+s.until('#');
 fragment = copyRef(s.untilEnd());
}
URL URL::relative(URL&& url) const {
 if(!url.scheme) {
  if(!url.path && url.host) url.path=move(url.host);
  if(!url.host && !startsWith(url.path,"/"_)) url.path = section(path,'/',0,-2)+"/"_+url.path;
  url.scheme=copy(scheme);
 }
 if(!url.host) url.host=copy(host);
 if(startsWith(url.path,"."_)) url.path=copyRef(url.path.slice(1));
 assert(!host.contains('/'));
 return move(url);
}
String str(const URL& url) {
 return url.scheme+(url.scheme?"://"_:""_)+url.authorization+(url.authorization?"@"_:""_)+url.host+url.path+(url.fragment?"#"_:""_)+url.fragment;
}

String cacheFile(const URL& url) {
 String name = replace(url.path,"/"_,"."_);
 if(startsWith(name,"."_)) name=copyRef(name.slice(1));
 if(!name) name="index.htm"__;
 assert(url.host && !url.host.contains('/'));
 //assert_((url.host+"/"_+name).size() < 256);
 return url.host+"/"_+name;
}

// HTTP

array<unique<HTTP>> requests; // weakly referenced by Thread::array<Poll*>

HTTP::HTTP(URL&& url, function<void(const URL&, Map&&)> contentAvailable, array<String>&& headers)
 : DataStream<SSLSocket>(resolve(url.host),url.scheme=="https"_?443:80,url.scheme=="https"_), Poll(Socket::fd,POLLOUT),
   url(move(url)), headers(move(headers)), contentAvailable(contentAvailable) {
 if(!Socket::fd) { error("Unknown host",this->url.host); done(); return; }
 registerPoll();
}

void HTTP::request() {
 String request = (url.post?"POST"_:"GET"_)+" "_+(startsWith(url.path,"/"_)?""_:"/"_)+url.path+" HTTP/1.1\r\nHost: "_+url.host+"\r\n"
   ;//"User-Agent: Serenity\r\n"_;
 if(url.post) {
  headers.append("Content-Type: application/x-www-form-urlencoded"__);
  headers.append("Content-Length: "+str(url.post.size));
 }
 for(string header: headers) request=request+header+"\r\n"_;
 write(request+"\r\n"_+url.post); state=Header;
}

void HTTP::header() {
 String cacheFileName = cacheFile(url);
 // Status
 if(!match("HTTP/1.1 "_)&&!match("HTTP/1.0 "_)) { log("No HTTP", url, data); done(); return; }
 int status = parseInteger(until(" "_));
 until("\r\n"_);
 if(status==200||status==301||status==302) {}
 else if(status==304) { //Not Modified
  assert(existsFile(cacheFileName, ::cache()));
  touchFile(cacheFileName, ::cache(), true);
  log("Not Modified", url);
  state = Available;
  return;
 }
 else if(status==400) { error("Bad Request"_, url, data); state = BadRequest; return; } // Caches reply anyway to avoid repeating bad requests
 else if(status==403) { log("Denied"_, url, data); state = Denied; return; }
 else if(status==404) log("Not Found"_, url);
 else if(status==408) log("Request timeout"_);
 else if(status==500) log("Internal Server Error"_, untilEnd());
 else if(status==502) log("Bad gateway"_);
 else if(status==504) log("Gateway timeout"_);
 else { log("Unknown status",status,"from", url); done(); return; }

 // Headers
 while(!match("\r\n"_)) {
  ref<byte> key = until(": "_); assert(key,buffer);
  ref<byte> value = until("\r\n"_);
  if(key=="Content-Length"_) contentLength=parseInteger(value);
  else if((key=="Transfer-Encoding"_||key=="transfer-encoding") && value=="chunked"_) chunked=true;
  else if((key=="Location"_ && status!=200) || key=="Refresh"_) {
   if(startsWith(value,"0;URL="_)) value=value.slice(6);
   log(status, key);
   URL url = this->url.relative(value);
   log(url);
   if(url.scheme != this->url.scheme || url.host != this->url.host) {
    uint ip = resolve(url.host);
    if(ip==uint(-1)) { log("Unknown host", url); done(); return; }
    log("Redirect", url.scheme, url.host);
    this->url = ::move(url);
    //state = Redirect; return;
    disconnect();
    connect(ip, url.scheme=="https"_?443:80, url.scheme=="https"_);
    Poll::fd = SSLSocket::fd;
   }
   data = {};
   index = 0;
   buffer = array<byte>();
   contentLength=0;
   chunked=0;
   if(cacheFileName) redirect.append(move(cacheFileName));
   state=Request; events=POLLOUT;
   return;
  }
 }
 state = Content;
}

void HTTP::receiveContent() {
 if(!content) assert_(cacheFile(url).size < 256); // Aborts on long names before starting content download
 if(chunked) {
  do {
   if(chunkSize==invalid) { // Parses chunk size if not already known
     chunkSize = integer(false, 16); match("\r\n"_);
     if(chunkSize==0) { state=Cache; assert_(content); break; } // Last chunk has no end marker?
   }
   if(chunkSize) { // Packet breaks within chunk
    size_t size = min(chunkSize, available(chunkSize));
    content.append( Data::read(size) ); // Already reads as much as possible into user space as chunk might be larger than kernel buffer
    chunkSize -= size;
    if(chunkSize) return; // Needs full chunk before end marker
   }
   if(available(2) < 2) return; // Waits for end marker
   match("\r\n"_); // Consumes chunk end marker
   chunkSize = invalid;
  } while(Data::available(3)>=3); // Parses any following chunk
  assert_(content);
 } else if(contentLength) {
  if(!content.capacity) {
   content = array<byte>(contentLength);
   if(Data::available(0)) content.append(Data::read(Data::available(0))); // Appends any content send directly with the header packet (reading directly into separate content buffer as no parsing is needed)
   if(contentLength > 64*1024) {
    Folder(section(cacheFile(url),'/'), ::cache(), true);
    assert_(!existsFile(cacheFile(url), ::cache()));
    file = File(cacheFile(url), ::cache(), Flags(WriteOnly|Create|Truncate));
    file.write(content);
   }
  }
  if(content.size < contentLength) {
   mref<byte> chunk(content.begin()+content.size, content.capacity-content.size);
   int64 size = readUpTo(chunk);
   if(size<0) error(size);
   chunk.size = size;
   if(file) file.write(chunk);
   content.size += chunk.size;
  }
  if(content.size == contentLength) state=Cache;
 }
 else error("Missing content", chunked, contentLength, buffer); //state=Cache;
}

void HTTP::cache() {
 if(!content) { log("Missing content", buffer); done(); return; }
 if(content.size>64*1024) log("Downloaded",url,content.size/1024,"KB"); else log("Downloaded",url,content.size,"B");
 if(!file) redirect.append(cacheFile(url)); // else already written progressively
 for(string file: redirect) {
  Folder(section(file,'/'), ::cache(), true);
  writeFile(file, content, ::cache(), true);
  log(file);
  assert_(existsFile(file, ::cache()));
 }
}

void HTTP::event() {
 if((revents&POLLHUP) && state <= Content) { log("Connection broken",url); done(); return; }
 if(state == Request) { events=POLLIN; request(); return; }
 if(!(revents&POLLIN) && state <= Content) { log("No data",url,revents,available(1),Data::available(1)); done(); return; }
 if(state == Header) { header(); }
 if(state == Content) { assert_(revents==POLLIN); receiveContent(); }
 if(state == Cache) {
  cache();
  // Caches other outstanding requests before handling this one
  state = Available;
  queue();
  return;
 }
 if(state==Available) { contentAvailable(url, Map(cacheFile(url), ::cache())); done(); return; }
}

void HTTP::done() { state=Handled; /*while(requests.contains(this))*/ if(requests.contains(this)) requests.remove(this); /*else was synchronous request*/ }

// Requests

Map getURL(URL&& url, function<void(const URL&, Map&&)> contentAvailable, int maximumAge, HTTP::State wait) {
 assert(url.host,url);
 String file = cacheFile(url);
 array<String> headers;
 if(url.authorization) headers.append( "Authorization: Basic "_+url.authorization );
 // Check if cached
 if(existsFile(file,cache())) {
  File file (cacheFile(url), cache());
  long modified = file.modifiedTime()/1000000000ull;
  if(currentTime()-modified < maximumAge*60*60) {
   //log("Cached", url);
   if(!contentAvailable) return Map(file);
   contentAvailable(url, Map(file));
   return {};
  }
  headers.append( "If-Modified-Since: "_+str(Date(modified),"ddd, dd MMM yyyy hh:mm:ss TZD"_) );
 }
 for(const unique<HTTP>& request: requests) if(request->url == url) { log("Duplicate request", url); return {}; }
 if(requests.size>5) error("Concurrent request limit", requests.size);
 if(wait == HTTP::Available) {
  HTTP request(move(url),contentAvailable,move(headers));
  while(request.state < wait) { assert_(request.wait(), request.events, request.revents); }
  assert_(existsFile(cacheFile(request.url),cache()), cacheFile(request.url));
  if(!contentAvailable) return Map(cacheFile(request.url),cache());
  contentAvailable(move(request.url), Map(cacheFile(request.url),cache()));
 } else {
  HTTP& request = requests.append( unique<HTTP>(move(url),contentAvailable,move(headers)) );
  while(request.state < wait) { assert_(request.wait(), request.events, request.revents); }
 }
 return {};
}

#if 0
struct ImageRequest {
 ImageRequest(Image* target, function<void()> imageLoaded, int2 size) : target(target), imageLoaded(imageLoaded), size(size) {}

 void load(const URL&, Map&&);

 /// Weak reference to target to load (need to stay valid)
 Image* target=0;
 /// Function to call on load if the image was not cached.
 function<void()> imageLoaded;
 /// Preferred size
 int2 size;
};
static array<unique<ImageRequest>> imageRequests; // weakly referenced by HTTP::contentAvailable

void getImage(URL&& url, Image* target, function<void()> imageLoaded, int2 size, uint maximumAge) {
 imageRequests.append( unique<ImageRequest>(target, imageLoaded, size) );
 getURL(move(url), {imageRequests.last().pointer, &ImageRequest::load}, maximumAge);
}

void ImageRequest::load(const URL&, Map&& file) {
 Image image = decodeImage(file);
 if(!image) return;
 if(size) image = resize(size, image);
 *target = move(image);
 imageLoaded();
 /*while(imageRequest.contains(this))*/ imageRequests.remove(this);
}
#endif
