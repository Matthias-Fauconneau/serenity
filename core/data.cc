#include "data.h"
#include "string.h"
//#include "math.h"

bool Data::wouldMatch(uint8 key) {
 if(available(1) && (uint8)peek() == key) return true;
 else return false;
}
bool Data::wouldMatch(char key) {
 if(available(1) && peek() == key) return true;
 else return false;
}

bool Data::match(uint8 key) {
 if(wouldMatch(key)) { advance(1); return true; }
 else return false;
}
bool Data::match(char key) {
 if(wouldMatch(key)) { advance(1); return true; }
 else return false;
}

bool Data::wouldMatch(const ref<uint8> key) {
 if(available(key.size)>=key.size && peek(key.size) == cast<byte>(key)) return true;
 else return false;
}
bool Data::wouldMatch(const string key) {
 if(available(key.size)>=key.size && peek(key.size) == key) return true;
 else return false;
}

bool Data::match(const ref<uint8> key) {
 if(wouldMatch(key)) { advance(key.size); return true; }
 else return false;
}
string Data::match(const string key) {
 if(wouldMatch(key)) { advance(key.size); return key; }
 else return {};
}

String escape(char c) {
 size_t index = string("\t\r\n"_).indexOf(c);
 return index != invalid ? string("\\"_)+string("trn"_)[index] : string()+c;
}
String escape(string s) {
 String target (s.size, 0);
 for(char c: s) target.append(escape(c));
 return move(target);
}

void Data::skip(const uint8 key) {
 if(!match(key)) error("Expected '"+hex(key)+"', got '"+hex(peek())+'\'');
}
void Data::skip(const char key) {
 if(!match(key)) error("Expected '"+escape(key)+"', got '"+peek()+'\'', peek(min(data.size-index,16ul)));
}

void Data::skip(const ref<uint8> key) {
 if(!match(key)) error("Expected '"+hex(key)+"', got '"+hex(peek(key.size))+'\'');
}
void Data::skip(const string key) {
 if(!match(key)) error("Expected '"+escape(key)+"', got '"+escape(peek(key.size))+'\'', escape(data.slice(index, 128)));
}

ref<byte> BinaryData::whileNot(uint8 key) {
 uint start=index;
 while(available(1) && (uint8)peek() != key) advance(1);
 return slice(start, index-start);
}

TextData::TextData(ref<byte> data) : Data(data) {
 if(data && uint8(data[0]) >= 0x80 && !match("\xEF\xBB\xBF"))
  error("Expected Unicode BOM, got", peek(3), hex(peek(3)));
}

void TextData::advance(size_t step) {
 assert(index+step<=data.size, index, data.size);
 for(uint start=index; index<start+step; index++) if(data[index]=='\n') lineIndex++, columnIndex=1; else columnIndex++;
}

char TextData::wouldMatchAny(const string any) {
 if(!available(1)) return false;
 char c = peek();
 for(char e: any) if(c == e) return c;
 return 0;
}

string TextData::wouldMatchAny(const ref<string> keys) {
 for(string key: keys) if(wouldMatch(key)) return key;
 return ""_;
}


char TextData::matchAny(const string any) {
 char c = wouldMatchAny(any);
 if(c) advance(1);
 return c;
}

string TextData::matchAny(const ref<string> keys) {
 for(string key: keys) if(match(key)) return key;
 return ""_;
}

bool TextData::matchNo(const string any) {
 char c = peek();
 for(char e: any) if(c == e) return false;
 advance(1); return true;
}

string TextData::whileAny(char key) {
 uint start=index; while(match(key)) {} return slice(start, index-start);
}
string TextData::whileAny(const string any) {
 uint start=index; while(matchAny(any)){} return slice(start,index-start);
}

string TextData::whileNot(char key) {
 uint start=index, end;
 for(;;advance(1)) {
  if(!available(1)) { end=index; break; }
  if(peek() == key) { end=index; break; }
 }
 return slice(start, end-start);
}
string TextData::whileNot(const string key) {
 uint start=index, end;
 for(;;advance(1)) {
  if(available(key.size)<key.size) { advance(key.size-1); end=index; break; }
  if(peek(key.size) == key) { end=index; break; }
 }
 return slice(start, end-start);
}

string TextData::whileNo(const string any) {
 uint start=index; while(available(1) && matchNo(any)){} return slice(start,index-start);
}
string TextData::whileNo(const string any, char left, char right) {
 uint start=index; int nest=0;
 while(available(1)) {
  /***/ if(match(left)) nest++;
  else {
   if(peek()==right) { nest--; assert(nest>=0); }
   if(nest) advance(1);
   else if(!matchNo(any)) break;
  }
 }
 return slice(start,index-start);
}

string TextData::until(char key) {
 uint start=index, end;
 for(;;advance(1)) {
  if(!available(1)) { end=index; break; }
  if(peek() == key) { end=index; advance(1); break; }
 }
 return slice(start, end-start);
}

string TextData::until(const string key) {
 uint start=index, end;
 for(;;advance(1)) {
  if(available(key.size)<key.size) { advance(key.size-1); end=index; break; }
  if(peek(key.size) == key) { end=index; advance(key.size); break; }
 }
 return slice(start, end-start);
}

string TextData::untilAny(const string any) {
 uint start=index, end;
 for(;;advance(1)) {
  if(!available(1)) { end=index; break; }
  if(matchAny(any)) { end=index-1; break; }
 }
 return slice(start,end-start);
}

string TextData::line() { return until('\n'); }

string TextData::word(const string special) {
 uint start=index;
 while(available(1)) { char c = peek(); if(!(c>='a'&&c<='z' ) && !(c>='A'&&c<='Z') && !special.contains(c)) break; advance(1); }
 assert(index>=start, line());
 return slice(start,index-start);
}

string TextData::identifier(const string special) {
 uint start=index;
 if(!available(1)) return {};
 char c = peek();
 if(c>='0'&&c<='9') return {};
 while(available(1)) {
  char c = peek();
  if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||special.contains(c))) break;
  advance(1);
 }
 return slice(start,index-start);
}

char TextData::character() {
 char c = next();
 if(c!='\\') return c;
 c = peek();
 int i = string("\'\"nrtbf()\\"_).indexOf(c);
 if(i<0) { /*error("Invalid escape sequence '\\"+str(c)+'\'');*/ return '/'; }
 advance(1);
 return string("\'\"\n\r\t\b\f()\\"_)[i];
}

bool TextData::isInteger(int base) {
 if(!available(1)) return false;
 assert(base==10 || base==16);
 char c = peek(); return (c>='0' && c<='9') || (base==16 && ((c>='a'&&c<='f')||(c>='A'&&c<='F')));
}

string TextData::whileInteger(bool sign, int base) {
 size_t start = index;
 if(sign) matchAny("-+");
 while(isInteger(base)) advance(1);
 return slice(start,index-start);
}

int TextData::integer(bool maySign, int base) {
 assert_(data);
 assert(base==10 || base==16);
 int sign=1;
 if(maySign) { if(match('-')) sign=-1; else match('+'); }
 assert_(isInteger(base), "Expected integer, got '"+/*escape(*/slice(index, min(16ul, data.size-index))/*)*/+"'", line(), data);
 int value=0;
 do {
  char c = peek();
  int n;
  /**/  if(c>='0' && c<='9') n = c-'0';
  //else if(c == '.') { error("Unexpected decimal"_, line()); break; }
  else if(base!=16) break;
  else if(c>='a' && c<='f') n = c+10-'a';
  else if(c>='A' && c<='F') n = c+10-'A';
  else break;
  advance(1);
  value *= base;
  value += n;
 } while(available(1));
 return sign*value;
}

int TextData::mayInteger(int defaultValue) {
 string s = whileInteger(true);
 return s ? TextData(s).integer() : defaultValue;
}

string TextData::whileDecimal() {
 uint start=index;
 matchAny("-+");
 if(!match("∞")) for(bool gotNumber=false, gotDot=false, gotE=false;available(1);) {
  char c = peek();
  /**/ if(c>='0'&&c<='9') { gotNumber=true; advance(1); }
  else if(gotNumber && c=='.') { if(gotDot||gotE) break; gotDot=true; advance(1); }
  else if(gotNumber &&  (c=='e' || c=='E')) {
   if(gotE) break;
   gotE=true;
   advance(1);
   matchAny("-+");
  }
  else break;
 }
 if(index>start) match("µ") || matchAny("m%KM");
 return slice(start,index-start);
}

static inline double exp10(double x) { return __builtin_exp2(__builtin_log2(10)*x); }

double TextData::decimal() {
 if(!available(1)) return __builtin_nan("");
 double sign=1;
 if(match('-')) sign=-1; else match('+');
 double significand=0, decimal=0, eSign=1, exponent=0;
 if(match("∞")) return sign*__builtin_inf();
 if(match("NaN")) return __builtin_nan("");
 assert_(isInteger(), lineIndex, line());
 //if(!isInteger()) return nan;
 for(bool gotDot=false, gotE=false; available(1);) {
  /**/  if(!gotDot && match('.')) gotDot=true;
  else if(!gotE && matchAny("eE")) { gotE=true; if(match('-')) eSign=-1; else match('+'); }
  else {
   char c = peek();
   if(c>='0' && c<='9') {
    int n = c-'0';
    if(gotE) {
     exponent *= 10;
     exponent += n;
    } else {
     significand *= 10;
     significand += n;
     if(gotDot) decimal++;
    }
   } else break;
   advance(1);
  }
 }
 if(match("µ")) exponent-=6;
 else if(match('m')) exponent-=3;
 else if(match('%')) exponent-=2;
 else if(match('K')) exponent+=3;
 else if(match('M')) exponent+=6;
 return sign*significand*exp10(eSign*exponent-decimal);
}
