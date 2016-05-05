#pragma once
#include "variant.h"

static Variant parseJSON(TextData& s) {
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
  else if(c=='/' && s.peek()=='/') {
   s.advance(1);
   u.append('/');
  }
  else u.append(c);
 }
 return move(u);
}
String unescape(string s) { return unescape(TextData(s)); }
