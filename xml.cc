#include "xml.h"
#include "string.h"
#include "utf8.h"

static Element parse(const ref<byte>& document, bool html) {
    assert(document);
    TextData s(document);
    s.match("\xEF\xBB\xBF"_); //spurious BOM
    Element root;
    while(s) {
        s.skip();
        if(s.match("</"_)) log("Unexpected","</"_+s.until('>')+">"_);
        else if(s.match('<')) root.children << unique<Element>(s,html);
        else log("Unexpected '",s.line(),"'");
        s.skip();
    }
    return root;
}

Element parseXML(const ref<byte>& document) { return parse(document,false); }
Element parseHTML(const ref<byte>& document) { return parse(document,true); }

Element::Element(TextData& s, bool html) {
    //uint begin = s.index;
    if(s.match("!DOCTYPE"_)||s.match("!doctype"_)) { s.until('>'); return; }
    else if(s.match("?xml"_)) { s.until("?>"_); return; }
    else if(s.match("!--"_)) { s.until("-->"_); return; }
    else if(s.match('?')){ log("Unexpected <?",s.until("?>"_),"?>"); return; }
    else name = string(s.identifier("_-:"_));
    if(!name) { log(s.slice(0,s.index)); log("expected tag name got",s.line()); }
    if(html) name=toLower(name);
    s.skip();
    while(!s.match('>')) {
        if(s.match("/>"_)) { s.skip(); return; }
        else if(s.match('/')) s.skip(); //spurious /
        else if(s.match('<')) break; //forgotten >
        string key = string(s.identifier("_-:"_));/*TODO:reference*/ s.skip();
        if(!key) { /*log("Attribute syntax error"_,s.slice(begin,s.index-begin),"|"_,s.until('>'));*/ s.until('>'); break; }
        if(html) key=toLower(key);
        string value;
        if(s.match('=')) {
            s.skip();
            if(s.match('"')) value=unescape(s.until('"'));
            else if(s.match('\'')) value=unescape(s.until('\''));
            else { value=string(s.untilAny(" \t\n>"_)); if(s.slice(s.index-1,1)==">"_) s.index--; }
            s.match("\""_); //duplicate "
        }
        attributes.insertMulti(move(key), move(value));
        s.skip();
    }
    if(html) {
        static array<ref<byte>> voidElements = split("area base br col command embed hr img input keygen link meta param source track wbr"_,' ');
        if(voidElements.contains(name)) return; //HTML tags which are implicity void (i.e not explicitly closed)
        if(name=="style"_||name=="script"_) { //Raw text elements can contain <>
            s.skip();
            content = string(s.until(string("</"_+name+">"_)));
            s.skip();
            return;
        }
    }
    for(;;) {
        if(s.available(4)<4) return; //ignore unclosed tag
        if(s.match("<![CDATA["_)) {
            string content (s.until("]]>"_));
            if(content) children << Element(move(content));
        }
        else if(s.match("<!--"_)) { s.until("-->"_); }
        else if(s.match("</"_)) { if(name==s.until(">"_)) break; } //ignore
        else if(s.match(string("<?"_+name+">"_))) { log("Invalid tag","<?"_+name+">"_); return; }
        else if(s.match('<')) children << Element(s,html);
        else {
            string content = unescape(s.whileNot('<'));
            if(content) children << Element(move(content));
        }
    }
}

ref<byte> Element::attribute(const ref<byte>& attribute) const {
    assert(attributes.contains(string(attribute)),"attribute", attribute,"not found in",*this);
    return attributes.at(string(attribute));
}

ref<byte> Element::operator[](const ref<byte>& attribute) const {
    if(!attributes.contains(string(attribute))) return ""_;
    return attributes.at(string(attribute));
}

const Element& Element::child(const ref<byte>& name) const {
    for(const Element& e: children) if(e.name==name) return e;
    error("children"_, name, "not found in"_, *this);
}

const Element& Element::operator()(const ref<byte>& name) const {
    for(const Element& e: children) if(e.name==name) return e;
    static Element empty;
    return empty;
}

void Element::visit(const function<void(const Element&)>& visitor) const {
    for(const Element& e: children) e.visit(visitor);
    visitor(*this);
}

void Element::mayVisit(const function<bool(const Element&)>& visitor) const {
    if(visitor(*this)) for(const Element& e: children) e.mayVisit(visitor);
}

void Element::xpath(const ref<byte>& path, const function<void(const Element &)>& visitor) const {
    assert(path);
    if(startsWith(path,"//"_)) {
        ref<byte> next = path.slice(2);
        visit([&next,&visitor](const Element& e)->void{ e.xpath(next,visitor); });
    }
    ref<byte> first = section(path,'/');
    ref<byte> next = section(path,'/',1,-1);
    array<Element> collect;
    if(next) { for(const Element& e: children) if(e.name==first) e.xpath(next,visitor); }
    else { for(const Element& e: children) if(e.name==first) visitor(e); }
}

string Element::text() const { string text; visit([&text](const Element& e){ text<<unescape(e.content); }); return text; }

string Element::text(const ref<byte>& path) const {
    string text;
    xpath(path,[&text](const Element& e){ text<<e.text(); });
    return text;
}

string Element::str(uint depth) const {
    //assert(name||content, attributes, children);
    string line;
    string indent = repeat(" "_,depth);
    if(name) line << indent<<"<"_+name;
    for(auto attr: attributes) line << " "_+attr.key+"=\""_+attr.value+"\""_;
    if(trim(content)||children) {
        if(name) line << ">\n"_;
        if(trim(content)) line<<indent<<replace(simplify(string(trim(content))),"\n"_,string("\n"_+indent))<<"\n"_;
        if(children) for(const Element& e: children) line << e.str(depth+1);
        if(name) line << indent+"</"_+name+">\n"_;
    } else if(name) line << " />\n"_;
    return line;
}
string str(const Element& e) { return e.str(); }

string unescape(const ref<byte>& xml) {
    static map<ref<byte>, ref<byte>> entities;
    if(!entities) {
        array<ref<byte>> kv = split(
"quot \" acute ´ amp & apos ' lt < gt > nbsp \xA0 copy © euro € reg ® trade ™ lsaquo ‹ rsaquo › ldquo “ rdquo ” laquo « raquo » rsquo ’ hellip … ndash – not ¬ mdash — "
"larr ← uarr ↑ rarr → darr ↓ infin ∞ deg ° middot · bull • "
"aacute á Aacute Á agrave à Agrave À acirc â ccedil ç Ccedil Ç eacute é Eacute É egrave è Egrave È ecirc ê euml ë ocirc ô ouml ö oslash ø oelig œ iacute í icirc î Icirc Î iuml ï ugrave ù ucirc û szlig ß yen ¥"_,' ');
        assert(kv.size%2==0,kv.size);
        entities.reserve(kv.size/2);
        for(uint i=0;i<kv.size;i+=2) entities.insert(move(kv[i]), move(kv[i+1]));
    }
    string out;
    for(TextData s(xml);s;) {
        out << s.until('&');
        if(!s) break;

        if(s.match("#x"_)) out << utf8(toInteger(toLower(s.until(";"_)),16));
        else if(s.match('#')) out << utf8(toInteger(s.until(";"_)));
        else {
            ref<byte> key = s.word();
            if(s.match(';')) {
                ref<byte>* c = entities.find(key);
                if(c) out<<*c; else { log("Unknown entity",key); out<<key; }
            }
            else out<<"&"_; //unescaped &
        }
    }
    return out;
}
