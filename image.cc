#include "image.h"
#include "stream.h"
#include "vector.h"
#include "string.h"
#include <zlib.h>

template<class T> void filter(byte4* dst, const byte* raw, int width, int height) {
    byte4* zero = allocate<byte4>(width); clear((byte*)zero,width*4); byte4* prior=zero;
    for(int y=0;y<height;y++,raw+=width*sizeof(T),dst+=width) {
        int filter = *raw++; assert(filter>=0 && filter<=4);
        T* src = (T*)raw;
        byte4 a(0,0,0,0);
        if(filter==0) for(int i=0;i<width;i++) dst[i]= src[i];
        if(filter==1) for(int i=0;i<width;i++) dst[i]= a= a+src[i];
        if(filter==2) for(int i=0;i<width;i++) dst[i]= prior[i]+src[i];
        if(filter==3) for(int i=0;i<width;i++) dst[i]= a= byte4((int4(prior[i])+int4(a))/2)+src[i];
        if(filter==4) {
            byte4 a; int4 b;
            for(int i=0;i<width;i++) {
                int4 c = b;
                b = int4(prior[i]);
                int4 d = int4(a) + b - c;
                int4 pa = abs(d-int4(a)), pb = abs(d-b), pc = abs(d-c);
                byte4 p; for(int i=0;i<4;i++) p[i]=uint8(pa[i] <= pb[i] && pa[i] <= pc[i] ? a[i] : pb[i] <= pc[i] ? b[i] : c[i]);
                dst[i]= a= p+src[i];
            }
        }
        prior = dst;
    }
    free(zero);
}

Image::Image(array<byte>&& file) {
    Stream<BigEndian> s(move(file));
    if(!s.match("\x89PNG\r\n\x1A\n"_)) error("Unknown image format"_);
    z_stream z; clear(z); inflateInit(&z);
    array<byte> idat(file.size*16); //FIXME
    z.next_out = (Bytef*)&idat, z.avail_out = (uint)idat.capacity;
    int depth=0;
    while(s) {
        uint32 size = s.read();
        string name = s.read(4);
        if(name == "IHDR"_) {
            width = (int)(uint32)s.read(), height = (int)(uint32)s.read();
            //uint8 bitDepth = s.read(), type = s.read(), compression = s.read(), filter = s.read(), interlace = s.read();
            //assert(bitDepth==8); assert(compression==0); assert(filter==0); assert(interlace==0);
            s++; uint8 type = s.read(); s+=3;
            depth = (int[]){0,0,3,0,2,0,4}[type];
            if(!depth) return;
        } else if(name == "IDAT"_) {
            z.avail_in = size;
            z.next_in = (Bytef*)&s.read((int)size);
            inflate(&z, Z_NO_FLUSH);
        } else s += (int)size;
        s+=4; //CRC
    }
    inflate(&z, Z_FINISH);
    inflateEnd(&z);
    idat.size = (int)z.total_out;
    assert(idat.size == height*(1+width*depth), idat.size, width, height);
    data = allocate<byte4>(width*height);
    /**/ if(depth==2) filter<byte2>((byte4*)data,&idat,width,height);
    else if(depth==4) filter<rgba4>((byte4*)data,&idat,width,height);
    else error("depth"_,depth);
}

Image& Image::resize(int w, int h) {
    if(w==width && h==height) return *this;
    const byte4* src = data;
    byte4* buffer = allocate<byte4>(w*h);
    byte4* dst = buffer;
    assert(width/w==height/h && !(width%w) && !(height%h)); //integer uniform downscale
    int scale = width/w;
    for(int y=0;y<h;y++) {
        for(int x=0;x<w;x++) {
            int4 s{0,0,0,0};
            for(int i=0;i<scale;i++){
                for(int j=0;j<scale;j++) {
                    s+= int4(src[i*width+j]);
                }
            }
            *dst = byte4(s/(scale*scale));
            src+=scale, dst++;
        }
        src += (scale-1)*width;
    }
    width=w, height=h;
    if(own) delete[] data; data=buffer; own=true;
    return *this;
}

Image& Image::swap() {
    uint32* p = (uint32*)data;
    for(int i=0;i<width*height;i++) {
        p[i] = swap32(p[i]);
    }
    return *this;
}
