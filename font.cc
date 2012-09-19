#include "font.h"

/// Fixed point rounding
inline constexpr int truncate(int width, uint value) { return value/width*width; } //towards 0
inline constexpr int floor(int width, int value) { return value>=0?truncate(width,value):-truncate(width,-value+width-1); } //towards negative
inline constexpr int round(int width, int value) { return  value>=0?truncate(width,value+width/2):-truncate(width,-value+width/2); } //0.5 rounds away from 0
inline constexpr int ceil(int width, int value) { return value>=0?truncate(width,value+width-1):-truncate(width,-value); } //towards positive

/// Gamma correction lookup table
constexpr uint8 sRGB[257] = { 0, 12, 21, 28, 33, 38, 42, 46, 49, 52, 55, 58, 61, 63, 66, 68, 70, 73, 75, 77, 79, 81, 82, 84, 86, 88, 89, 91, 93, 94, 96, 97, 99, 100, 102, 103, 104, 106, 107, 109, 110, 111, 112, 114, 115, 116, 117, 118, 120, 121, 122, 123, 124, 125, 126, 127, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 151, 152, 153, 154, 155, 156, 157, 157, 158, 159, 160, 161, 161, 162, 163, 164, 165, 165, 166, 167, 168, 168, 169, 170, 171, 171, 172, 173, 174, 174, 175, 176, 176, 177, 178, 179, 179, 180, 181, 181, 182, 183, 183, 184, 185, 185, 186, 187, 187, 188, 189, 189, 190, 191, 191, 192, 193, 193, 194, 194, 195, 196, 196, 197, 197, 198, 199, 199, 200, 201, 201, 202, 202, 203, 204, 204, 205, 205, 206, 206, 207, 208, 208, 209, 209, 210, 210, 211, 212, 212, 213, 213, 214, 214, 215, 215, 216, 217, 217, 218, 218, 219, 219, 220, 220, 221, 221, 222, 222, 223, 223, 224, 224, 225, 226, 226, 227, 227, 228, 228, 229, 229, 230, 230, 231, 231, 232, 232, 233, 233, 234, 234, 235, 235, 236, 236, 237, 237, 237, 238, 238, 239, 239, 240, 240, 241, 241, 242, 242, 243, 243, 244, 244, 245, 245, 245, 246, 246, 247, 247, 248, 248, 249, 249, 250, 250, 251, 251, 251, 252, 252, 253, 253, 254, 254, 254, 255 };
#if 0
#define pow __builtin_pow
inline float sRGB(float c) { if(c>=0.0031308f) return 1.055f*pow(c,1/2.4f)-0.055f; else return 12.92f*c; }
int main() {  for(int i=0;i<=256;i++) write(1,string(str(min(255,int(255*sRGB(i/255.f))))+", "_));  }
#endif

static const Folder& fonts() { static Folder folder = "usr/share/fonts"_; return folder; }

/// Automatic grid fitting
int fit=0;
/// RGB subpixel rendering
int subpixel=3; //=3 to enable
/// LCD filtering
int filter=1;
/// Luminance perception correction
int correct=0;

/// Supersampling (normal=16, =1 to debug)
int down=16;
/// Nearest upsampling (normal=1, >1 to debug)
int up=1;

Font::Font(const ref<byte>& name, int size) : keep(Map(name,fonts())), size(size) {
    BinaryData s = BinaryData(keep, true);
    uint32 unused scaler=s.read();
    uint16 numTables=s.read(), unused searchRange=s.read(), unused numSelector=s.read(), unused rangeShift=s.read();
    BinaryData head, hhea;
    for(int i=0;i<numTables;i++) {
        uint32 tag=s.read<uint32>()/*no swap*/, unused checksum=s.read(), offset=s.read(), unused size=s.read();
        if(tag==raw<uint32>("head"_)) head=s.slice(offset,size);
        if(tag==raw<uint32>("hhea"_)) hhea=s.slice(offset,size);
        if(tag==raw<uint32>("cmap"_)) cmap=s.slice(offset,size);
        if(tag==raw<uint32>("kern"_)) kern=s.slice(offset,size);
        if(tag==raw<uint32>("hmtx"_)) hmtx=cast<uint16>(s.Data::slice(offset,size));
        if(tag==raw<uint32>("loca"_)) loca=s.Data::slice(offset,size);
        if(tag==raw<uint32>("glyf"_)) glyf=s.Data::slice(offset,size);
    }
    {BinaryData& s = head;
       uint32 unused version=s.read(), unused revision=s.read();
       uint32 unused checksum=s.read(), unused magic=s.read();
       uint16 unused flags=s.read(), unitsPerEm=s.read();
       s.advance(8+8+4*2+2+2+2); //created, modified, bbox[4], maxStyle, lowestRec, direction
       indexToLocFormat=s.read();
       // parameters for scale from design (FUnits) to device (.4 pixel)
       shift=0; for(int v=unitsPerEm;v>>=1;) shift++; shift-=4; assert(shift<32);
       round = (1<<shift)/2; //round to nearest not down
    }
    {BinaryData& s = hhea;
        uint32 unused version=s.read();
        ascent=ceil(16,scaleY(s.read16())), descent=scaleY(s.read16()), lineGap=scaleY(s.read16());
        //uint16 unused maxAdvance=s.read(), unused minLeft=s.read(), unused minRight=s.read(), unused maxExtent=s.read();*/
    }
}

inline int Font::scaleX(int p) { /*log(((subpixel*size*int64(p))%(1<<shift))/size);*/ return (subpixel*size*int64(p)+round)>>shift; }
//inline int Font::scaleX(int p) { return (subpixel*size*int64(p)/*+round*/)>>shift; }
//inline int Font::scaleX(int p) { int64 s=(subpixel*size*int64(p)+round); int r=s>>shift; return ((s%(1<<shift)==0) && r%2)?r-1:r; } //Tie to even
//inline int Font::scaleY(int p) { return (size*int64(p)+round)>>shift; }
//inline int Font::scaleY(int p) { return (size*int64(p)/*+round*/)>>shift; }
inline int Font::scaleY(int p) { int64 s=(size*int64(p)+round); int r=s>>shift; return ((s%(1<<shift)==0) && r%2)?r-1:r; } //Tie to even
inline int Font::scale(int p) { return scaleY(p); }
inline int Font::unscaleX(int p) { return (p<<shift)/(size*subpixel); }
inline int Font::unscaleY(int p) { return (p<<shift)/size; }
inline int Font::unscale(int p) { return unscaleY(p); }

uint16 Font::index(uint16 code) {
    cmap.seek(0); BinaryData& s = cmap;
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
        } else {warn("Unsupported"_,format,code); return this->index('?');}
        s.index=index;
    }
    error("Not Found"_);
}

int Font::advance(uint16 index) { return up*16/down*scale(big16(hmtx[2*index])); }

int Font::kerning(uint16 leftIndex, uint16 rightIndex) {
    kern.seek(0); BinaryData& s = kern;
    uint16 unused version=s.read(), numTables=s.read();
    for(uint i=0;i<numTables;i++) {
        uint16 unused version=s.read(), unused length = s.read(); uint8 unused coverage = s.read(), unused format = s.read();
        assert(coverage==0); assert(format==1);
        uint16 nPairs = s.read(), unused searchRange = s.read(), unused entrySelector = s.read(), unused rangeShift = s.read();
        assert(14+nPairs*6==length);
        for(uint i=0;i<nPairs;i++) {
            uint16 left=s.read(), right=s.read(); int16 value=s.read();
            if(left==leftIndex && right==rightIndex) return up*16/down*scale(value);
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
inline void Font::line(Bitmap& raster, int2 p0, int2 p1) {
    int x0=p0.x, y0=p0.y, x1=p1.x, y1=p1.y;
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

inline void Font::curve(Bitmap& raster, int2 p0, int2 p1, int2 p2) {
    //line(raster,p0,p1); line(raster,p1,p2); return;
    const int N=3;
    int2 a = p0;
    for(int t=1;t<=N;t++) {
        int2 b = ((N-t)*(N-t)*p0 + 2*(N-t)*t*p1 + t*t*p2)/(N*N);
        line(raster,a,b);
        a=b;
    }
}

void Font::render(Bitmap& raster, int index, int& xMin, int& xMax, int& yMin, int& yMax, int xx, int xy, int yx, int yy, int dx, int dy){
    int pointer = ( indexToLocFormat? big32(((uint32*)loca.data)[index]) : 2*big16(((uint16*)loca.data)[index]) );
    int length = ( indexToLocFormat? big32(((uint32*)loca.data)[index+1]) : 2*big16(((uint16*)loca.data)[index+1]) ) - pointer;
    BinaryData s=BinaryData(ref<byte>(glyf.data +pointer, length), true);
    if(!s) return;

    int16 numContours = s.read();
    if(!raster.data) {
        xMin= s.read16(), yMin= s.read16(), xMax= s.read16(), yMax= s.read16();
        xMin  = unscaleX(floor(subpixel*16,scaleX(xMin))); xMax = unscaleX(ceil(subpixel*16,scaleX(xMax))); //align canvas to integer pixels
        yMin = unscaleY(floor(16,scaleY(yMin))); yMax = unscaleY(ceil(16,scaleY(yMax))); //align canvas to integer pixels

        int width=scaleX(xMax-xMin), height=scaleY(yMax-yMin); assert(width>0,xMax,xMin); assert(height>0,yMax,yMin);
        if(fit) new (&raster) Bitmap(width+2*subpixel*16,height+3*16); //new (&raster) Bitmap(width+48,height+16);
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
        //for(int n=0,i=0; n<numContours; n++) { for(last= big16(endPtsOfContours[n]), prev=last; i<=last; i++) { log_(P[i]-P[prev]); prev=i; } log(""); }
        for(int i=0;i<nofPoints;i++) { int2& p=P[i];
            p.x=scaleX(xx*p.x/16384+yx*p.y/16384+dx), p.y=scaleY(xy*p.x/16384+yy*p.y/16384+dy);
            //p.x=xx*p.x/16384+yx*p.y/16384+dx, p.y=xy*p.x/16384+yy*p.y/16384+dy;
        }
        //for(int n=0,i=0; n<numContours; n++) { for(last= big16(endPtsOfContours[n]), prev=last; i<=last; i++) { log_(P[i]-P[prev]); prev=i; } log(""); }

        // Fit (TODO: keep consistent baseline and x-height, vertical: subpixel position hint | only width hint)
        if(fit) {
            struct Stem {
                array<int2> tn; //array of points in (tangent, normal) coordinates
                array<int> ref; //array of original references to the points
                int pos=0,min=1<<30,max=0; int direction=0;
                int size() {assert(tn.size()==ref.size(),tn,ref); return tn.size(); }
            };

            for(int pass=0;pass<2;pass++) { //vertically fit horizontal stems (then horizontally fit vertical stems)
                int I = pass?16:16; //TODO: fit to subpixel ?
                int M = pass?raster.width:raster.height;
#define xy(p) (pass?int2(p.y,p.x):p)
                array<Stem> stems;
                /*array< array<int2> > contours; //for debugging;
                for(int n=0,i=0; n<numContours; n++) { //create stems from points
                    int last= big16(endPtsOfContours[n]);
                    array<int2> contour;
                    for(; i<=last; i++) contour << P[i];
                }*/
                for(int n=0,i=0; n<numContours; n++) { //create stems from points
                    for(int first=i, last= big16(endPtsOfContours[n]); i<=last; i++) {
                        int2 n=xy(P[i]); int x=n.x,y=n.y;
                        Stem* best=0;
                        for(Stem& s: stems) {
                            for(int2 a: s.tn) if(12*abs(a.y-y)>abs(a.x-x)) goto compareNextStem; //point too far from this stem
                            if(((s.ref.contains(first) && !(s.ref.last()==first && i==first+1)) || (s.direction && ((((s.direction==1?s.min:s.max)<x)?1:-1)!=s.direction))) ||
                                    (x<s.min || x>s.max)) { /*assert(!best);*/ best=&s; }
                            compareNextStem:;
                        }
                        if(best) {
                            Stem& s=*best;
                            int oldSize=s.size();
                            if((s.ref.contains(first) && !(s.ref.last()==first && i==first+1)) ||
                                    (s.direction && ((((s.direction==1?s.min:s.max)<x)?1:-1)!=s.direction))) { //stem cut by contour loop seam
                                if(!s.direction){ assert(s.size()==1);
                                    s.tn.insertAt(0,n); s.ref.insertAt(0,i); assert(s.tn.size()==s.ref.size());
                                    s.direction = s.tn.first().x<s.tn.last().x?1:-1;
                                    //log(pass,s.direction,s.tn,s.ref);
                                    int2 a=s.tn[0]; for(int2 b:s.tn.slice(1)) { assert(s.direction==(a.x<b.x?1:-1),pass,s.direction,s.tn); a=b; }  //assert monotonic
                                } else {
                                    int j=0; for(;j<s.size() && (x<s.tn[j].x?1:-1)!=s.direction;j++) {} //log(x,s.tn[j].x,s.direction, (n<s.tn[j]?1:-1)!=s.direction);
                                    //log(n,s.tn[j],(n<s.tn[j]?1:-1),j<s.size(),(n<s.tn[j]?1:-1)!=s.direction);
                                    assert(s.tn.size()==s.ref.size());
                                    s.tn.insertAt(j,n); s.ref.insertAt(j,i);
                                    assert(s.tn.size()==s.ref.size()); //log(s.tn);
                                    int2 a=s.tn[0]; for(int2 b:s.tn.slice(1)) { //assert monotonic
                                        assert(s.direction==(a.x<b.x?1:-1),pass,s.direction,s.tn,j);
                                        a=b;
                                    }
                                }
                            } else if(x<s.min || x>s.max) {
                                assert(!s.direction || (s.tn[0].x<x?1:-1)==s.direction,s.tn,n,s.ref[0],first,i,last); //assert new is extremum
                                s.tn<<n; s.ref<<i; assert(s.tn.size()==s.ref.size());
                                if(!s.direction){
                                    s.direction = s.tn.first().x<s.tn.last().x?1:-1;
                                    int2 a=s.tn[0]; for(int2 b:s.tn.slice(1)) { assert(s.direction==(a.x<b.x?1:-1),pass,s.direction,s.tn); a=b; }  //assert monotonic
                                }
                                //log(pass,s.direction,s.tn,s.ref);
                                int2 a=s.tn[0]; for(int2 b:s.tn.slice(1)) { assert(s.direction==(a.x<b.x?1:-1),pass,s.direction,s.tn,s.ref,first,last); a=b; }  //assert monotonic
                            } else continue;
                            if(y>M) log("1: pos",y,"/",M);
                            assert(s.size()==oldSize+1,oldSize,s.tn); s.min=min(s.min,x); s.max=max(s.max,x); s.pos+=y;
                        } else {
                            if(y>M) log("2: pos",y,"/",M);
                            Stem s; s.tn<<n; s.ref<<i; s.min=s.max=x; s.pos=y; stems<<move(s);
                        }
                    }
                }

                for(uint i=0;i<stems.size();i++) { Stem& s=stems[i];
                    if(s.size()==1) { stems.removeAt(i); i--; } else { s.pos/=s.size(); assert(s.pos<M,s.pos,s.size(),M); } //TODO: weighted mean
                }

                int H[nofPoints]; constexpr int unhinted=1<<30; clear(H,nofPoints,unhinted);

                while(stems) { //fit stems
                    uint best[2]={uint(-1),uint(-1)}; uint bestScore=-1;
                    for(uint i=0;i<stems.size();i++) for(uint j=0;j<i;j++) { Stem& a = stems[i]; Stem& b=stems[j];
                        assert(a.direction); assert(b.direction); assert(a.pos!=b.pos || a.direction != b.direction,a.tn," - ",b.tn);
                        //assert(a.direction==b.direction || (b.pos-a.pos>0)==b.direction,pass,a.direction,a.tn,b.direction,b.tn);
                        //different condition because image coordinates system (downwards Y) is not right-handed
                        if(a.direction != b.direction && ((a.pos-b.pos)>0)==(a.direction!=(pass?-1:1))) {
                            int min=::max(a.min,b.min), max=::min(a.max,b.max); int len=max-min;
                            if(len>=I) {
                                uint dist = abs(b.pos-a.pos);
                                uint score = dist + 3000/len;
                                if(score<bestScore) bestScore=score, best[0]=i, best[1]=j;
                                //else  log("score",score,bestScore,a.min,a.max,b.min,b.max,len);
                            }// else log(a.min,a.max,b.min,b.max,"len",len);
                        } //else log("!stem","\tA",a.pos,"\tdir",a.direction,a.tn,"\tB",b.pos,"\tdir",b.direction,b.tn);
                        //else if(a.direction != b.direction) log("miss",pass,a.direction,a.tn,b.direction,b.tn,a.pos-b.pos,a.max-a.min,b.max-b.min);
                    }
                    if(best[0]==uint(-1)) {
                        if(stems.size()>=2) {
                            if(stems.size()==2) { Stem& a=stems[0], &b=stems[1];
                                int min=::max(a.min,b.min), max=::min(a.max,b.max); int len=max-min;
                                log("X",pass,b.pos-a.pos,"\tA",a.direction,a.size(),"\t",a.pos,"\t",a.min,"\t",a.max,"\t","B",b.direction,b.size(),"\t",b.pos,"\t",b.min,"\t",b.max,abs(b.pos-a.pos),len);
                            } else {
                                log("X",pass);
                                for(Stem& a: stems) log(a.direction,a.size(),"\t",a.pos,"\t",a.min,"\t",a.max,"\t",a.tn);
                            }
                        }
                        for(Stem& a: stems) for(int i: a.ref) H[i]=clip(0,::round(I,a.pos),M-1); //iff end point
                        break;
                    }
                    Stem a=stems.take(best[0]);
                    assert(best[0]>best[1]);
                    Stem b=stems.take(best[1]);
                    if(a.pos>b.pos) swap(a,b);
                    int m = ::round(I,(a.pos+b.pos)/2+I/2)-I/2; //round to nearest %I==I/2 step
                    int w = ::round(I,(b.pos-a.pos)/2+I/2)-I/2; //round to nearest %I==I/2 step
                    int t1=m-w, t2=m+w;
                    int min=::max(a.min,b.min), max=::min(a.max,b.max); int len=max-min;
                    log("O",pass,"(a+b)/2",(a.pos+b.pos)/2,"(a+b)%2",(a.pos+b.pos)%2,"m",m,"(b-a)/2",(b.pos-a.pos)/2,"w",w,"t1",t1,"t2",t2,"d",abs(b.pos-a.pos),"l",len);
                    log("A",a.direction,a.size(),"\t",a.pos,"\t",a.min,"\t",a.max,"\t",a.tn);
                    log("B",b.direction,b.size(),"\t",b.pos,"\t",b.min,"\t",b.max,"\t",b.tn);
                    assert(t1<t2,t1,t2);
                    assert(t1%I==0,m,w,t1,t2);
                    assert((t2)%I==0);
                    if(t1<0 || t1>M-1) log("clip",t1,M); //t1=clip(0,t1,M-1);
                    if(t2<0 || t2>M-1) log("clip",t2,M); //t2=clip(0,t2,M-1);

                    for(int i: a.ref) H[i]=t1;
                    for(int i: b.ref) H[i]=t2;
                }

                for(int n=0,i=0; n<numContours; n++) for(int last= big16(endPtsOfContours[n]); i<=last; i++) assert(H[i]>=0,i,H[i]);

#if 0
                // Fix unhinted points to keep relative offset with nearest hinted point
                for(int n=0,i=0; n<numContours; n++) {
                    for(int first=i, last= big16(endPtsOfContours[n]), prev=last; i<=last; i++) {
                        int next = (i==last?first:i+1);
                        int py=xy(P[prev]).y, y = xy(P[i]).y, ny=xy(P[next]).y;
                        if(H[i]==unhinted) {
                            if(H[prev]) { log(P[i],y,H[prev]-py); H[i]=y+H[prev]-py; }
                            else if(H[next]) { log(P[i],y,H[next]-ny); H[i]=y+H[next]-ny; }
                            else error(H[prev],H[next],py,ny);
                            H[i]=clip(0,H[i],M-1);
                            //assert(H[i]>=0,pass,H[prev],H[i],H[next],P[prev],P[i],P[next]);
                        }
                        //assert(H[i]>=0,i,H[i]);
                        prev=i;
                    }
                }
#else
                for(int n=0,i=0; n<numContours; n++) for(int last= big16(endPtsOfContours[n]); i<=last; i++) if(H[i]==unhinted) H[i]=xy(P[i]).y;
#endif
                //for(int n=0,i=0; n<numContours; n++) for(int last= big16(endPtsOfContours[n]); i<=last; i++) assert(H[i]>=0,i,H[i]);

                // Overwrites original position with hinted positions
                for(int n=0,i=0; n<numContours; n++) {
                    for(int last= big16(endPtsOfContours[n]); i<=last; i++) {
                        if(pass) P[i].x = H[i]; else P[i].y=H[i];
                    }
                }
            }
            //for(int n=0,i=0; n<numContours; n++) for(int last= big16(endPtsOfContours[n]); i<=last; i++) assert(P[i]>=int2(0,0) && P[i]<int2(raster.width,raster.height),i,P[i]);
        }
        // Render
        for(int n=0,i=0; n<numContours; n++) {
            int last= big16(endPtsOfContours[n]);
            int2 p; //last on point
            if(flagsArray[last].on_curve) p=P[last];
            else if(flagsArray[last-1].on_curve) p=P[last-1];
            else p=(P[last-1]+P[last])/2;
            for(int i=last;i>0;i--) if(/*P[i-1].y != P[i].y*/true) { lastStepY = (P[i-1].y < P[i].y) ? 1 : -1; break; }
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
    if(fit || !filter) fx=0; else fx=fx&(16-1);
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

    if(down!=16) {
        uint width=raster.width, height=raster.height;
        glyph.image = Image(width, height);
        for(uint y=0; y<height; y++) {
            int acc=0;
            for(uint x=0; x<width; x++) { //Supersampled rasterization
                acc+=raster(x,y);
                //acc = abs(raster(x,y)); //contour
                //glyph.image(x,y)= acc>0? byte4(x%16*15,y%16*15,0,255) : byte4(255,255,255,0);
                //glyph.image(x,y)= acc>0? byte4(0,0,0,255) : byte4(255,255,255,0);
                glyph.image(x,y)= acc!=0? byte4(x%16*15,y%16*15,128+acc*63,255) : byte4(255,255,255,0);
            }
        }
    } else {
        uint width=ceil(subpixel*down,raster.width)/(subpixel*down)+1, height=raster.height/down; //add 1px for filtering
        glyph.image = Image(width, height);
        for(uint y=0; y<height; y++) {
            uint16 line[width*subpixel];
            int acc[16]={};
            for(uint x=0; x<width; x++) { //Supersampled rasterization
                for(int c=0; c<subpixel; c++) {
                    int sum=0;
                    for(int j=0; j<16; j++) for(int i=0; i<16; i++) {
                        int idx=-fx*subpixel+(x*subpixel+c)*16+i; if(idx>=0 && idx<(int)raster.width) acc[j] += raster(idx,y*16+j);
                        sum += acc[j]>0;
                    }
                    line[x*subpixel+c]=sum; assert(sum<=256);
                }
            }
            if(subpixel==1) {
                for(uint x=0; x<width; x++) { int v=line[x], l=sRGB[256-v]; assert(v>=0 && v<=256,v); glyph.image(x,y)=byte4(l,l,l,min(255,v)); }
            } else for(uint x=0; x<width; x++) { //LCD subpixel filtering
                uint16 filter[5] = {1, 4, 6, 4, 1}; if(!::filter) filter[0]=filter[1]=0, filter[2]=16, filter[3]=filter[4]=0;
                uint16 pixel[3] = {0,0,0};
                for(uint c=0; c<3; c++) for(int i=0;i<5;i++) { int idx=x*3+c+i-2; if(idx>=0 && idx<(int)width*3) pixel[c]+=filter[i]*line[idx]; }
                int r=16*256-pixel[0],g=16*256-pixel[1],b=16*256-pixel[2];
                if(r||g||b) {
                    int l = (66*r+129*g+25*b); /*standard perceived luminance (also depends on display calibration and lighting condition)*/
                    int v = (85*r+86*g+85*b)/16; /*target luminance (from coverage)*/
                    if(correct) r = min(256,r*v/l), g=min(256,g*v/l), b=min(256,b*v/l); //scale all components to match perceptual intensity to target value
                    else r = r/16, g=g/16, b=b/16;
                    glyph.image(x,y) = byte4(sRGB[b],sRGB[g],sRGB[r],min(255,pixel[0]+pixel[1]+pixel[2])); //sRGB
                } else glyph.image(x,y) = byte4(0,0,0,255); //full coverage
                //correct for different color intensity perception (r~0.3,g~0.5,b~0.2), i.e: lighten blue, darken green
                //const int r=3/*0.3~1/3*/, g=2/*0.5~1/2*/, b=5/*0.2~1/5*/;
                //glyph.image(x,y) = byte4(255-gamma[pixel[2]/(16*b)],255-gamma[pixel[1]/(16*g)],255-gamma[pixel[0]/(16*r)],min(255,pixel[0]+pixel[1]+pixel[2])); //vertical RGB pixels
            }
        }
    }
    if(up>1){
        glyph.image=resize(glyph.image,up*glyph.image.width,up*glyph.image.height); glyph.offset*=up;
    }
    return glyph;
}
