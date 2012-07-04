#include "font.h"

static int fonts() { static int fd = openFolder("usr/share/fonts"_); return fd; }

Font::Font(string name, int size) : keep(mapFile(name,fonts())), size(size) {
    DataStream s(keep);
    s.bigEndian=true;
    uint32 unused scaler=s.read();
    uint16 numTables=s.read(), unused searchRange=s.read(), unused numSelector=s.read(), unused rangeShift=s.read();
    DataStream head, hhea;
    for(int i=0;i<numTables;i++) {
        uint32 tag=s.read<uint32>()/*no swap*/, unused checksum=s.read(), offset=s.read(), unused size=s.read();
        if(tag==raw<uint32>("head"_)) head=DataStream(s.slice(offset,size));
        if(tag==raw<uint32>("hhea"_)) hhea=DataStream(s.slice(offset,size));
        if(tag==raw<uint32>("cmap"_)) cmap=DataStream(s.slice(offset,size));
        if(tag==raw<uint32>("hmtx"_)) hmtx=(uint16*)(s.buffer.data()+offset);
        if(tag==raw<uint32>("loca"_)) loca=s.buffer.data()+offset;
        if(tag==raw<uint32>("glyf"_)) glyf=s.buffer.data()+offset;
    }
    {
       DataStream& s = head;
       uint32 unused version=s.read(), unused revision=s.read();
       uint32 unused checksum=s.read(), unused magic=s.read();
       uint16 unused flags=s.read(), unitsPerEm=s.read();
       s.advance(8+8+4*2+2+2+2); //created, modified, bbox[4], maxStyle, lowestRec, direction
       indexToLocFormat=s.read();
       // parameters for scale from design (FUnits) to device (.4 pixel)
#define scale(p) ((size*(p)+round)>>scale)
#define unscale(p) (((p)<<scale)/size)
       scale=0; for(int v=unitsPerEm;v>>=1;) scale++; scale-=4;
       round = (1<<scale)/2; //round to nearest not down
    }
    /*{
       DataStream& s = hhea;
       uint32 unused version=s.read();
       uint16 unused ascent=s.read(), unused descent=s.read(), unused lineGap=s.read();
       uint16 unused maxAdvance=s.read(), unused minLeft=s.read(), unused minRight=s.read(), unused maxExtent=s.read();
       s.advance(16); //caret*, 0
       numOfLongHorMetrics=s.read();
    }*/
}

uint16 Font::index(uint16 code) {
    cmap.seek(0); DataStream& s = cmap;
    uint16 unused version=s.read(), numTables=s.read();
    for(int i=0;i<numTables;i++) {
        uint16 unused platformID=s.read(), unused platformSpecificID=s.read(); uint32 offset=s.read();
        uint index=s.index; s.seek(offset);
        uint16 format = s.read(), unused size=s.read(), unused language=s.read();
        if(format==4) {
            uint16 segCount=s.read(), unused searchRange=s.read(),unused entrySelector=s.read(), unused rangeShift=s.read();
            segCount /= 2;
            array<uint16> endCode = s.read(segCount); //TODO: read<T[N]>
            s.advance(2); //pad
            array<uint16> startCode = s.read(segCount);
            array<uint16> idDelta = s.read(segCount);
            array<uint16> idRangeOffset = s.read(segCount);
            int i=0; while(endCode[i] < code) i++;
            if(startCode[i]<=code) {
                if(idRangeOffset[i]) return *( &idRangeOffset[i] + idRangeOffset[i] / 2 + (code - startCode[i]) );
                else return idDelta[i] + code;
            }
        } else error("Unsupported");
        s.index=index;
    }
    error("Not Found");
}

int Font::kerning(uint16 /*leftCode*/, uint16 /*rightCode*/) {
    //TODO: parse kern table
    return 0;
}

void line(Image<int8>& raster, int2 p0, int2 p1) {
    int x0=p0.x, y0=p0.y, x1=p1.x, y1=p1.y;
    if(y0==y1) return;
    int dx = abs(x1-x0);
    int dy = abs(y1-y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx-dy;
#define raster \
    if(raster(x0,y0)!=-sy \
            && (x0==0 || raster(x0-1,y0)!=-sy) \
            && (x0==int(raster.width-1) || raster(x0+1,y0)!=-sy)) raster(x0,y0) -= sy; //first y (try to avoid dropout inducing duplicates)
    raster
    for(;;) {
        if(x0 == x1 && y0 == y1) break;
        int e2 = 2*err;
        if(e2 > -dy) { err -= dy, x0 += sx; }
        if(e2 < dx) { err += dx, y0 += sy; raster } //only raster at y step
    }
#undef raster
}

void curve(Image<int8>& raster, int2 p0, int2 p1, int2 p2) {
#if 1
        int2 mid = (p0 + 2*p1 + p2)/4;
        line(raster,p0,mid);
        line(raster,mid,p2);
#else
    const int N=2;
    for(int t=1;t<=N;t++) {
        int2 b = ((N-t)*(N-t)*p0 + 2*(N-t)*t*p1 + t*t*p2)/(N*N);
        line(raster,a,b);
        a=b;
    }
#endif
}

Glyph Font::glyph(uint16 code) {
    // Lookup glyph in cache
    assert(code<256);
    Glyph& glyph = cache[code];
    if(glyph.image || glyph.advance) return Glyph{glyph.offset,glyph.advance,share(glyph.image)};

    // map unicode to glyf outline
    int i = index(code);
    glyph.advance = scale(swap16(hmtx[2*i]));
    int start = ( indexToLocFormat? swap32(((uint32*)loca)[i]) : 2*swap16(((uint16*)loca)[i]) );
    int length = ( indexToLocFormat? swap32(((uint32*)loca)[i+1]) : 2*swap16(((uint16*)loca)[i+1]) ) - start;
    DataStream s(array<byte>(glyf +start, length),true);
    if(!s) return Glyph{glyph.offset,glyph.advance,share(glyph.image)};

    int16 numContours = s.read();
    int16 xMin=s.read(), yMin=s.read(), xMax=s.read(), yMax=s.read();
    xMin = scale(xMin); xMin=(xMin/16)*16; xMin=unscale(xMin); //round down to pixel
    //yMin = scale(yMin); yMin=(yMin/16)*16; yMin=unscale(yMin); //round down to pixel
    xMax = scale(xMax); if(xMax%16) xMax+=16-xMax%16; xMax=unscale(xMax); //round up to pixel
    yMax = scale(yMax); if(yMax%16) yMax+=16-yMax%16; yMax=unscale(yMax); //round up to pixel
    if(numContours>0) {
        array<uint16> endPtsOfContours = s.read(numContours);
        int nofPoints = endPtsOfContours[numContours-1]+1;

        uint16 instructionLength = s.read(); array<uint8> unused instructions = s.read(instructionLength);

        struct Flags { byte on_curve:1, short_x:1, short_y:1, repeat:1, same_sign_x:1, same_sign_y:1; };
        Flags flagsArray[nofPoints];
        for(int i=0;i<nofPoints;i++) {
            Flags flags = s.read();
            if(flags.repeat) { ubyte times = s.read(); for(int n=0;n<times;n++) flagsArray[i++]=flags; }
            flagsArray[i]=flags;
        }

        int P_[2*nofPoints]; int2* P=(int2*)&P_;
        {
            int16 last=-xMin;
            for(int i=0;i<nofPoints;i++) { Flags flags=flagsArray[i];
                if(flags.short_x) {
                    if(flags.same_sign_x) last+= (uint8)s.read();
                    else last-= (uint8)s.read();
                } else if(!flags.same_sign_x) last+= (int16)s.read();
                P[i].x= scale(last);
            }
            last=-yMax;
            for(int i=0;i<nofPoints;i++) { Flags flags=flagsArray[i];
                if(flags.short_y) {
                    if(flags.same_sign_y) last+= (uint8)s.read();
                    else last-= (uint8)s.read();
                } else if(!flags.same_sign_y) last+= (int16)s.read();
                P[i].y= scale(-last); //flip to downward y
            }
        }

        int width=scale(xMax-xMin), height=scale(yMax-yMin);
        Image<int8> raster(width+1,height+1);
        for(int i=0;i<width*height; i++) raster.data[i]=0;

        for(int n=0,i=0; n<numContours; n++) {
            int last= endPtsOfContours[n];
            int2 p; //last on point
            if(flagsArray[last].on_curve) p=P[last];
            else if(flagsArray[last-1].on_curve) p=P[last-1];
            else p=(P[last-1]+P[last])/2;
            for(int prev=last; i<=last; i++) {
                if(flagsArray[prev].on_curve && flagsArray[i].on_curve) { line(raster, P[prev], P[i]); p=P[i]; } //on-on
                else if(flagsArray[prev].on_curve && !flagsArray[i].on_curve) p=P[prev]; //on-off (drawn on next step (either on-off-on or on-off-off))
                else if(!flagsArray[prev].on_curve && flagsArray[i].on_curve) { curve(raster, p, P[prev], P[i]); p=P[i]; } //off-on
                else if(!flagsArray[prev].on_curve && !flagsArray[i].on_curve) { int2 m=(P[prev]+P[i])/2; curve(raster, p, P[prev], m); p=m; } //off-off
                prev=i;
            }
        }
        Image<int8> bitmap(width,height);
        for(int y=0; y<height; y++) {
            int acc=0;
            for(int x=0; x<width; x++) {
                acc += raster(x,y);
                bitmap(x,y) = acc>0; //TODO: alpha blit
            }
        }

        glyph.image = Image<gray>(width/16,height/16);
        for(int y=0; y<height/16; y++) {
            for(int x=0; x<width/16; x++) {
                int acc=0;
                for(int j=0; j<16; j++) for(int i=0; i<16; i++) acc += bitmap(x*16+i,y*16+j);
                glyph.image(x,y) = acc? 256-acc : 255; //TODO: alpha blit
            }
        }
    } else {
        numContours = -numContours;
        for(int i=0;i<numContours;i++) {
            uint16 unused flags = s.read(), unused glyphIndex=s.read();
            error("compound");
        }
    }
    glyph.offset = int2(scale(xMin),(size<<4)-scale(yMax));
    return Glyph{glyph.offset,glyph.advance,share(glyph.image)};
}
