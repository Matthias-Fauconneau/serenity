#include "xml.h"
#include "string.h"
#include "array.cc"

Element parseXML(array<byte>&& document) {
    TextBuffer s(move(document));
    if(s.match("<?xml "_)) s.until("?>"_); s.skip(); //XML declaration
    if(!s.match("<"_)) error(s.until("\n"_));
    return {s};
}

Element::Element(TextBuffer& s) {
    name=s.word(); s.skip();
    while(!s.match(">"_)) {
        if(s.match("/>"_)) { s.skip(); return; }
        string key=s.until("="_);
        if(!s.match("\""_)) error(s.until("\n"_));
        string value=s.until("\""_); //FIXME: escape
        attributes[move(key)]=move(value);
        s.skip();
    }
    s.skip();
    while(!s.match("</"_)) {
        if(s.match("<"_)) { children<<pointer<Element>(Element(s)); }
        else { content = s.until("</"_); break; }
    }
    if(!s.match(name)) error(s.until("\n"_),name);
    if(!s.match(">"_)) error(s.until("\n"_));
    s.skip();
}

string Element::operator[](const string& attribute) const { return copy(attributes.at(move(attribute))); }

array<Element> Element::operator()(const string& path) const { return xpath(path); }

array<Element> Element::xpath(const string& path) const {
    assert(path);
    log(path);
    string first = section(path,'/');
    string next = section(path,'/',1,-1);
    array<Element> collect;
    if(next) for(const auto& e: children) if(e->name==first) collect<<e->xpath(next);
    else for(const auto& e: children) if(e->name==first) collect<<copy(*e);
    return collect;
}


string Element::str(const string& prefix) const {
    string r;
    r << prefix+"<"_+name;
    for(const_pair<string,string> attr: attributes) r << " "_+attr.key+"=\""_+attr.value+"\""_;
    if(content) r<<">"_<< content <<"</"_+name+">\n"_; //content element
    else if(children) {
        r << ">\n"_;
        for(const Element* e: children) r << e->str(prefix+" "_);
        r << prefix+"</"_+name+">\n"_;
    } else r << "/>\n"_;
    return r;
}

template<> Element copy(const Element& o) {
    Element e; e.name=copy(o.name); e.content=copy(o.content); e.attributes=copy(o.attributes); e.children=copy(o.children); return e;
}
template<> string str(const Element& e) { return e.str(); }
