/// \file bmp.cc Decodes Windows device independent bitmap (DIB) headers
#include "image.h"
#include "data.h"

generic struct rgb { T r,g,b; operator byte4() const { return byte4 {b,g,r,255}; } };
typedef vector<rgb,uint8,3> rgb3;

/// Decodes Windows device independent bitmap (DIB) headers
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

buffer<byte> encodeBMP(const Image& image) {
    buffer<byte4> palette (256); for(uint i: range(256)) palette[i]=byte4(i);
    assert_(image.buffer.size == image.width*image.height);
    buffer<byte> data (image.buffer.size); for(uint i: range(data.size)) data[i]=image.data[i].b;
    struct Header { uint32 headerSize, width, height; uint16 planeCount, depth; uint32 compression, size, xPPM, yPPM, colorCount, importantColorCount; } packed header
                  = {sizeof(Header), image.width, image.height, 1, 8, 0, image.width*image.height, 0, 0, 0, 0};
    struct FileHeader { uint16 type; uint32 size, reserved, offset; } packed fileHeader
                      = {'B'|('M'<<8), uint32(sizeof(FileHeader)+sizeof(Header)+palette.size*4+data.size), 0, uint32(sizeof(FileHeader)+sizeof(Header)+palette.size*4) };
    buffer<byte> file = raw(fileHeader)+raw(header)+cast<byte>(palette)+data;
    assert_(file.size == sizeof(FileHeader)+sizeof(Header)+palette.size*4+data.size);
    return file;
}
