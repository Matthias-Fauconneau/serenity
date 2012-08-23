#include "image.h"
#include "stream.h"

struct Directory { uint16 reserved, type, count; };
struct Entry { ubyte width, height, colorCount, reserved; uint16 planeCount, depth; uint32 size, offset; };
struct Header { uint32 headerSize, width, height; uint16 planeCount, depth; uint32 compression, size, xPPM, yPPM, colorCount, importantColorCount; };

Image decodeICO(const ref<byte>& file) {
    DataStream s(array<byte>(file.data,file.size));

    Directory unused directory = s.read<Directory>();
    assert(directory.reserved==0);
    assert(directory.type==1);
    assert(directory.count>=1);
    Entry entry = s.read<Entry>();
    s.index=entry.offset; //skip other entries

    Header header = s.read<Header>();
    assert(header.width==entry.width && header.height==entry.height*2);
    assert(header.headerSize==sizeof(Header));
    assert(header.planeCount==1);
    assert(header.depth==4||header.depth==8||header.depth==24||header.depth==32,header.depth);
    assert(header.compression==0);

    array<byte4> palette;
    if(header.depth<=8) {
        assert(header.colorCount==0 || header.colorCount==(1u<<header.depth),header.colorCount,header.depth);
        palette = array<byte4>( s.read<byte4>(1<<header.depth) );
        for(int i=0;i<1<<header.depth;i++) palette[i].a=255;
    }

    uint w=header.width,h=header.height/2;
    uint size = header.depth*w*h/8;
    if(size>s.available(size)) { warn("Invalid ICO"); return Image(); }

    Image image(w,h,true);
    array<byte> source = s.read(size);
    assert(source,size);
    ubyte* src=(ubyte*)source.data();
    if(header.depth==32) { copy((ubyte*)image.data,src,size); }
    if(header.depth==24) for(uint i=0;i<w*h;src+=3) image.data[i++] = byte4(src[0],src[1],src[2],255);
    if(header.depth==8) for(uint i=0;i<w*h;src++) image.data[i++] = palette[*src];
    if(header.depth==4) {
        assert(w%8==0); //TODO: pad
        for(uint i=0;i<w*h;src++){ image.data[i++] = palette[(*src)>>4]; image.data[i++] = palette[(*src)&0xF]; }
    }
    if(header.depth!=32) {
        ref<byte> mask = s.read<byte>(8*align(4,w/8)*h/8); //1-bit transparency
        assert(mask.size==8*align(4,w/8)*h/8);
        ubyte* src=(ubyte*)mask.data;
        assert(w%8==0); //TODO: pad
        for(uint i=0,y=0;y<h;y++) {
            uint x=0; for(;x<w;src++) {
                for(int b=7;b>=0;b--,x++){ if((*src)&(1<<b)) image.data[i].a=0; i++; }
            }
            assert(x%8==0);
            x/=8;
            while(x%4) x++, src++;
        }
    }
    return flip(move(image));
}
