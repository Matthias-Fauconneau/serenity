#include "image.h"
#include "data.h"

Image decodeICO(const ref<byte>& file) {
    BinaryData s(array<byte>(file.data,file.size));

    struct Directory { uint16 reserved, type, count; };
    Directory unused directory = s.read<Directory>();
    assert(directory.reserved==0);
    assert(directory.type==1);
    assert(directory.count>=1);
    struct Entry { uint8 width, height, colorCount, reserved; uint16 planeCount, depth; uint32 size, offset; };
    Entry entry = s.read<Entry>();
    s.index=entry.offset; //skip other entries

    struct Header { uint32 headerSize, width, height; uint16 planeCount, depth; uint32 compression, size, xPPM, yPPM, colorCount, importantColorCount; };
    Header header = s.read<Header>();
    assert(header.width==entry.width && header.height==entry.height*2);
    assert(header.headerSize==sizeof(Header));
    assert(header.planeCount==1);
    assert(header.depth==4||header.depth==8||header.depth==24||header.depth==32,header.depth);
    assert(header.compression==0);

    byte4 palette[256];
    if(header.depth<=8) {
        assert(header.colorCount==0 || header.colorCount==(1u<<header.depth),header.colorCount,header.depth);
        s.read<byte4>(palette, 1<<header.depth);
        for(int i=0;i<1<<header.depth;i++) palette[i].a=255;
    }

    uint w=header.width,h=header.height/2;
    uint size = header.depth*w*h/8;
    if(size>s.available(size)) { warn("Invalid ICO"); return Image(); }

    Image image(w,h,true);
    byte4* dst = (byte4*)image.data;
    const uint8* src = s.read<uint8>(size).data;
    if(header.depth==32) { copy(dst,(byte4*)src,size); }
    if(header.depth==24) for(uint i: range(w*h)) dst[i] = byte4(src[i*3+0],src[i*3+1],src[i*3+2],255);
    if(header.depth==8) for(uint i: range(w*h)) dst[i] = palette[src[i]];
    if(header.depth==4) {
        assert(w%8==0); //TODO: pad
        for(uint i=0;i<w*h;src++){ dst[i++] = palette[(*src)>>4]; dst[i++] = palette[(*src)&0xF]; }
    }
    if(header.depth!=32) {
        ref<byte> mask = s.read<byte>(8*align(4,w/8)*h/8); //1-bit transparency
        assert(mask.size==8*align(4,w/8)*h/8);
        byte* src=(byte*)mask.data;
        assert(w%8==0); //TODO: pad
        for(uint i=0,y=0;y<h;y++) {
            uint x=0; for(;x<w;src++) {
                for(int b=7;b>=0;b--,x++){ if((*src)&(1<<b)) dst[i].a=0; i++; }
            }
            assert(x%8==0);
            x/=8;
            while(x%4) x++, src++;
        }
    }
    return flip(move(image));
}
