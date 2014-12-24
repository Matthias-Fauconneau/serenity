#include "pdf-renderer.h"
#include "variant.h"
#include "file.h"
#include "font.h"
#include "deflate.h"
#include "graphics.h"
#include "text.h"
#include "rle.h"
#include "matrix.h"
#include "function.h"

static Variant parseVariant(TextData& s) {
    s.whileAny(" \t\r\n");
    if("0123456789.-"_.contains(s.peek())) {
        string number = s.whileDecimal();
        if(s[0]==' '&&(s[1]>='0'&&s[1]<='9')&&s[2]==' '&&s[3]=='R') s.advance(4); //FIXME: regexp
        return number.contains('.') ? Variant(parseDecimal(number)) : Variant(parseInteger(number));
    }
    if(s.match('/')) return copyRef(s.identifier("-+."_));
    if(s.match('(')) {
        array<char> data;
        while(!s.match(')')) data.append( s.character() );
        return move(data);
    }
    if(s.match('[')) {
        array<Variant> list;
        s.whileAny(" \t\r\n");
        while(s && !s.match(']')) { list.append( parseVariant(s) ); s.whileAny(" \t\r\n"); }
        return move(list);
    }
    if(s.match("<<"_)) {
        map<String,Variant> dict;
        for(;;) {
            for(;!s.match('/');s.advance(1)) if(s.match(">>"_)) goto dictionaryEnd;
            string key = s.identifier("."_);
            dict.insert(copyRef(key), parseVariant(s));
        }
        dictionaryEnd: s.whileAny(" \t\r\n");
        Variant v = move(dict);
        if(s.match("stream"_)) { s.whileAny(" \t\r\n");
            array<byte> stream = unsafeRef( s.until("endstream"_) );
            if(v.dict.contains("Filter"_)) {
                string filter = v.dict.at("Filter"_).list?v.dict.at("Filter"_).list[0].data:v.dict.at("Filter"_).data;
                if(filter=="FlateDecode"_) stream = inflate(stream, true);
                else if(filter=="RunLengthDecode"_) stream = decodeRunLength(stream);
                else { error("Unsupported filter",v.dict); /*return Variant();*/ }
            }
            if(v.dict.contains("DecodeParms"_)) {
                assert(v.dict.at("DecodeParms"_).dict.size() == 2);
                uint size = stream.size;
                uint width = v.dict.at("DecodeParms"_).dict.contains("Columns"_) ? v.dict.at("DecodeParms"_).dict.at("Columns"_).integer() : 1;
                uint height = size/(width+1);
                assert(size == (width+1)*height); // grayscale
                const byte* src = stream.data;
                byte* dst = stream.begin(); // in-place
                int predictor = v.dict.at("DecodeParms"_).dict.contains("Predictor"_) ? v.dict.at("DecodeParms"_).dict.at("Predictor"_).integer() : 1;
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
        array<char> data;
        while(!s.match('>')) data.append( parseInteger(s.read(2),16) );
        return move(data);
    }
    if(s.match("true"_)) return true;
    if(s.match("false"_)) return false;
    error("Unknown type"_,s.slice(s.index-32,32),"|"_,s.slice(s.index,32),s.data.size-s.index);
    //return Variant();
}
static Variant parseVariant(const string& buffer) { TextData s(buffer); return parseVariant(s); }
static Dict toDict(const array<String>& xref, Variant&& object) { return object.dict ? move(object.dict) : parseVariant(xref[object.integer()]).dict; }


/// Portable Document Format rendering context
struct PDF {
    PDF(string data);

    // Current page rendering context
    mat3x2 Tm,Cm;
    vec2 boxMin, boxMax;
    vec2 pageMin, pageMax;
    void extend(vec2 p) { pageMin=min(pageMin, p), pageMax=max(pageMax, p); }

    // Rendering primitives
    struct Line { vec2 a, b; bool operator <(const Line& o) const{return a.y<o.a.y || b.y<o.b.y;}};
    array<Line> lines;
    enum Flags { Close=1,Stroke=2,Fill=4,OddEven=8,Winding=16,Trace=32 };
    void drawPath(ref<array<vec2>> paths, int flags);

    struct Fonts {
        String name;
        buffer<byte> data;
        map<float, unique<Font>> fonts;
        array<float> widths;
    };
    map<String, Fonts> fonts;
    Font& getFont(Fonts& fonts, float size);

    struct Character {
        Fonts* fonts; float size; uint16 index; vec2 position; uint16 code;
        bool operator <(const Character& o) const{return position.y<o.position.y;}
    };
    array<Character> characters;
    void drawText(Fonts& fonts, int fontSize, float spacing, float wordSpacing, const string& data);

    map<String, Image> images;
    struct Blit {
        vec2 position, size; Image image; Image resized;
        bool operator <(const Blit& o) const{return position.y<o.position.y;}
    };
    array<Blit> blits;

    /*struct Polygon {
        vec2 min,max; array<Line> edges;
    };
    array<Polygon> polygons;*/
    array<Cubic> cubics;

    array<Graphics> pages;
};

PDF::PDF(string data) {
    array<String> xref; Dict catalog;
    {
        TextData s (data);
        for(s.index=s.data.size-sizeof("\r\n%%EOF");!( (s[-2]=='\r' && s[-1]=='\n') || s[-1]=='\n' || (s[-2]==' ' && s[-1]=='\r') );s.index--){}
        int index = s.integer(); assert(index>0, s.untilEnd()); s.index = index;
        int root = 0;
        struct CompressedXRef { uint object, index; }; array<CompressedXRef> compressedXRefs;
        for(;;) { /// Parse XRefs
            if(s.match("xref"_)) { // Direct cross reference
                s.whileAny(" \t\r\n");
                uint i=s.integer(); s.whileAny(" \t\r\n");
                uint n=s.integer(); s.whileAny(" \t\r\n");
                if(xref.size<i+n) xref.slice(xref.grow(i+n)).clear();
                for(;n>0;n--,i++) {
                    int offset=s.integer(); s.whileAny(" \t\r\n"); s.integer(); s.whileAny(" \t\r\n");
                    if(s.match('n')) xref[i] = unsafeRef(s.slice(offset+(i<10?1:(i<100?2:i<1000?3:4))+6));
                    else if(s.match('f')) {}
                    else error(s.untilEnd());
                    s.whileAny(" \t\r\n");
                }
                if(!s.match("trailer"_)) error("trailer");
                s.whileAny(" \t\r\n");
            } else { // Cross reference dictionnary
                uint i=s.integer(); s.whileAny(" \t\r\n");
                uint unused n=s.integer(); s.whileAny(" \t\r\n");
                s.skip("obj"_);
                if(xref.size<=i) xref.grow(i+1);
                xref[i] = unsafeRef(s.until("endobj"_));
            }
            Variant object = parseVariant(s);
            Dict& dict = object.dict;
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
                else list.append( Variant(0) ), list.append( Variant(dict.at("Size"_).integer()) );
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
                            xref[i] = unsafeRef(s.slice(offset+(i<10?1:(i<100?2:i<1000?3:4))+6));
                            b.advance(W[2].integer());
                            //log("u",hex(offset));
                        } else if(type==2) { // compressed objects
                            uint16 stream=b.read();
                            if(w1==3) stream = stream<<16|(uint8)b.read();
                            uint8 index=0; if(W[2].integer()) index=b.read();
                            compressedXRefs.append( CompressedXRef{stream,index} );
                        } else error("type",type);
                    }
                }
            }
            if(!root && dict.contains("Root"_)) { assert_(dict.at("Root"_).type==Variant::Integer, dict); root = dict.at("Root"_).integer(); }
            if(!dict.contains("Prev"_)) break;
            s.index = dict.at("Prev"_).integer();
            assert_(int(s.index) > 0);
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
            xref[objectNumber] = unsafeRef( s.slice(stream.dict.at("First"_).integer()+offset) );
        }
    }
    Variant kids = move(parseVariant(xref[catalog.at("Pages"_).integer()]).dict.at("Kids"_));
    array<Variant> pages = kids.list ? move(kids.list) : parseVariant(xref[kids.integer()]).list;
    //vec2 documentMin = vec2(+inf), documentMax = vec2(-inf);
    //struct Break { size_t blits, lines, characters, polygons; };
    //array<Break> breaks;
    for(uint i=0; i<pages.size; i++) {
        const Variant& page = pages[i];
        auto dict = parseVariant(xref[page.integer()]).dict;
        assert_(!dict.contains("UserUnit"_), dict);
        if(dict.contains("Resources"_)) {
            auto resources = toDict(xref, move(dict.at("Resources"_)));
            // Parses font definitions
            if(resources.contains("Font"_)) {
                for(auto e : toDict(xref, move(resources.at("Font"_)))) {
                    if(fonts.contains(e.key)) continue;
                    Dict fontDict = parseVariant(xref[e.value.integer()]).dict;
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
                    Fonts& font = fonts.insert(copyRef(e.key), Fonts{move(name), parseVariant(xref[fontFile->integer()]).data, {}, {}});
                    Variant* firstChar = fontDict.find("FirstChar"_);
                    if(firstChar) font.widths.grow(firstChar->integer());
                    Variant* widths = fontDict.find("Widths"_);
                    if(widths) for(const Variant& width : widths->list) font.widths.append( width.real() );
                }
            }
            // Parses image definitions
            if(resources.contains("XObject"_)) {
                Variant& object = resources.at("XObject"_);
                Dict dict = object.type==Variant::Integer ? parseVariant(xref[object.integer()]).dict : move(object.dict);
                for(auto e: dict) {
                    if( images.contains(unsafeRef(e.key)) ) continue;
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
                                for(int i=0;i<256;i++) { s.match('/'); uint8 v = parseInteger(s.read(3),8); palette[i]=byte4(v,v,v,0xFF); }
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
                    images.insert(unsafeRef(e.key), move(image));
                }
            }
        }
        Variant* contents = dict.find("Contents"_);
        if(contents) {
            // Keeps current page first geometry indices to translate the page primitives after full parse
            //breaks.append({blits.size, lines.size, characters.size, polygons.size});
            //uint firstBlit = blits.size, firstLine = lines.size, firstCharacter = characters.size, firstPolygon=polygons.size;
            // Parses page bounds
            const array<Variant>& box = // Lower-left, upper-right
                    (dict.find("ArtBox"_)?:dict.find("TrimBox"_)?:dict.find("BleedBox"_)?:dict.find("CropBox"_)?:&dict.at("MediaBox"_))->list;
            boxMin = vec2(min(box[0].real(),box[2].real()),min(box[1].real(),box[3].real()));
            boxMax = vec2(max(box[0].real(),box[2].real()),max(box[1].real(),box[3].real()));
            // Resets rendering context
            pageMin = vec2(+inf), pageMax=vec2(-inf);
            Cm=Tm=mat3x2(); array<mat3x2> stack;
            Fonts* font=0; float fontSize=1,spacing=0,wordSpacing=0,leading=0; mat3x2 Tlm;
            array<array<vec2>> path;
            array<Variant> args;
            // Dereferences page content
            if(contents->type==Variant::Integer) contents->list.append( contents->integer() );
            array<byte> data;
            for(const auto& contentRef : contents->list) {
                Variant content = parseVariant(xref[contentRef.integer()]);
                assert(content.data, content);
                //for(const Variant& dataRef : content.list) data << parseVariant(xref[dataRef.integer()]).data;
                data.append( content.data );
            }
            for(TextData s = move(data); s.whileAny(" \t\r\n"), s;) {
                string id = s.word("'*"_);
                if(!id || id=="true"_ || id=="false"_) {
                    assert(!((s[0]>='a' && s[0]<='z')||(s[0]>='A' && s[0]<='Z')||s[0]=='\''||s[0]=='"'),s.peek(min(16ul,s.data.size-s.index)));
                    args.append( parseVariant(s) );
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
                    OP('b') drawPath(path,Close|Stroke|Fill|Winding|Trace); path.clear();
                    OP2('b','*') drawPath(path,Close|Stroke|Fill|OddEven|Trace); path.clear();
                    OP('B') drawPath(path,Stroke|Fill|Winding|Trace);
                    OP2('B','I') ;
                    OP2('I','D') while(!s.match("EI"_)) s.advance(1);
                    OP2('B','*') drawPath(path,Stroke|Fill|OddEven|Trace); path.clear();
                    OP3('B','D','C') ;
                    OP3('E','M','C') ;
                    OP('c') path.last().append(ref<vec2>{p(0,1), p(2,3), p(4,5)});
                    OP('d') ; // setDashOffset();
                    OP('f') drawPath(path,Fill|Winding|Trace); path.clear();
                    OP2('f','*') drawPath(path,Fill|OddEven|Trace); path.clear();
                    OP('g') ; //brushColor = f(0);
                    OP('h') ; //closePath
                    OP2('r','g') ; // brushColor = vec3(f(0),f(1),f(2));
                    OP('G') ; // penColor = f(0);
                    OP('i') ;
                    OP('j') ; // joinStyle = {Miter,Round,BevelJoin}[f(0)];
                    OP('J') ; // capStyle = {Flat,Round,Square}[f(0)];
                    OP('l') path.last().append(ref<vec2>{p(0,1), p(0,1), p(0,1)});
                    OP('m') path.append( copyRef( ref<vec2>{p(0,1)} ) );
                    OP('M') ; // setMiterLimit(f(0));
                    OP('n') path.clear();
                    OP('q') stack.append( Cm );
                    OP('Q') Cm = stack.pop();
                    OP('s') drawPath(path,Close|Stroke|OddEven); path.clear();
                    OP('S') drawPath(path,Stroke|OddEven|Trace); path.clear();
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
                        blits.append( Blit{Cm*vec2(0,1),Cm*vec2(1,1)-Cm*vec2(0,0),share(images.at(args[0].data)),{}} );
                    } //else error("No such image", args[0].data);
                    OP2('E','T') ;
                    OP2('g','s') ;
                    OP2('r','e') {
                        vec2 p1 = p(0,1), p2 = p1 + vec2(f(2)*Cm(0,0),f(3)*Cm(1,1));
                        path.append(copyRef(ref<vec2>{p1, vec2(p1.x,p2.y), vec2(p1.x,p2.y), vec2(p1.x,p2.y),
                                                      p2, p2, p2,
                                                      vec2(p2.x,p1.y), vec2(p2.x,p1.y), vec2(p2.x,p1.y)}));
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
                    OP2('T','f') font = &fonts.at(args[0].data); fontSize=f(1);
                    OP2('T','m') Tm=Tlm=mat3x2(f(0),f(1),f(2),f(3),f(4),f(5));
                    OP2('T','w') wordSpacing=f(0);
                    OP2('W','*') path.clear(); // intersect odd even clip
                }
                args.clear();
            }
            //pageMin.x=min(pageMin.x, boxMin.x); pageMax.x = max(pageMax.x, boxMax.x); // Keep full horizontal margins
            //pageMin.y=(pageMin.y+min(pageMin.y, boxMin.y))/2; pageMax.y = (pageMax.y+max(pageMax.y, boxMax.y))/2; // Keep half vertical margins
            {mat3x2 m (1,0, 0, -1, 0, pageMax.y);
                { vec2 a = m*pageMin, b = m*pageMax; pageMin = min(a, b); pageMax = max(a, b); }
                // Transforms from bottom-left to top left
                for(Blit& b: blits) b.position = m*b.position ;
                for(Line& l: lines) { l.a=m*l.a; l.b=m*l.b; }
                for(Character& c: characters) c.position=m*c.position;
                /*for(Polygon& polygon: polygons) {
                    { vec2 a=m*polygon.min, b=m*polygon.max; polygon.min = min(a, b); polygon.max = max(a, b); }
                    for(Line& l: polygon.edges) { l.a=m*l.a; l.b=m*l.b; }
                }*/
                for(Cubic& o: cubics) for(vec2& p: o.points) p = m*p;
            }

            { // Normalizes coordinates (Aligns top-left to 0, scales to DPI)
                //float scale = 96. /*ppi*/ / 72/*PostScript point per inch*/; // px/pt
                float scale = 768 / (pageMax.y-pageMin.y); // Fit height
                mat3x2 m (scale, 0,0, scale, -pageMin.x*scale, -pageMin.y*scale);
                { vec2 a = m*pageMin, b = m*pageMax; pageMin = min(a, b); pageMax = max(a, b); }
                assert_(int2(pageMin) == int2(0), pageMin);
                for(Blit& b: blits) b.position = m*b.position, b.size *= scale;
                for(Line& l: lines) { l.a=m*l.a; l.b=m*l.b; }
                for(Character& c: characters) c.position=m*c.position, c.size *= scale;
                for(Cubic& o: cubics) for(vec2& p: o.points) p = m*p;
            }

            { // Hints lnes
               // for(Line& l: lines) { l.a=round(l.a); l.b=round(l.b); }
            }

            // Converts to Graphics page
            Graphics page;
            for(Line l: lines) page.lines.append(l.a, l.b);
            for(Character o: characters) {
                Font& font = getFont(*o.fonts, o.size);
                assert_(font.index(o.code) == o.index);
                if(font.metrics(o.index).size) {
                    page.glyphs.append(o.position, font, o.code, o.index);
                    assert_(font.render(o.index).image);
                }
            }
            for(Cubic& o: cubics) page.cubics.append(move(o));
            lines.clear();
            blits.clear();
            characters.clear();
            //polygons.clear();
            cubics.clear();
            page.size = pageMax-pageMin;
            this->pages.append(move(page));
        }
        // add any children
        if(dict.contains("Kids"_)) pages.append( move(dict.at("Kids"_).list) );
    }
}

vec2 cubic(vec2 A,vec2 B,vec2 C,vec2 D,float t) { return ((1-t)*(1-t)*(1-t))*A + (3*(1-t)*(1-t)*t)*B + (3*(1-t)*t*t)*C + (t*t*t)*D; }
//inline float cross(vec2 a, vec2 b) { return a.y*b.x - a.x*b.y; }
void PDF::drawPath(const ref<array<vec2>> paths, int flags) {
    for(ref<vec2> path : paths) {
        //if(path.size > 5) continue; //FIXME: triangulate concave polygons
        for(vec2 p : path) if(p >= boxMin && p <= boxMax) extend(p); // FIXME: clip
        array<vec2> polyline;
        if(path.size>=4) for(uint i=0; i<path.size-3; i+=3) {
            if( path[i+1] == path[i+2] && path[i+2] == path[i+3] ) {
                polyline.append( copy(path[i]) );
            } else {
                for(int t=0;t<16;t++) polyline.append( cubic(path[i],path[i+1],path[i+2],path[i+3],float(t)/16) );
            }
        }
        polyline.append( copy(path.last()) );
        array<Line> lines;
        if(flags&Stroke || (flags&Fill) || polyline.size>16) {
            for(uint i: range(polyline.size-1)) {
                if(polyline[i] != polyline[i+1])
                    lines.append( Line{ polyline[i], polyline[i+1] } );
            }
            if(flags&Close) lines.append( Line{polyline.last(), polyline.first()} );
        }
        if(flags&Fill) {
#if 0
            Polygon polygon;
            // Software rendering (FIXME: precompute line equations)
            polygon.min=path[0], polygon.max=path[0];
            for(vec2 p : path) {
                polygon.min=min(polygon.min,p);
                polygon.max=max(polygon.max,p);
            }
            assert(polygon.min <= polygon.max);
            polygon.edges = move(lines);
            /*float area=0;
            for(uint i: range(polyline.size)) {
                area += cross(polyline[(i+1)%polyline.size]-polyline[i], polyline[(i+2)%polyline.size]-polyline[i]);
            }
            if(area>0) for(Line& e: polygon.edges) swap(e.a,e.b); // Converts to CCW winding in top-left coordinate system*/
            polygons.append( move(polygon) );
#else
            if(!(flags&Close)) {
                //assert_(path[0]==path.last(), path);
                path = path.slice(0, path.size-1);
            }
            cubics.append( copyRef(path) );
#endif
        }
        else if(flags&Stroke) this->lines.append( lines );
    }
}

Font& PDF::getFont(Fonts& fonts, float size) {
     return *(fonts.fonts.find(size) ?: &fonts.fonts.insert(size, ::unique<Font>(unsafeRef(fonts.data), size, fonts.name)));
}

void PDF::drawText(Fonts& fonts, int size, float spacing, float wordSpacing, const string& data) {
    assert_(&fonts);
    //if(!fonts) return;
    assert_(fonts.data);
    Font& font = getFont(fonts, (Tm*Cm)(0,0)*size);
    for(uint8 code : data) {
        if(code==0) continue;
        mat3x2 Trm = Tm*Cm;
        uint16 index = font.index(code);
        vec2 position = vec2(Trm(0,2),Trm(1,2));
        auto metrics = font.metrics(index);
        if(position > boxMin && position < boxMax && metrics.size) {
            if(find(font.name, "HelsinkiStd"_)) { // HACK: to trim text
                pageMin.x=min(pageMin.x, position.x+metrics.bearing.x), pageMax.x=max(pageMax.x, position.y+metrics.bearing.x+metrics.width);
                pageMin.y=min(pageMin.y, position.y-metrics.bearing.y), pageMax.y=max(pageMax.y, position.y-metrics.bearing.y+metrics.height);
            }
            characters.append( Character{&fonts, Trm(0,0)*size, index, position, code} );
        }
        float advance = spacing+(code==' '?wordSpacing:0);
        if(code < fonts.widths.size) advance += size*fonts.widths[code]/1000;
        else advance += metrics.advance;
        Tm = mat3x2(advance, 0) * Tm;
    }
}

buffer<Graphics> decodePDF(ref<byte> file, array<unique<Font>>& fonts) {
    PDF pdf(file);
    for(PDF::Fonts& pdfFonts: pdf.fonts.values) fonts.append(move(pdfFonts.fonts.values));
    return move(pdf.pages);
}
