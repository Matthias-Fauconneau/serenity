#include "volume-operation.h"
#include "time.h"
#include "png.h"

class(ToPNG, Operation), virtual VolumeInput {
    void execute(const map<ref<byte>, Variant>& args, const ref<byte>& name, const Volume& volume) override {
        Folder folder = Folder(args.at("name"_)+"."_+name+".png"_, args.at("resultFolder"_), true);
        uint marginZ = volume.margin.z;
        Time time; Time report;
        for(int z: range(marginZ, volume.sampleCount.z-marginZ)) {
            if(report/1000>=2) { log(z-marginZ,"/",volume.sampleCount.z-marginZ, ((z-marginZ)*volume.sampleCount.x*volume.sampleCount.y/1024/1024)/(time/1000), "MB/s"); report.reset(); }
            writeFile(dec(z,4)+".png"_, encodePNG(slice(volume,z,args.contains("cylinder"_))), folder);
        }
    }
};

inline void itoa(byte* target, uint n) {
    assert(n<1000);
    uint i=4;
    do { target[--i]="0123456789"[n%10]; n /= 10; } while( n!=0 );
    while(i>0) target[--i]=' ';
}

void toASCII(Volume& target, const Volume& source) {
    uint X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY=X*Y;
    uint marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    assert_(!target.offsetX && !target.offsetY && !target.offsetZ);
    const uint* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
    Line* const targetData = (Line*)target.data.data;
    for(uint z=0; z<Z; z++) {
        Line* const targetZ = targetData + z*XY;
        for(uint y=0; y<Y; y++) {
            Line* const targetZY = targetZ + y*X;
            for(uint x=0; x<X; x++) {
                uint value = 0;
                if(x >= marginX && x<X-marginX && y >= marginY && y<Y-marginY && z >= marginZ && z<Z-marginZ) {
                    uint index = offsetX ? offsetX[x] + offsetY[y] + offsetZ[z] : z*XY + y*X + x;
                    if(source.sampleSize==1) value = ((byte*)source.data.data)[index];
                    else if(source.sampleSize==2) value = ((uint16*)source.data.data)[index];
                    else if(source.sampleSize==4) value = ((uint32*)source.data.data)[index];
                    else error(source.sampleSize);
                    assert(value <= source.maximum, value, source.maximum);
                }

                Line& line = targetZY[x];
                itoa(line, x);
                line[4]=' ';
                itoa(line+5, y);
                line[9]=' ';
                itoa(line+10,z);
                line[14]=' ';
                itoa(line+15, min<uint>(0xFF, value * 0xFF / source.maximum)); //FIXME
                line[19]='\n';
            }
        }
    }
}
PASS(ToASCII, Line, toASCII);
