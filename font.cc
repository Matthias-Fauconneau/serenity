#include "font.h"
typedef int16 FWord; //FUnits design coordinates

#include "array.cc"
Array_Default(Glyph)
Array_Default(Font::GlyphCache)

static int fonts() { static int fd = openFolder("/usr/share/fonts"_); return fd; }

Font::Font(string name) : keep(mapFile(name,fonts())) {
    DataStream s(keep);
    s.bigEndian=true;
    uint32 unused scaler=s.read();
    uint16 numTables=s.read(), unused searchRange=s.read(), unused numSelector=s.read(), unused rangeShift=s.read();
    DataStream head;
    for(int i=0;i<numTables;i++) {
        uint32 tag=s.read<uint32>(), unused checksum=s.read(), offset=s.read(), unused size=s.read();
        if(tag==raw<uint32>("head"_)) head=DataStream(s.slice(offset,size));
        if(tag==raw<uint32>("cmap"_)) cmap=DataStream(s.slice(offset,size));
        if(tag==raw<uint32>("loca"_)) loca=s.buffer.data()+offset;
        if(tag==raw<uint32>("glyf"_)) glyf=s.buffer.data()+offset;
    }
    {
       DataStream& s = head;
       uint32 unused version=s.read(), unused revision=s.read();
       uint32 unused checksum=s.read(), unused magic=s.read();
       uint16 unused flags=s.read(); unitsPerEm=s.read();
       uint64 unused created=s.read(), unused modified=s.read();
       array<FWord> unused bbox = s.read<FWord>(4);
       uint16 unused macStyle=s.read(), unused lowestRecPPEM=s.read();
       int16 unused fontDirection = s.read(); indexToLocFormat=s.read(); int16 unused glyphDataFormat=s.read();
    }
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
            array<uint16> endCode = s.read<uint16>(segCount); //TODO: read<T[N]>
            s.advance(2); //pad
            array<uint16> startCode = s.read<uint16>(segCount);
            array<uint16> idDelta = s.read<uint16>(segCount);
            array<uint16> idRangeOffset = s.read<uint16>(segCount);
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

const Glyph& Font::glyph(int size, uint16 code) {
    // Lookup glyph in cache
    GlyphCache& glyphs = cache[size];
    Glyph& glyph = glyphs[code]; //TODO: lookup for code in [0x20..0x80]
    if(glyph.image || glyph.advance.x) return glyph;

    // inline function to scale from design (FUnits) to device (.8 pixel)
    int scale=0; for(int v=unitsPerEm;v>>=1;) scale++; scale-=8;
    int round = (1<<scale)/2; //round to nearest not down
    #define scale(p) ((size*(p)+round)>>scale)

    // map unicode to glyf outline
    int i = index(code);
    //glyph.advance = TODO: parse 'hmtx' table
    int start = ( indexToLocFormat? swap32(((uint32*)loca)[i]) : 2*swap16(((uint16*)loca)[i]) );
    int length = ( indexToLocFormat? swap32(((uint32*)loca)[i+1]) : 2*swap16(((uint16*)loca)[i+1]) ) - start;
    DataStream s(array<byte>(glyf +start, length),true);

    int16 numContours = s.read();
    array<FWord> bbox = s.read<FWord>(4);

    if(numContours>0) {
        array<uint16> endPtsOfContours = s.read<uint16>(numContours);
        int nofPoints = endPtsOfContours[numContours-1]+1;

        uint16 instructionLength = s.read(); array<uint8> unused instructions = s.read<uint8>(instructionLength);

        struct Flags { byte on_curve:1, short_x:1, short_y:1, repeat:1, same_sign_x:1, same_sign_y:1; };
        Flags flagsArray[nofPoints];
        for(int i=0;i<nofPoints;i++) {
            Flags flags = s.read();
            if(flags.repeat) { ubyte times = s.read(); for(int n=0;n<times;n++) flagsArray[i++]=flags; }
            flagsArray[i]=flags;
        }

        int16 X[nofPoints]; FWord last=-bbox[0];
        for(int i=0;i<nofPoints;i++) { auto flags=flagsArray[i];
            if(flags.short_x) {
                if(flags.same_sign_x) last+= (uint8)s.read();
                else last-= (uint8)s.read();
            } else if(!flags.same_sign_x) last+= (int16)s.read();
            X[i]= scale(last);
        }

        int16 Y[nofPoints]; last=-bbox[1];
        for(int i=0;i<nofPoints;i++) { auto flags=flagsArray[i];
            if(flags.short_y) {
                if(flags.same_sign_y) last+= (uint8)s.read();
                else last-= (uint8)s.read();
            } else if(!flags.same_sign_y) last+= (int16)s.read();
            Y[i]= scale((bbox[3]-bbox[1])-last); //flip to downward y
        }

        int width= (scale(bbox[2]-bbox[0])>>8)+1, height= (scale(bbox[3]-bbox[1])>>8)+1;
        struct cover_flag{ uint8 cover; int8 flag; };
        Image<cover_flag> raster(width,height);
        clear((byte*)raster.data,sizeof(cover_flag)*width*height);

        for(int n=0,i=0; n<numContours; n++) {
            //rasterizing contours as polygon // TODO: quadratic
            int last= endPtsOfContours[n];
            int X1,Y1,X2=X[last], Y2=Y[last]; //begin by closing contour
            for(; i<=last; i++) {
                X1=X2, X2=X[i];
                Y1=Y2, Y2=Y[i];
                int x1=X1, y1=Y1, x2=X2, y2=Y2;
                int dir=-1;
                if(y1>y2) swap(x1,x2), swap(y1,y2), dir=1; //up
                else if(y1==y2) continue;
                int deltaX=x2-x1; uint deltaY=y2-y1;
                for(int y=y1;y<=y2;y+=1<<8) { //for each horizontal crossing
                    int x = x1+(y-y1)*deltaX/deltaY;
                    raster(x>>8,y>>8).flag+=dir; //fill flag
                }
            }
        }

        glyph.image= Image<ubyte>(width,height);
        for(int y=0; y<height; y++) {
            int acc=0;
            for(int x=0; x<width; x++) {
                acc += raster(x,y).flag;
                glyph.image(x,y) = acc>0 ? 255 : 0;
            }
        }
    } else {
        numContours = -numContours;
        for(int i=0;i<numContours;i++) {
            uint16 unused flags = s.read(), unused glyphIndex=s.read();
            error("compound");
        }
    }
    glyph.offset= int2(scale(bbox[0]),scale(bbox[1]));
    return glyph;
}
