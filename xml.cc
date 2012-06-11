#include "xml.h"
#include "string.h"

#include "array.cc"
Array(Element)
Array(Element*)
ArrayOfCopyable(pointer<Element>)

static Element parse(array<byte>&& document, bool html) {
    assert(document);
    TextBuffer s(move(document));
    s.match("\xEF\xBB\xBF"_); //spurious BOM
    Element root;
    while(s) {
        s.skip();
        if(s.match("</"_)) warn("Unexpected","</"_+s.until(">"_)+">"_);
        else if(s.match("<"_)) root.children << Element(s,html);
        else warn("Unexpected '",s.until("\n"_),"'");
        s.skip();
    }
    return root;
}

Element parseXML(array<byte>&& document) { return parse(move(document),false); }
Element parseHTML(array<byte>&& document) { return parse(move(document),true); }

Element::Element(TextBuffer& s, bool html) {
    uint start = s.index;
    if(s.match("!DOCTYPE"_)||s.match("!doctype"_)) { s.until(">"_); return; }
    else if(s.match("?xml"_)) { s.until("?>"_); return; }
    else if(s.match("!--"_)) { s.until("-->"_); return; }
    else if(s.match("?"_)){ log("Unexpected <?",s.until("?>"_),"?>"); return; }
    else name=s.xmlIdentifier();
    if(!name) { log((string)slice(s.buffer,0,s.index)); warn("expected tag name got",s.until("\n"_)); }
    if(html) name=toLower(name);
    s.skip();
    while(!s.match(">"_)) {
        if(s.match("/>"_)) { s.skip(); return; }
        else if(s.match("/"_)) s.skip(); //spurious /
        else if(s.match("<"_)) break; //forgotten >
        string key=s.xmlIdentifier(); s.skip();
        if(!key) { log("Attribute syntax error",(string)slice(s.buffer,start,s.index-start),"^",s.until(">"_)); break; }
        if(html) key=toLower(key);
        string value;
        if(s.match("="_)) {
            s.skip();
            if(s.match("\""_)) value=s.until("\""_); //FIXME: escape
            else if(s.match("'"_)) value=s.until("'"_); //FIXME: escape
            else { value=s.untilAny(" \t\n>"_); if(s.buffer[s.index-1]=='>') s.index--; }
            s.match("\""_); //duplicate "
        }
        attributes.insert(move(key), move(value));
        s.skip();
    }
    if(html) {
        static array<string> voidElements
                = {"area"_,"base"_,"br"_,"col"_,"command"_,"embed"_,"hr"_,"img"_,"input"_,"keygen"_,"link"_,"meta"_,"param"_,"source"_,"track"_,"wbr"_};
        if(contains(voidElements,name)) return; //HTML tags which are implicity void (i.e not explicitly closed)
        if(name=="style"_||name=="script"_) { //Raw text elements can contain <>
            s.skip();
            content=simplify(unescape(s.until("</"_+name+">"_)));
            s.skip();
            return;
        }
    }
    while(!s.match(string("</"_+name+">"_))) {
        //if(s.available(4)<4) { warn("Expecting","</"_+name+">"_,"got EOF"); return; } //warn unclosed tag
        if(s.available(4)<4) {  return; } //ignore unclosed tag
        if(s.match("<![CDATA["_)) {
            string content=simplify(unescape(s.until("]]>"_)));
            if(content) children << Element(move(content));
        }
        else if(s.match("<!--"_)) { s.until("-->"_); }
        else if(s.match("</"_)) { s.until(">"_); } //ignore
        else if(s.match(string("<?"_+name+">"_))) { log("Invalid tag","<?"_+name+">"_); return; }
        else if(s.match("<"_)) children << Element(s,html);
        else {
            string content=simplify(trim(unescape(s.until("<"_)))); s.index--;
            if(content) children << Element(move(content));
        }
    }
    //if(!s.match(string(name+">"_))) { /*log((string)slice(s.buffer,start,s.index-start),s.index);*/ log("Expecting", name,"got",s.until(">"_)); }
}

string Element::at(const string& attribute) const {
    assert(attributes.contains(attribute),"attribute", attribute,"not found in",*this);
    return copy(attributes.at(attribute));
}


string Element::operator[](const string& attribute) const {
    if(!attributes.contains(attribute)) return ""_;
    return copy(attributes.at(attribute));
}

Element Element::operator()(const string& name) const {
    for(const auto& e: children) if(e->name==name) return copy(*e);
    //return Element();
    error("children", name, "not found in", *this);
}

#if FUNCTIONAL

template struct std::function<void(const Element&)>;
void Element::visit(const std::function<void(const Element&)>& visitor) const {
    for(const auto& e: children) e->visit(visitor);
    visitor(*this);
}

void Element::xpath(const string& path, const std::function<void(const Element &)>& visitor) const {
    assert(path);
    if(startsWith(path,"//"_)) {
        const string& next = slice(path,2);
        visit([&next,&visitor](const Element& e)->void{ e.xpath(next,visitor); });
    }
    string first = section(path,'/');
    string next = section(path,'/',1,-1);
    array<Element> collect;
    if(next) { for(const auto& e: children) if(e->name==first) e->xpath(next,visitor); }
    else { for(const auto& e: children) if(e->name==first) visitor(*e); }
}

bool Element::match(const string& path) const {
    bool match=false;
    xpath(path,[&match](const Element&)->void{ match=true; });
    return match;
}

string Element::text() const { string text; visit([&text](const Element& e)->void{ text<<e.content; }); return text; }

string Element::text(const string& path) const {
    string text;
    xpath(path,[&text](const Element& e){ text<<e.text(); });
    return text;
}
#endif

string Element::str(const string& prefix) const {
    if(!name&&!trim(content)&&!children) return ""_;
    string line; line<< prefix;
    if(name||attributes) line << "<"_+name;
    for(const_pair<string,string> attr: attributes) line << " "_+attr.key+"=\""_+attr.value+"\""_;
    if(content||children) {
        if(name||attributes) line << ">\n"_;
        if(content) { assert(!children); line << join(split(content,'\n'),"\n"_+prefix)+"\n"_; }
        if(children) for(const auto& e: children) line << e->str(prefix+" "_);
        if(name||attributes) line << prefix+"</"_+name+">\n"_;
    } else if(name||attributes) line << "/>\n"_;
    return line;
}

template<> Element copy(const Element& o) {
    Element e; e.name=copy(o.name); e.content=copy(o.content); e.attributes=copy(o.attributes); e.children=copy(o.children); return e;
}
template<> string str(const Element& e) { return e.str(); }

string unescape(const string& xml) {
    static map<string, string> entities;
    if(!entities) {
        array<string> kv = split(
"quot \" amp & apos ' lt < gt > nbsp \xA0 copy © reg ® trade ™ laquo « raquo » rsquo ’ oelig œ hellip … ndash – not ¬ mdash — "
"euro € lsaquo ‹ rsaquo › ldquo “ rdquo ” larr ← uarr ↑ rarr → darr ↓ ouml ö oslash ø eacute é infin ∞ deg ° middot · bull • "
"agrave à acirc â egrave è ocirc ô ecirc ê"_,' ');
        assert(kv.size()%2==0,kv.size());
        for(uint i=0;i<kv.size();i+=2) entities.insert(move(kv[i]), move(kv[i+1]));
    }
    string out;
    TextBuffer s(copy(xml));
    while(s) {
        out << s.until("&"_);
        if(!s) break;

        if(s.match("#x"_)) out << utf8(toInteger(toLower(s.until(";"_)),16));
        else if(s.match("#"_)) out << utf8(toInteger(s.until(";"_)));
        else {
            string key=s.word();
            if(s.match(";"_)) {
                string* c = entities.find(key);
                if(c) out<<*c; else warn("Unknown entity",key);
            }
            else out<<"&"_; //unescaped &
        }
    }
    return out;
}
