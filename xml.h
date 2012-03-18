#pragma once
#include "stream.h"
#include "array.cc"
//TODO: xml.cc

/// XML element
struct Element {
    string name,content,declaration;
    map< string, string > attributes;
    array<Element*> children;
    Element(){}
    Element(array<byte>&& document) {
        TextBuffer s(move(document));
        if(s.match("<?xml "_)) s.until("?>"_); s.skip(); //XML declaration
        if(!s.match("<"_)) error(s.until("\n"_));
        parseContent(s);
    }
    ~Element(){ for(auto& e: children) delete e; }
    void parseContent(TextBuffer& s) {
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
            if(s.match("<"_)) { children<<new Element(); children.last()->parseContent(s); }
            else { s.skip(); error(s.until("\n"_)); }
        }
        if(!s.match(name)) error(s.until("\n"_),name);
        if(!s.match(">"_)) error(s.until("\n"_));
        s.skip();
    }
    string operator[](string&& attribute) const { return copy(attributes.at(move(attribute))); }
    const Element& operator[]( int i ) const { return *children[i]; }
    array<const Element*> operator()(const string& path) const {
        if(!path) return array<const Element*>{this};
        //if(s.match("//")) return fold( collect, ( ref XML[] match, XML n ){ match ~= n.XPath( peekAll ); } );
        if(startsWith(path,"/"_)) { array<const Element*> match; for(const Element* e: children) match << (*e)(slice(path,1)); return match; }
        else {
            TextBuffer s(copy(path));
            do {
               string name = s.word();
               if(name == this->name) return operator()(s.readAll());
            } while(s.match("|"_));
            return array<const Element*>{};
        }
    }
    string str(const string& prefix=""_) const {
        string r;
        if(content) r << content; //content element
        else {
            r << prefix+"<"_+name;
            for(const_pair<string,string> attr: attributes) r << " "_+attr.key+"=\""_+attr.value+"\""_;
            if(children) {
                r << ">\n"_;
                for(const Element* e: children) r << e->str(prefix+" "_);
                r << prefix+"</"_+name+">\n"_;
            } else r << "/>\n"_;
        }
        return r;
    }
};
template<> string str(const Element& e) { return e.str(); }
