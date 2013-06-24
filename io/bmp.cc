/// \file bmp.cc Decodes Windows Device independent bitmap (DIB) files
#include "image.h"
#include "data.h"

generic struct rgb { T r,g,b; operator byte4() const { return byte4 {b,g,r,255}; } };
typedef vector<rgb,uint8,3> rgb3;

/// Decodes Windows Device independent bitmap (DIB) files
Image decodeBMP(const ref<byte>& file) {
    BinaryData s(file);
    struct BitmapFileHeader { uint16 type; uint32 size, reserved, offset; } packed;
    BitmapFileHeader unused bitmapFileHeader = s.read<BitmapFileHeader>();

    struct Header { uint32 headerSize, width, height; uint16 planeCount, depth; uint32 compression, size, xPPM, yPPM, colorCount, importantColorCount; };
    Header header = s.read<Header>();
    assert_(header.headerSize==sizeof(Header));
    assert_(header.planeCount==1);
    assert_(header.compression==0);

    if(header.depth<=8) s.advance((1<<header.depth)*sizeof(byte4)); // Assumes palette[i]=i
    assert_(bitmapFileHeader.offset == s.index);

    uint w=header.width,h=header.height;
    uint size = header.depth*w*h/8;
    if(size>s.available(size)) { warn("Invalid BMP"); return Image(); }

    Image image(w,h);
    byte4* dst = (byte4*)image.data;
    const uint8* src = s.read<uint8>(size).data;
    if(header.depth==8) for(uint i: range(w*h)) dst[i] = src[i];
    else if(header.depth==24) for(uint i: range(w*h)) dst[i] = ((rgb3*)src)[i];
    else error("header.depth", header.depth);
    return image;
}
