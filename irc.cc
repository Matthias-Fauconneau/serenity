#include "thread.h"
#include "data.h"
#include "time.h"
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

// Sockets

struct sockaddress { uint16 family; uint16 port; uint host; int pad[2]; };

struct UDPSocket : Socket {
 UDPSocket(uint host, uint16 port) : Socket(PF_INET,SOCK_DGRAM) {
  sockaddress addr = {PF_INET, big16(port), host, {0,0}}; check(::connect(fd, (const sockaddr*)&addr, sizeof(addr)));
 }
};
/// TCP socket (POSIX)
struct TCPSocket : Socket {
 /// Connects to \a port on \a host
 TCPSocket(uint host, uint16 port);
 void connect(uint host, uint16 port);
};

TCPSocket::TCPSocket(uint host, uint16 port) { connect(host, port); }
void TCPSocket::connect(uint host, uint16 port) {
 assert_(!fd);
 fd = check(socket(PF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0));
 if(host==uint(-1)) { close(); return; }
 sockaddress addr = {PF_INET, big16(port), host, {0,0}}; connect(Socket::fd, (const sockaddr*)&addr, sizeof(addr));
 fcntl(Socket::fd, F_SETFL, 0);
}

/// Implements Data::available using Stream::readUpTo
template<class T/*: Stream*/> struct DataStream : T, virtual Data {
 using T::T;
 /// Feeds Data buffer using T::readUpTo
 size_t available(size_t need) override;
};

template<class T> size_t DataStream<T>::available(size_t need) {
 while(need>Data::available(need) && T::poll(-1)) {
  ::buffer<byte> chunk = T::readUpTo(max(4096ul, need-Data::available(need)));
  if(!chunk) { error("Empty chunk: already buffered ", Data::available(need), "but need", need); break; }
  buffer.append( chunk ); data=buffer;
 }
 return Data::available(need);
}

// Cache
const Folder& cache() { static Folder cache(".cache", currentWorkingDirectory()); return cache; }

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
  if(!dns.poll(2000)){log("DNS query timed out, retrying... "); dns.write(query); if(!dns.poll(2000)){log("giving up"); return -1; }}
  BinaryData s(dns.readUpTo(4096), true);
  header = s.read<Header>();
  for(int i=0;i<big16(header.qd);i++) { for(uint8 n;(n=s.read());) s.advance(n); s.advance(4); } // Skips any query headers
  for(int i=0;i<big16(header.an);i++) {
   for(uint8 n;(n=s.read());) { if(n>=0xC0) { s.advance(1); break; } s.advance(n); } // Skips name
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

template<class T/*: Stream*/> struct TextDataStream : DataStream<T>, TextData { using DataStream<T>::DataStream; };

struct URL {
 string host;
 uint16 port;
 array<string> channels;
 string bot;
 string number;
 URL(string url) {
  TextData s (url);
  s.skip("irc://");
  host = s.until(':');
  port = s.integer();
  s.match('/');
  do {
   channels.append(s.identifier("-_.[]"));
  } while(s.match(','));
  s.whileAny(' ');
  if(s.match("/msg ")) {
   bot = s.until(' ');
   s.skip("xdcc send #");
  } else {
   s.skip("/");
   bot = s.until('#');
  }
  number = s.whileInteger();
  assert_(!s, s.untilEnd(), s);
  log(host, channels, bot, number);
 }
};

struct DCC : URL {
 TextDataStream<TCPSocket> irc{resolve(host), port};
 String nick = copyRef("Guest0"_);
 String fileName;
 size_t position;
 size_t fileSize;
 void write(string command) {
  irc.write(command);
  log_(">"+command);
 }
 bool listen(int stage) {
  for(;;) {
   string prefix;
   if(irc.match(':')) {
    prefix = irc.until(' ');
   }
   string command = irc.until(' ');
   string params = irc.until("\r\n"_);
   if(     command=="NOTICE" ||
           command=="433" /*Nick already exists*/ ||

           command=="001" /*Welcome*/ ||
           command=="002" /*Host*/ ||
           command=="003" /*Age*/ ||
           command=="004" /*Name Version*/ ||
           command=="005" /*Limits*/ ||
           command=="251" /*Users*/ ||
           command=="252" /*Operators*/ ||
           command=="254" /*Channels*/ ||
           command=="265" /*Local users*/ ||
           command=="266" /*Global users*/ ||
           command=="375" /*Message of the Day Start*/ ||
           command=="372" /*Message of the Day */ ||
           command=="376" /*Message of the Day End*/ ||

           command=="422" /*?*/ ||
           command=="439" /*?*/ ||

           command=="MODE" ||
           command=="JOIN" ||

           command=="332" /*Topic*/ ||
           command=="333" /*?*/ ||
           command=="353" /*Names*/ ||
           command=="366" /*Names End*/ ||

           command=="253" /*Unknown connections*/ ||

           command=="PRIVMSG" ||
           command=="PART" ||
           command=="QUIT" ||
           command=="NICK" ||
           command=="PING" ||
           0) {
    /**/ if(command=="PING") {
     log(prefix, command, params);
     write("PONG "+params+"\r\n"_);
    }
    else if(command=="PRIVMSG" && startsWith(prefix, bot) && startsWith(params, nick)) {
     log(prefix, command, params);
     TextData s (params);
     s.skip(nick);
     s.skip(" :\x01");
     if(stage==2) s.skip("DCC SEND ");
     else {
      if(s.match("DCC SEND ")) {
       continue;
       log("Repeated DCC SEND, Expecting DCC Accept");
      }
      s.skip("DCC ACCEPT ");
     }
     string name = s.until(' ');
     this->fileName = copyRef(name);
     uint ip = stage==2 ? big32(parseInteger(s.until(' '))) : stage;
     uint16 port = parseInteger(s.until(' '));
     size_t partSize = parseInteger(s.until(' '));
     if(stage == 2) fileSize = partSize; // else partSize | position ?
     log(name, str(uint8(raw(ip)[0]))+"."_+str(uint8(raw(ip)[1]))+"."_+
       str(uint8(raw(ip)[2]))+"."_+str(uint8(raw(ip)[3])),
       port, fileSize);
     File file (name, currentWorkingDirectory(), Flags(WriteOnly|Create|Append));
     position = file.size();
     if(stage == 2 && position) {
      assert_(position <= fileSize);
      if(position == fileSize) { log("Completed"); return true; }
      write(":"+nick+" PRIVMSG "_+bot+" :\x01""DCC RESUME "_+name+" "_+str(port)+" "_+str(position)+"\x01\r\n"_);
      return listen(ip);
     }
     assert(position+partSize == fileSize);
     TCPSocket dcc (ip, port);
     int64 startTime = realTime(), lastTime = startTime;
     size_t readSinceLastTime = 0;
     while(position<fileSize) {
      if(!dcc.poll(10000)) {
       log("10s");
       if(!dcc.poll(10000)) {
        log("20s");
        return false;
       }
      }
      buffer<byte> chunk = dcc.readUpTo(min(1ul<<21,fileSize-position));
      if(!chunk) {
       log("!chunk");
       return false;
      }
      position += chunk.size;
      readSinceLastTime += chunk.size;
      if(realTime() > lastTime+second) {
       log(100*position/fileSize, readSinceLastTime*second/(realTime()-lastTime)/1024, (fileSize-position)*(realTime()-startTime)/position/second/60);
       lastTime = realTime();
       readSinceLastTime = 0;
      }
      file.write(chunk);
     }
     assert_(position == fileSize);
     log(name, fileSize);
     return true;
    }
    else if(stage==0 && command=="MODE") {
     log(prefix, command, params);
     return true;
    }
    else if(stage==0 && command=="433" ) { // Nick name already in use
     log(prefix, command, params);
     return false;
    }
    else if(stage==1 && command=="366"/*End of names list*/) {
     log(prefix, command, params);
     return true;
    } else if(1 && (
             (command=="PRIVMSG" && startsWith(params,"#") && !startsWith(prefix,bot))
             || (command == "NOTICE" && !startsWith(prefix,bot))
             || (1 && (command == "JOIN" || command == "PART" || command=="QUIT" || command=="MODE" || command=="NICK"
                       || command=="001" || command=="002" || command=="003" || command=="004" || command=="005"
                       || command=="251" || command=="252" || command=="253" || command=="254" || command=="255" || command=="265" || command=="266"
                       || command=="375" || command=="372" || command=="376" || command=="439"
                       || command=="332" || command=="333" || command=="353" || command=="366"))))
    {} //log_(".");
    else log(prefix, command, params);
   } else
    log(prefix, command, params);
  }
 }
 DCC(string url) : URL(url) {
  for(int i=0;i<=9;i++) {
   nick.last() = '0'+i;
   write("NICK "+nick+"\r\n"_);
   write("USER "+nick+" localhost "_+host+" :"+nick+"\r\n"_);
   if(listen(0)) break; // Waits for MODE
   // else NICK already in use
  }
  for(string channel: channels) write("JOIN #"+channel+"\r\n");
  write(":"+nick+" PRIVMSG "_+bot+" :xdcc remove"+"\r\n"_);
  listen(1); // Waits for first JOIN confirmation
  write(":"+nick+" PRIVMSG "_+bot+" :xdcc send #"_+number+"\r\n"_);
  listen(2); // Handles DCC
 }
};

struct DCCApp {
 DCCApp() {
  for(int i: range(7)) {
   DCC dcc(join(arguments(), " "));
   if(!dcc.fileName || !dcc.fileSize || !dcc.position) return; // Failed
   if(dcc.position == dcc.fileSize) return; // Completed
   log("Retry", i); // Retry
  }
 }
} fetch;
