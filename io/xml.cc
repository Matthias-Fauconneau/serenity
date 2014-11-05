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

Element parseXML(const string& document) { return parse(document,false); }
Element parseHTML(const string& document) { return parse(document,true); }

Element::Element(TextData& s, bool html) {
	uint begin = s.index;
	if(s.match("!DOCTYPE")||s.match("!doctype")) { s.until('>'); return; }
	else if(s.match("?xml")) { s.until("?>"); return; }
	else if(s.match("!--")) { s.until("-->"); return; }
	else if(s.match('?')){ log("Unexpected <?",s.until("?>"),"?>"); return; }
	else name = copyRef(s.identifier("_-:"));
	if(!name) { log(s.slice(0,s.index)); log("Expected tag name got",s.line()); }
    if(html) name=toLower(name);
	s.whileAny(" \t\n");
    while(!s.match('>')) {
		if(s.match("/>")) { /*s.skip();*/ return; }
		else if(s.match('/')) {} //s.skip(); //spurious /
        else if(s.match('<')) break; //forgotten >
		s.whileAny(" \t\n");
		String key = copyRef(s.identifier("_-:"));/*TODO:reference*/ //s.skip();
		if(!key) { log("Attribute syntax error"_,s.slice(begin,s.index-begin),"|"_,s.until('>')); s.until('>'); break; }
        if(html) key=toLower(key);
        String value;
        if(s.match('=')) {
			//s.skip();
            if(s.match('"')) value=unescape(s.until('"'));
            else if(s.match('\'')) value=unescape(s.until('\''));
			else { value=copyRef(s.untilAny(" \t\n>")); if(s.slice(s.index-1,1)==">") s.index--; }
			s.match("\""); //duplicate "
        }
        attributes.insertMulti(move(key), move(value));
		s.whileAny(" \t\n");
    }
	/*if(html) {
        static array<string> voidElements = split("area base br col command embed hr img input keygen link meta param source track wbr"_,' ');
        if(voidElements.contains(name)) return; //HTML tags which are implicity void (i.e not explicitly closed)
        if(name=="style"_||name=="script"_) { //Raw text elements can contain <>
            s.skip();
            content = String(s.until(String("</"_+name+">"_)));
            s.skip();
            return;
        }
	}*/
    for(;;) {
        if(s.available(4)<4) return; //ignore unclosed tag
        if(s.match("<![CDATA["_)) {
			string content = s.until("]]>");
			if(content) children.append( unique<Element>(copyRef(content)) );
        }
        else if(s.match("<!--"_)) { s.until("-->"_); }
        else if(s.match("</"_)) { if(name==s.until(">"_)) break; } //ignore
        //else if(s.match(String("<?"_+name+">"_))) { log("Invalid tag","<?"_+name+">"_); return; }
		else if(s.match('<')) children.append( unique<Element>(s,html) );
        else {
            String content = unescape(s.whileNot('<'));
			if(trim(content)) children.append( unique<Element>(move(content)) );
        }
    }
}

string Element::attribute(const string& attribute) const {
    assert_(attributes.contains(attribute),"attribute", attribute,"not found in",*this);
    return attributes.at(attribute);
}

string Element::operator[](const string& attribute) const {
    if(!attributes.contains(attribute)) return ""_;
    return attributes.at(attribute);
}

const Element& Element::child(const string& name) const {
    for(const Element& e: children) if(e.name==name) return e;
    error("children"_, name, "not found in"_, *this);
}

const Element& Element::operator()(const string& name) const {
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

void Element::xpath(const string& path, const function<void(const Element &)>& visitor) const {
    assert(path);
    if(startsWith(path,"//"_)) {
        string next = path.slice(2);
        visit([&next,&visitor](const Element& e)->void{ e.xpath(next,visitor); });
    }
    string first = section(path,'/');
    string next = section(path,'/',1,-1);
    array<Element> collect;
    if(next) { for(const Element& e: children) if(e.name==first) e.xpath(next,visitor); }
    else { for(const Element& e: children) if(e.name==first) visitor(e); }
}

String Element::text() const { array<char> text; visit([&text](const Element& e){ text.append( unescape(e.content) ); }); return move(text); }

String Element::text(const string& path) const {
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
		if(name) line.append(">\n");
		if(trim(content)) line.append(indent+replace(simplify(trim(content)),"\n",'\n'+indent)+'\n');
		if(children) for(const Element& e: children) line.append( e.str(depth+1) );
		if(name) line.append(indent+"</"_+name+">\n");
	} else if(name) line.append(" />\n");
	return move(line);
}
String str(const Element& e) { return e.str(); }

String unescape(const string& xml) {
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
