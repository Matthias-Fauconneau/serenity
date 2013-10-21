#include "pdf.h"
#include "file.h"
#include "font.h"
#include "deflate.h"
#include "display.h"
#include "text.h" //annotations
#if GL
FILE(pdf)
#endif

struct Variant { //TODO: union
    enum { Empty, Boolean, Integer, Real, Data, List, Dict } type = Empty;
    double number=0; String data; array<Variant> list; map<string,Variant> dict;
    Variant():type(Empty){}
    Variant(bool boolean) : type(Boolean), number(boolean) {}
    Variant(int number) : type(Integer), number(number) {}
    Variant(int64 number) : type(Integer), number(number) {}
    Variant(long number) : type(Integer), number(number) {}
    Variant(double number) : type(Real), number(number) {}
    Variant(String&& data) : type(Data), data(move(data)) {}
    Variant(array<Variant>&& list) : type(List), list(move(list)) {}
    Variant(map<string,Variant>&& dict) : type(Dict), dict(move(dict)) {}
    operator bool() const { return type!=Empty; }
    operator int() const { assert(type==Integer, *this); return number; }
    int integer() const { assert(type==Integer, *this); return number; }
    double real() const { assert(type==Real||type==Integer); return number; }
};
String str(const Variant& o) {
    if(o.type==Variant::Boolean) return String(o.number?"true"_:"false"_);
    if(o.type==Variant::Integer) return str(int(o.number));
    if(o.type==Variant::Real) return str(float(o.number));
    if(o.type==Variant::Data) return copy(o.data);
    if(o.type==Variant::List) return str(o.list);
    if(o.type==Variant::Dict) return str(o.dict);
    error("Invalid Variant"_,int(o.type));
}

static Variant parseVariant(TextData& s) {
    s.skip();
    if("0123456789.-"_.contains(s.peek())) {
        string number = s.whileDecimal();
        if(s[0]==' '&&(s[1]>='0'&&s[1]<='9')&&s[2]==' '&&s[3]=='R') s.advance(4); //FIXME: regexp
        return number.contains('.') ? Variant(toDecimal(number)) : Variant(toInteger(number));
    }
    if(s.match('/')) return String(s.identifier("-+."_));
    if(s.match('(')) {
        String data;
        while(!s.match(')')) data<<s.character();
        return move(data);
    }
    if(s.match('[')) {
        array<Variant> list;
        s.skip();
        while(s && !s.match(']')) { list << parseVariant(s); s.skip(); }
        return move(list);
    }
    if(s.match("<<"_)) {
        map<string,Variant> dict;
        for(;;) {
            for(;!s.match('/');s.advance(1)) if(s.match(">>"_)) goto dictionaryEnd;
            string key = s.identifier("."_);
            dict.insert(key, parseVariant(s));
        }
        dictionaryEnd: s.skip();
        Variant v = move(dict);
        if(s.match("stream"_)) { s.skip();
            /*array<byte> stream (s.read(v.dict.at("Length"_).integer()));
            s.skip(); if(!s.match("endstream"_)) error(v.dict,s.peek(16));*/
            array<byte> stream ( s.until("endstream"_) );
            if(v.dict.contains("Filter"_)) {
                string filter = v.dict.at("Filter"_).list?v.dict.at("Filter"_).list[0].data:v.dict.at("Filter"_).data;
                if(filter=="FlateDecode"_) stream=inflate(stream, true);
                else error("Unsupported filter",v.dict);
            }
            if(v.dict.contains("DecodeParms"_)) {
                assert(v.dict.at("DecodeParms"_).dict.size() == 2);
                uint size = stream.size;
                uint width = v.dict.at("DecodeParms"_).dict.value("Columns"_,1);
                uint height = size/(width+1);
                assert(size == (width+1)*height); // grayscale
                const byte* src = stream.data;
                byte* dst = stream.begin(); // in-place
                int predictor = v.dict.at("DecodeParms"_).dict.value("Predictor"_,1);
                if(predictor>=10) { // PNG predictor
                    uint8 prior[width]; clear(prior,width);
                    for(uint unused y: range(height)) {
                        uint filter = *src++; assert(filter<=4,"Unknown PNG filter",filter);
                        uint8 a=0;
                        if(filter==0) for(uint i=0;i<width;i++) dst[i]= prior[i]=      src[i];
                        if(filter==1) for(uint i=0;i<width;i++) dst[i]= prior[i]= a= a+src[i];
                        if(filter==2) for(uint i=0;i<width;i++) dst[i]= prior[i]=      prior[i]+src[i];
                        if(filter==3) for(uint i=0;i<width;i++) dst[i]= prior[i]= a= uint8((int(prior[i])+int(a))/2)+src[i];
                        if(filter==4) {
                            int b=0;
                            for(uint i=0;i<width;i++) {
                                int c = b;
                                b = prior[i];
                                int d = int(a) + b - c;
                                int pa = abs(d-int(a)), pb = abs(d-b), pc = abs(d-c);
                                uint8 p = uint8(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
                                dst[i]= prior[i]=a= p+src[i];
                            }
                        }
                        src+=width; dst+=width;
                    }
                } else error("Unsupported predictor",predictor);
                stream.shrink(size-height);
            }
            v.data=move(stream);
        }
        return move(v);
    }
    if(s.match('<')) {
        String data;
        while(!s.match('>')) data << toInteger(s.read(2),16);
        return move(data);
    }
    if(s.match("true"_)) return true;
    if(s.match("false"_)) return false;
    error("Unknown type"_,s.index,s.slice(s.index>32?s.index-32:0,32),"|"_,s.slice(s.index,32));
    return Variant();
}
static Variant parseVariant(const string& buffer) { TextData s(buffer); return parseVariant(s); }
static map<string,Variant> toDict(const array<String>& xref, Variant&& object) { return object.dict ? move(object.dict) : parseVariant(xref[object.integer()]).dict; }

void PDF::open(const string& data) {
    clear();
    array<String> xref; map<string,Variant> catalog;
    {
        TextData s(data);
        for(s.index=s.buffer.size-sizeof("\r\n%%EOF");!( (s[-2]=='\r' && s[-1]=='\n') || s[-1]=='\n' || (s[-2]==' ' && s[-1]=='\r') );s.index--){}
        int index=s.integer(); assert(index!=-1,s.untilEnd()); s.index=index;
        int root=0;
        struct CompressedXRef { uint object, index; }; array<CompressedXRef> compressedXRefs;
        for(;;) { /// Parse XRefs
            if(s.match("xref"_)) { // Direct cross reference
                s.skip();
                uint i=s.integer(); s.skip();
                uint n=s.integer(); s.skip();
                if(xref.size<i+n) xref.grow(i+n);
                for(;n>0;n--,i++) {
                    int offset=s.integer(); s.skip(); s.integer(); s.skip();
                    if(s.match('n')) {
                        xref[i] = String(s.slice(offset+(i<10?1:(i<100?2:i<1000?3:4))+6));
                    } else if(s.match('f')) {}
                    else error(s.untilEnd());
                    s.skip();
                }
                if(!s.match("trailer"_)) error("trailer");
                s.skip();
            } else { // Cross reference dictionnary
                uint i=s.integer(); s.skip();
                uint unused n=s.integer(); s.skip();
                if(!s.match("obj"_)) error("");
                if(xref.size<=i) xref.grow(i+1);
                xref[i]=String(s.slice(s.index));
            }
            Variant object = parseVariant(s);
            map<string,Variant>& dict = object.dict;
            if(dict.contains("Type"_) && dict.at("Type"_).data=="XRef"_) {  // Cross reference stream
                const array<Variant>& W = dict.at("W"_).list;
                assert(W[0].integer()==1);
                int w1 = W[1].integer(); assert(w1==2||w1==3,w1);
                assert(W[2].integer()==0 || W[2].integer()==1);
                uint n=dict.at("Size"_).integer();
                if(xref.size<n) xref.grow(n);
                BinaryData b(object.data,true);
                array<Variant> list;
                if(dict.contains("Index"_)) list = move(dict.at("Index"_).list);
                else list << Variant(0) << Variant(dict.at("Size"_).integer());
                for(uint l: range(list.size/2)) {
                    for(uint i=list[l*2].integer(),n=list[l*2+1].integer();n>0;n--,i++) {
                        uint8 type=b.read();
                        if(type==0) { // free objects
                            uint16 unused offset = b.read();
                            if(w1==3) offset = offset<<16|(uint8)b.read();
                            uint8 unused g = b.read();
                            //log("f",hex(n),g);
                        } else if(type==1) { // uncompressed objects
                            uint16 offset = b.read();
                            if(w1==3) offset = offset<<16|(uint8)b.read();
                            xref[i]=String(s.slice(offset+(i<10?1:(i<100?2:i<1000?3:4))+6));
                            b.advance(W[2].integer());
                            //log("u",hex(offset));
                        } else if(type==2) { // compressed objects
                            uint16 stream=b.read();
                            if(w1==3) stream = stream<<16|(uint8)b.read();
                            uint8 index=0; if(W[2].integer()) index=b.read();
                            compressedXRefs << CompressedXRef{stream,index};
                        } else error("type",type);
                    }
                }
            }
            if(!root && dict.contains("Root"_)) root=dict.at("Root"_).integer();
            if(!dict.contains("Prev"_)) break;
            s.index=dict.at("Prev"_).integer();
        }
        catalog = parseVariant(xref[root]).dict;
        for(CompressedXRef ref: compressedXRefs) {
            Variant stream = parseVariant(xref[ref.object]);
            TextData s(stream.data);
            uint objectNumber=-1,offset=-1;
            for(uint i=0;i<=ref.index;i++) {
                objectNumber=s.integer(); s.match(' ');
                offset=s.integer(); s.match(' ');
            }
            xref[objectNumber] = String(s.slice(stream.dict.at("First"_).integer()+offset));
        }
    }
    x1 = +__FLT_MAX__, x2 = -__FLT_MAX__; vec2 pageOffset=0;
    Variant kids = move(parseVariant(xref[catalog.at("Pages"_).integer()]).dict.at("Kids"_));
    array<Variant> pages = kids.list ? move(kids.list) : parseVariant(xref[kids.integer()]).list;
    y1 = __FLT_MAX__, y2 = -__FLT_MAX__;
    for(uint i=0; i<pages.size; i++) {
        const Variant& page = pages[i];
        uint pageFirstBlit = blits.size, pageFirstLine = lines.size, pageFirstCharacter = characters.size, pageFirstPath=paths.size,
                pageFirstPolygon=polygons.size;
        auto dict = parseVariant(xref[page.integer()]).dict;
        if(dict.contains("Resources"_)) {
            auto resources = toDict(xref,move(dict.at("Resources"_)));
            if(resources.contains("Font"_)) for(auto e : toDict(xref,move(resources.at("Font"_)))) {
                if(fonts.contains(e.key)) continue;
                map<string,Variant> fontDict = parseVariant(xref[e.value.integer()]).dict;
                Variant* descendant = fontDict.find("DescendantFonts"_);
                if(descendant) fontDict = parseVariant(xref[descendant->type==Variant::Integer?descendant->integer():descendant->list[0].integer()]).dict;
                if(!fontDict.contains("FontDescriptor"_)) continue;
                String name = move(fontDict.at("BaseFont"_).data);
                auto descriptor = parseVariant(xref[fontDict.at("FontDescriptor"_).integer()]).dict;
                Variant* fontFile = descriptor.find("FontFile"_)?:descriptor.find("FontFile2"_)?:descriptor.find("FontFile3"_);
                if(!fontFile) continue;
                Font& font = fonts.insert(e.key, Font{move(name), unique<::Font>(parseVariant(xref[fontFile->integer()]).data), {}, {}});
                Variant* firstChar = fontDict.find("FirstChar"_);
                if(firstChar) font.widths.grow(firstChar->integer());
                Variant* widths = fontDict.find("Widths"_);
                if(widths) for(const Variant& width : widths->list) font.widths << width.real();
            }
            if(resources.contains("XObject"_)) {
                Variant& object = resources.at("XObject"_);
                map<string,Variant> dict = object.type==Variant::Integer ? parseVariant(xref[object.integer()]).dict : move(object.dict);
                for(auto e: dict) {
                    if(images.contains(String(e.key))) continue;
                    Variant object = parseVariant(xref[e.value.integer()]);
                    Image image(object.dict.at("Width"_).integer(), object.dict.at("Height"_).integer());
                    int depth=object.dict.at("BitsPerComponent"_).integer();
                    assert(depth, object.dict.at("BitsPerComponent"_).integer());
                    byte4 palette[256]; bool indexed=false;
                    if(depth==8 && object.dict.contains("ColorSpace"_)) {
                        Variant cs = object.dict.at("ColorSpace"_).data ? move(object.dict.at("ColorSpace"_).data) :
                                                                          parseVariant(xref[object.dict.at("ColorSpace"_).integer()]);
                        if(cs.data=="DeviceGray"_ || cs.data=="DeviceRGB"_) {}
                        else {
                            if(cs.list[0].data=="Indexed"_ && cs.list[1].data=="DeviceGray"_ && cs.list[2].integer()==255) {
                                TextData s (cs.list[3].data);
                                for(int i=0;i<256;i++) { s.match('/'); uint8 v=toInteger(s.read(3),8); palette[i]=byte4(v,v,v,0xFF); }
                                indexed=true;
                            } else if(cs.list[0].data=="Indexed"_ && cs.list[1].data=="DeviceRGB"_ && cs.list[2].integer()==255) {
                                Data s = cs.list[3].integer()?parseVariant(xref[cs.list[3].integer()]).data:move(cs.list[3].data);
                                for(int i=0;i<256;i++) {
                                    byte r=s.next(), g=s.next(), b=s.next();
                                    palette[i]=byte4(b,g,r,0xFF); }
                                indexed=true;
                            } else { log("Unsupported colorspace",cs,cs.list[1].integer()?parseVariant(xref[cs.list[1].integer()]).data:""_); continue; }
                        }
                    }
                    const uint8* src = (uint8*)object.data.data; assert(object.data.size==image.height*image.width*depth/8);
                    byte4* dst = (byte4*)image.data;
                    if(depth==1) {
                        assert(image.width%8==0);
                        for(uint y=0;y<image.height;y++) for(uint x=0;x<image.width; src++) {
                            for(int b=7;b>=0;b--,x++,dst++) dst[0] = src[0]&(1<<b) ? 0 : 0xFF;
                        }
                    } else if(depth==8) {
                        if(indexed) for(uint y=0;y<image.height;y++) for(uint x=0;x<image.width;x++,dst++,src++) dst[0]=palette[src[0]];
                        else for(uint y=0;y<image.height;y++) for(uint x=0;x<image.width;x++,dst++,src++) dst[0]=byte4(src[0],src[0],src[0],0xFF);
                    } else if(depth==24) for(uint y=0;y<image.height;y++) for(uint x=0;x<image.width;x++,dst++,src+=3) dst[0]=byte4(src[0],src[1],src[2],0xFF);
                    else if(depth==32) {
                        for(uint y=0;y<image.height;y++) for(uint x=0;x<image.width;x++,dst++,src+=4) dst[0]=byte4(src[0],src[1],src[2],src[3]);
                        image.alpha=true;
                    }
                    else error("Unsupported depth",depth);
                    images.insert(String(e.key), move(image));
                }
            }
        }
        Variant* contents = dict.find("Contents"_);
        if(contents) {
            if(contents->type==Variant::Integer) contents->list << contents->integer();
            array<byte> data;
            for(const auto& contentRef : contents->list) {
                Variant content = parseVariant(xref[contentRef.integer()]);
                assert(content.data, content);
                //for(const Variant& dataRef : content.list) data << parseVariant(xref[dataRef.integer()]).data;
                data << content.data;
            }
            TextData s = move(data);
            //y1 = __FLT_MAX__, y2 = -__FLT_MAX__;
            Cm=Tm=mat3x2(); array<mat3x2> stack;
            Font* font=0; float fontSize=1,spacing=0,wordSpacing=0,leading=0; mat3x2 Tlm;
            array<array<vec2>> path;
            array<Variant> args;
            while(s.skip(), s) {
                string id = s.word("'*"_);
                if(!id) {
                    assert(!((s[0]>='a' && s[0]<='z')||(s[0]>='A' && s[0]<='Z')||s[0]=='\''||s[0]=='"'),s.peek(min(16ul,s.buffer.size-s.index)));
                    args << parseVariant(s);
                    continue;
                }
                uint op = id[0]; if(id.size>1) { op|=id[1]<<8; if(id.size>2) op|=id[2]<<16; }
                switch( op ) {
                default: error("Unknown operator '"_+str((const char*)&op)+"'"_,id);
#define OP(c) break;case c:
#define OP2(c1,c2) break;case c1|c2<<8:
#define OP3(c1,c2,c3) break;case c1|c2<<8|c3<<16:
#define f(i) args[i].real()
#define p(x,y) (Cm*vec2(f(x),f(y)))
                    OP('b') drawPath(path,Close|Stroke|Fill|Winding|Trace);
                    OP2('b','*') drawPath(path,Close|Stroke|Fill|OddEven|Trace);
                    OP('B') drawPath(path,Stroke|Fill|Winding|Trace);
                    OP2('B','*') drawPath(path,Stroke|Fill|OddEven|Trace);
                    OP3('B','D','C') ;
                    OP3('E','M','C') ;
                    OP('c') path.last() << p(0,1) << p(2,3) << p(4,5);
                    OP('d') {} //setDashOffset();
                    OP('f') drawPath(path,Fill|Winding|Trace);
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
                    OP('n') path.clear();
                    OP('q') stack<<Cm;
                    OP('Q') Cm=stack.pop();
                    OP('s') drawPath(path,Close|Stroke|OddEven);
                    OP('S') drawPath(path,Stroke|OddEven|Trace);
                    OP('v') ;//curveTo (replicate first)
                    OP('w') ;//setWidth(Cm.m11()*f(0));
                    OP('W') path.clear(); //intersect winding clip
                    OP('y') ;//curveTo (replicate last)

                    OP2('B','T') Tm=Tlm=mat3x2();
                    OP2('c','s') ;//set fill colorspace
                    OP2('C','S') ;//set stroke colorspace
                    OP2('c','m') Cm=mat3x2(f(0),f(1),f(2),f(3),f(4),f(5))*Cm;
                    OP2('D','o') if(images.contains(args[0].data)) {
                        extend(Cm*vec2(0,0)); extend(Cm*vec2(1,1));
                        blits<<Blit{Cm*vec2(0,1),Cm*vec2(1,1)-Cm*vec2(0,0),share(images.at(args[0].data)),{}};
                    }
                    OP2('E','T') ;
                    OP2('g','s') ;
                    OP2('r','e') {
                        vec2 p1 = p(0,1), p2 = p1 + vec2(f(2)*Cm(0,0),f(3)*Cm(1,1));
                        path << move(array<vec2>() << p1
                                     << vec2(p1.x,p2.y) << vec2(p1.x,p2.y) << vec2(p1.x,p2.y)
                                     << p2 << p2 << p2
                                     << vec2(p2.x,p1.y) << vec2(p2.x,p1.y) << vec2(p2.x,p1.y));
                    }
                    OP2('S','C') ;
                    OP2('s','c') ;
                    OP3('S','C','N') ;
                    OP3('s','c','n') ;
                    OP2('T','*') Tm=Tlm=mat3x2(0,-leading)*Tlm;
                    OP2('T','c') spacing=f(0);
                    OP2('T','d') Tm=Tlm=mat3x2(f(0),f(1))*Tlm;
                    OP2('T','D') Tm=Tlm=mat3x2(f(0),f(1))*Tlm; leading=-f(1);
                    OP2('T','L') leading=f(0);
                    OP2('T','r') ; //set render mode
                    OP2('T','z') ; //set horizontal scaling
                    OP('\'') { Tm=Tlm=mat3x2(0,-leading)*Tlm; drawText(font,fontSize,spacing,wordSpacing,args[0].data); }
                    OP2('T','j') drawText(font,fontSize,spacing,wordSpacing,args[0].data);
                    OP2('T','J') for(const auto& e : args[0].list) {
                        if(e.type==Variant::Integer||e.type==Variant::Real) Tm=mat3x2(-e.real()*fontSize/1000,0)*Tm;
                        else if(e.type==Variant::Data) drawText(font,fontSize,spacing,wordSpacing,e.data);
                        else error("Unexpected type",(int)e.type);
                    }
                    OP2('T','f')
                            assert(fonts.contains(args[0].data), args[0].data);
                            font = fonts.contains(args[0].data)?&fonts.at(args[0].data):0; fontSize=f(1);
                    OP2('T','m') Tm=Tlm=mat3x2(f(0),f(1),f(2),f(3),f(4),f(5));
                    OP2('T','w') wordSpacing=f(0);
                    OP2('W','*') path.clear(); //intersect odd even clip
                }
                //log(str((const char*)&op),args);
                args.clear();
            }
        }
        // translate from page to document
        //const array<Variant>& cropBox = (dict.value("CropBox"_)?:dict.at("MediaBox"_)).list; pageOffset += vec2(0,-cropBox[3].real()); vec2 offset = pageOffset;
        pageOffset += vec2(0,y1-y2-16); vec2 offset = pageOffset+vec2(0,-y1);
        for(uint i: range(pageFirstBlit,blits.size)) blits[i].position += offset;
        for(uint i: range(pageFirstLine,lines.size)) lines[i].a += offset, lines[i].b += offset;
        for(uint i: range(pageFirstCharacter,characters.size)) characters[i].position += offset;
        for(uint i: range(pageFirstPath,paths.size)) for(vec2& pos: paths[i]) pos += offset;
        for(uint i: range(pageFirstPolygon,polygons.size)) {
            polygons[i].min+=offset; polygons[i].max+=offset;
            for(Line& l: polygons[i].edges) l.a += offset, l.b += offset;
            for(vec2& pos: polygons[i].vertices) pos += offset;
        }
        // add any children
        pages << move(dict["Kids"_].list);
    }
    y2=pageOffset.y;

    for(Blit& b: blits) b.position.x-=x1, b.position.y=-b.position.y;
    for(Line& l: lines) { l.a.x-=x1, l.a.y=-l.a.y; l.b.x-=x1, l.b.y=-l.b.y; }
    for(Character& c: characters) c.position.x-=x1, c.position.y=-c.position.y;
    for(array<vec2>& path: paths) for(vec2& pos: path) pos.x-=x1, pos.y=-pos.y;
    for(Polygon& polygon: polygons) {
        float t = polygon.min.y;
        polygon.min.x-=x1, polygon.min.y=-polygon.max.y;
        polygon.max.x-=x1, polygon.max.y=-t;
        for(Line& l: polygon.edges) { l.a.x-=x1, l.a.y=-l.a.y; l.b.x-=x1, l.b.y=-l.b.y; }
        for(vec2& pos: polygon.vertices) pos = vec2(pos.x-x1, -pos.y);
    }

    // insertion sorts blits for culling
    if(blits) for(int i : range(1,blits.size)) {
        auto e = move(blits[i]);
        while(i>0 && blits[i-1] > e) { blits[i]=move(blits[i-1]);  i--; }
        blits[i] = move(e);
    }

    // insertion sorts lines for culling
    if(lines) for(int i : range(1,lines.size)) {
        auto e = lines[i];
        while(i>0 && e < lines[i-1]) { lines[i]=lines[i-1];  i--; }
        lines[i] = e;
    }

    // insertion sorts characters for culling
    if(characters) for(int i : range(1,characters.size)) {
        auto e = characters[i];
        while(i>0 && characters[i-1] > e) { characters[i]=characters[i-1];  i--; }
        characters[i] = e;
    }

    scale = normalizedScale = 1280/(x2-x1); // Normalize width to 1280 for onGlyph/onPath callbacks
    for(const array<vec2>& path : paths) { array<vec2> scaled; for(vec2 pos : path) scaled<<scale*pos; onPath(scaled); }
    for(uint i: range(characters.size)) { Character& c = characters[i]; onGlyph(i, scale*c.position, scale*c.size, c.font->name, c.code, c.index); }
    for(uint i: range(characters.size)) { Character& c = characters[i]; onGlyph(i, scale*c.position, scale*c.size, c.font->name, c.code, c.index); }
    paths.clear();
}

vec2 cubic(vec2 A,vec2 B,vec2 C,vec2 D,float t) { return ((1-t)*(1-t)*(1-t))*A + (3*(1-t)*(1-t)*t)*B + (3*(1-t)*t*t)*C + (t*t*t)*D; }
void PDF::drawPath(array<array<vec2>>& paths, int flags) {
    for(array<vec2>& path : paths) {
        for(vec2 p : path) extend(p);
        array<vec2> polyline;
        for(uint i=0; i<path.size-3; i+=3) {
            if( path[i+1] == path[i+2] && path[i+2] == path[i+3] ) {
                polyline << copy(path[i]);
            } else {
                for(int t=0;t<16;t++) polyline << cubic(path[i],path[i+1],path[i+2],path[i+3],float(t)/16);
            }
        }
        polyline << copy(path.last());
        array<Line> lines;
        if((flags&Stroke) || (flags&Fill) || polyline.size>16) {
            for(uint i: range(polyline.size-1)) {
                if(polyline[i] != polyline[i+1])
                    lines << Line{ polyline[i], polyline[i+1] };
            }
            if(flags&Close) lines << Line{polyline.last(), polyline.first()};
        }
        /*if(flags&Stroke)*/ this->lines << lines;
        if(flags&Fill) {
            Polygon polygon;
            // Software rendering (FIXME: precompute line equations)
            polygon.min=path.first(), polygon.max=path.first();
            for(vec2 p : path) {
                polygon.min=min(polygon.min,p);
                polygon.max=max(polygon.max,p);
            }
            polygon.edges = move(lines);
            float area=0;
            for(uint i: range(polyline.size)) {
                area += cross(polyline[(i+1)%polyline.size]-polyline[i], polyline[(i+2)%polyline.size]-polyline[i]);
            }
            if(area>0) for(Line& e: polygon.edges) swap(e.a,e.b);
            polygon.vertices = move(polyline); // GL
            polygons << move(polygon);
        }
        if(flags&Trace) this->paths << move(path);
    }
    paths.clear();
}

void PDF::drawText(Font* font, int fontSize, float spacing, float wordSpacing, const string& data) {
    assert(font);
    if(!font || !font->font->face) return;
    font->font->setSize(fontSize);
    for(uint8 code : data) {
        if(code==0) continue;
        mat3x2 Trm = Tm*Cm;
        uint16 index = font->font->index(code);
        vec2 position = vec2(Trm(0,2),Trm(1,2));
        if(position.y<y1) y1=position.y; if(position.y>y2) y2=position.y; //extend(position); extend(position+Trm.m11*font->font.size(index));
        characters << Character{font, Trm(0,0)*fontSize, index, position, code};
        float advance = spacing+(code==' '?wordSpacing:0);
        if(code < font->widths.size) advance += fontSize*font->widths[code]/1000;
        else advance += font->font->linearAdvance(index);
        Tm = mat3x2(advance, 0) * Tm;
    }
}

int2 PDF::sizeHint() { return int2(-scale*(x2-x1),scale*(y2-y1)); }
void PDF::render(int2 position, int2 size) {
#if GL
    if(!softwareRendering) {
        float newScale = size.x/(x2-x1); // Fit width
        if(newScale != scale) {
            scale = newScale;
            glLines = GLVertexBuffer();
            array<vec2> lineVertices;
            for(const Line& l: lines) {
                vec2 a = scale*l.a, b = scale*l.b;
                if(a.x==b.x) a.x=b.x=floor(a.x)+0.5; if(a.y==b.y) a.y=b.y=floor(a.y)+0.5;
                lineVertices << vertex(a) << vertex(b);
            }
            glLines.upload(lineVertices);

            array<vec2> vertices;
            for(const Polygon& polygon: polygons) {
                const array<vec2>& p = polygon.vertices;
                if(p.size > 4) continue; //FIXME: triangulate concave polygons
                vec2 p0 = vertex(scale*p[0]);
                for(uint i: range(1,p.size-1))
                    vertices << p0 << vertex(scale*p[i]) << vertex(scale*p[i+1]);
            }
            glTriangles.upload(vertices);

            for(Font& font: fonts.values) font.cache.clear();
            glBlits.clear();
            glBlits.reserve(characters.size);
            for(Character& c: characters) {
                GLTexture& texture = c.font->cache[scale*c.size][c.index];
                c.font->font->setSize(scale*c.size);
                const Glyph& glyph = c.font->font->glyph(c.index);
                if(!glyph.image) continue;
                if(!texture) texture = GLTexture(glyph.image);
                vec2 min = round(vec2(scale*c.position)+vec2(glyph.offset));
                vec2 max = min+vec2(texture.size());
                glBlits << GLBlit{vertex(min), vertex(max), texture};
            }
        }
        //FIXME: clip, VFC
        assert(!blits); //FIXME

        glBlendSubstract(); // Assumes black on white render

        // Render lines
        GLShader fill (pdf());
        vec2 offset = vec2(2.*position.x/viewportSize.x,-2.*position.y/viewportSize.y);
        fill["offset"_] = offset;
        fill["color"] = vec4(1);
        glLines.bindAttribute(fill, "position"_, 2);
        glLines.draw(Lines); // No VFC cull

        // Render triangles
        glTriangles.bindAttribute(fill, "position"_, 2);
        glTriangles.draw(Triangles);  // No VFC cull

        // Render glyphs, TODO: texture array + VBO
        GLShader blit (pdf(), {"blit"_});
        blit["offset"_] = offset;
        blit["sampler"_] = 0;
        int i = 0; for(GLBlit b: glBlits) { // No VFC vcull
            vec2 min = b.min, max = b.max;
            if(offset.x+max.x <= -1 || offset.y+min.y <=-1 || offset.x+min.x >= 1 || offset.y+max.y >= 1) { i++; continue; }
            blit["color"_] = vec4(1)-colors.value(i,black);
            b.texture.bind(0);
            GLVertexBuffer vertexBuffer;
            vertexBuffer.upload<Vertex>({{vec2(min.x,min.y),vec2(0,0)}, {vec2(max.x,min.y),vec2(1,0)},
                                         {vec2(min.x,max.y),vec2(0,1)}, {vec2(max.x,max.y),vec2(1,1)}});
            vertexBuffer.bindAttribute(blit, "position"_, 2, offsetof(Vertex, position));
            vertexBuffer.bindAttribute(blit, "texCoord"_, 2, offsetof(Vertex, texCoord));
            vertexBuffer.draw(TriangleStrip);
            i++;
        }

        return;
    }
#endif
    scale = size.x/(x2-x1); // Fit width

    for(Blit& blit: blits) {
        if(position.y+scale*(blit.position.y+blit.size.y) < currentClip.min.y) continue;
        if(position.y+scale*blit.position.y > currentClip.max.y) break;
        if(!blit.resized) blit.resized=resize(blit.image,scale*blit.size.x,scale*blit.size.y);
        ::blit(position+int2(scale*blit.position),blit.resized);
    }

    for(const Line& l: lines.slice(lines.binarySearch(Line{vec2(-position-int2(0,200))/scale,vec2(-position-int2(0,200))/scale}))) {
        vec2 a = scale*l.a, b = scale*l.b;
        a+=vec2(position), b+=vec2(position);
        if(a.y < currentClip.min.y && b.y < currentClip.min.y) continue;
        if(a.y > currentClip.max.y+200 && b.y > currentClip.max.y+200) break;
        if(a.x==b.x) a.x=b.x=round(a.x); if(a.y==b.y) a.y=b.y=round(a.y);
        line(a,b);
    }

    for(const Polygon& polygon: polygons) {
        int2 min=position+int2(scale*polygon.min), max=position+int2(scale*polygon.max);
        Rect rect = Rect(min,max) & currentClip;
        for(int y=rect.min.y; y<=::min<int>(framebuffer.height-1,rect.max.y); y++) {
            for(int x=rect.min.x; x<=::min<int>(framebuffer.width-1,rect.max.x); x++) {
                vec2 p = vec2(x,y);
                for(const Line& e: polygon.edges) {
                    vec2 a = vec2(position)+scale*e.a, b=vec2(position)+scale*e.b;
                    if(cross(p-a,b-a)>0) goto outside;
                }
                /*else*/ framebuffer(x,y) = byte4(0,0,0,0xFF);
outside:;
            }
        }
    }

    int i = characters.binarySearch(Character{0,0,0,vec2(-position-int2(0,100))/scale,0});
    for(const Character& c: characters.slice(i)) {
        int2 pos = position+int2(round(scale*c.position));
        if(pos.y<=currentClip.min.y-100) { i++; continue; }
        if(pos.y>=currentClip.max.y+100) break;
        c.font->font->setSize(scale*c.size);
        const Glyph& glyph = c.font->font->glyph(c.index); //FIXME: optimize lookup
        if(glyph.image) blit(pos+glyph.offset,glyph.image,colors.value(i,black));
        i++;
    }

    for(const_pair<vec2,String> text: (const map<vec2, String>&)annotations) {
        int2 pos = position+int2(text.key*scale/normalizedScale);
        if(pos.y<=currentClip.min.y) continue;
        if(pos.y>=currentClip.max.y) continue; //break;
        Text(text.value,12,vec4(1,0,0,1)).render(pos,int2(0,0));
    }
}
