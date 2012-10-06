#include "pdf.h"
#include "file.h"
#include "font.h"
#include "inflate.h"
#include "display.h"
#include "text.h" //annotations

struct Variant { //TODO: union
    enum { Empty, Number, Data, List, Dict } type;
    double number=0; string data; array<Variant> list; map<ref<byte>,Variant> dict;
    Variant():type(Empty){}
    Variant(double number) : type(Number), number(number) {}
    Variant(string&& data) : type(Data), data(move(data)) {}
    Variant(array<Variant>&& list) : type(List), list(move(list)) {}
    Variant(map<ref<byte>,Variant>&& dict) : type(Dict), dict(move(dict)) {}
};
string str(const Variant& o) {
    if(o.type==Variant::Number) return str(o.number);
    if(o.type==Variant::Data) return copy(o.data);
    if(o.type==Variant::List) return str(o.list);
    if(o.type==Variant::Dict) return str(o.dict);
    error("Invalid Variant"_,int(o.type));
}

static Variant parse(TextData& s) {
    s.skip();
    if("0123456789.-"_.contains(s.peek())) {
        double number = s.decimal();
        if(s[0]==' '&&(s[1]>='0'&&s[1]<='9')&&s[2]==' '&&s[3]=='R') s.advance(4); //FIXME: regexp
        assert(!__builtin_isnan(number));
        return number;
    }
    if(s.match('/')) return string(s.identifier("-+"_));
    if(s.match('(')) {
        string data;
        while(!s.match(')')) data<<s.character();
        return move(data);
    }
    if(s.match('[')) {
        array<Variant> list;
        s.skip();
        while(s && !s.match(']')) { list << parse(s); s.skip(); }
        return move(list);
    }
    if(s.match("<<"_)) {
        map<ref<byte>,Variant> dict;
        for(;;) {
            for(;!s.match('/');s.advance(1)) if(s.match(">>"_)) goto dictionaryEnd;
            ref<byte> key = s.identifier();
            dict.insert(key, parse(s));
        }
        dictionaryEnd: s.skip();
        Variant v = move(dict);
        if(s.match("stream"_)) { s.skip();
            array<byte> stream = inflate(s.until("endstream"_),true);
            if(v.dict.contains("DecodeParms"_)) {
                assert(v.dict.at("DecodeParms"_).dict.size() == 2);
                int predictor = v.dict.at("DecodeParms"_).dict.value("Predictor"_,Variant(1.0)).number;
                if(predictor != 12) error("Unsupported predictor",predictor);
                int size = stream.size();
                int w = v.dict.at("DecodeParms"_).dict.value("Columns"_,Variant(1.0)).number;
                int h = size/(w+1);
                assert(size == (w+1)*h);
                const byte* src = stream.data();
                byte* dst = stream.data();
                for(int y=0;y<h;y++) {
                    int filter = *src++;
                    if(filter != 2) error("Unsupported filter",filter);
                    for(int x=0;x<w;x++) {
                        *dst = (y>0 ? *(dst-w) : 0) + *src;
                        dst++; src++;
                    }
                }
                stream.shrink(size-h);
            }
            v.data=move(stream);
        }
        return move(v);
    }
    if(s.match('<')) {
        string data;
        while(!s.match('>')) data << toInteger(s.read(2),16);
        return move(data);
    }
    error("Unknown type"_,s.peek(64));
    return Variant(0);
}
static Variant parse(const ref<byte>& buffer) { TextData s(buffer); return parse(s); }
static map<ref<byte>,Variant> toDict(const array< string >& xref, Variant&& object) { return object.dict ? move(object.dict) : parse(xref[object.number]).dict; }

void PDF::open(const ref<byte>& path, const Folder& folder) {
    lines.clear(); fonts.clear(); characters.clear(); paths.clear();
    file = Map(path,folder);
    array<string> xref; map<ref<byte>,Variant> catalog;
    {
        TextData s(file);
        for(s.index=s.buffer.size()-sizeof("\r\n%%EOF");!( (s[-2]=='\r' && s[-1]=='\n') || s[-1]=='\n' || (s[-2]==' ' && s[-1]=='\r') );s.index--){}
        s.index=s.integer(); assert(s.index!=uint(-1));
        int root=0;
        struct CompressedXRef { uint object, index; }; array<CompressedXRef> compressedXRefs;
        for(;;) { /// Parse XRefs
            if(s.match("xref"_)) { // Direct cross reference
                s.skip();
                uint i=s.integer(); s.skip();
                uint n=s.integer(); s.skip();
                if(xref.size()<i+n) xref.grow(i+n);
                for(;n>0;n--,i++) {
                    int offset=s.integer(); s.skip(); s.integer(); s.skip();
                    if(s.match('n'))  xref[i] = string(s.slice(offset+(i<10?1:(i<100?2:3))+6));
                    else if(s.match('f')) {}
                    else error(s.untilEnd());
                    s.skip();
                }
                if(!s.match("trailer"_)) error("trailer");
                s.skip();
            } else { // Cross reference dictionnary
                uint i=s.integer(); s.skip();
                uint unused n=s.integer(); s.skip();
                if(!s.match("obj"_)) error("");
                if(xref.size()<=i) xref.grow(i+1);
                xref[i]=string(s.slice(s.index));
            }
            Variant object = parse(s);
            map<ref<byte>,Variant>& dict = object.dict;
            if(dict.contains("Type"_) && dict.at("Type"_).data=="XRef"_) {  // Cross reference stream
                const array<Variant>& W = dict.at("W"_).list;
                assert(W[0].number==1);
                assert(W[1].number==2);
                assert(W[2].number==0 || W[2].number==1);
                uint n=dict.at("Size"_).number;
                if(xref.size()<n) xref.grow(n);
                BinaryData b(object.data,true);
                array<Variant> list;
                if(dict.contains("Index"_)) list = move(dict.at("Index"_).list);
                else list << Variant(0) << Variant(dict.at("Size"_).number);
                for(uint l: range(list.size()/2)) {
                    for(uint i=list[l*2].number,n=list[l*2+1].number;n>0;n--,i++) {
                        uint8 type=b.read();
                        if(type==0) { // free objects
                            uint16 unused n = b.read();
                            assert(W[2].number==1);
                            uint8 unused g = b.read();
                            //log("f",hex(n),g);
                        } else if(type==1) { // uncompressed objects
                            uint16 offset=b.read();
                            xref[i]=string(s.slice(offset+(i<10?1:(i<100?2:3))+6));
                            b.advance(W[2].number);
                            //log("u",hex(offset));
                        } else if(type==2) { // compressed objects
                            uint16 unused stream=b.read();
                            uint8 unused index=0; if(W[2].number) index=b.read();
                            compressedXRefs << CompressedXRef __(stream,index);
                        } else error("type",type);
                    }
                }
            }
            if(!root && dict.contains("Root"_)) root=dict.at("Root"_).number;
            if(!dict.contains("Prev"_)) break;
            s.index=dict.at("Prev"_).number;
        }
        catalog = parse(xref[root]).dict;
        for(CompressedXRef ref: compressedXRefs) {
            Variant stream = parse(xref[ref.object]);
            TextData s(stream.data);
            uint objectNumber=-1,offset=-1; for(uint i=0;i<=ref.index;i++) { objectNumber=s.integer(); s.match(' '); offset=s.integer(); s.match(' ');}
            xref[objectNumber] = string(s.slice(stream.dict.at("First"_).number+offset));
        }
    }
    x1 = +__FLT_MAX__, x2 = -__FLT_MAX__; vec2 pageOffset=0;
    array<Variant> pages = move(parse(xref[catalog.at("Pages"_).number]).dict.at("Kids"_).list);
    for(const Variant& page : pages) {
        uint pageFirstLine = lines.size(), pageFirstCharacter = characters.size(), pageFirstPath=paths.size();
        auto dict = parse(xref[page.number]).dict;
        if(dict.contains("Resources"_)) {
            auto resources = toDict(xref,move(dict.at("Resources"_)));
            if(resources.contains("Font"_)) for(auto e : toDict(xref,move(resources.at("Font"_)))) {
                if(fonts.contains(e.key)) continue;
                fonts[e.key]=&heap<Font>();
                auto fontDict = parse(xref[e.value.number]).dict;
                auto descendant = fontDict.find("DescendantFonts"_);
                if(descendant) fontDict = parse(xref[descendant->list[0].number]).dict;
                if(!fontDict.contains("FontDescriptor"_)) continue;
                fonts[e.key]->name = move(fontDict.at("BaseFont"_).data);
                auto descriptor = parse(xref[fontDict.at("FontDescriptor"_).number]).dict;
                auto fontFile = descriptor.find("FontFile"_)?:descriptor.find("FontFile2"_)?:descriptor.find("FontFile3"_);
                if(fontFile) fonts[e.key]->font = ::Font(parse(xref[fontFile->number]).data);
                Variant* firstChar = fontDict.find("FirstChar"_);
                if(firstChar) fonts[e.key]->widths.grow(firstChar->number);
                Variant* widths = fontDict.find("Widths"_);
                if(widths) for(const Variant& width : widths->list) fonts[e.key]->widths << width.number;
            }
        }
        auto contents = dict.find("Contents"_);
        if(contents) {
            if(contents->number) contents->list << contents->number;
            TextData s;
            for(const auto& contentRef : contents->list) {
                Variant content = parse(xref[contentRef.number]);
                assert(content.data, content);
                //for(const Variant& dataRef : content.list) data << parse(xref[dataRef.number]).data;
                s.buffer << content.data;
            }
            y1 = __FLT_MAX__, y2 = -__FLT_MAX__;
            Cm=Tm=mat32(); array<mat32> stack;
            Font* font=0; float fontSize=1,spacing=0,wordSpacing=0,leading=0; mat32 Tlm;
            array< array<vec2> > path;
            array<Variant> args;
            while(s.skip(), s) {
                ref<byte> id = s.word("'*"_);
                if(!id) {
                    assert(!((s[0]>='a' && s[0]<='z')||(s[0]>='A' && s[0]<='Z')||s[0]=='\''||s[0]=='"'),s.peek(min(16u,s.buffer.size()-s.index)));
                    args << parse(s);
                    continue;
                }
                uint op = id[0]|(id.size>1?id[1]:0)<<8|(id.size>2?id[2]:0)<<16;
                switch( op ) {
                default: error("Unknown operator",str((const char*)&op),s.peek(16));
#define OP(c) break;case c:
#define OP2(c1,c2) break;case c1|c2<<8:
#define OP3(c1,c2,c3) break;case c1|c2<<8|c3<<16:
#define f(i) args[i].number
#define p(x,y) (Cm*vec2(f(x),f(y)))
                    OP('b') drawPath(path,Close|Stroke|Fill|Winding);
                    OP2('b','*') drawPath(path,Close|Stroke|Fill|OddEven);
                    OP('B') drawPath(path,Stroke|Fill|Winding);
                    OP2('B','*') drawPath(path,Stroke|Fill|OddEven);
                    OP('c') path.last() << p(0,1) << p(2,3) << p(4,5);
                    OP('d') {} //setDashOffset();
                    OP('f') drawPath(path,Fill|Winding);
                    OP2('f','*') drawPath(path,Fill|OddEven|Trace);
                    OP('g') ;//brushColor = f(0);
                    OP('h') ;//close path
                    OP2('r','g') ;//brushColor = vec3(f(0),f(1),f(2));
                    OP('G') ;//penColor = f(0);
                    OP('i') ;
                    OP('j') ;//joinStyle = {Miter,Round,BevelJoin}[f(0)];
                    OP('J') ;//capStyle = {Flat,Round,Square}[f(0)];
                    OP('l') path.last() << p(0,1) << p(0,1) << p(0,1);
                    OP('m') path << move(array<vec2>()<<p(0,1));
                    OP('M') ;//setMiterLimit(f(0));
                    OP('q') stack<<Cm;
                    OP('Q') Cm=stack.pop();
                    OP('s') drawPath(path,Close|Stroke|OddEven);
                    OP('S') drawPath(path,Stroke|OddEven|Trace);
                    OP('w') ;//setWidth(Cm.m11()*f(0));
                    OP('W') path.clear(); //intersect winding clip
                    OP2('W','*') path.clear(); //intersect odd even clip
                    OP('n') path.clear();
                    OP2('c','s') ;//set fill colorspace
                    OP2('C','S') ;//set stroke colorspace
                    OP3('S','C','N') ;
                    OP3('s','c','n') ;
                    OP2('c','m') Cm=mat32(f(0),f(1),f(2),f(3),f(4),f(5))*Cm;
                    OP2('r','e') {
                        vec2 p1 = p(0,1), p2 = p1 + vec2(f(2)*Cm.m11,f(3)*Cm.m22);
                        path << move(array<vec2>() << p1
                                     << vec2(p1.x,p2.y) << vec2(p1.x,p2.y) << vec2(p1.x,p2.y)
                                     << p2 << p2 << p2
                                     << vec2(p2.x,p1.y) << vec2(p2.x,p1.y) << vec2(p2.x,p1.y));
                    }
                    OP2('D','o') ;//p->drawPixmap(Cm.mapRect(QRect(0,0,1,1)),images[args[0].data]);
                    OP2('g','s') ;
                    OP2('B','T') Tm=Tlm=mat32();
                    OP2('E','T') ;
                    OP2('T','*') Tm=Tlm=mat32(0,-leading)*Tlm;
                    OP2('T','d') Tm=Tlm=mat32(f(0),f(1))*Tlm;
                    OP2('T','D') Tm=Tlm=mat32(f(0),f(1))*Tlm; leading=-f(1);
                    OP2('T','L') leading=f(0);
                    OP2('T','c') spacing=f(0);
                    OP2('T','z') ; //set horizontal scaling
                    OP('\'') { Tm=Tlm=mat32(0,-leading)*Tlm; drawText(font,fontSize,spacing,wordSpacing,args[0].data); }
                    OP2('T','j') drawText(font,fontSize,spacing,wordSpacing,args[0].data);
                    OP2('T','J') for(const auto& e : args[0].list) {
                        if(e.number) Tm=mat32(-e.number*fontSize/1000,0)*Tm;
                        else drawText(font,fontSize,spacing,wordSpacing,e.data);
                    }
                    OP2('T','f') font = fonts.at(args[0].data); fontSize=f(1);
                    OP2('T','m') Tm=Tlm=mat32(f(0),f(1),f(2),f(3),f(4),f(5));
                    OP2('T','w') wordSpacing=f(0);
                }
                //log(str((const char*)&op),args);
                args.clear();
            }
        }
        // tighten page bounds
        pageOffset += vec2(0,y1-y2-16);
        vec2 offset = pageOffset+vec2(0,-y1);
        for(uint i: range(pageFirstLine,lines.size())) lines[i].a += offset, lines[i].b += offset;
        for(uint i: range(pageFirstCharacter,characters.size())) characters[i].pos += offset;
        for(uint i: range(pageFirstPath,paths.size())) for(vec2& pos: paths[i]) pos += offset;
    }
    y2=pageOffset.y;
    for(Line& l: lines) { l.a.x-=x1, l.a.y=-l.a.y; l.b.x-=x1, l.b.y=-l.b.y; assert(l.a!=l.b,l.a,l.b); }
    for(Character& c: characters) c.pos.x-=x1, c.pos.y=-c.pos.y;
    for(array<vec2>& path: paths) for(vec2& pos: path) pos.x-=x1, pos.y=-pos.y;

    float scale = normalizedScale = 1280/(x2-x1); // Normalize width to 1280 for onGlyph/onPath callbacks
    for(uint i: range(characters.size())) { Character& c = characters[i]; onGlyph(i, scale*c.pos, scale*c.size, c.font->name, c.code); }
    for(const array<vec2>& path : paths) { array<vec2> scaled; for(vec2 pos : path) scaled<<scale*pos; onPath(scaled); }
    paths.clear();
}

vec2 cubic(vec2 A,vec2 B,vec2 C,vec2 D,float t) { return ((1-t)*(1-t)*(1-t))*A + (3*(1-t)*(1-t)*t)*B + (3*(1-t)*t*t)*C + (t*t*t)*D; }
void PDF::drawPath(array<array<vec2> >& paths, int flags) {
    for(array<vec2>& path : paths) {
        for(vec2 p : path) extend(p);
        array<vec2> polyline;
        for(uint i=0; i<path.size()-3; i+=3) {
            if( path[i+1] == path[i+2] && path[i+2] == path[i+3] ) {
                polyline << copy(path[i]);
            } else {
                for(int t=0;t<16;t++) polyline << cubic(path[i],path[i+1],path[i+2],path[i+3],float(t)/16);
            }
        }
        polyline << copy(path.last());
        if((flags&Stroke) || (flags&Fill) || polyline.size()>16) {
            for(uint i: range(polyline.size()-1)) {
                //assert(polyline[i] != polyline[i+1],polyline,flags);
                if(polyline[i] != polyline[i+1])
                    lines << Line __( polyline[i], polyline[i+1] );
            }
            if(flags&Close) lines << Line __( polyline.last(), polyline.first() );
        }
        if(flags&Trace) this->paths << move(path);
    }
    paths.clear();
}

void PDF::drawText(Font* font, int fontSize, float spacing, float wordSpacing, const ref<byte>& data) {
    if(!font->font.face) return;
    font->font.setSize(fontSize*64);
    for(uint8 code : data) {
        if(code==0) continue;
        mat32 Trm = Tm*Cm;
        uint16 index = font->font.index(code);
        vec2 position = vec2(Trm.dx,Trm.dy);
        extend(position); extend(position+Trm.m11*font->font.size(index));
        characters << Character __(font, Trm.m11*fontSize, index, position, code);
        float advance = spacing+(code==' '?wordSpacing:0);
        if(code < font->widths.size()) advance += fontSize*font->widths[code]/1000;
        else advance += font->font.advance(index)/16.f;
        Tm = mat32(advance,0) * Tm;
    }
}

int2 PDF::sizeHint() { return int2(scale*(x2-x1),scale*(y2-y1)); }
inline int round(float x) { return int(x + 0.5); }
void PDF::render(int2 position, int2 size) {
    scale = size.x/(x2-x1); // Fit width

    for(const Line& l: lines) {
        vec2 a = scale*l.a, b = scale*l.b;
        a+=vec2(position), b+=vec2(position);
        if((a.y < 0 && b.y < 0) || (a.y > size.y && b.y > size.y)) continue;
        if(a.x==b.x) a.x=b.x=round(a.x); if(a.y==b.y) a.y=b.y=round(a.y);
        line(a.x,a.y,b.x,b.y);
    }

    int i=0; for(const Character& c: characters) {
        int2 pos = position+int2(round(scale*c.pos.x), round(scale*c.pos.y));
        if(pos.y>0 && pos.y<size.y) { //clip without glyph cache lookup
            c.font->font.setSize(round(scale*c.size*64));
            const Glyph& glyph = c.font->font.glyph(c.index); //FIXME: optimize lookup
            if(glyph.image) substract(pos+glyph.offset,glyph.image,colors.value(i,black));
        }
        i++;
    }

    static Font font __(string(),array<float>(),::Font(File("dejavu/DejaVuSans.ttf"_,::fonts()),10));
    for(const pair<vec2,string>& text: annotations) Text(copy(text.value)).render(position+int2(text.key*scale/normalizedScale),int2(0,0));
}
