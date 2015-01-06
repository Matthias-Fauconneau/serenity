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

static Image trimWhite(const Image& image) {
    int y0 = 0;
    for(int y: range(image.height)) {
        uint sum = 0;
        for(int x : range(image.width)) sum += image(x,y).g; // Assumes r=g=b
        if(sum < image.width*256*31/32) break;
        y0 = y;
    }
    int y1 = image.height;
    for(int y: reverse_range(image.height)) {
        uint sum = 0;
        for(int x : range(image.width)) sum += image(x,y).g; // Assumes r=g=b
        if(sum < image.width*256*31/32) break;
        y1 = y;
    }
    int x0 = 0;
    for(int x: range(image.width)) {
        uint sum = 0;
        for(int y : range(image.height)) sum += image(x,y).g; // Assumes r=g=b
        if(sum < image.height*255) break;
        x0 = x;
    }
    int x1 = image.width;
    for(int x: reverse_range(image.width)) {
        uint sum = 0;
        for(int y : range(image.height)) sum += image(x,y).g; // Assumes r=g=b
        if(sum < image.height*255) break;
        x1 = x;
    }
    return copy(cropShare(image, int2(x0, y0), int2(x1-x0, y1-y0)));
}

static Variant parseVariant(TextData& s) {
    s.whileAny(" \t\r\n"_);
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
        if(s.match("stream"_)) {
            s.whileAny(" \t\r\n"_);
            buffer<byte> stream = unsafeRef( s.until("endstream"_) );
            if(v.dict.contains("Filter"_)) {
                string filter = v.dict.at("Filter"_).list?v.dict.at("Filter"_).list[0].data:v.dict.at("Filter"_).data;
                if(filter=="FlateDecode"_) stream = inflate(stream, true);
                else if(filter=="RunLengthDecode"_) stream = decodeRunLength(stream);
                else { log("Unsupported filter",v.dict); return ""_; }
            }
            if(v.dict.contains("DecodeParms"_)) {
                //assert(v.dict.at("DecodeParms"_).dict.size() == 2, v);
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
                } else if(predictor==1) { // Left
                    for(uint unused y: range(height)) {
                        uint8 a=0;
                        for(uint i=0;i<width;i++) dst[i]= a= a+src[i];
                        src+=width; dst+=width;
                    }
                } else error("Unsupported predictor",predictor);
                stream.size = size-height;
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
    if(s.match("null")) return nullptr;
    error("Unknown type"_, escape(s.slice(s.index, 128)), hex(s.slice(s.index, 128)));
}
static Variant parseVariant(string buffer) { TextData s(buffer); return parseVariant(s); }
static Dict toDict(const array<String>& xref, Variant&& object) { return object.dict ? move(object.dict) : parseVariant(xref[object.integer()]).dict; }

buffer<Graphics> decodePDF(ref<byte> file, array<unique<FontData>>& outFonts) {
    array<String> xref;
    array<Variant> pageXrefs;
    { // Parses cross reference table and catalog
        TextData s (file);
        for(s.index=s.data.size-"0\r\n%%EOF\r\n"_.size; !( /*(s[-2]=='\r' && s[-1]=='\n') ||*/ s[-1]=='\n' || (/*s[-2]==' ' &&*/ s[-1]=='\r') );s.index--) {}
        int index = s.integer(); assert_(index>0, index, s.slice(s.index-16)); s.index = index;
        struct CompressedXRef { uint object, index; }; array<CompressedXRef> compressedXRefs;
        size_t root = invalid;
        for(;;) { /// Parse XRefs
            Variant object = ""_;
            if(s.match("xref"_)) { // Direct cross reference
                while(!s.match("trailer"_)) {
                    s.whileAny(" \t\r\n"_);
                    uint i=s.integer(); assert_(i!=uint(-1)); s.whileAny(" \t\r\n");
                    uint n=s.integer(); s.whileAny(" \t\r\n");
                    if(xref.size<i+n) xref.slice(xref.grow(i+n)).clear();
                    for(;n>0;n--,i++) {
                        int offset=s.integer(); s.whileAny(" \t\r\n"); s.integer(); s.whileAny(" \t\r\n");
                        if(s.match('n')) xref[i] = unsafeRef(s.slice(offset+(i<10?1:(i<100?2:i<1000?3:4))+6));
                        else if(s.match('f')) {}
                        else error("Expected [nf], got", s.untilEnd(), n, i);
                        s.whileAny(" \t\r\n");
                    }
                    s.whileAny(" \t\r\n"_);
                }
                s.whileAny(" \t\r\n");
                object = parseVariant(s);
            } else if(s.isInteger()) { // Cross reference dictionnary
                uint i=s.integer(); assert_(i!=uint(-1)); s.whileAny(" \t\r\n");
                uint unused n=s.integer(); assert_(n==0, n); s.whileAny(" \t\r\n");
                s.skip("obj"_);
                object = parseVariant(s.until("endobj"_));
                assert_(object.data);
                if(xref.size <= i ) xref.slice(xref.grow(i+1)).clear();
                xref[i] = copyRef(object.data);
            } else { s.advance(1); continue; } // Wrong offset, advances until next cross reference //log("Wrong cross reference", escape(s.slice(s.index,128)));
            Dict& dict = object.dict;
            if(dict.contains("Type"_) && dict.at("Type"_).data=="XRef"_) { // Cross reference stream
                const array<Variant>& W = dict.at("W"_).list;
                assert(W[0].integer()==1);
                const int w1 = W[1].integer(); assert(w1==2||w1==3,w1);
                assert(W[2].integer()==0 || W[2].integer()==1);
                uint n = dict.at("Size"_).integer();
                if(xref.size<n) xref.grow(n);
                BinaryData b(object.data, true);
                array<Variant> list;
                if(dict.contains("Index"_)) list = move(dict.at("Index"_).list);
                else list.append( Variant(0) ), list.append( Variant(dict.at("Size"_).integer()) );
                for(uint l: range(list.size/2)) {
                    for(uint i = list[l*2].integer(), n = list[l*2+1].integer(); n>0; n--, i++) {
                        uint8 type = b.read();
                        if(type==0) { // Free objects
                            uint unused offset = (uint16)b.read();
                            if(w1==3) offset = (offset<<8) | (uint8)b.read();
                            uint8 unused g = (uint8)b.read();
                        } else if(type==1) { // Uncompressed objects
                            uint offset = (uint16)b.read();
                            if(w1==3) offset = (offset<<8) | (uint8)b.read();
                            b.advance(W[2].integer());
                            TextData x (s.slice(offset));
                            size_t unused index = x.integer(false);
                            assert(index == i);
                            x.skip(" 0 obj\r"_);
                            xref[i] = unsafeRef(x.slice(x.index));
                        } else if(type==2) { // Compressed objects
                            uint stream = (uint16)b.read();
                            if(w1==3) stream = (stream<<8) | (uint8)b.read();
                            uint8 index=0; if(W[2].integer()) index = (uint8)b.read();
                            compressedXRefs.append( CompressedXRef{stream, index} );
                        } else error(type);
                    }
                }
            }
            if(root==invalid && dict.contains("Root"_)) { assert_(dict.at("Root"_).type==Variant::Integer, dict); root = dict.at("Root"_).integer(); }
            if(!dict.contains("Prev"_)) break;
            s.index = dict.at("Prev"_).integer();
            assert_(int(s.index) > 0);
        }
        for(CompressedXRef ref: compressedXRefs) {
            Variant stream = parseVariant(xref[ref.object]);
            TextData s(stream.data);
            uint objectNumber=-1,offset=-1;
            for(uint i=0;i<=ref.index;i++) {
                objectNumber=s.integer(); s.match(' ');
                offset=s.integer(); s.match(' ');
            }
            xref[objectNumber] = copyRef( s.slice(stream.dict.at("First"_).integer()+offset) );
        }
        // Parses page references in catalog
        assert_(root!=invalid, xref.size);
        Dict catalog = parseVariant(xref[root]).dict;
        Variant kids = move(parseVariant(xref[catalog.at("Pages"_).integer()]).dict.at("Kids"_));
        pageXrefs = kids.list ? move(kids.list) : parseVariant(xref[kids.integer()]).list;
    }

    // Ressources
    struct FontData : ::FontData {
        using ::FontData::FontData;
        array<float> widths;
    };
    map<String, unique<FontData>> fonts; // Referenced by glyphs
    map<String, Image> images; // Moved by blits

    // Device output
    array<Graphics> pages;

    for(size_t i=0; i<pageXrefs.size; i++) { // Does not use range as new pages might be added within loop
        const Variant& page = pageXrefs[i];
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
                            fontDict = copy(descendant->list[0].dict);
                    }
                    assert_(fontDict.values.size == fontDict.keys.size);
                    if(!fontDict.contains("FontDescriptor"_)) { log("Missing FontDescriptor", fontDict); continue; }
                    auto descriptor = parseVariant(xref[fontDict.at("FontDescriptor"_).integer()]).dict;
                    Variant* fontFile = descriptor.find("FontFile"_)?:descriptor.find("FontFile2"_)?:descriptor.find("FontFile3"_);
                    if(!fontFile) { /*log("Missing FontFile", fontDict);*/ continue; }
                    String name = move(fontDict.at("BaseFont"_).data);
                    FontData& font = fonts.insert(copyRef(e.key), unique<FontData>(parseVariant(xref[fontFile->integer()]).data, move(name)));
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
                    if(!object.dict.contains("Width"_) || !object.dict.contains("Height"_)) { log("Missing Width|Height", object.dict, e.value); continue; }
                    Image image(object.dict.at("Width"_).integer(), object.dict.at("Height"_).integer());
                    int depth=object.dict.at("BitsPerComponent"_).integer();
                    assert(depth, object.dict.at("BitsPerComponent"_).integer());
                    byte4 palette[256]; bool indexed=false;
                    if(depth==8 && object.dict.contains("ColorSpace"_)) {
                        Variant cs = object.dict.at("ColorSpace"_).data ? move(object.dict.at("ColorSpace"_).data) :
                                                                          parseVariant(xref[object.dict.at("ColorSpace"_).integer()]);
                        if(cs.data=="DeviceGray"_ ) {}
                        else if(cs.data=="DeviceRGB"_) { if(depth==8) depth=24; }
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
                    const uint8* src = (uint8*)object.data.data;
                    assert_(object.data.size==image.height*((image.width*depth+7)/8), object.dict, object.data.size, image.height*image.width*depth/8);
                    byte4* dst = (byte4*)image.data;
                    if(depth==1) {
                        for(uint y=0;y<image.height;y++) for(uint x=0;x<image.width; src++) {
                            for(int b=7;b>=0 && x<image.width;b--,x++,dst++) dst[0] = src[0]&(1<<b) ? 0xFF : 0;
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
                    images.insert(copyRef(e.key), trimWhite(image));
                }
            }
        }
        Variant* contents = dict.find("Contents"_);
        if(contents) {
            // Parses page bounds
            const array<Variant>& pageBox = // Lower-left, upper-right
                    (dict.find("ArtBox"_)?:dict.find("TrimBox"_)?:dict.find("BleedBox"_)?:dict.find("CropBox"_)?:&dict.at("MediaBox"_))->list;
            Rect box (vec2(min(pageBox[0].real(),pageBox[2].real()),min(pageBox[1].real(),pageBox[3].real())),
                    vec2(max(pageBox[0].real(),pageBox[2].real()),max(pageBox[1].real(),pageBox[3].real())));

            // Rendering context
            mat3x2 Cm, Tm; array<mat3x2> stack;
            unique<FontData>* font=0; float fontSize=1,spacing=0,wordSpacing=0,leading=0; mat3x2 Tlm;
            bgr3f brushColor = black;
            array<array<vec2>> paths;

            // Device output
            Graphics page;

            // Conversion helpers
            enum Flags { Close=1<<0, Stroke=1<<1, Fill=1<<2, OddEven=1<<3, Winding=1<<4 };
            auto drawPaths = [&paths, &brushColor, box, &page](int flags) {
                if(brushColor ==  white) { paths.clear(); return; }
                for(ref<vec2> path : paths) {
                    assert_(path);
                    Rect bounds (path[0], path[0]);
                    for(vec2 p : path) if(box.contains(p)) bounds.extend(p);
                    if(bounds.max.x < box.min.x) continue; // Clips
                    page.bounds.extend(bounds.min);
                    page.bounds.extend(bounds.max);
                    if(path.size == 4 /*&& path[1]==path[2] && path[2]==path[3]*/) {
                        bool fuzzy = false; // Finale? seems to export files with thick lines emulated by repeated thin lines O_o
                        for(auto& o : page.lines) if(sq(path[0]-o.a) < 1 && sq(path[3]-o.b) < 1) fuzzy=true;
                        if(fuzzy) continue;
                        page.lines.append(path[0], path[3]);
                    } else {
                        if(flags&Fill) {
                            if(!(flags&Close)) path = path.slice(0, path.size-1);
                            page.cubics.append( copyRef(path) );
                        }
                        else if(flags&Stroke) {
                            array<vec2> polyline;
                            if(path.size>=4) for(uint i=0; i<path.size-3; i+=3) {
                                if(path[i+1] == path[i+2] && path[i+2] == path[i+3]) polyline.append( copy(path[i]) );
                            }
                            polyline.append( copy(path.last()) );

                            array<Line> lines;
                            for(size_t i: range(polyline.size-1)) {
                                if(polyline[i] != polyline[i+1])
                                    lines.append( Line{ polyline[i], polyline[i+1] } );
                            }
                            if(flags&Close) lines.append(polyline.last(), polyline.first());
                            page.lines.append( lines );
                        }
                    }
                }
                paths.clear();
            };

            auto drawText= [&Cm,&Tm,box,&page](FontData& fontData, float size, float spacing, float wordSpacing, string data) {
                assert_(&fontData && fontData.data);
                if(!(Cm*Tm)(0,0)) { log("Rotated glyph"); return; } // FIXME: rotated glyphs
                Font& font = fontData.font((Cm*Tm)(0,0)*size);
                for(uint8 code : data) {
                    if(code==0 && data.size==2) { /*assert_(data[1]!=0, hex(data));*//*Misparsed 16bit ccode*/ continue; }
                    if(code==0 && data.size==4) { assert_(data[1]!=0 && data[2]==0 && data[3]!=0, hex(data));/*2x Misparsed 16bit ccode*/ continue; }
                    mat3x2 Trm = Cm*Tm;
                    uint index = font.index(code);
                    vec2 position = vec2(Trm(0,2),Trm(1,2));
                    if(Trm(0,0)*size >= 16) // Filters grace notes (FIXME: select on which sheets to apply)
                    if(box.contains(position)) page.glyphs.append(position, Trm(0,0)*size, fontData, code, index);
                    float advance = spacing+(code==' '?wordSpacing:0);
                    if(code < fontData.widths.size) advance += size*fontData.widths[code]/1000;
                    else advance += font.metrics(index).advance;
                    Tm = Tm * mat3x2(advance, 0);
                }
            };

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
                    OP('b') drawPaths(Close|Stroke|Fill|Winding);
                    OP2('b','*') drawPaths(Close|Stroke|Fill|OddEven);
                    OP('B') drawPaths(Stroke|Fill|Winding);
                    OP2('B','I') ;
                    OP2('I','D') while(!s.match("EI"_)) s.advance(1);
                    OP2('B','*') drawPaths(Stroke|Fill|OddEven);
                    OP3('B','D','C') ;
                    OP3('E','M','C') ;
                    OP('c') paths.last().append(ref<vec2>{p(0,1), p(2,3), p(4,5)});
                    OP('d') ; // setDashOffset();
                    OP('f') drawPaths(Fill|Winding);
                    OP2('f','*') drawPaths(Fill|OddEven);
                    OP('g') brushColor = f(0);
                    OP('h') ; //closePaths
                    OP2('r','g') ; // brushColor = vec3(f(0),f(1),f(2));
                    OP('G') ; // penColor = f(0);
                    OP('i') ;
                    OP('j') ; // joinStyle = {Miter,Round,BevelJoin}[f(0)];
                    OP('J') ; // capStyle = {Flat,Round,Square}[f(0)];
                    OP('l') paths.last().append(ref<vec2>{p(0,1), p(0,1), p(0,1)});
                    OP('m') paths.append( copyRef( ref<vec2>{p(0,1)} ) );
                    OP('M') ; // setMiterLimit(f(0));
                    OP('n') paths.clear();
                    OP('q') stack.append( Cm );
                    OP('Q') Cm = stack.pop();
                    OP('s') drawPaths(Close|Stroke|OddEven);
                    OP('S') drawPaths(Stroke|OddEven);
                    OP('v') ; // curveTo (replicate first)
                    OP('w') ; // setWidth(Cm.m11()*f(0));
                    OP('W') paths.clear(); //intersect winding clip
                    OP('y') ; // curveTo (replicate last)
                    OP2('B','T') Tm=Tlm=mat3x2();
                    OP2('c','s') ; // setFillColorspace
                    OP2('C','S') ; // setStrokeColorspace
                    OP2('c','m') Cm = Cm*mat3x2(f(0),f(1),f(2),f(3),f(4),f(5));
                    OP2('D','o') if(images.contains(args[0].data)) {
                        page.bounds.extend(Cm*vec2(0,0)); page.bounds.extend(Cm*vec2(1,1));
                        page.blits.append(Cm*vec2(0,1), Cm*vec2(1,1)-Cm*vec2(0,0), move(images.at(args[0].data)));
                        page.blits.last().size.x = page.blits.last().image.size.x*page.blits.last().size.y/page.blits.last().image.size.y; // Fit height to preserve aspect ratio
                        page.bounds.extend(page.blits.last().origin + vec2(page.blits.last().size.x, 0));
                    } else log("No such image", args[0].data, images.keys);
                    OP2('E','T') ;
                    OP2('g','s') ;
                    OP2('r','e') {
                        vec2 p1 = p(0,1), p2 = p1 + vec2(Cm(0,0)*f(2),Cm(1,1)*f(3));
                        paths.append(copyRef(ref<vec2>{p1,
                                                      vec2(p1.x,p2.y), vec2(p1.x,p2.y), vec2(p1.x,p2.y),
                                                      p2, p2, p2,
                                                      vec2(p2.x,p1.y), vec2(p2.x,p1.y), vec2(p2.x,p1.y)}));
                    }
                    OP2('S','C') ;
                    OP2('s','c') ;
                    OP3('S','C','N') ;
                    OP3('s','c','n') ;
                    OP2('T','*') Tm=Tlm=Tlm*mat3x2(0,-leading);
                    OP2('T','c') spacing=f(0);
                    OP2('T','d') Tm=Tlm=Tlm*mat3x2(f(0),f(1));
                    OP2('T','D') Tm=Tlm=Tlm*mat3x2(f(0),f(1)); leading=-f(1);
                    OP2('T','L') leading=f(0);
                    OP2('T','r') ; // setRenderMode
                    OP2('T','z') ; // setHorizontalScaling
                    OP('\'') { Tm=Tlm=Tlm*mat3x2(0,-leading); drawText(*font,fontSize,spacing,wordSpacing,args[0].data); }
                    OP2('T','j') if(font) drawText(*font, fontSize, spacing, wordSpacing, args[0].data); //else log("Missing font");
                    OP2('T','J') for(const auto& e : args[0].list) {
                        if(e.type==Variant::Integer||e.type==Variant::Real) Tm=Tm*mat3x2(-e.real()*fontSize/1000,0);
                        else if(e.type==Variant::Data) {
                            if(font) drawText(*font,fontSize,spacing,wordSpacing,e.data);
                            //else log("Missing font");
                        } else error("Unexpected type",(int)e.type);
                    }
                    OP2('T','f') font = fonts.find(args[0].data); /*if(!font) log("No such font", args[0].data, fonts.keys);*/ fontSize=f(1);
                    OP2('T','m') Tm=Tlm=mat3x2(f(0),f(1),f(2),f(3),f(4),f(5));
                    OP2('T','w') wordSpacing=f(0);
                    OP2('W','*') paths.clear(); // intersect odd even clip
                }
                args.clear();
            }
            { // Transforms from bottom-left to top left
                mat3x2 m (1,0, 0, -1, 0, page.bounds.max.y);
                { vec2 a = m*page.bounds.min, b = m*page.bounds.max; page.bounds.min = min(a, b); page.bounds.max = max(a, b); }
                for(Blit& o: page.blits) o.origin = m*o.origin;
                for(Line& o: page.lines) o.a=m*o.a, o.b=m*o.b;
                for(Glyph& o: page.glyphs) o.origin=m*o.origin;
                for(Cubic& o: page.cubics) for(vec2& p: o.points) p = m*p;
            }

            { // Normalizes coordinates (Aligns top-left to 0, scales to DPI)
                float scale = min(96.f/72, min(vec2(1366,768)/(page.bounds.size()))); // Fit page
                mat3x2 m (scale, 0,0, scale, -page.bounds.min.x*scale, -page.bounds.min.y*scale);
                { vec2 a = m*page.bounds.min, b = m*page.bounds.max; page.bounds.min = min(a, b); page.bounds.max = max(a, b); }
                for(Blit& o: page.blits) o.origin = m*o.origin, o.size *= scale;
                for(Line& o: page.lines) o.a=m*o.a, o.b=m*o.b;
                for(Glyph& o: page.glyphs) o.origin = m*o.origin, o.fontSize *= scale;
                page.glyphs.filter([](const Glyph& o) { return o.origin.x<0; });
                for(Cubic& o: page.cubics) for(vec2& p: o.points) p = m*p;
            }
            pages.append(move(page));
        }
        // add any children
        if(dict.contains("Kids"_)) pageXrefs.append( move(dict.at("Kids"_).list) );
    }
    for(unique<FontData>& fonts: fonts.values) outFonts.append(move(fonts));
    return move(pages);
}
