#include "tiff.h"
#include "data.h"
#include "function.h"

Image16 parseTIF(ref<byte> file) {
	BinaryData s(file);
    s.isBigEndian = (bool)s.match("MM");
    if(!s.isBigEndian) s.skip("II");
    if(s.read16()!=42) error("");
    Image16 image;
    int compression = 0; // 1: raw, 7: jpeg
    uint2 tileSize = 0_0;
    ImageT<ref<byte>> tiles;
    for(;;) {
        s.index = s.read32();
        if(!s.index) break;
        function<void(BinaryData& s)> readIFD = [&](BinaryData& s) -> void {
            bool preview = false;
            uint16 entryCount = s.read16();
            for(auto_: range(entryCount)) {
                struct Entry { uint16 tag, type; uint count; } e = s.read<Entry>();
                if(s.isBigEndian) e = {big16(e.tag), big16(e.type), big32(e.count)};
                if(e.tag == 0) break;
                uint value = -1;
                BinaryData reference;
                if(e.count == 1) {
                    if(e.type==2) { value = (uint8)s.read8(); s.advance(3); }
                    else if(e.type==3) { value = (uint16)s.read16(); s.advance(2); }
                    else if(e.type==4) value = s.read32();
                    // rational, int8, uint8, int16, int32
                    else if(e.type==10) s.advance(4); // srational
                    // float, double
                    else error("type", hex(e.type), hex(e.tag));
                } else {
                    reference = BinaryData(s.data, s.isBigEndian);
                    reference.index = s.read32();
                }
                log(hex(e.tag), hex(e.type), hex(e.count), value);
                /**/ if(e.tag == 0xFE) preview = value != 0; // NewSubfileType
                else if(e.tag == 0x14A) { // SubIFDs
                    assert_(e.count == 1, e.type);
                    BinaryData subIFD(s.data, s.isBigEndian);
                    subIFD.index = value;
                    readIFD(subIFD);
                }
                else if(preview) continue;
                else if(e.tag == 0x100) image.size.x = value;
                else if(e.tag == 0x101) image.size.y = value;
                else if(e.tag == 0x102) assert_(value == 16); // BitsPerSample
                else if(e.tag == 0x103) {
                    compression = value;
                    assert_(compression == 1 || compression == 7, compression);
                }
                else if(e.tag == 0x106) assert_(value == 0x8023); // Photometric Interpretation
                //10F: Manufacturer, Model
                else if(e.tag == 0x111) { // Strip offsets
                    uint32 offset;
                    if(e.count==1) {
                        offset = value;
                        image.stride = image.size.x;
                    } else {
                        error(e.count);
                        assert_(e.count>1);
                        offset = reference.read32();
                        uint32 lastOffset = offset;
                        for(uint unused i : range(1, e.count)) {
                            uint32 nextOffset = reference.read32();
                            uint32 stride = (nextOffset - lastOffset)/2;
                            if(!image.stride) image.stride = stride; //16bit
                            else assert_(image.stride == stride);
                            break;
                        }
                        assert_(offset+image.size.y*image.size.x*2<= file.size, offset, image.size, file.size);
                    }
                    ((ref<int16>&)image) = cast<int16>(s.slice(offset, image.size.y*image.size.x*2));
                }
                else if(e.tag == 0x115) assert_(value == 1); // Samples per pixel
                else if(e.tag == 0x116) assert_(value == 1 || value == image.size.y, value, e.type, e.count); // 1 row per strip or single strip
                else if(e.tag == 0x11c) assert_(value == 1); // Planar configuration
                else if(e.tag == 0x142) tileSize.x = value;
                else if(e.tag == 0x143) tileSize.y = value;
                else if(e.tag == 0x144) {
                    tiles = ImageT<ref<byte>>((image.size+tileSize-uint2(1u))/tileSize);
                    assert_(e.count == tiles.ref::size, e.count, tiles.ref::size, tiles.size);
                    for(uint i: range(e.count)) tiles[i] = s.slice(reference.read32());
                }
                else if(e.tag == 0x145) {
                    assert_(e.count == tiles.ref::size && e.type == 4);
                    for(uint i: range(e.count)) tiles[i].size = reference.read32();
                    assert_(compression == 1);
                }
                // 306: DateTime
                // {DNG 50xxx} 706: Version, Backward, Model; 721: ColorMatrix; 740: Private; 778: CalibrationIlluminant, 827: OriginalName
                // 931,2: Camera,Profile Calibration Signature; 936: ProfileName; 940-2: Profile Tone Curve, Embed Policy, Copyright
                // 964: ForwardMatrix, 981-2: Look Table
            }
        };
        readIFD(s);
    }
    assert_(image, compression, tiles.size);
	return image;
}
