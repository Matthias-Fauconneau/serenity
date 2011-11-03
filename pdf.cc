#include "document.h"
#include "gl.h"

#include <float.h>
#include <math.h>
#include <zlib.h>

#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftlcdfil.h>

static FT_Library ft;

struct Variant { //TODO: union
    MoveOnly(Variant);
    enum { Number, Data, List, Dict } type;
    double number=0; string data; array<Variant> list; map<string,Variant> dict;
	Variant(double number) : type(Number), number(number) {}
    Variant(string&& data) : type(Data), data(move(data)) {}
    Variant(array<Variant>&& list) : type(List), list(move(list)) {}
    Variant(map<string,Variant>&& dict) : type(Dict), dict(move(dict)) {}
};
void log_(const Variant& o) {
    if(o.type==Variant::Number) log_(long(o.number));
    else if(o.type==Variant::Data) log_(o.data);
    else if(o.type==Variant::List) log_(o.list);
    else if(o.type==Variant::Dict) log_(o.dict);
    else fail("Invalid Variant",int(o.type));
}

struct Font {
    Font() : face(0) {}
    string name;
    string file;
	FT_Face* face;
    map<int, map<int, int> > cache;
    array<float> widths;
};

struct Glyph {
    Font* font;
    int charCode;
    int code;
    float scale;
    float recoScale;
    vec2 offset;
    vec2 size;
    float advance;
    GLTexture texture;
};

struct Character {
    Character() : glyph(0), color(0) {}
    Character(vec2 position, int glyph) : position(position), glyph(glyph), color(0) {}
    vec2 position;
    int glyph;
    float color;
};

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
                int i=_("nrtbf()\\").indexOf(s[0]);
                assert(i>=0,"unknown escape sequence",s[0]);
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
            auto decodeParms = dict.find(_("DecodeParms"));
            if(decodeParms) { fail("unsupported stream compression");
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
        while(s[0]!='>') { assert(s[1]!='>',"Invalid hex data");
            data << ((string("0123456789ABCDEF",16).indexOf(s[0])<<4) | string("0123456789ABCDEF",16).indexOf(s[1]));
            s+=2;
        } s++;
        return move(data);
    }
    fail("unknown type",string(s-64,64),string(s,64));
    return Variant(0);
}
static Variant parse(const char* s) { return parse_(s); }
static map<string,Variant> toDict(const array<const char*>& xref, Variant&& object) {
    return object.dict ? move(object.dict) : move(parse(xref[object.number]).dict);
}
//#define toDict(object) ({ object.dict ? : parse(xref[object.number]).dict })

class(PDF,Document) {
    enum Flags { Close=1,Stroke=2,Fill=4,OddEven=8,Winding=16,Trace=32 };

    float x1,y1,x2,y2;
    void extend(vec2 p) { if(p.x<x1) x1=p.x; if(p.x>x2) x2=p.x; if(p.y<y1) y1=p.y; if(p.y>y2) y2=p.y; }

    map<string, Font> fonts;
    mat32 Tm,Cm;
    float recognitionScale; //hack to avoid changing scale of recognition values
    array< array<vec2> > paths; //only for recognition
    array< vec2 > lines;
    array< uint > indices; array< vec2 > vertices; //filled curves

    int width, height; //target resolution for glyphs and hinting
    GLBuffer stroke;
    GLBuffer fill;
    array< Glyph > glyphs;
    array< Character > characters;

	GLShader* flatShader,blitShader;

    void open(const string& path, Recognizer* recognizer) {
        if(!ft) FT_Init_FreeType( &ft ); FT_Library_SetLcdFilter(ft,FT_LCD_FILTER_DEFAULT);

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
                    assert(s[0]=='t'&&s[1]=='r'&&s[2]=='a'&&s[3]=='i'&&s[4]=='l'&&s[5] =='e'&&s[6]=='r',"invalid xref");
                    s+=sizeof("trailer"); if(s[0]=='\r'||s[0]=='\n') s++;
                    dict = parse( s ).dict;
                } else fail("xref stream");
                if(!root) root=dict.at(_("Root")).number;
                const Variant* offset = dict.find(_("Prev"));
                if(!offset) break;
                s=pdf.data+int(offset->number);
            }
            catalog = parse(xref[root]).dict;
        }
        x1 = +FLT_MAX, x2 = -FLT_MAX; vec2 pageOffset;
        array<Variant> pages = move(parse(xref[catalog.at(_("Pages")).number]).dict.at(_("Kids")).list);
        for(const Variant& page : pages) {
            //TODO: compute page indices range for culling
            int pageFirstLine = lines.size, pageFirstIndex = vertices.size, pageFirstCharacter = characters.size;
            auto dict = parse(xref[page.number]).dict;
            //pages << dict[_("Kids")].list;
            Variant empty(0);
            for(auto e : toDict(xref,move(toDict(xref,move(dict.value(_("Resources"),empty))).value(_("Font"),empty)))) {
                if(fonts.contains(e.key)) continue;
                auto fontDict = parse(xref[e.value.number]).dict;
                auto descendant = fontDict.find(_("DescendantFonts"));
                if(descendant) fontDict = parse(xref[descendant->list[0].number]).dict;
                Font& font = fonts.insert(move(e.key));
                font.name = move(fontDict.at(_("BaseFont")).data);
                auto descriptor = parse(xref[fontDict.at(_("FontDescriptor")).number]).dict;
                auto fontFile = descriptor.find(_("FontFile"))?:descriptor.find(_("FontFile2"))?:descriptor.find(_("FontFile3"));
                if(fontFile) {
                    font.file = parse(xref[fontFile->number]).data;
                    FT_New_Memory_Face(ft,(const FT_Byte*)font.file.data,font.file.size,0,&font.face);
                } //else standard font
                auto firstChar = fontDict.find(_("FirstChar"));
                if(firstChar) font.widths.resize(firstChar->number);
                auto widths = fontDict.find(_("Widths"));
                if(widths) for(const Variant& width : widths->list) font.widths << width.number;
            }
            auto contents = dict.find(_("Contents"));
            if(contents) {
                //FIXME: hack to avoid changing scale of recognition distances
                const auto& cropBox = (dict.find(_("CropBox"))?:dict.find(_("MediaBox"))?:&empty)->list;
                recognitionScale = 1280/cropBox[2].number;

                mat32 stack[16]; int index = 0;
                array< array<vec2> > path;
                Font* font=0; float fontSize=1,spacing=0,wordSpacing=0,leading=0; mat32 Tlm;
                array<Variant> args;
                y1 = FLT_MAX, y2 = -FLT_MAX;
                for(const auto& contentRef : /*contents->number?*/ contents->list ) {
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
                            OP('m') path << (array<vec2>()<<p(0,1));
                            OP('M') ;//setMiterLimit(f(0));
                            OP('q') stack[index++]=Cm;
                            OP('Q') Cm=stack[--index];
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
                                path << (array<vec2>() << p1
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
                            OP2('T','f') font = fonts.contains(args[0].data)? &fonts[args[0].data] : 0; fontSize=f(1);
                            OP2('T','m') Tm=Tlm=mat32(f(0),f(1),f(2),f(3),f(4),f(5));
                            OP2('T','w') wordSpacing=f(0);
                        }
                        //log(string(s,opLength),args);
                        args.clear();
                        s+=opLength;
                    }
                }
                Cm=Tm=mat32();
                pageOffset += vec2(0,(y1-y2)*1.05);
                for(int i=pageFirstLine;i<lines.size;i++) lines[i] += pageOffset;
                for(int i=pageFirstCharacter;i<characters.size;i++) {
                    Character& c = characters[i];
                    c.position += pageOffset;
                    if(recognizer) recognizer->onGlyph(i, recognitionScale*vec2(c.position.x,-c.position.y),
                                                       recognitionScale*glyphs[c.glyph-1].recoScale,
                                                       glyphs[c.glyph-1].font->name,glyphs[c.glyph-1].charCode);
                }
                for(const auto& path : paths) {
                    array<vec2> scaled; for(vec2 p : path) scaled<<recognitionScale*vec2(p.x,-p.y-pageOffset.y);
                    if(recognizer) recognizer->onPath(scaled);
                }
                paths.clear();
                for(int i=pageFirstIndex;i<vertices.size;i++) vertices[i] += pageOffset;
            }
        }
    }

    float pow2(float x) { return x*x; }
    float pow3(float x) { return x*x*x; }
    vec2 cubic(vec2 A,vec2 B,vec2 C,vec2 D,float t) { return pow3(1-t) * A + 3*pow2(1-t)*t * B + 3*(1-t)*pow2(t) * C + pow3(t) * D; }

    void drawPath(array<array<vec2> >& paths, int flags) {
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
            if((flags&Fill) && polyline.size<=16) {
                float area=0; //FIXME: complex polygons are not handled
                for(int i=0; i < polyline.size-1; i++) area += cross(polyline[i],polyline[i+1]);
                if(area < 0) reverse(polyline);

                int index = vertices.size;
                vertices << polyline;

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
            }
        }
        if(flags&Trace) this->paths << move(paths);
        paths.clear();
    }

    void drawText(Font* font, int fontSize, float spacing, float wordSpacing, const string& data) {
        if(!font->face) return;
        for(uint8 code : data) {
            if(code==0) continue;
            mat32 Trm = Tm*Cm;
            int& index = font->cache[round(fontSize*Trm.m11)][code];
            Glyph glyph;
            if(!index) {
                FT_Set_Char_Size(font->face, 0, fontSize*Trm.m11*64, 72, 72 );
                glyph.font = font;
                glyph.scale = fontSize*Trm.m11;
                glyph.recoScale = Trm.m11;
                glyph.code = glyph.charCode = code;
                if(!font->face->charmaps) {
                    FT_Load_Glyph(font->face, code, FT_LOAD_DEFAULT);
                } else for(int i=0;i<font->face->num_charmaps;i++) {
                    FT_Set_Charmap(font->face, font->face->charmaps[i] );
                    glyph.code = FT_Get_Char_Index(font->face, code );
                    FT_Load_Glyph(font->face, glyph.code, FT_LOAD_DEFAULT);
                    if(font->face->glyph->advance.x) break;
                }
                glyph.advance = (float)font->face->glyph->advance.x/64;
                glyph.size = vec2(font->face->glyph->metrics.width/64,font->face->glyph->metrics.height/64);
                glyphs << glyph; index=glyphs.size;
            } else glyph=glyphs[index-1];
            vec2 position(Trm.dx,Trm.dy);
            extend(position+glyph.offset);
            extend(position+glyph.offset+vec2(glyph.size.x,glyph.size.y));
            characters << Character(position,index);
            float advance = spacing+(code==' '?wordSpacing:0);
            if(code < font->widths.size) advance += fontSize*font->widths[code]/1000;
            else advance += glyph.advance/Trm.m11;
            Tm = mat32(advance,0) * Tm;
        }
    }

    void resize(int w, int h) {
        width = w; height = h;
        float scale = width/(x2-x1);

        // Upload stroke paths
        for(int i=0;i<lines.size; i+=2) { //hint axis-aligned lines
            vec2& a = lines[i]; vec2& b = lines[i+1];
            if(a.x == b.x) a.x = b.x = round(a.x*scale)/scale;
            if(a.y == b.y) a.y = b.y = round(a.y*scale)/scale;
        }
        stroke.primitiveType = 2;
        stroke.upload(lines); lines.clear();

        // Upload fill paths
        fill.primitiveType = 3;
        fill.upload(vertices); vertices.clear();
        fill.upload(indices); indices.clear();

        //Render all glyphs
        for(int i=0; i < glyphs.size; i++) {
            Glyph& glyph = glyphs[i];
            FT_Set_Char_Size(glyph.font->face, 0, ceil(scale*glyph.scale)*64, 72, 72);
            FT_Load_Glyph(glyph.font->face, glyph.code, FT_LOAD_TARGET_LCD);
            FT_Render_Glyph(glyph.font->face->glyph, FT_RENDER_MODE_LCD);
            FT_Bitmap bitmap=glyph.font->face->glyph->bitmap;
            if(!bitmap.buffer) continue;
            int width = bitmap.width/3, height = bitmap.rows;
            uint8* data = new uint8[height*width*4];
            for(int y=0;y<height;y++) for(int x=0;x<width;x++) {
                uint8* rgb = &bitmap.buffer[y*bitmap.pitch+x*3];
                data[(y*width+x)*4+0] = 255-rgb[0];
                data[(y*width+x)*4+1] = 255-rgb[1];
                data[(y*width+x)*4+2] = 255-rgb[2];
                data[(y*width+x)*4+3] = clip(0,rgb[0]+rgb[1]+rgb[2],255);
            }
            glyph.texture.upload(data,width,height,4);
			delete[] data;
            glyph.offset = vec2( glyph.font->face->glyph->bitmap_left, glyph.font->face->glyph->bitmap_top );
        }
        //TODO: free unused ressources
        //for(Font font : fonts) FT_Done_Face(font.face);
    }

    void render(float scroll) {
        glBindWindow(width,height);

        scroll = scroll/recognitionScale; //FIXME
        vec2 scale = vec2( 2/(x2-x1), 2.0*width/height/(x2-x1) );
        vec2 offset = vec2(-1-x1*scale.x,-1+(round((height/2)*scroll*scale.y)+0.5)/(height/2));

        blitShader.bind();
        blitShader["scale"] = scale;
        blitShader["offset"] = offset;
        blitShader["color"] = vec4(0,0,0,1);
        glHard();
        vec2 pxToNormalized = vec2(1,1)/(scale*vec2(width/2,height/2));
        for(const auto& character : characters) {
            if(character.color != 0) blitShader["color"] = vec4(0,0,1,1);
            Glyph glyph = glyphs[character.glyph-1];
            glyph.texture.bind();
            glQuad(character.position+glyph.offset*pxToNormalized,
                   character.position+glyph.offset*pxToNormalized+vec2(glyph.texture.width,-glyph.texture.height)*pxToNormalized,
                   blitShader.attribLocation("position"),blitShader.attribLocation("texCoord"));
            if(character.color != 0) blitShader["color"] = vec4(0,0,0,1);
        }

        flatShader.bind();
        flatShader["scale"] = scale;
        flatShader["offset"] = offset;
        flatShader["color"] = vec4(0,0,0,1);
        glSmooth();
        stroke.bindAttribute(&flatShader,"position",2,0);
        stroke.draw();
        fill.bindAttribute(&flatShader,"position",2,0);
        fill.draw();

        flatShader["scale"] = vec2(1,1);
        flatShader["offset"] = vec2(0,0);
        flatShader["color"] = vec4(1,1,1,1);
        glQuad(vec2(-1,-1),vec2(1,1),flatShader.attribLocation("position"));
    }
};
