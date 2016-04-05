#include "thread.h"
#include "http.h"
#include "data.h"
#include "variant.h"
#include "time.h"
#include "asound.h"
#include "audio.h"
#include <unistd.h>

static string key = arguments()[0];

Variant parseJSON(TextData& s) {
 if(s.match("true")) return true;
 if(s.match("false")) return false;
 if(s.match('"')) {
  array<char> data;
  while(!s.match('"')) data.append( s.character() );
  return move(data);
 }
 if(s.match('[')) {
  array<Variant> list;
  s.whileAny(" \t\r\n");
  if(!s.match('[')) for(;;) {
   list.append(parseJSON(s));
   s.whileAny(" \t\r\n");
   if(s.match(']')) break;
   s.skip(',');
   s.whileAny(" \t\r\n");
  }
  return move(list);
 }
 if(s.match("{")) {
  Dict dict;
  s.whileAny(" \t\r\n");
  if(!s.match('}')) for(;;) {
   s.skip('"');
   //if(!s.match('"')) error('"',s);
   string key = s.until('"');
   s.skip(':');
   s.whileAny(" \t\r\n");
   dict.insert(copyRef(key), parseJSON(s));
   s.whileAny(" \t\r\n");
   if(s.match('}')) break;
   //if(!s.match(',')) error(',', s);
   s.skip(',');
   s.whileAny(" \t\r\n");
  }
  return move(dict);
 }
 else {
  assert_(s.isInteger());
  return s.decimal();
 }
}
Variant parseJSON(string buffer) { TextData s (buffer); return parseJSON(s); }

#include "utf8.h"
String unescapeU(TextData s) {
 array<char> u;
 while(s) {
  char c = s.next();
  if(c=='\\') {
   char c = s.peek();
   if(c=='u') {
    s.advance(1);
    u.append(utf8(TextData(s.read(4)).integer(false,16)));
   }
   else error(c);
  } else u.append(c);
 }
 return move(u);
}
String unescape(TextData s) {
 array<char> u;
 while(s) {
  char c = s.next();
  if(c=='%') {
   if(s.peek() == '%') { s.advance(1); u.append('%'); }
   else { u.append(TextData(s.read(2)).integer(false,16)); }
  }
  else if(c=='\\') {
   char c = s.peek();
   {
    int i="\'\"nrtbf()\\"_.indexOf(c);
    assert(i>=0);
    s.advance(1);
    u.append("\'\"\n\r\t\b\f()\\"[i]);
   }
  }
  else u.append(c);
 }
 return move(u);
}
String unescape(string s) { return unescape(TextData(s)); }

struct Youtube {
 unique<HTTP> request = nullptr;
 Folder folder; String title;
 ::buffer<byte> buffer;
 File file;
 int64 startTime, lastTime;
 size_t readSinceLastTime;
 unique<FFmpeg> audioFile = nullptr;
 //AudioOutput audio {{this,&Youtube::read}};

 Youtube() { next(); }
 void next() {
  array<String> titles;
  for(string path : Folder("/Music").list(Files|Recursive)) {
   string file = section(path,'/',1,-1);
   if(file.contains('.')) file = section(file,'.');
   titles.append(section(path,'/')+" - "+file);
  }

  String scores = readFile("Scores.txt");
  for(string title: split(scores, "\n")) {
   title = trim(title);
   if(titles.contains(title)) continue; // Already downloaded
   log(title);
   Map map = getURL(URL("https://www.googleapis.com/youtube/v3/search?key="_+key+"&q="+replace(title," ","+")+
                        "&part=snippet"));
   Variant root = parseJSON(map);
   for(const Variant& item: root.dict.at("items").list) {
    string itemTitle = item.dict.at("snippet").dict.at("title").data;
    if(!title.contains('-')) { log(itemTitle); continue; }
    log(itemTitle);
    Folder folder(split(title," - ")[0], "/Music"_, true);
    assert_(!existsFile(split(title," - ")[1], folder));
    string id = item.dict.at("id").dict.at("videoId").data;
    Map map = getURL(URL("https://www.youtube.com/watch?v="+id), {}, 0);
    TextData s(map);
    s.until("adaptive_fmts\":\"");
    uint minRate = -1; String minURL;
    array<uint> rates;
    for(string format: split(s.until('"'),",")) {
     ::map<string, String> dict;
     TextData s(unescapeU(TextData(format)));
     while(s) {
      string key = s.until('=');
      string value = s.until('&');
      dict.insert(key, unescape(TextData(value)));
     }
     dict.at("url") = unescape(dict.at("url"));
     if(find(dict.at("type"),"audio")) {
      uint rate = parseInteger(dict.at("bitrate"));
      rates.append(rate);
      if(rate > 74343 && rate < minRate) {
       minRate = rate;
       minURL = dict.take("url");
      }
     }
    }
    log(rates, minRate);
    request = unique<HTTP>(URL(minURL));
    request->downloadHandler = {this, &Youtube::receive};
    this->title = copyRef(title);
    this->folder = ::move(folder);
    while(request->state < HTTP::Content) {
     if(request->state == HTTP::Denied) { log("Denied"); break; }
     assert_(request->wait());
    }
    if(request->state == HTTP::Denied) { request = nullptr; log("Denied"); continue; }
    return;
   }
  }
 }

 void receive() {
  if(!buffer) { // First chunk
   log(request->contentLength);
   assert_(request->contentLength && request->contentLength < 16*1024*1024);
   buffer = ::buffer<byte>(request->contentLength, 0);
   file = File(split(title," - ")[1], folder, Flags(WriteOnly|Create|Truncate));
   startTime = realTime(), lastTime = startTime;
   readSinceLastTime = 0;
  }
  if(buffer.size < buffer.capacity) {
   mref<byte> chunk(buffer.begin()+buffer.size, buffer.capacity-buffer.size);
   int64 size = request->readUpTo(chunk);
   if(size<0) error(size);
   chunk.size = size;
   buffer.size += chunk.size;
   file.write(chunk);
   readSinceLastTime += chunk.size;
   if(realTime() > lastTime+second) {
    log(100*buffer.size/buffer.capacity, readSinceLastTime*second/(realTime()-lastTime)/1024, (buffer.capacity-buffer.size)*(realTime()-startTime)/buffer.size/second);
    lastTime = realTime();
    readSinceLastTime = 0;
   }
  }
  if(buffer.size == buffer.capacity) { // Done
   file = File(); // Closes file
   request = nullptr; // Closes socket
   audioFile = nullptr; // Closes decoder
   buffer = ::buffer<byte>(); // Releases buffer
   next();
  } else if(!audioFile) { // Playback while downloading
   audioFile = unique<FFmpeg>(file.name());
   //audio.start()
  }
 }

 /*size_t read(mref<short2> output) {
     assert_(audio.rate == file->audioFrameRate);
     uint readSize = 0;
     for(mref<short2> chunk=output;;) {
         if(!file) return readSize;
         assert(readSize<output.size);
         if(audio.rate != file->audioFrameRate) { queue(); return readSize; } // Returns partial period and schedule restart
         size_t read = file->read16(mcast<int16>(chunk));
         assert(read<=chunk.size, read);
         chunk = chunk.slice(read); readSize += read;
         if(readSize == output.size) { update(file->audioTime/file->audioFrameRate,file->duration/file->audioFrameRate); break; } // Complete chunk
         else next(); // End of file
     }
     if(!lastPeriod) for(uint i: range(output.size)) { // Fades in
         float level = exp(12. * ((float) i / output.size - 1) ); // Linear perceived sound level
         output[i][0] *= level;
         output[i][1] *= level;
     }
     lastPeriod = output;
     return readSize;
 }*/
} app;
