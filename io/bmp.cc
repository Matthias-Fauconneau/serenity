/// \file bmp.cc Decodes Windows Device independent bitmap (DIB) files
#include "image.h"
#include "data.h"

/// Decodes Windows Device independent bitmap (DIB) files
Image decodeBMP(const ref<byte>& file) {
    BinaryData s(file);
    struct BitmapFileHeader { uint16 type; uint32 size, reserved, offset; } packed;
    BitmapFileHeader unused bitmapFileHeader = s.read<BitmapFileHeader>();

    struct Header { uint32 headerSize, width, height; uint16 planeCount, depth; uint32 compression, size, xPPM, yPPM, colorCount, importantColorCount; };
    Header header = s.read<Header>();
    assert(header.headerSize==sizeof(Header));
    assert(header.planeCount==1);
    assert(header.depth==8);
    assert(header.compression==0);

    if(header.depth<=8) s.advance((1<<header.depth)*sizeof(byte4)); // Assumes palette[i]=i
    assert(bitmapFileHeader.offset == s.index);

    uint w=header.width,h=header.height;
    uint size = header.depth*w*h/8;
    if(size>s.available(size)) { warn("Invalid BMP"); return Image(); }

    Image image(w,h);
    byte4* dst = (byte4*)image.data;
    const uint8* src = s.read<uint8>(size).data;
    if(header.depth==8) for(uint i: range(w*h)) dst[i] = src[i];
    return image;
}
