#include "thread.h"
#include "http.h"
#include "data.h"
#include "variant.h"

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
 Youtube() {
  array<String> titles;
  for(string path : Folder("/Music").list(Files|Recursive)) {
   string file = section(path,'/',1,-1);
   if(file.contains('.')) file = section(file,'.');
   titles.append(section(path,'/')+" - "+file);
  }

  String scores = readFile("Scores.txt");
  for(string title: split(scores, "\n")) {
   if(titles.contains(title)) continue; // Already downloaded
   log(title);
   Map map = getURL(URL("https://www.googleapis.com/youtube/v3/search?key="_+key+"&q="+replace(title," ","+")+
                        "&part=snippet"));
   Variant root = parseJSON(map);
   for(const Variant& item: root.dict.at("items").list) {
    string itemTitle = item.dict.at("snippet").dict.at("title").data;
    log(itemTitle);
    if(!title.contains('-')) error(title, itemTitle);
    Folder folder(split(title," - ")[0], "/Music"_, true);
    assert_(!existsFile(split(title," - ")[1], folder));
    string id = item.dict.at("id").dict.at("videoId").data;
    Map map = getURL(URL("https://www.youtube.com/watch?v="+id), {}, 0);
    TextData s(map);
    s.until("adaptive_fmts\":\"");
    uint minRate = -1; String minURL;
    for(string format: split(s.until('"'),",")) {
     ::map<string, String> dict;
     TextData s(unescapeU(TextData(format)));
     while(s) {
      string key = s.until('=');
      string value = s.until('&');
      dict.insert(key, unescape(TextData(value)));
     }
     dict.at("url") = unescape(dict.at("url"));
     uint rate = parseInteger(dict.at("bitrate"));
     if(find(dict.at("type"),"audio") && rate < minRate) {
      minRate = rate;
      minURL = dict.take("url");
     }
    }
    {
     log(writeFile(split(title," - ")[1], downloadFile(minURL), folder));
    }
    break;
   }
  }
 }
} app;
