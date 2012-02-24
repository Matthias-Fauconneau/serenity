#include "pdf.h"
#include "file.h"
#include "gl.h"
#include "font.h"
#include <float.h>
#include <math.h>
#include <zlib.h>
#include <stdlib.h>
//#include <ft2build.h>
//#include <freetype/freetype.h>

struct Variant { //TODO: union
    enum { Number, Data, List, Dict } type;
    float number=0; string data; array<Variant> list; map<string,Variant> dict;
    Variant(double number) : type(Number), number(number) {}
    Variant(string&& data) : type(Data), data(move(data)) {}
    Variant(array<Variant>&& list) : type(List), list(move(list)) {}
    Variant(map<string,Variant>&& dict) : type(Dict), dict(move(dict)) {}
};
string str(const Variant& o) {
    if(o.type==Variant::Number) return str(o.number);
    if(o.type==Variant::Data) return str(o.data);
    if(o.type==Variant::List) return str(o.list);
    if(o.type==Variant::Dict) return str(o.dict);
    error("Invalid Variant"_,int(o.type));
}


static Variant parse_(const char*& s) {
    while(s[0]==' '||s[0]=='\r'||s[0]=='\n') s++;
    if((s[0]>='0'&&s[0]<='9') || s[0]=='.' || s[0]=='-') {
        double number = readFloat(s);
        if(s[0]==' '&&(s[1]>='0'&&s[1]<='9')&&s[2]==' '&&s[3]=='R') s+=4;
        return number;
    }
    if(s[0]=='/') { s++;
        const char* n=s;
        while((s[0]>='a' && s[0]<='z')||(s[0]>='A' && s[0]<='Z')||(s[0]>='0'&&s[0]<='9')||s[0]=='-'||s[0]=='+') s++;
        return string(n,s);
    }
    if(s[0]=='(') { s++;
        bool escape=false;
        const char* e=s; for(;e[0]!=')';e++) { if(e[0]=='\\') { escape=true; e++; } }
        if(!escape) { string data(s,e); s=e; s++; return move(data); } //slice reference
        string data(e-s); //reserve
        for(;s[0]!=')';s++) {
            if(s[0]=='\\') {
                s++;
                int i="nrtbf()\\"_.indexOf(s[0]);
                assert(i>=0,"unknown escape sequence"_,s[0]);
                data << "\n\r\t\b\f()\\"[i];
            } else data << s[0];
        }
        s++;
        return move(data);
    }
    if(s[0]=='[') { s++;
        array<Variant> list;
        for(;s[0]!=']';) { list << parse_(s); while(s[0]==' '||s[0]=='\r'||s[0]=='\n') s++; }
        s++;
        return move(list);
    }
    if(s[0]=='<' && s[1]=='<') { s+=2;
        map<string,Variant> dict;
        for(;;) {
            while(s[0]!='/' && !(s[0]=='>' && s[1]=='>')) s++;
            if(s[0]=='>' && s[1]=='>') break;
            s++;
            const char* k=s;
            while((s[0]>='a' && s[0]<='z')||(s[0]>='A' && s[0]<='Z')||(s[0]>='0'&&s[0]<='9')) s++;
            const char* e=s;
            dict.insert(string(k,e), parse_(s));
        }
        s+=2; while(s[0]==' '||s[0]=='\r'||s[0]=='\n') s++;
        if(s[0]=='s' && s[1]=='t' && s[2] == 'r' && s[3]=='e' && s[4]=='a' && s[5] == 'm') {
            s+=sizeof("stream"); if(s[0]=='\r'||s[0]=='\n') s++;
            const char* stream=s;
            while(!(s[0]=='e'&&s[1]=='n'&&s[2]=='d'&&s[3]=='s'&&s[4]=='t'&&s[5]=='r'&&s[6]=='e'&&s[7]=='a'&&s[8]=='m')) s++;
            z_stream z; clear(z);
            int size = s-stream;
            string data(size*256); //FIXME
            z.avail_in = size;
            z.avail_out = size*256;
            z.next_in = (Bytef*)stream;
            z.next_out = (Bytef*)data.data;
            inflateInit(&z);
            inflate(&z, Z_FINISH);
            data.size=z.total_out;
            inflateEnd(&z);
            auto decodeParms = dict.find("DecodeParms"_);
            if(decodeParms) { error("unsupported stream compression"_);
                /*assert(decodeParms->dict.size() == 2);
                int predictor = decodeParms->dict.value("Predictor",Variant(1)).number;
                if(predictor != 12) abort();
                int size = data.size;
                int w = decodeParms->dict.value("Columns",Variant(1)).number;
                int h = size/(w+1);
                assert(size == (w+1)*h);
                const uchar* src = (uchar*)data.constData();
                uchar* dst = (uchar*)data.data();
                for(int y=0;y<h;y++) {
                    int filter = *src++;
                    if(filter != 2) abort();
                    for(int x=0;x<w;x++) {
                        *dst = (y>0 ? *(dst-w) : 0) + *src;
                        dst++; src++;
                    }
                }
                data.resize(size-h);*/
            }
            return move(data);
        }
        return move(dict);
    }
    if(s[0]=='<') { s++;
        string data;
        while(s[0]!='>') { assert(s[1]!='>',"Invalid hex data"_);
            data << ((string("0123456789ABCDEF",16).indexOf(s[0])<<4) | string("0123456789ABCDEF",16).indexOf(s[1]));
            s+=2;
        } s++;
        return move(data);
    }
    error("unknown type"_,string(s-64,64),string(s,64));
    return Variant(0);
}
static Variant parse(const char* s) { return parse_(s); }
static map<string,Variant> toDict(const array<const char*>& xref, Variant&& object) {
    return object.dict ? move(object.dict) : move(parse(xref[object.number]).dict);
}
//#define toDict(object) ({ object.dict ? : parse(xref[object.number]).dict })

void PDF::open(const string& path) {
    string pdf = mapFile(path);
    array<const char*> xref; map<string,Variant> catalog;
    {
        const char* s=pdf.data+pdf.size-sizeof("\r\n%%EOF");
        while(!( (s[-2]=='\r' && s[-1]=='\n') || s[-1]=='\n' || (s[-2]==' ' && s[-1]=='\r') )) s--;
        s=pdf.data+readInteger(s); //startxref
        int root=0;
        for(;;) { /// Parse XRefs
            map<string,Variant> dict;
            if(s[0]=='x'&&s[1]=='r'&&s[2]=='e'&&s[3]=='f') { //xref table
                s+=sizeof("xref"); if(s[0]=='\r'||s[0]=='\n') s++;
                int i=readInteger(s); s++; if(s[0]=='\r'||s[0]=='\n') s++;
                int n=readInteger(s); s++; if(s[0]=='\r'||s[0]=='\n') s++;
                if(xref.size<i+n) xref.resize(i+n);
                for(;n>0;n--,i++, s+=20) xref[i] = pdf.data+atoi(s)+(i<10?1:(i<100?2:3))+6;
                assert(s[0]=='t'&&s[1]=='r'&&s[2]=='a'&&s[3]=='i'&&s[4]=='l'&&s[5] =='e'&&s[6]=='r',"invalid xref"_);
                s+=sizeof("trailer"); if(s[0]=='\r'||s[0]=='\n') s++;
                dict = parse( s ).dict;
            } else error("xref stream"_);
            if(!root) root=dict.at("Root"_).number;
            const Variant* offset = dict.find("Prev"_);
            if(!offset) break;
            s=pdf.data+int(offset->number);
        }
        catalog = parse(xref[root]).dict;
    }
    x1 = +FLT_MAX, x2 = -FLT_MAX; vec2 pageOffset;
    array<Variant> pages = move(parse(xref[catalog.at("Pages"_).number]).dict.at("Kids"_).list);
    for(const Variant& page : pages) {
        //TODO: compute page indices range for culling
        int pageFirstLine = lines.size, pageFirstIndex = vertices.size, pageFirstCharacter = characters.size;
        auto dict = parse(xref[page.number]).dict;
        //pages << dict["Kids"_].list;
        Variant empty(0);
        for(auto e : toDict(xref,move(toDict(xref,move(dict.value("Resources"_,empty))).value("Font"_,empty)))) {
            if(fonts.contains(e.key)) continue;
            auto fontDict = parse(xref[e.value.number]).dict;
            auto descendant = fontDict.find("DescendantFonts"_);
            if(descendant) fontDict = parse(xref[descendant->list[0].number]).dict;
            Font& font = fonts.insert(move(e.key));
            font.name = move(fontDict.at("BaseFont"_).data);
            log("FontName"_,font.name);
            auto descriptor = parse(xref[fontDict.at("FontDescriptor"_).number]).dict;
            auto fontFile = descriptor.find("FontFile"_)?:descriptor.find("FontFile2"_)?:descriptor.find("FontFile3"_);
            if(fontFile) font = Font( parse(xref[fontFile->number]).data );
            auto firstChar = fontDict.find("FirstChar"_);
            if(firstChar) font.widths.resize(firstChar->number);
            auto widths = fontDict.find("Widths"_);
            if(widths) for(const Variant& width : widths->list) font.widths << width.number;
        }
        auto contents = dict.find("Contents"_);
        if(contents) {
            //FIXME: hack to avoid changing scale of recognition distances
            const auto& cropBox = (dict.find("CropBox"_)?:dict.find("MediaBox"_)?:&empty)->list;
            recognitionScale = 1280/cropBox[2].number;
            y1 = FLT_MAX, y2 = -FLT_MAX;
            Cm=Tm=mat32(); array<mat32> stack;
            Font* font=0; float fontSize=1,spacing=0,wordSpacing=0,leading=0; mat32 Tlm;
            array< array<vec2> > path;
            array<Variant> args;
            if(contents->number) contents->list << contents->number;
            for(const auto& contentRef : contents->list) {
                Variant content = parse(xref[contentRef.number]);
                string data = move(content.data);
                assert(content.type == Variant::Data);
                //for(const Variant& dataRef : content.list) data << parse(xref[dataRef.number]).data;
                for(const char* s=data.data,*end=s+data.size;s<end;) {
                    while(s[0]==' '||s[0]=='\r'||s[0]=='\n') s++;
                    if(s>=end) break;
                    uint op = *(uint*) s;
                    int opLength;
                    if((s[1]<'A'&&s[1]!='*')||s[1]>'z'||(s[1]>'Z'&&s[1]<'a')) { op&=0xFF; opLength=1; }
                    else if(s[2]<'A'||s[2]>'z'||(s[2]>'Z'&&s[2]<'a')) { op&=0xFFFF; opLength=2; }
                    else if(s[3]<'A'||s[3]>'z'||(s[3]>'Z'&&s[3]<'a')) { op&=0xFFFFFF; opLength=3; }
                    else goto arg;
                    switch( op ) {
                    default:
arg:
                        assert(!((s[0]>='a' && s[0]<='z')||(s[0]>='A' && s[0]<='Z')||s[0]=='\''||s[0]=='"'),
                        string(s-16,s),'|',string(s,16));
                        args << parse_(s);
                        continue;
#define OP(c) break;case c:
#define OP2(c1,c2) break;case c1|c2<<8:
#define OP3(c1,c2,c3) break;case c1|c2<<8|c3<<16:
#define f(i) args[i].number
#define p(x,y) (Cm*vec2(f(x),f(y)))
                        OP('b') drawPath(path,Close|Stroke|Fill|Winding);
                        OP2('b','*') drawPath(path,Close|Stroke|Fill|OddEven);
                        OP('B') drawPath(path,Stroke|Fill|Winding);
                        OP2('B','*') drawPath(path,Stroke|Fill|OddEven);
                        OP('c') path.last() << p(0,1) << p(2,3) << p(4,5); //TODO: cubic
                        OP('d') {} //setDashOffset();
                        OP('f') drawPath(path,Fill|Winding);
                        OP2('f','*') drawPath(path,Fill|OddEven|Trace);
                        OP('g') ;//brushColor = f(0);
                        OP('h') ;//close path
                        OP2('r','g') ;//brushColor = vec3(f(0),f(1),f(2));
                        OP('G') ;//penColor = f(0);
                        OP('i') ;
                        OP('j') ;//joinStyle = {Miter,Round,BevelJoin}[f(0)];
                        OP('J') ;//capStyle = {Flat,Round,Square)[f(0)];
                        OP('l') path.last() << p(0,1) << p(0,1) << p(0,1);
                        OP('m') path << move(array<vec2>()<<p(0,1));
                        OP('M') ;//setMiterLimit(f(0));
                        OP('q') stack<<Cm;
                        OP('Q') Cm=stack.takeLast();
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
                        OP('\'') {
                            Tm=Tlm=mat32(0,-leading)*Tlm;
                            drawText(font,fontSize,spacing,wordSpacing,args[0].data);
                        }
                        OP2('T','j') drawText(font,fontSize,spacing,wordSpacing,args[0].data);
                        OP2('T','J') for(const auto& e : args[0].list) {
                            if(e.number) Tm=mat32(-e.number*fontSize/1000,0)*Tm;
                            else drawText(font,fontSize,spacing,wordSpacing,e.data);
                        }
                        OP2('T','f') font = fonts.find(args[0].data); fontSize=f(1);
                        OP2('T','m') Tm=Tlm=mat32(f(0),f(1),f(2),f(3),f(4),f(5));
                        OP2('T','w') wordSpacing=f(0);
                    }
                    //log(string(s,opLength),args);
                    args.clear();
                    s+=opLength;
                }
            }
            // tighten page bounds
            pageOffset += vec2(0,y1-y2-16);
            vec2 offset = pageOffset+vec2(0,-y1);
            for(int i=pageFirstLine;i<lines.size;i++) lines[i] += offset;
            for(int i=pageFirstCharacter;i<characters.size;i++) {
                Character& c = characters[i];
                c.position += offset;
                /*onGlyph.emit(i, recognitionScale*vec2(c.position.x,-c.position.y),
                recognitionScale*glyphs[c.glyph-1].recoScale,
                glyphs[c.glyph-1].font->name,glyphs[c.glyph-1].charCode);*/
            }
            /*for(const auto& path : paths) {
                array<vec2> scaled; for(vec2 p : path) scaled<<recognitionScale*vec2(p.x,-p.y-pageOffset.y);
                onPath.emit(scaled);
            }*/
            paths.clear();
            for(int i=pageFirstIndex;i<vertices.size;i++) vertices[i] += offset;
        }
    }
    // Convert coordinates to device space and upload to GL

    float scale = size.x/(x2-x1);

    // Upload stroke paths
    /*for(int i=0;i<lines.size; i+=2) { //hint axis-aligned lines
        vec2& a = lines[i]; vec2& b = lines[i+1];
        if(a.x == b.x) a.x = b.x = round(a.x*scale);
        if(a.y == b.y) a.y = b.y = round(a.y*scale);
    }*/
    for(vec2& v: lines) { v.x=round(scale*(v.x-x1)), v.y=round(-scale*v.y); } //scale all lines
    stroke.primitiveType = Line;
    stroke.upload(lines); lines.clear();

    // Upload fill paths
    //fill.primitiveType = 3;
    //fill.upload(vertices); vertices.clear();
    //fill.upload(indices); indices.clear();

    // Render glyphs, TODO: atlas + blit VBO
    blits.reserve(characters.size);
    for(Character& c: characters) {
        Glyph& glyph = c.font->glyph(round(scale*c.size),c.code);
        if(!glyph.texture) continue;
        vec2 min = vec2(round(scale*(c.position.x-x1)), round(-scale*c.position.y))+glyph.offset;
        vec2 max = min+vec2(glyph.texture.width,glyph.texture.height);
        Blit blit = { min, max, glyph.texture.id }; blits << blit;
    }
}

inline float pow2(float x) { return x*x; }
inline float pow3(float x) { return x*x*x; }
inline float cross(vec2 a, vec2 b) { return a.x*b.y - a.y*b.x; }
vec2 cubic(vec2 A,vec2 B,vec2 C,vec2 D,float t) { return pow3(1-t) * A + 3*pow2(1-t)*t * B + 3*(1-t)*pow2(t) * C + pow3(t) * D; }

void PDF::drawPath(array<array<vec2> >& paths, int flags) {
    for(const auto& path : paths) {
        for(vec2 p : path) extend(p);
        array<vec2> polyline;
        for(int i=0; i < path.size-3; i+=3) {
            if( path[i+1] == path[i+2] && path[i+2] == path[i+3] ) {
                polyline << copy(path[i]);
            } else {
                const float step=1/8.0;
                for(float t=0;t<1;t+=step) { //TODO: adaptive tesselation, indexed line strips
                    //lines << cubic(path[i],path[(i+1)%path.size],path[(i+2)%path.size],path[(i+3)%path.size],t);
                    polyline << cubic(path[i],path[i+1],path[i+2],path[i+3],t);
                }
            }
        }
        polyline << copy(path.last());
        if((flags&Stroke) || polyline.size>16) {
            for(int i=0; i < polyline.size-1; i++) {
                lines << polyline[i] << polyline[i+1];
            }
            if(flags&Close) lines << polyline.last() << polyline.first();
        }
        /*if((flags&Fill) && polyline.size<=16) {
            float area=0; //FIXME: complex polygons are not handled
            for(int i=0; i < polyline.size-1; i++) area += cross(polyline[i],polyline[i+1]);
            if(area < 0) polyline.reverse();

            int index = vertices.size;

            array<int> polygon;
            for(int i = 0; i < polyline.size; i++) polygon << i;

            for( int i=0, loop=0; polygon.size > 2; i++ ) { // ear clipping (FIXME: complex polygons are not handled)
                int a = (i)%polygon.size, b = (i+1)%polygon.size, c = (i+2)%polygon.size;
                vec2 A = polyline[polygon[a]], B = polyline[polygon[b]], C = polyline[polygon[c]];
                if( cross( B-A, C-A ) >= 0 ) { //TODO: check for triangle-curve intersection
                    indices << index+polygon[a] << index+polygon[b] << index+polygon[c];
                    polygon.removeAt(b);
                    loop = i+polygon.size;
                }
                if(i > loop) break; //complex polygon
            }

            vertices << move(polyline);
        }*/
    }
    if(flags&Trace) this->paths << move(paths);
    paths.clear();
}

void PDF::drawText(Font* font, int fontSize, float spacing, float wordSpacing, const string& data) {
    if(!font->face) return;
    for(uint8 code : data) {
        if(code==0) continue;
        mat32 Trm = Tm*Cm;
        vec2 position = vec2(Trm.dx,Trm.dy);
        float size = fontSize*Trm.m11*64;
        GlyphMetrics metrics = font->metrics(round(size), code);
        extend(position); extend(position+metrics.size);
        Character c = { position, font, size, code }; characters << c;
        float advance = spacing+(code==' '?wordSpacing:0);
        if(code < font->widths.size) advance += fontSize*font->widths[code]/1000;
        else advance += metrics.advance.x/Trm.m11;
        Tm = mat32(advance,0) * Tm;
    }
}

void PDF::render(int2 parent) {
    float scroll = this->scroll; ///recognitionScale; //FIXME

    // PDF coordinates to normalized device coordinates
    //vec2 scale = vec2( 2/(x2-x1), 2.0*size.x/size.y/(x2-x1) );
    //vec2 offset = vec2(-1-x1*scale.x,-1+(round((size.y/2)*scroll*scale.y)+0.5)/(size.y/2));
    parent.y += scroll/size.y;

    blit.bind(); blit["offset"]=vec2(parent); //blit["color"] = vec4(0,0,0,1);
    //glHard();
    for(const auto& b : blits) {
        //if(character.color != 0) blit["color"] = vec4(0,0,1,1);
        GLTexture::bind(b.id); glQuad(blit, b.min, b.max, true);
        //if(character.color != 0) blit["color"] = vec4(0,0,0,1);
    }

    flat.bind(); flat["offset"]=vec2(parent); flat["color"] = vec4(0,0,0,1);
    //glSmooth();
    stroke.bindAttribute(flat,"position",2);
    stroke.draw();
    fill.bindAttribute(flat,"position",2);
    fill.draw();

    //flat["scale"] = vec2(1,1); flat["offset"] = vec2(0,0); flat["color"] = vec4(1,1,1,1);
    //glQuad(flat,vec2(-1,-1),vec2(1,1));
}

bool PDF::mouseEvent(int2, Event event, Button button) {
    if(event==Press && button==WheelDown) { scroll-=size.y/4; needUpdate.emit(); return true; }
    if(event==Press && button==WheelUp) { scroll+=size.y/4; needUpdate.emit();return true; }
    return false;
}
