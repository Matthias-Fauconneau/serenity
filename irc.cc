#include "thread.h"
#include "data.h"
#include "time.h"
#include "http.h"
#include "xml.h"
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

template<class T/*: Stream*/> struct TextDataStream : DataStream<T>, TextData { using DataStream<T>::DataStream; };

struct Link {
 string host;
 uint16 port;
 array<string> channels;
 string bot;
 string number;
 Link(string url) {
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
  //log(host, channels, bot, number);
 }
};

struct DCC : Link {
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
     string name;
     if(s.match('"')) { name = s.until('"'); s.skip(' '); }
     else name = s.until(' ');
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
 DCC(string url) : Link(url) {
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
  String query = join(arguments(), " ");
  String url;
  if(true || arguments().size > 1) {
   String config = readFile(".irc");
   String search = replace(section(config, '\n') ,"%", query);
   buffer<string> bots = split(section(config, '\n', 1, -1), "\n");
   Map document = getURL(search, {}, 1);
   Element root = parseHTML(document);
   const Element& table = root("//table");
   array<string> linkBots;
   for(size_t i=1; i<table.children.size; i+=2) {
    const Element& row = table(i);
    String file = row(0).text();
    if(!(find(file, query) || find(file, replace(query, " ", ".")))) continue;
    string irc = row(0)(0)["href"];
    String size = row(6).text();
    String age = unescape(row(8).text());
    string command = table(i+1)(0)(0)["value"];
    String linkURL = irc+" "+command;
    Link link(linkURL);
    if(bots.contains(link.bot)) {
     log(file, size, age, link.channels, link.bot);
     url = ::move(linkURL);
     break;
    }
    linkBots.append(link.bot);
   }
   if(!url) error(search, linkBots);
  }
  else url = unsafeRef(query);
  log(url);
  for(int i: range(8)) {
   DCC dcc(url);
   if(!dcc.fileName || !dcc.fileSize || !dcc.position) return; // Failed
   if(dcc.position == dcc.fileSize) return; // Completed
   log("Retry", i); // Retry
  }
 }
} fetch;
