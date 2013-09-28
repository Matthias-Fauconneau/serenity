/// \file bmp.cc Decodes Windows device independent bitmap (DIB) headers
#include "image.h"
#include "data.h"

generic struct bgr { T b,g,r; operator byte4() const { return byte4 {b,g,r,0xFF}; } };
typedef vector<bgr,uint8,3> bgr3;

/// Decodes Windows device independent bitmap (DIB) headers
Image decodeTGA(const ref<byte>& file) {
    BinaryData s(file);
    struct Header { uint8 idSize, hasColorMap, imageType; uint16 mapOffset, mapSize; uint8 mapDepth; uint16 x,y,width,height; uint8 depth, descriptor; } packed;
    Header header = s.read<Header>();
    assert(header.idSize==0);
    assert(header.hasColorMap==0);
    assert(header.imageType==2 || header.imageType==10);

    uint w=header.width, h=header.height, depth=header.depth, size = w*h*depth/8;
    Image image(w,h,depth==32);
    mref<byte4> dst = image.buffer;
    if(header.imageType==2) {
        if(size>s.available(size)) { warn("Invalid TGA"); return Image(); }
        const uint8* src = s.read<uint8>(size).data;
        /**/ if(depth==24) for(uint y: range(h)) for(uint x: range(w)) dst[(h-1-y)*w+x] = ((bgr3*)src)[y*w+x]; // Flips and unpacks to 32bit
        else if(depth==32) for(uint y: range(h)) for(uint x: range(w)) dst[(h-1-y)*w+x] = ((byte4*)src)[y*w+x]; // Flips
        else error("depth", depth);
    } else {
        for(uint i=0;i<w*h;) {
            if(!s) error("Unexpected end of file");
            int header = s.read<uint8>();
            /**/ if(header<=127) {
                if(depth==24) for(uint unused j: range(header+1)) dst[i++] = s.read<bgr3>();
                else if(depth==32) for(uint unused j: range(header+1)) dst[i++] = s.read<byte4>();
                else error(depth);
            }
            else if(header!=255) {
                byte4 c;
                if(depth==24) c = s.read<bgr3>();
                else if(depth==32) c = s.read<byte4>();
                else error(depth);
                for(uint unused j: range(header-127)) dst[i++] = c;
            }
        }
        image = flip(move(image));
    }
    return image;
}

