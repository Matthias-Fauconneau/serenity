#include "xml.h"
#include "string.h"
#include "utf8.h"


static Element parse(const ref<byte>& document, bool html) {
    assert(document);
    TextStream s=TextStream::byReference(document);
    s.match("\xEF\xBB\xBF"_); //spurious BOM
    Element root(""_);
    while(s) {
        s.skip();
        if(s.match("</"_)) warn("Unexpected","</"_+s.until('>')+">"_);
        else if(s.match('<')) root.children << unique<Element>(s,html);
        else warn("Unexpected '",s.until('\n'),"'");
        s.skip();
    }
    return root;
}

Element parseXML(const ref<byte>& document) { return parse(document,false); }
Element parseHTML(const ref<byte>& document) { return parse(document,true); }

Element::Element(TextStream& s, bool html) {
    uint start = s.index;
    if(s.match("!DOCTYPE"_)||s.match("!doctype"_)) { s.until('>'); return; }
    else if(s.match("?xml"_)) { s.until("?>"_); return; }
    else if(s.match("!--"_)) { s.until("-->"_); return; }
    else if(s.match('?')){ log("Unexpected <?",s.until("?>"_),"?>"); return; }
    else name = string(s.identifier()); //TODO: reference
    if(!name) { log(s.slice(0,s.index)); warn("expected tag name got",s.until('\n')); }
    if(html) name=toLower(name);
    s.skip();
    while(!s.match('>')) {
        if(s.match("/>"_)) { s.skip(); return; }
        else if(s.match('/')) s.skip(); //spurious /
        else if(s.match('<')) break; //forgotten >
        string key = string(s.identifier());/*TODO:reference*/ s.skip();
        if(!key) { log("Attribute syntax error"_,s.slice(start,s.index-start),"|"_,s.until('>')); break; }
        if(html) key=toLower(key);
        ref<byte> value;
        if(s.match('=')) {
            s.skip();
            if(s.match('"')) value=s.until('"'); //FIXME: escape
            else if(s.match('\'')) value=s.until('\''); //FIXME: escape
            else { value=s.untilAny(" \t\n>"_); if(s.buffer[s.index-1]=='>') s.index--; }
            s.match("\""_); //duplicate "
        }
        attributes.insertMulti(move(key), move(value));
        s.skip();
    }
    if(html) {
        static array< ref<byte> > voidElements = split("area base br col command embed hr img input keygen link meta param source track wbr"_,' ');
        if(voidElements.contains(name)) return; //HTML tags which are implicity void (i.e not explicitly closed)
        if(name=="style"_||name=="script"_) { //Raw text elements can contain <>
            s.skip();
            //content=simplify(unescape(s.until(string("</"_+name+">"_))));
            //content=unescape(s.until(string("</"_+name+">"_)));
            //removeAll(content,byte('\n'));
            content = s.until(string("</"_+name+">"_));
            s.skip();
            return;
        }
    }
    for(;;) {
        //if(s.available(4)<4) { warn("Expecting","</"_+name+">"_,"got EOF"); return; } //warn unclosed tag
        if(s.available(4)<4) {  return; } //ignore unclosed tag
        if(s.match("<![CDATA["_)) {
            ref<byte> content= s.until("]]>"_);
            if(content) children << Element(content);
        }
        else if(s.match("<!--"_)) { s.until("-->"_); }
        else if(s.match("</"_)) { if(name==s.until(">"_)) break; } //ignore
        else if(s.match(string("<?"_+name+">"_))) { log("Invalid tag","<?"_+name+">"_); return; }
        else if(s.match('<')) children << Element(s,html);
        else {
            //string content=simplify(trim(unescape(s.until("<"_)))); s.index--;
            //string content=unescape(trim(s.until("<"_))); s.index--;
            ref<byte> content = trim(s.whileNot('<'));
            if(content) children << Element(content);
        }
    }
}

ref<byte> Element::at(const ref<byte>& attribute) const {
    assert(attributes.contains(string(attribute)),"attribute", attribute,"not found in",*this);
    return attributes.at(string(attribute));
}

ref<byte> Element::operator[](const ref<byte>& attribute) const {
    if(!attributes.contains(string(attribute))) return ""_;
    return attributes.at(string(attribute));
}

const Element& Element::operator()(const ref<byte>& name) const {
    for(const Element& e: children) if(e.name==name) return e;
    error("children"_, name, "not found in"_, *this);
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

string Element::str(const ref<byte>& prefix) const {
    if(!name&&!trim(content)&&!children) return string();
    string line; line<< prefix;
    if(name||attributes) line << "<"_+name;
    for(auto attr: attributes) line << " "_+attr.key+"=\""_+attr.value+"\""_;
    if(content||children) {
        if(name||attributes) line << ">\n"_;
        if(content) { assert(!children);
            line<< "'"_+content+"'"_;
            //line << ::str<array<ref<byte>>>(split(content,'\n'),string("\n"_+prefix))+"\n"_;
        }
        if(children) for(const Element& e: children) line << e.str(string(prefix+" "_));
        if(name||attributes) line << prefix+"</"_+name+">\n"_;
    } else if(name||attributes) line << "/>\n"_;
    return line;
}
string str(const Element& e) { return e.str(); }

string unescape(const ref<byte>& xml) {
    static map< ref<byte>, ref<byte> > entities;
    if(!entities) {
        array< ref<byte> > kv = split(
"quot \" amp & apos ' lt < gt > nbsp \xA0 copy © reg ® trade ™ laquo « raquo » rsquo ’ oelig œ hellip … ndash – not ¬ mdash — "
"euro € lsaquo ‹ rsaquo › ldquo “ rdquo ” larr ← uarr ↑ rarr → darr ↓ ouml ö oslash ø eacute é infin ∞ deg ° middot · bull • "
"agrave à acirc â egrave è ocirc ô ecirc ê szlig ß"_,' ');
        assert(kv.size()%2==0,kv.size());
        entities.reserve(kv.size()/2);
        for(uint i=0;i<kv.size();i+=2) entities.insert(move(kv[i]), move(kv[i+1]));
    }
    string out;
    TextStream s=TextStream::byReference(xml);
    while(s) {
        out << s.until("&"_);
        if(!s) break;

        if(s.match("#x"_)) out << utf8(toInteger(toLower(s.until(";"_)),16));
        else if(s.match('#')) out << utf8(toInteger(s.until(";"_)));
        else {
            ref<byte> key = s.word();
            if(s.match(';')) {
                ref<byte>* c = entities.find(key);
                if(c) out<<*c; else warn("Unknown entity",key);
            }
            else out<<"&"_; //unescaped &
        }
    }
    return out;
}
