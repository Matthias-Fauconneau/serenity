#include "xml.h"
#include "string.h"
#include "utf8.h"

static Element parse(string document, bool html) {
 TextData s (document);
 s.match("\xEF\xBB\xBF"_); //spurious BOM
 Element root;
 while(s) {
  if(s.match("</"_)) log("Unexpected","</"_+s.until('>')+">"_);
  else if(s.match('<')) root.children.append( unique<Element>(s, html) );
  else error(str(s.lineIndex)+": Expected end of file, got", escape(s.line()));
  s.whileAny(" \r\t\n");
 }
 return root;
}

Element parseXML(string document) { return parse(document,false); }

Element::Element(TextData& s, bool html) {
 size_t begin = s.index;
 if(s.match("!DOCTYPE")||s.match("!doctype")) { s.until('>'); return; }
 else if(s.match("?xml")) { s.until("?>"); return; }
 else if(s.match("!--")) { s.until("-->"); return; }
 else if(s.match('?')){ log("Unexpected <?",s.until("?>"),"?>"); return; }
 else name = s.identifier("_-:");
 if(!name) { log(s.slice(0,s.index)); log("Expected tag name got",s.line()); }
 s.whileAny(" \t\n");
 while(!s.match('>')) {
  if(s.match("/>")) return;
  else if(s.match('/')) {} //spurious /
  else if(s.match('<')) break; // forgotten >
  s.whileAny(" \t\n");
  string key = s.identifier("_-:"); /*TODO:reference*/
  if(!key) { log("Attribute syntax error"_,s.slice(begin,s.index-begin),"|"_,s.until('>')); s.until('>'); break; }
  string value;
  if(s.match('=')) {
   if(s.match('"')) value=s.until('"');
   else if(s.match('\'')) value=s.until('\'');
   else { value=s.untilAny(" \t\n>"); if(s.slice(s.index-1,1)==">") s.index--; }
  }
  attributes.insertMulti(key, value);
  s.whileAny(" \t\n");
 }
 for(;;) {
  //if(s.available(4)<4) return; // Ignores unclosed tag
  if(s.match("<![CDATA["_)) {
   string content = s.until("]]>");
   if(content) children.append( unique<Element>(content) );
  }
  else if(s.match("<!--"_)) s.until("-->"_);
  else if(s.match("</"_)) { if(name==s.until(">"_)) break; } // FIXME: check correct closing tag
  //else if(s.match(String("<?"_+name+">"_))) { log("Invalid tag","<?"_+name+">"_); return; }
  else if(s.match('<')) children.append( unique<Element>(s,html) );
  else {
   assert_(!content);
   content = trim(s.whileNot('<'));
  }
 }
}

string Element::attribute(string attribute) const {
 assert_(attributes.contains(attribute),"attribute", attribute,"not found"/*,"in",*this*/);
 return attributes.at(attribute);
}

string Element::operator[](string attribute) const {
 if(!attributes.contains(attribute)) return ""_;
 return attributes.at(attribute);
}

bool Element::contains(string name) const {
 for(const Element& e: children) if(e.name==name) return true;
 return false;
}

const Element& Element::child(string name) const {
 const Element* element = 0;
 for(const Element& e: children) if(e.name==name) {
  if(element) log("Multiple match for", name/*, "in", *this*/);
  element=&e;
 }
 assert_(element, "No such element", name);
 static Element empty;
 return element ? *element : empty;
}
const Element& Element::operator()(string name) const { return child(name); }

void Element::visit(const function<void(const Element&)>& visitor) const {
 for(const Element& e: children) e.visit(visitor);
 visitor(*this);
}

void Element::mayVisit(const function<bool(const Element&)>& visitor) const {
 if(visitor(*this)) for(const Element& e: children) e.mayVisit(visitor);
}

void Element::xpath(string path, const function<void(const Element &)>& visitor) const {
 assert(path);
 if(startsWith(path,"//"_)) {
  string next = path.slice(2);
  visit([&next,&visitor](const Element& e)->void{ e.xpath(next,visitor); });
 }
 string first = section(path,'/');
 string next = section(path,'/',1,-1);
 if(next) { for(const Element& e: children) if(e.name==first) e.xpath(next,visitor); }
 else { for(const Element& e: children) if(e.name==first) visitor(e); }
}

String Element::text() const { array<char> text; visit([&text](const Element& e){ text.append(e.content); }); return move(text); }

String Element::text(string path) const {
 array<char> text;
 xpath(path, [&text](const Element& e) { text.append( e.text() ); });
 return move(text);
}

String Element::str(uint depth) const {
 //assert(name||content, attributes, children);
 array<char> line;
 String indent = repeat(" "_, depth);
 if(name) line.append(indent+'<'+name);
 for(auto attr: attributes) line.append(' '+attr.key+"=\""+attr.value+'"');
 if(trim(content)||children) {
  if(name) line.append(">");
  if(trim(content)) line.append(replace(/*simplify(trim(content))*/content,"\n",'\n'+indent));
  if(children) {
   line.append('\n');
   for(const Element& e: children) line.append( e.str(depth+1) );
   line.append(indent);
  }
  if(name) line.append("</"_+name+">\n");
 } else if(name) line.append(" />\n");
 return move(line);
}
String str(const Element& e) { return e.str(); }

String unescape(string xml) {
 static map<string, string> entities;
 if(!entities) {
  array<string> kv = split(
     "quot \" acute ´ amp & apos ' lt < gt > nbsp \xA0 copy © euro € reg ® trade ™ "
     "lsaquo ‹ rsaquo › ldquo “ rdquo ” laquo « raquo » rsquo ’ hellip … ndash – not ¬ mdash — "
     "larr ← uarr ↑ rarr → darr ↓ infin ∞ deg ° middot · bull • "
     "aacute á Aacute Á agrave à Agrave À acirc â ccedil ç Ccedil Ç eacute é Eacute É egrave è Egrave È ecirc ê euml ë "
     "ocirc ô ouml ö oslash ø oelig œ iacute í icirc î Icirc Î iuml ï ugrave ù ucirc û szlig ß yen ¥"_," ");
  assert(kv.size%2==0,kv.size);
  entities.reserve(kv.size/2);
  for(uint i=0;i<kv.size;i+=2) entities.insert(move(kv[i]), move(kv[i+1]));
 }
 array<char> out;
 for(TextData s(xml);s;) {
  out.append( s.until('&') );
  if(!s) break;

  if(s.match("#x"_)) out.append( utf8(parseInteger(toLower(s.until(";"_)),16)) );
  else if(s.match('#')) out.append( utf8(parseInteger(s.until(";"_))) );
  else {
   string key = s.word();
   if(s.match(';')) {
    string* c = entities.find(key);
    if(c) out.append( *c ); else { log("Unknown entity",key); out.append( key ); }
   }
   else out.append( "&"_ ); //unescaped &
  }
 }
 return move(out);
}
