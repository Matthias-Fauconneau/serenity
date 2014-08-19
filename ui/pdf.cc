#include "pdf.h"
#include "file.h"
#include "font.h"
#include "deflate.h"
#include "graphics.h"
#include "text.h" //annotations

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
    explicit operator bool() const { return type!=Empty; }
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
        return number.contains('.') ? Variant(fromDecimal(number)) : Variant(fromInteger(number));
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
            array<byte> stream ( s.until("endstream"_) );
            if(v.dict.contains("Filter"_)) {
                string filter = v.dict.at("Filter"_).list?v.dict.at("Filter"_).list[0].data:v.dict.at("Filter"_).data;
                if(filter=="FlateDecode"_) stream=inflate(stream, true);
                else { log("Unsupported filter",v.dict); return Variant(); }
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
                    uint8 prior[width]; mref<uint8>(prior, width).clear();
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
        while(!s.match('>')) data << fromInteger(s.read(2),16);
        return move(data);
    }
    if(s.match("true"_)) return true;
    if(s.match("false"_)) return false;
    log("Unknown type"_,s.slice(s.index-32,32),"|"_,s.slice(s.index,32),s.buffer.size-s.index);
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
    Variant kids = move(parseVariant(xref[catalog.at("Pages"_).integer()]).dict.at("Kids"_));
    array<Variant> pages = kids.list ? move(kids.list) : parseVariant(xref[kids.integer()]).list;
    vec2 documentMin = vec2(+inf), documentMax = vec2(-inf);
    for(uint i=0; i<pages.size; i++) {
        const Variant& page = pages[i];
        auto dict = parseVariant(xref[page.integer()]).dict;
        if(dict.contains("Resources"_)) {
            auto resources = toDict(xref,move(dict.at("Resources"_)));
            // Parses font definitions
            if(resources.contains("Font"_)) for(auto e : toDict(xref,move(resources.at("Font"_)))) {
                if(fonts.contains(e.key)) continue;
                map<string,Variant> fontDict = parseVariant(xref[e.value.integer()]).dict;
                Variant* descendant = fontDict.find("DescendantFonts"_);
                if(descendant) {
                    if(descendant->type==Variant::Integer)
                        fontDict = parseVariant(xref[descendant->integer()]).dict;
                    else if(descendant->type==Variant::List && descendant->list[0].type==Variant::Integer)
                        fontDict = parseVariant(xref[descendant->list[0].integer()]).dict;
                    else if(descendant->type==Variant::List && descendant->list[0].type==Variant::Dict)
                        fontDict = move(descendant->list[0].dict);
                }
                if(!fontDict.contains("FontDescriptor"_)) continue;
                String name = move(fontDict.at("BaseFont"_).data);
                auto descriptor = parseVariant(xref[fontDict.at("FontDescriptor"_).integer()]).dict;
                Variant* fontFile = descriptor.find("FontFile"_)?:descriptor.find("FontFile2"_)?:descriptor.find("FontFile3"_);
                if(!fontFile) continue;
                Fonts& font = fonts.insert(e.key, Fonts{move(name), parseVariant(xref[fontFile->integer()]).data, {}, {}});
                Variant* firstChar = fontDict.find("FirstChar"_);
                if(firstChar) font.widths.grow(firstChar->integer());
                Variant* widths = fontDict.find("Widths"_);
                if(widths) for(const Variant& width : widths->list) font.widths << width.real();
            }
            // Parses image definitions
            if(resources.contains("XObject"_)) {
                Variant& object = resources.at("XObject"_);
                map<string,Variant> dict = object.type==Variant::Integer ? parseVariant(xref[object.integer()]).dict : move(object.dict);
                for(auto e: dict) {
                    if(images.contains(String(e.key))) continue;
                    Variant object = parseVariant(xref[e.value.integer()]);
                    if(!object.dict.contains("Width"_) || !object.dict.contains("Height"_)) continue;
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
                                for(int i=0;i<256;i++) { s.match('/'); uint8 v=fromInteger(s.read(3),8); palette[i]=byte4(v,v,v,0xFF); }
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
            // Keeps current page first geometry indices to translate the page primitives after full parse
            uint firstBlit = blits.size, firstLine = lines.size, firstCharacter = characters.size, firstPath=paths.size, firstPolygon=polygons.size;
            // Parses page bounds
            const array<Variant>& box = // Lower-left, upper-right
                    (dict.value("ArtBox"_)?:dict.value("TrimBox"_)?:dict.value("BleedBox"_)?:dict.value("CropBox"_)?:dict.at("MediaBox"_)).list;
            boxMin = vec2(min(box[0].real(),box[2].real()),min(box[1].real(),box[3].real()));
            boxMax = vec2(max(box[0].real(),box[2].real()),max(box[1].real(),box[3].real()));
            // Resets rendering context
            pageMin = vec2(+inf), pageMax=vec2(-inf);
            Cm=Tm=mat3x2(); array<mat3x2> stack;
            Fonts* font=0; float fontSize=1,spacing=0,wordSpacing=0,leading=0; mat3x2 Tlm;
            array<array<vec2>> path;
            array<Variant> args;
            // Dereferences page content
            if(contents->type==Variant::Integer) contents->list << contents->integer();
            array<byte> data;
            for(const auto& contentRef : contents->list) {
                Variant content = parseVariant(xref[contentRef.integer()]);
                assert(content.data, content);
                //for(const Variant& dataRef : content.list) data << parseVariant(xref[dataRef.integer()]).data;
                data << content.data;
            }
            for(TextData s = move(data); s.skip(), s;) {
                string id = s.word("'*"_);
                if(!id || id=="true"_ || id=="false"_) {
                    assert(!((s[0]>='a' && s[0]<='z')||(s[0]>='A' && s[0]<='Z')||s[0]=='\''||s[0]=='"'),s.peek(min(16ul,s.buffer.size-s.index)));
                    args << parseVariant(s);
                    continue;
                }
                uint op = id[0]; if(id.size>1) { op|=id[1]<<8; if(id.size>2) op|=id[2]<<16; }
                switch( op ) {
                default: log("Unknown operator '"_+id+"'"_);
#define OP(c) break;case c:
#define OP2(c1,c2) break;case c1|c2<<8:
#define OP3(c1,c2,c3) break;case c1|c2<<8|c3<<16:
#define f(i) args[i].real()
#define p(x,y) (Cm*vec2(f(x),f(y)))
                    OP('b') drawPath(path,Close|Stroke|Fill|Winding|Trace);
                    OP2('b','*') drawPath(path,Close|Stroke|Fill|OddEven|Trace);
                    OP('B') drawPath(path,Stroke|Fill|Winding|Trace);
                    OP2('B','I') ;
                    OP2('I','D') while(!s.match("EI"_)) s.advance(1);
                    OP2('B','*') drawPath(path,Stroke|Fill|OddEven|Trace);
                    OP3('B','D','C') ;
                    OP3('E','M','C') ;
                    OP('c') path.last() << p(0,1) << p(2,3) << p(4,5);
                    OP('d') ; // setDashOffset();
                    OP('f') drawPath(path,Fill|Winding|Trace);
                    OP2('f','*') drawPath(path,Fill|OddEven|Trace);
                    OP('g') ; //brushColor = f(0);
                    OP('h') ; //closePath
                    OP2('r','g') ; // brushColor = vec3(f(0),f(1),f(2));
                    OP('G') ; // penColor = f(0);
                    OP('i') ;
                    OP('j') ; // joinStyle = {Miter,Round,BevelJoin}[f(0)];
                    OP('J') ; // capStyle = {Flat,Round,Square}[f(0)];
                    OP('l') path.last() << p(0,1) << p(0,1) << p(0,1);
                    OP('m') path << move(array<vec2>()<<p(0,1));
                    OP('M') ; // setMiterLimit(f(0));
                    OP('n') path.clear();
                    OP('q') stack<<Cm;
                    OP('Q') Cm=stack.pop();
                    OP('s') drawPath(path,Close|Stroke|OddEven);
                    OP('S') drawPath(path,Stroke|OddEven|Trace);
                    OP('v') ; // curveTo (replicate first)
                    OP('w') ; // setWidth(Cm.m11()*f(0));
                    OP('W') path.clear(); //intersect winding clip
                    OP('y') ; // curveTo (replicate last)
                    OP2('B','T') Tm=Tlm=mat3x2();
                    OP2('c','s') ; // setFillColorspace
                    OP2('C','S') ; // setStrokeColorspace
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
                    OP2('T','r') ; // setRenderMode
                    OP2('T','z') ; // setHorizontalScaling
                    OP('\'') { Tm=Tlm=mat3x2(0,-leading)*Tlm; drawText(*font,fontSize,spacing,wordSpacing,args[0].data); }
                    OP2('T','j') drawText(*font,fontSize,spacing,wordSpacing,args[0].data);
                    OP2('T','J') for(const auto& e : args[0].list) {
                        if(e.type==Variant::Integer||e.type==Variant::Real) Tm=mat3x2(-e.real()*fontSize/1000,0)*Tm;
                        else if(e.type==Variant::Data) drawText(*font,fontSize,spacing,wordSpacing,e.data);
                        else error("Unexpected type",(int)e.type);
                    }
                    OP2('T','f') font = fonts.find(args[0].data); fontSize=f(1);
                    OP2('T','m') Tm=Tlm=mat3x2(f(0),f(1),f(2),f(3),f(4),f(5));
                    OP2('T','w') wordSpacing=f(0);
                    OP2('W','*') path.clear(); // intersect odd even clip
                }
                args.clear();
            }
            pageMin.x=min(pageMin.x, boxMin.x); pageMax.x = max(pageMax.x, boxMax.x); // Keep full horizontal margins
            pageMin.y=(pageMin.y+min(pageMin.y, boxMin.y))/2; pageMax.y = (pageMax.y+max(pageMax.y, boxMax.y))/2; // Keep half vertical margins
            mat3x2 m (1,0, 0,-1, 0, pageMax.y + (documentMax.y==-inf?0:documentMax.y));
            { vec2 a = m*pageMin, b = m*pageMax; pageMin = min(a, b); pageMax = max(a, b); }
            // Transforms from page to document
            for(Blit& b: blits.slice(firstBlit)) b.position = m*b.position ;
            for(Line& l: lines.slice(firstLine)) { l.a=m*l.a; l.b=m*l.b; }
            for(Character& c: characters.slice(firstCharacter)) c.position=m*c.position;
            for(array<vec2>& path: paths.slice(firstPath)) for(vec2& pos: path) pos=m*pos;
            for(Polygon& polygon: polygons.slice(firstPolygon)) {
                { vec2 a=m*polygon.min, b=m*polygon.max; polygon.min = min(a, b); polygon.max = max(a, b); }
                for(Line& l: polygon.edges) { l.a=m*l.a; l.b=m*l.b; }
#if GL
                for(vec2& pos: polygon.vertices) pos=m*pos;
#endif
            }
            // Updates document bounds
            documentMin = min(documentMin, pageMin);
            documentMax = max(documentMax, pageMax);
        }
        // add any children
        pages << move(dict["Kids"_].list);
    }

    // Normalizes coordinates (aligns top-left to 0, fit width to 1)
    float width = documentMax.x-documentMin.x;
    mat3x2 m (1/width, 0,0, 1/width, -documentMin.x/width, -documentMin.y/width);
    for(Blit& b: blits) b.position = m*b.position ;
    for(Line& l: lines) { l.a=m*l.a; l.b=m*l.b; }
    for(Character& c: characters) c.position=m*c.position, c.size /= width;
    for(array<vec2>& path: paths) for(vec2& pos: path) pos=m*pos;
    for(Polygon& polygon: polygons) {
        { vec2 a=m*polygon.min, b=m*polygon.max; polygon.min = min(a, b); polygon.max = max(a, b); }
        for(Line& l: polygon.edges) { l.a=m*l.a; l.b=m*l.b; }
    }
    height = (documentMax.y-documentMin.y)/width;

    // Sorts primitives for culling
    quicksort(blits), quicksort(lines), quicksort(characters);

    /*//FIXME: interface directly with an array of paths and of characters
    for(const array<vec2>& path : paths) onPath(path);
    for(uint i: range(characters.size)) { Character& c = characters[i]; onGlyph(i, c.position, c.size, c.font.name, c.code, c.index); }
    for(uint i: range(characters.size)) { Character& c = characters[i]; onGlyph(i, c.position, c.size, c.font.name, c.code, c.index); }
    paths.clear();*/
}

vec2 cubic(vec2 A,vec2 B,vec2 C,vec2 D,float t) { return ((1-t)*(1-t)*(1-t))*A + (3*(1-t)*(1-t)*t)*B + (3*(1-t)*t*t)*C + (t*t*t)*D; }
void PDF::drawPath(array<array<vec2>>& paths, int flags) {
    for(array<vec2>& path : paths) {
        //if(path.size > 5) continue; //FIXME: triangulate concave polygons
        for(vec2 p : path) if(p > boxMin && p < boxMax) extend(p); // FIXME: clip
        array<vec2> polyline;
        if(path.size>=4) for(uint i=0; i<path.size-3; i+=3) {
            if( path[i+1] == path[i+2] && path[i+2] == path[i+3] ) {
                polyline << copy(path[i]);
            } else {
                for(int t=0;t<16;t++) polyline << cubic(path[i],path[i+1],path[i+2],path[i+3],float(t)/16);
            }
        }
        polyline << copy(path.last());
        array<Line> lines;
        if(flags&Stroke || (flags&Fill) || polyline.size>16) {
            for(uint i: range(polyline.size-1)) {
                if(polyline[i] != polyline[i+1])
                    lines << Line{ polyline[i], polyline[i+1] };
            }
            if(flags&Close) lines << Line{polyline.last(), polyline.first()};
        }
        if(flags&Stroke) this->lines << lines;
        if(flags&Fill) {
            Polygon polygon;
            // Software rendering (FIXME: precompute line equations)
            polygon.min=path.first(), polygon.max=path.first();
            for(vec2 p : path) {
                polygon.min=min(polygon.min,p);
                polygon.max=max(polygon.max,p);
            }
            assert(polygon.min < polygon.max);
            polygon.edges = move(lines);
            float area=0;
            for(uint i: range(polyline.size)) {
                area += cross(polyline[(i+1)%polyline.size]-polyline[i], polyline[(i+2)%polyline.size]-polyline[i]);
            }
            if(area>0) for(Line& e: polygon.edges) swap(e.a,e.b); // Converts to CCW winding in top-left coordinate system
            polygons << move(polygon);
        }
        if(flags&Trace) this->paths << move(path);
    }
    paths.clear();
}

Font& PDF::getFont(Fonts& fonts, float size) {
     return *(fonts.fonts.find(size) ?: &fonts.fonts.insert(size, ::Font(buffer<byte>((string)fonts.data), size)));
}

void PDF::drawText(Fonts& fonts, int size, float spacing, float wordSpacing, const string& data) {
    assert_(&fonts);
    //if(!fonts) return;
    assert_(fonts.data);
    Font& font = getFont(fonts, size);
    for(uint8 code : data) {
        if(code==0) continue;
        mat3x2 Trm = Tm*Cm;
        uint16 index = font.index(code);
        vec2 position = vec2(Trm(0,2),Trm(1,2));
        if(position > boxMin && position < boxMax) {
            pageMin.y=min(pageMin.y, position.y), pageMax.y=max(pageMax.y, position.y+Trm(0,0)*size);
            characters << Character{&fonts, Trm(0,0)*size, index, position, code};
        }
        float advance = spacing+(code==' '?wordSpacing:0);
        if(code < fonts.widths.size) advance += size*fonts.widths[code]/1000;
        else advance += font./*linearA*/advance(index);
        Tm = mat3x2(advance, 0) * Tm;
    }
}

int2 PDF::sizeHint() { return lastSize ? int2(-lastSize, lastSize*height) : int2(-1); }
void PDF::render(int2 offset, int2 fullSize) {
    lastSize = fullSize.x;
    const float scale = fullSize.x; // Fit width
    int2 size = target.size();

    for(Blit& blit: blits) {
        if(offset.y+scale*(blit.position.y+blit.size.y) < 0) continue;
        if(offset.y+scale*blit.position.y > size.y) break;
        //if(!blit.resized) blit.resized=resize(blit.image,scale*blit.size.x,scale*blit.size.y);
        ::blit(target, offset+int2(scale*blit.position),blit.resized);
    }

    for(const Line& l: lines.slice(lines.binarySearch(Line{vec2(-offset-int2(0,200))/scale,vec2(-offset-int2(0,200))/scale}))) {
        vec2 a = scale*l.a, b = scale*l.b;
        a+=vec2(offset), b+=vec2(offset);
        if(a.y < 0 && b.y < 0) continue;
        if(a.y > size.y+200 && b.y > size.y+200) break;
        if(a.x==b.x) a.x=b.x=round(a.x); if(a.y==b.y) a.y=b.y=round(a.y);
        line(target, a,b);
    }

    for(const Polygon& polygon: polygons) {
        int2 min=offset+int2(floor(scale*polygon.min-vec2(1./2))), max=offset+int2(ceil(scale*polygon.max+vec2(1./2)));
        Rect rect = Rect(min,max) & Rect(size);
        for(int y=rect.min.y; y < ::min<int>(size.y,rect.max.y); y++) {
            for(int x=rect.min.x; x < ::min<int>(size.x,rect.max.x); x++) {
                vec2 p = vec2(x,y)+vec2(1./2); float coverage=1;
                for(const Line& e: polygon.edges) {
                    vec2 a = vec2(offset)+scale*e.a, b=vec2(offset)+scale*e.b;
                    float d = cross(p-a,normalize(b-a));
                    if(d>1./2) goto outside;
                    if(d>-1./2) coverage *= 1./2-d;
                } /*else*/ {
                    blend(target, x,y, 0, coverage?coverage:1);
                }
outside:;
            }
        }
    }

    int i = characters.binarySearch(Character{0,0,0,vec2(-offset-int2(0,100))/scale,0});
    for(const Character& c: characters.slice(i)) {
        int2 pos = offset+int2(round(scale*c.position));
        if(pos.y<=-100) { i++; continue; }
        if(pos.y>=size.y+100) break;
        ::Font& font = getFont(*c.fonts, scale*c.size);
        const Glyph& glyph = font.glyph(c.index);
        if(glyph.image) blit(target, pos+glyph.offset,glyph.image,colors.value(i,black));
        i++;
    }

    for(const_pair<vec2,String> text: (const map<vec2, String>&)annotations) {
        int2 pos = offset+int2(round(scale*text.key));
        if(pos.y<=0) continue;
        if(pos.y>=size.y) continue; //break;
        Text(text.value,14,red).render(target, pos);
    }
}
