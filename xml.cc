#include "xml.h"
#include "string.h"
#include "array.cc"

Element parseXML(array<byte>&& document) {
    assert(document);
    TextBuffer s(move(document));
    while(s.match("<?xml"_)) { s.until("?>"_); s.skip(); } //XML declarations
    while(s.match("<!--"_)) { s.until("-->"_); s.skip(); }
    if(!s.match("<"_)) error(s.until("\n"_));
    return {s};
}

Element::Element(TextBuffer& s) {
    uint start = s.index;
    name=s.xmlIdentifier();
    if(!name) { log((string)slice(s.buffer,0,s.index)); error("expected tag name got",s.until("\n"_)); }
    s.skip();
    while(!s.match(">"_)) {
        if(s.match("/>"_)) { s.skip(); return; }
        string key=s.until("="_);
        string value;
        if(s.match("\""_)) value=s.until("\""_); //FIXME: escape
        else if(s.match("'"_)) value=s.until("'"_); //FIXME: escape
        else error("bad attribute",s.until("\n"_));
        attributes[move(key)]=move(value);
        s.skip();
    }
    s.skip();
    while(!s.match("</"_)) {
        if(s.match("<![CDATA["_)) { content << s.until("]]>"_); }
        else if(s.match("<"_)) { children<<Element(s); }
        else if(s.match("<!--"_)) { s.until("-->"_); }
        else { content << s.until("</"_); break; }
    }
    if(!s.match(name)) { log((string)slice(s.buffer,start,s.index-start)); error("Expecting", name,"got",s.until(">"_)); }
    if(!s.match(">"_)) { log((string)slice(s.buffer,start,s.index-start)); error("Closed '"_+name+"' expecting >"_,"got",s.until("\n"_)); }
    s.skip();
}

string Element::operator[](const string& attribute) const {
    assert(attributes.contains(attribute),"attribute", attribute,"not found in",*this);
    return copy(attributes.at(move(attribute)));
}

Element Element::operator()(const string& name) const {
    for(const auto& e: children) if(e->name==name) return copy(*e);
    return Element();
}

void Element::xpath(const string& path, std::function<void(const Element&)> visitor) const {
    assert(path);
    string first = section(path,'/');
    string next = section(path,'/',1,-1);
    array<Element> collect;
    if(next) { for(const auto& e: children) if(e->name==first) e->xpath(next,visitor); }
    else { for(const auto& e: children) if(e->name==first) visitor(*e); }
}

array<string> Element::xpath(const string& path, const string& attribute) const {
    array<string> list;
    xpath(path,[&attribute,&list](const Element& e){list<<e[attribute];});
    return list;
}

string Element::str(const string& prefix) const {
    string r;
    r << prefix+"<"_+name;
    for(const_pair<string,string> attr: attributes) r << " "_+attr.key+"=\""_+attr.value+"\""_;
    if(content) r<<">"_<< content <<"</"_+name+">\n"_; //content element
    else if(children) {
        r << ">\n"_;
        for(const auto& e: children) r << e->str(prefix+" "_);
        r << prefix+"</"_+name+">\n"_;
    } else r << "/>\n"_;
    return r;
}

template<> Element copy(const Element& o) {
    Element e; e.name=copy(o.name); e.content=copy(o.content); e.attributes=copy(o.attributes); e.children=copy(o.children); return e;
}
template<> string str(const Element& e) { return e.str(); }
