#include "thread.h"
#include "http.h"
#include "data.h"
#include "variant.h"
#include "time.h"
#include "asound.h"
#include "audio.h"
#include "json.h"

static string key = arguments()[0];

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

/// Downloads HTTP URL content to file
/// \note Bypasses HTTP cache
struct HTTPFile : HTTP {
 size_t size = 0;
 File file;
 int64 startTime = 0, lastTime = 0;
 size_t readSinceLastTime = 0;

 HTTPFile() {}
 HTTPFile(URL&& url, File&& file) : HTTP(::move(url)), file(::move(file)) {}

 void receiveContent() override {
  if(!size) { // First chunk
   log(contentLength);
   assert_(contentLength && contentLength < 32*1024*1024, contentLength);
   startTime = realTime(), lastTime = startTime;
   readSinceLastTime = 0;
  }
  if(size < contentLength) {
   ::buffer<byte> chunk(contentLength);
   chunk.size = readUpTo(chunk);
   file.write(chunk);
   readSinceLastTime += chunk.size;
   if(realTime() > lastTime+second) {
    log(100*size/contentLength, readSinceLastTime*second/(realTime()-lastTime)/1024, (contentLength-size)*(realTime()-startTime)/size/second);
    lastTime = realTime();
    readSinceLastTime = 0;
   }
  }
  if(size == contentLength) state=Available;
  chunkReceived();
 }
 void cache() override {}
};

String youtubeURL(string id, bool onlyAudio) {
 Map map = ::getURL(URL("https://www.youtube.com/watch?v="+id), {}, 0);
 TextData s(map);
 s.until("adaptive_fmts\":\"");
 uint maxRate = 0; String bestURL;
 string formats = s.until('"');
 for(string format: split(formats,",")) {
  ::map<string, String> dict;
  TextData s(unescapeU(TextData(format)));
  while(s) {
   string key = s.until('=');
   string value = s.until('&');
   dict.insert(key, unescape(TextData(value)));
  }
  dict.at("url") = unescape(dict.at("url"));
  if(!onlyAudio || find(dict.at("type"),"audio")) {
   uint rate = parseInteger(dict.at("bitrate"));
   if(rate > maxRate) {
    maxRate = rate;
    bestURL = dict.take("url");
   }
  }
 }
 assert_(bestURL, formats);
 return bestURL;
}

#if 1
HTTPFile request(youtubeURL(arguments()[1], true), File(arguments()[0], currentWorkingDirectory(), Flags(WriteOnly|Create|Truncate)));
#else
struct YoutubeMusic {
 HTTPFile request;
 Folder folder; String title;
 unique<FFmpeg> audioFile = nullptr;
 Thread audioThread;
 AudioOutput audio {{this,&YoutubeMusic::read}, audioThread};

 YoutubeMusic() {
  next();
  mainThread.setPriority(-20);
 }
 void next() {
  array<String> titles;
  uint minSize = 1*1024*1024; string smallestFile;
  map<uint, string> byViewCount;
  for(string path : Folder("/Music").list(Files|Recursive)) {
   string file = section(path,'/',1,-1);
   if(file.contains('.')) file = section(file,'.');
   titles.append(section(path,'/')+" - "+file);
   size_t size = File(path, "/Music"_).size();
   if(size <= 16384) {
    log(path);
    ::remove(path, "/Music"_);
   } else if(size < minSize) {
    minSize = size;
    smallestFile = file;
   }
  }
  if(smallestFile) log(smallestFile);

  String scores = readFile("Scores.txt");
  for(string title: split(scores, "\n")) {
   title = trim(title);
   if(!title) continue;
   log(title);
#define DOWNLOAD 0
#if DOWNLOAD
   if(titles.contains(title)) continue; // Already downloaded
#endif

   Map map = getURL(URL("https://www.googleapis.com/youtube/v3/search?key="_+key+"&q="+replace(title," ","+")+
                        "&part=snippet"));
   Variant root = parseJSON(map);
   for(const Variant& item: root.dict.at("items").list) {
    string itemTitle = item.dict.at("snippet").dict.at("title").data;
    if(!title.contains('-')) { log(itemTitle); continue; }

#if DOWNLOAD
    log(itemTitle);
    if(find(itemTitle,"Extended")) continue;
#else
    {
     string id = item.dict.at("id").dict.at("videoId").data;
     Map map = getURL(URL("https://www.googleapis.com/youtube/v3/videos?key="_+key+"&id="+id+
                          "&part=statistics"));
     Variant root = parseJSON(map);
     uint viewCount = parseInteger(root.dict.at("items").list[0].dict.at("statistics").dict.at("viewCount").data);
     assert_(viewCount);
     byViewCount.insertSorted(viewCount, title);
     log(title, itemTitle, viewCount);
    }
    break;
#endif

    Folder folder(split(title," - ")[0], "/Music"_, true);
    assert_(!existsFile(split(title," - ")[1], folder));
    this->title = copyRef(title);
    this->folder = ::move(folder);
    new (&request) HTTPFile(youtubeURL(item.dict.at("id").dict.at("videoId").data, true), File(split(title," - ")[1], folder, Flags(WriteOnly|Create|Truncate)));
    { // Synchronously skips denied requests
     while(request.state < HTTP::Content) {
      if(request.state == HTTP::Denied) { log("Denied"); break; }
      assert_(request.wait());
     }
     if(request.state == HTTP::Denied) { request.state = HTTP::Invalid; log("Denied"); continue; }
     if(request.contentLength >= 32*1024*1024) { request.state = HTTP::Invalid; log(request.contentLength); continue; }
    }
    // Asynchronously receive data (allows concurrent playback)
    request.chunkReceived = {this, &YoutubeMusic::chunkReceived};
    request.contentAvailable = {this, &YoutubeMusic::contentAvailable};
    break;
   }
  }
  for(auto item: byViewCount) log(item.key, item.value);
 }

 void chunkReceived() {
  if(!audioFile) { // Playback while downloading
   audioFile = unique<FFmpeg>(request.file.name());
   audio.start(audioFile->audioFrameRate, 0, 16, 2);
   if(!audioThread) audioThread.spawn();
  }
 }

 void contentAvailable(const URL&, Map&&) {
  next();
  if(audioThread) audioThread.wait();
  audio.stop();
  audioFile = nullptr; // Closes decoder
 }

 size_t read(mref<short2> output) {
  size_t size = audioFile->read16(mcast<int16>(output));
  if(size < output.size) { // Resets
   log(size);
   audioFile = unique<FFmpeg>(request.file.name());
   return audioFile->read16(mcast<int16>(output));
  }
  return size;
 }
} app;
#endif
