#include "font.h"

/// Fixed point rounding
inline int truncate(int width, uint value) { return value/width*width; }
inline int floor(int width, int value) { return value>=0?truncate(width,value):-align(width,-value); }
inline int round(int width, int value) { return floor(width,value+width/2); }
inline int ceil(int width, int value) { return value>=0?align(width,value):-truncate(width,-value); }

/// Gamma correction lookup table
constexpr uint8 gamma[257] = { 0, 12, 21, 28, 33, 38, 42, 46, 49, 52, 55, 58, 61, 63, 66, 68, 70, 73, 75, 77, 79, 81, 82, 84, 86, 88, 89, 91, 93, 94, 96, 97, 99, 100, 102, 103, 104, 106, 107, 109, 110, 111, 112, 114, 115, 116, 117, 118, 120, 121, 122, 123, 124, 125, 126, 127, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 151, 152, 153, 154, 155, 156, 157, 157, 158, 159, 160, 161, 161, 162, 163, 164, 165, 165, 166, 167, 168, 168, 169, 170, 171, 171, 172, 173, 174, 174, 175, 176, 176, 177, 178, 179, 179, 180, 181, 181, 182, 183, 183, 184, 185, 185, 186, 187, 187, 188, 189, 189, 190, 191, 191, 192, 193, 193, 194, 194, 195, 196, 196, 197, 197, 198, 199, 199, 200, 201, 201, 202, 202, 203, 204, 204, 205, 205, 206, 206, 207, 208, 208, 209, 209, 210, 210, 211, 212, 212, 213, 213, 214, 214, 215, 215, 216, 217, 217, 218, 218, 219, 219, 220, 220, 221, 221, 222, 222, 223, 223, 224, 224, 225, 226, 226, 227, 227, 228, 228, 229, 229, 230, 230, 231, 231, 232, 232, 233, 233, 234, 234, 235, 235, 236, 236, 237, 237, 237, 238, 238, 239, 239, 240, 240, 241, 241, 242, 242, 243, 243, 244, 244, 245, 245, 245, 246, 246, 247, 247, 248, 248, 249, 249, 250, 250, 251, 251, 251, 252, 252, 253, 253, 254, 254, 254, 255 };
#if 0
#define pow __builtin_pow
inline float sRGB(float c) { if(c>=0.0031308f) return 1.055f*pow(c,1/2.4f)-0.055f; else return 12.92f*c; }
int main() {  for(int i=0;i<=256;i++) write(1,string(str(min(255,int(255*sRGB(i/255.f))))+", "_));  }
#endif

static int fonts() { static Folder fd = openFolder("usr/share/fonts"_); return fd; }

/// Enable automatic grid fitting
bool fit;
/// 2x nearest upsample for debugging
bool up;

Font::Font(const ref<byte>& name, int size) : keep(mapFile(name,fonts())), size(size) {
    DataStream s = DataStream(keep, true);
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
#define unscaleX(p) (((p)<<scale)/(size*3))
       scale=0; for(int v=unitsPerEm;v>>=1;) scale++; scale-=4; assert(scale<32);
       round = (1<<scale)/2; //round to nearest not down
    }
    {DataStream& s = hhea;
        uint32 unused version=s.read();
        ascent=ceil(16,scale(s.read16())), descent=scale(s.read16()), lineGap=scale(s.read16());
        //uint16 unused maxAdvance=s.read(), unused minLeft=s.read(), unused minRight=s.read(), unused maxExtent=s.read();*/
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
            ref<uint16> firstCode = s.read<uint16>(segCount);
            ref<uint16> idDelta = s.read<uint16>(segCount);
            ref<uint16> idRangeOffset = s.read<uint16>(segCount);
            int i=0; while(big16(endCode[i]) < code) i++;
            if(big16(firstCode[i])<=code) {
                if(big16(idRangeOffset[i])) return *( &idRangeOffset[i] + big16(idRangeOffset[i]) / 2 + (code - big16(firstCode[i])) );
                else return big16(idDelta[i]) + code;
            }
        } else if(format==12) {
            uint16 unused subformat = s.read();
            uint32 unused size=s.read(), unused language=s.read();
            uint32 groupCount=s.read();
            for(uint i=0;i<groupCount;i++) {
                uint32 first=s.read(), last=s.read(), firstIndex=s.read();
                if(code>=first && code<=last) return firstIndex+code-first;
            }
        } else { trace(); warn("Unsupported"_,format,code); return this->index('?'); }
        s.index=index;
    }
    error("Not Found"_);
}

int Font::advance(uint16 index) { return (up?2:1)*scale(big16(hmtx[2*index])); }

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
            if(left==leftIndex && right==rightIndex) return (up?2:1)*scale(value);
        }
    }
    return 0;
}

//Lightweight image with a single signed channel used for rasterization
struct Bitmap {
    no_copy(Bitmap)
    int8* data; uint width,height;
    Bitmap():data(0),width(0),height(0){}
    Bitmap(uint width,uint height):data(allocate<int8>(width*height)),width(width),height(height){clear((byte*)data,height*width);}
    ~Bitmap(){ if(data) unallocate(data,width*height);}
    int8& operator()(uint x, uint y){assert(x<width && y<height,x,y,width,height); return data[y*width+x];}
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

template<class T> T& min_(T& a, T& b) { assert(a!=b,a); return a<b ? a : b; }
template<class T> T& max_(T& a, T& b) { assert(a!=b,a); return a>b ? a : b; }
struct Stem { int& a; int& b; int& n; int& N; };
void fitStem(int I, array<Stem>& stems, int2& A, int2& B, int unused max) {
    int& a=A.x; int& b=B.x; int& n=A.y; int& N=B.y;
    for(Stem& s: stems) { //fit horizontal stems size (TODO: fit position, best not first)
        if((a>b) != (s.a<s.b)) continue; //only opposite directions (same stem)
        if(n==s.n) continue; //assert(n!=s.n,n,a,b,A,B);
        int& n1=min_(n,s.n); int& N1=min_(N,s.N);
        int& n2=max_(n,s.n); int& N2=max_(N,s.N);
        int m = ::round(I,(n1+n2)/2), w = ::round(I,n2-n1);
        n1=N1=m-floor(I,w/2), n2=N2=m+ceil(I,w/2)-1;
        assert(n1%I==0,n1,m,w,floor(I,w/2)); assert((n2+1)%I==0,n2+1,(n2+1)/I,(n2+1)%I,m,w);
        assert(n1<max,n1,max); assert(n2<max,n2,max);
        break;
    }
    stems<<Stem __(a,b,n,N);
}

void Font::render(Bitmap& raster, int index, int& xMin, int& xMax, int& yMin, int& yMax, int xx, int xy, int yx, int yy, int dx, int dy){
    int pointer = ( indexToLocFormat? big32(((uint32*)loca)[index]) : 2*big16(((uint16*)loca)[index]) );
    int length = ( indexToLocFormat? big32(((uint32*)loca)[index+1]) : 2*big16(((uint16*)loca)[index+1]) ) - pointer;
    DataStream s=DataStream(ref<byte>(glyf +pointer, length), true);
    if(!s) return;

    int16 numContours = s.read();
    if(!raster.data) {
        xMin= s.read16(), yMin= s.read16(), xMax= s.read16(), yMax= s.read16();
        xMin  = unscaleX(floor(48,scaleX(xMin))); xMax = unscaleX(ceil(48,scaleX(xMax))); //align canvas to integer pixels
        yMin = unscale(floor(16,scaleY(yMin))); yMax = unscale(ceil(16,scaleY(yMax))); //align canvas to integer pixels

        int width=scaleX(xMax-xMin), height=scaleY(yMax-yMin); assert(width>0,xMax,xMin); assert(height>0,yMax,yMin);
        if(fit) new (&raster) Bitmap(width+16,height+16);
        else new (&raster) Bitmap(width+1,height+1);
    } else s.advance(4*2); //TODO: resize as needed

    if(numContours>0) {
        ref<uint16> endPtsOfContours = s.read<uint16>(numContours);
        int nofPoints = big16(endPtsOfContours[numContours-1])+1;

        uint16 instructionLength = s.read(); ref<uint8> unused instructions = s.read<uint8>(instructionLength);

        struct Flags { byte on_curve:1, short_x:1, short_y:1, repeat:1, same_sign_x:1, same_sign_y:1; };
        Flags flagsArray[nofPoints];
        for(int i=0;i<nofPoints;i++) {
            Flags flags = s.read<Flags>();
            if(flags.repeat) { uint8 times = s.read(); for(int n=0;n<times;n++) flagsArray[i++]=flags; }
            flagsArray[i]=flags;
        }

#if __clang__
        int P_[2*nofPoints]; int2* P=(int2*)P_;
#else
        int2 P[nofPoints];
#endif
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

        if(fit) {
            // Fit (TODO: align baseline and x-height to avoid overshoot (blue zone = xzroesc), align horizontal stems, align vertical stems width)
            array<Stem> hStems; array<Stem> vStems;
            for(int n=0,i=0; n<numContours; n++) {
                int last= big16(endPtsOfContours[n]);
                for(int prev=last; i<=last; i++) {
                    int2& A = P[prev]; int2& B=P[i];
                    if(A==B) continue;
                    if(A.y==B.y) fitStem(16,hStems,A,B,raster.height); //vertically fit horizontal stems
                    if(A.x==B.x) fitStem(48,vStems,A,B,raster.width); //horizontally fit horizontal stems
                    prev=i;
                }
            }
        }
        // Render
        for(int n=0,i=0; n<numContours; n++) {
            int last= big16(endPtsOfContours[n]);
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

const Glyph& Font::glyph(uint16 index, int fx) {
    if(fit) fx=0; else fx=fx&(16-1);
    // Lookup glyph in cache
    Glyph& glyph = index<128 ? cacheASCII[fx][index] : cacheUnicode[fx][index];
    if(glyph.valid) return glyph;
    glyph.valid=true;

    Bitmap raster; int xMin,xMax,yMin,yMax;
    render(raster,index,xMin,xMax,yMin,yMax,1<<14,0,0,1<<14,0,0);
    if(!raster.data) return glyph;
    glyph.offset = int2(floor(16,scale(xMin)),ascent-scale(yMax)-16); //yMax was rounded up
    assert(glyph.offset.x%16==0); assert(glyph.offset.y%16==0,glyph.offset.y);
    glyph.offset /= 16;

    uint width=ceil(48,raster.width)/48+1, height=raster.height/16; //add 1px for filtering
    glyph.image = Image(width, height);
    for(uint y=0; y<height; y++) {
        uint16 line[width*3];
        int acc[16]={};
        for(uint x=0; x<width; x++) { //Supersampled rasterization
            for(uint c=0; c<3; c++) {
                int sum=0;
                for(int j=0; j<16; j++) for(int i=0; i<16; i++) {
                    sum += acc[j]>0;
                    int idx=-fx*3+(x*3+c)*16+i; if(idx>=0 && idx<(int)raster.width) acc[j] += raster(idx,y*16+j);
                }
                line[x*3+c]=sum; assert(sum<=256);
            }
        }
        for(uint x=0; x<width; x++) { //LCD subpixel filtering
            uint8 filter[5] = {1, 4, 6, 4, 1}; if(fit) clear(filter,5), filter[2]=16;
            uint16 pixel[3]={};
            for(uint c=0; c<3; c++) for(int i=0;i<5;i++) { int idx=x*3+c+i-2; if(idx>=0 && idx<(int)width*3) pixel[c]+=filter[i]*line[idx]; }
            glyph.image(x,y) = byte4(255-gamma[pixel[2]/16],255-gamma[pixel[1]/16],255-gamma[pixel[0]/16],min(255,pixel[0]+pixel[1]+pixel[2])); //vertical RGB pixels
        }
    }
    if(up){
        Image image = Image(width*2,height*2);
        for(uint y=0; y<height; y++) for(uint x=0; x<width; x++) {
            image(x*2+0,y*2+0)=image(x*2+0,y*2+1)=image(x*2+1,y*2+0)=image(x*2+1,y*2+1)= glyph.image(x,y);
        }
        glyph.image=move(image); glyph.offset*=2;
    }
    return glyph;
}
