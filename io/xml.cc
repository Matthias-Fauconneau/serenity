#include "xml.h"
#include "string.h"
#include "utf8.h"

static Element parse(string document, bool html) {
 TextData s (document);
 s.match("\xEF\xBB\xBF"_); //spurious BOM
 Element root;
 s.whileAny(" \r\t\n");
 while(s) {
  if(s.match("</"_)) log("Unexpected","</"_+s.until('>')+">"_);
  else if(s.match('<')) root.children.append( unique<Element>(s, html) );
  else error(str(s.lineIndex)+": Expected end of file, got '"+escape(s.line())+"'");
  s.whileAny(" \r\t\n");
 }
 return root;
}

Element parseXML(string document) { return parse(document,false); }
Element parseHTML(string document) { return parse(document,true); }

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
  s.whileAny(" \t\r\n");
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
 if(html) {
  static array<string> voidElements = split("area base br col command embed hr img input keygen link meta param source track wbr"_," "_);
  if(voidElements.contains(name)) return; //HTML tags which are implicity void (i.e not explicitly closed)
  if(name=="style"_||name=="script"_) { //Raw text elements can contain <>
   s.whileAny(" \t\n");
   content = s.until("</"_+name+">"_);
   s.whileAny(" \t\n");
   return;
  }
 }
 for(;;) {
  //if(s.available(4)<4) return; // Ignores unclosed tag
  if(s.match("<![CDATA["_)) {
   string content = s.until("]]>");
   if(content) children.append( unique<Element>(content) );
  }
  else if(s.match("<!--"_)) s.until("-->"_);
  else if(s.match("<!"_)) s.until(">"_);
  else if(s.match("</"_)) { if(name==s.until(">"_)) break; } // FIXME: check correct closing tag
  //else if(s.match(String("<?"_+name+">"_))) { log("Invalid tag","<?"_+name+">"_); return; }
  else if(s.match('<')) children.append( unique<Element>(s,html) );
  else {
   string content = s.whileNot('<');
   if(trim(content)) {
    //assert_(!this->content, *this, content); // FIXME:
    this->content = trim(content);
   }
   else if(!content) {
    //log("Unclosed tag"/*, *this*/);
    return;
   }
  }
 }
}

string Element::attribute(string attribute) const {
 assert_(attributes.contains(attribute),"attribute", attribute,"not found in",*this);
 return attributes.at(attribute);
}

string Element::operator[](string attribute) const {
 if(!attributes.contains(attribute)) return ""_;
 return attributes.at(attribute);
}

const Element& Element::operator()(size_t index) const { return children[index]; }

bool Element::contains(string name) const {
 for(const Element& e: children) if( (startsWith(name, "#") && e.attributes.contains("id") && e.attribute("id") == name.slice(1)) ||
                                     (startsWith(name, ".") && e.attributes.contains("class") && split(e.attribute("class")," ").contains(name.slice(1))) ||
                                     e.name==name) return true;
 return false;
}

const Element* Element::child(string path) const {
 const Element* element = 0;
 xpath(path, [&element, path, this](const Element& e)->void{
  if(element) { /*log("Multiple matches for", path, "in", *this);*/ return; }
  element = &e;
 });
 return element;
}
const Element& Element::operator()(string path) const {
    const Element* element = child(path);
    if(element) {
        return *element;
    } else {
        assert_(element, "No such ", path, "in", this->name, apply(children, [](const Element& e) { return e.name; }));
        static Element empty;
        return empty;
    }
}

void Element::visit(const function<void(const Element&)>& visitor) const {
 for(const Element& e: children) e.visit(visitor);
 visitor(*this);
}

void Element::mayVisit(const function<bool(const Element&)>& visitor) const {
 if(visitor(*this)) for(const Element& e: children) e.mayVisit(visitor);
}

bool Element::XPath(string path, const function<bool(const Element &)>& visitor) const {
 assert(path);
 if(startsWith(path,"//"_)) {
  string next = path.slice(2);
  bool stop=false;
  mayVisit([&next,&visitor,&stop](const Element& e)->bool{ if(stop) return false; stop = e.XPath(next,visitor); if(stop) return false; else return true; });
  return stop;
 }
 string current = section(path,'/');
 string next = section(path,'/',1,-1);
 for(const Element& e: children) {
  if(  (startsWith(current, "#") && e.attributes.contains("id") && e.attribute("id") == current.slice(1)) ||
       (startsWith(current, ".") && e.attributes.contains("class") && split(e.attribute("class")," ").contains(current.slice(1))) ||
       e.name==current) {
   if(next) e.XPath(next,visitor); else if(visitor(e)) return true;
  }
 }
 return false;
}
void Element::xpath(string path, const function<void(const Element &)>& visitor) const {
 XPath(path, [&visitor](const Element& e){visitor(e); return false;});
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
  if(trim(content)) line.append(replace(simplify(copyRef(trim(content))),"\n",'\n'+indent));
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
