#include "font.h"

/// Gamma correction
static uint8 gamma[257];
#define pow __builtin_pow
inline float sRGB(float c) { if(c>=0.0031308) return 1.055*pow(c,1/2.4f)-0.055; else return 12.92*c; }
inline bool computeGammaLookup() { for(int i=0;i<=256;i++) gamma[i]=min(255,int(255*sRGB(i/255.))); return true; }

static int fonts() { static int fd = openFolder("usr/share/fonts"_); return fd; }

Font::Font(const ref<byte>& name, int size) : keep(mapFile(name,fonts())), size(size) {
    static bool unused once = computeGammaLookup();
    DataStream s = DataStream::byReference(keep, true);
    uint32 unused scaler=s.read();
    uint16 numTables=s.read(), unused searchRange=s.read(), unused numSelector=s.read(), unused rangeShift=s.read();
    DataStream head, hhea;
    for(int i=0;i<numTables;i++) {
        uint32 tag=s.read<uint32>()/*no swap*/, unused checksum=s.read(), offset=s.read(), unused size=s.read();
        if(tag==raw<uint32>("head"_)) head=s.slice(offset,size);
        if(tag==raw<uint32>("hhea"_)) hhea=s.slice(offset,size);
        if(tag==raw<uint32>("cmap"_)) cmap=s.slice(offset,size);
        if(tag==raw<uint32>("kern"_)) kern=s.slice(offset,size);
        if(tag==raw<uint32>("hmtx"_)) hmtx=(uint16*)(s.buffer.data()+offset);
        if(tag==raw<uint32>("loca"_)) loca=s.buffer.data()+offset;
        if(tag==raw<uint32>("glyf"_)) glyf=s.buffer.data()+offset;
    }
    {DataStream& s = head;
       uint32 unused version=s.read(), unused revision=s.read();
       uint32 unused checksum=s.read(), unused magic=s.read();
       uint16 unused flags=s.read(), unitsPerEm=s.read();
       s.advance(8+8+4*2+2+2+2); //created, modified, bbox[4], maxStyle, lowestRec, direction
       indexToLocFormat=s.read();
       // parameters for scale from design (FUnits) to device (.4 pixel)
#define scaleX(p) ((3*size*int64(p)+round)>>scale)
#define scaleY(p) ((size*int64(p)+round)>>scale)
#define scale(p) scaleY(p)
#define unscale(p) (((p)<<scale)/size)
       scale=0; for(int v=unitsPerEm;v>>=1;) scale++; scale-=4;
       round = (1<<scale)/2; //round to nearest not down
    }
    {DataStream& s = hhea;
        uint32 unused version=s.read();
        ascent=s.read(); /*uint16 unused descent=s.read(), unused lineGap=s.read();
        uint16 unused maxAdvance=s.read(), unused minLeft=s.read(), unused minRight=s.read(), unused maxExtent=s.read();*/
    }
}

uint16 Font::index(uint16 code) {
    cmap.seek(0); DataStream& s = cmap;
    uint16 unused version=s.read(), numTables=s.read();
    for(int i=0;i<numTables;i++) {
        uint16 unused platformID=s.read(), unused platformSpecificID=s.read(); uint32 offset=s.read();
        uint index=s.index; s.seek(offset);
        uint16 format = s.read();
        if(format==4) {
            uint16 unused size=s.read(), unused language=s.read();
            uint16 segCount=s.read(), unused searchRange=s.read(),unused entrySelector=s.read(), unused rangeShift=s.read();
            segCount /= 2;
            ref<uint16> endCode = s.read<uint16>(segCount);
            s.advance(2); //pad
            ref<uint16> startCode = s.read<uint16>(segCount);
            ref<uint16> idDelta = s.read<uint16>(segCount);
            ref<uint16> idRangeOffset = s.read<uint16>(segCount);
            int i=0; while(swap16(endCode[i]) < code) i++;
            if(swap16(startCode[i])<=code) {
                if(swap16(idRangeOffset[i])) return *( &idRangeOffset[i] + swap16(idRangeOffset[i]) / 2 + (code - swap16(startCode[i])) );
                else return swap16(idDelta[i]) + code;
            }
        } else if(format==12) {
            uint16 unused subformat = s.read();
            uint32 unused size=s.read(), unused language=s.read();
            uint32 groupCount=s.read();
            for(uint i=0;i<groupCount;i++) {
                uint32 first=s.read(), last=s.read(), firstIndex=s.read();
                if(code>=first && code<=last) return firstIndex+code-first;
            }
        } else error("Unsupported"_,format,code);
        s.index=index;
    }
    error("Not Found"_);
}

int Font::kerning(uint16 leftIndex, uint16 rightIndex) {
    kern.seek(0); DataStream& s = kern;
    uint16 unused version=s.read(), numTables=s.read();
    for(uint i=0;i<numTables;i++) {
        uint16 unused version=s.read(), unused length = s.read(); uint8 unused coverage = s.read(), unused format = s.read();
        assert(coverage==0); assert(format==1);
        uint16 nPairs = s.read(), unused searchRange = s.read(), unused entrySelector = s.read(), unused rangeShift = s.read();
        assert(14+nPairs*6==length);
        for(uint i=0;i<nPairs;i++) {
            uint16 left=s.read(), right=s.read(); int16 value=s.read();
            if(left==leftIndex && right==rightIndex) return scale(value);
        }
    }
    return 0;
}

struct Bitmap {
    int8* data; uint width,height;
    Bitmap():data(0),width(0),height(0){}
    Bitmap(uint width,uint height):data(allocate<int8>(width*height)),width(width),height(height){clear((byte*)data,height*width);}
    int8& operator()(uint x, uint y){assert(x<width && y<height); return data[y*width+x];}
};

static int lastStepY; //dont flag first/last point twice but cancel on direction changes
void line(Bitmap& raster, int2 p0, int2 p1) {
    int x0=p0.x, y0=p0.y, x1=p1.x, y1=p1.y;
    if(y0==y1) return;
    int dx = abs(x1-x0);
    int dy = abs(y1-y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx-dy;
    if(sy!=lastStepY) raster(x0,y0) -= sy;
    for(;;) {
        if(x0 == x1 && y0 == y1) break;
        int e2 = 2*err;
        if(e2 > -dy) { err -= dy, x0 += sx; }
        if(e2 < dx) { err += dx, y0 += sy; raster(x0,y0) -= sy; } //only raster at y step
    }
    lastStepY=sy;
}

void curve(Bitmap& raster, int2 p0, int2 p1, int2 p2) {
    const int N=3;
    int2 a = p0;
    for(int t=1;t<=N;t++) {
        int2 b = ((N-t)*(N-t)*p0 + 2*(N-t)*t*p1 + t*t*p2)/(N*N);
        line(raster,a,b);
        a=b;
    }
}

/// Fixed point rounding
int truncate(int width, uint value) { return value/width*width; }
int floor(int width, int value) { return value>=0?truncate(width,value):-align(width,-value); }
int ceil(int width, int value) { return value>=0?align(width,value):-truncate(width,-value); }

void Font::render(Bitmap& raster, int index, int16& xMin, int16& xMax, int16& yMin, int16& yMax, int xx, int xy, int yx, int yy, int dx, int dy) {
    int start = ( indexToLocFormat? swap32(((uint32*)loca)[index]) : 2*swap16(((uint16*)loca)[index]) );
    int length = ( indexToLocFormat? swap32(((uint32*)loca)[index+1]) : 2*swap16(((uint16*)loca)[index+1]) ) - start;
    DataStream s=DataStream::byReference(ref<byte>(glyf +start, length), true);
    if(!s) return;

    int16 numContours = s.read();
    if(!raster.data) {
        xMin=s.read(), yMin=s.read(), xMax=s.read(), yMax=s.read();
        //xMin  = unscale(floor(16,scale(xMin))); xMax = unscale(ceil(16,scale(xMax))); //keep horizontal subpixel accuracy
        yMin = unscale(floor(16,scaleY(yMin))); yMax = unscale(ceil(16,scaleY(yMax))); //align canvas to integer pixels

        int width=scaleX(xMax-xMin), height=scaleY(yMax-yMin);
        raster = Bitmap(width+1,height+1);
    } else s.advance(4*2); //TODO: resize as needed

    if(numContours>0) {
        ref<uint16> endPtsOfContours = s.read<uint16>(numContours);
        int nofPoints = swap16(endPtsOfContours[numContours-1])+1;

        uint16 instructionLength = s.read(); ref<uint8> unused instructions = s.read<uint8>(instructionLength);

        struct Flags { byte on_curve:1, short_x:1, short_y:1, repeat:1, same_sign_x:1, same_sign_y:1; };
        Flags flagsArray[nofPoints];
        for(int i=0;i<nofPoints;i++) {
            Flags flags = s.read<Flags>();
            if(flags.repeat) { ubyte times = s.read(); for(int n=0;n<times;n++) flagsArray[i++]=flags; }
            flagsArray[i]=flags;
        }

        int P_[2*nofPoints]; int2* P=(int2*)P_;
        int16 last=-xMin;
        for(int i=0;i<nofPoints;i++) { Flags flags=flagsArray[i];
            if(flags.short_x) {
                uint8 delta=s.read();
                if(flags.same_sign_x) last+=delta; else last-= delta;
            } else if(!flags.same_sign_x) last+= (int16)s.read();
            P[i].x= last;
            assert(P[i].x<32768);
        }
        last=-yMax;
        for(int i=0;i<nofPoints;i++) { Flags flags=flagsArray[i];
            if(flags.short_y) {
                if(flags.same_sign_y) last+= (uint8)s.read();
                else last-= (uint8)s.read();
            } else if(!flags.same_sign_y) last+= (int16)s.read();
            P[i].y= -last; //flip to downward y
        }
        for(int i=0;i<nofPoints;i++) { int2& p=P[i];
            p.x=scaleX(xx*p.x/16384+yx*p.y/16384+dx);
            p.y=scaleY(xy*p.x/16384+yy*p.y/16384+dy);
        }

        for(int n=0,i=0; n<numContours; n++) {
            int last= swap16(endPtsOfContours[n]);
            int2 p; //last on point
            if(flagsArray[last].on_curve) p=P[last];
            else if(flagsArray[last-1].on_curve) p=P[last-1];
            else p=(P[last-1]+P[last])/2;
            for(int i=last;i>0;i--) if(P[i-1].y != P[i].y) { lastStepY = (P[i-1].y < P[i].y) ? 1 : -1; break; }
            for(int prev=last; i<=last; i++) {
                if(flagsArray[prev].on_curve && flagsArray[i].on_curve) { line(raster, P[prev], P[i]); p=P[i]; } //on-on
                else if(flagsArray[prev].on_curve && !flagsArray[i].on_curve) { p=P[prev]; } //on-off (next step draws as on-off-on or on-off-off))
                else if(!flagsArray[prev].on_curve && flagsArray[i].on_curve) { curve(raster, p, P[prev], P[i]); p=P[i]; } //off-on
                else if(!flagsArray[prev].on_curve && !flagsArray[i].on_curve) { int2 m=(P[prev]+P[i])/2; curve(raster, p, P[prev], m); p=m; } //off-off
                prev=i;
            }
        }
    } else {
        for(bool more=true;more;) {
            struct Flags{ uint16 instructions:1, metrics:1, overlap:1, pad:5, word:1, offset:1, round:1, uniform:1, zero:1, more:1, scale:1, affine:1; };
            Flags unused flags = s.read<Flags>(); more=flags.more;
            uint16 glyphIndex=s.read();
            int dx,dy;
            /**/ if(flags.word && flags.offset) dx=(int16)s.read(), dy=(int16)s.read();
            else if(!flags.word && flags.offset) dx=(int8)s.read(), dy=(int8)s.read();
            else if(flags.word && !flags.offset) dx=(uint16)s.read(), dy=(uint16)s.read();
            else if(!flags.word && !flags.offset) dx=(uint8)s.read(), dy=(uint8)s.read();
            uint16 xx=16384,xy=0,yx=0,yy=16384; //signed 1.14
            if(flags.uniform) xx=yy= s.read();
            else if (flags.scale) xx=s.read(), yy=s.read();
            else if (flags.affine) xx=s.read(), xy=s.read(), yx=s.read(), yy=s.read();
            render(raster,glyphIndex,xMin,xMax,yMin,yMax,xx,xy,yx,yy,dx,dy);
        }
    }
}

Glyph Font::glyph(uint16 index, int fx) { //fx=0;
    // Lookup glyph in cache
    Glyph& glyph = index<256 ? cacheASCII[fx%16][index] : cacheUnicode[fx%16][index];
    if(glyph.image || glyph.advance) return Glyph(glyph);

    // map unicode to glyf outline
    glyph.advance = scale(swap16(hmtx[2*index]));
    Bitmap raster; int16 xMin,xMax,yMin,yMax;
    render(raster,index,xMin,xMax,yMin,yMax,1<<14,0,0,1<<14,0,0);
    if(!raster.data) return Glyph(glyph);
    glyph.offset = int2(scale(xMin),scale(ascent)-scale(yMax)-16); //yMax was rounded up

    int width=raster.width,height=raster.height;
#if 0
    glyph.image = Image<uint8>(width,height);
    for(int y=0; y<height; y++) {
        int acc=0;
        for(int x=0; x<width; x++) {
            glyph.image(x,y) = 128+raster(x,y)*63+acc*31;
            acc += raster(x,y);
        }
    }
    glyph.advance *= 16; glyph.offset=glyph.offset*16;
#else
    /// Rasterizes edge flags
    for(int y=0; y<height; y++) {
        int acc=0;
        for(int x=0; x<width; x++) {
            acc += raster(x,y);
            raster(x,y) = acc>0;
        }
    }
    /// Resolves supersampling (TODO: directly rasterize 16 parallel lines in target)
    Bitmap subpixel(ceil(16,width)/16+1,height/16);
    for(uint y=0; y<glyph.image.height; y++) for(uint x=0; x<glyph.image.width; x++) {
        int r=0,g=0,b=0;
        for(int j=0; j<16; j++) {
#define acc(c) { int sx=x*48+i-fx%16; if(sx>0 && sx<(int)raster.width) c += raster(x*48+i-fx%16,y*16+j); }
            for(int i=0; i<16; i++) acc(r)
            for(int i=16; i<32; i++) acc(g)
            for(int i=32; i<48; i++) acc(b)
#undef acc
        }
        subpixel(x,y) = byte4(255-gamma[b],255-gamma[g],255-gamma[r],255);
    }
    glyph.image = Image(ceil(48,width)/48+1,height/16);
    for(uint y=0; y<glyph.image.height; y++) {
        for(uint x=0; x<glyph.image.width; x++) {
            uint8 filter[5] = {1, 4, 6, 4, 1};
            int r=0,g=0,b=0;
            for(int i=0;i<5;i++) if(x+i-2>0 && x+i-2<glyph.image.width) sum+=filter[i]*line[x+i-2,y];
            glyph.image(x,y) = byte4(sum/16);
        }
    }
#endif
    unallocate(raster.data,raster.w*raster.h);
    return Glyph(glyph);
}
