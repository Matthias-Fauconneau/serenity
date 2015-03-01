#include "tiff.h"
#include "data.h"
#include "function.h"

Image16 parseTIF(ref<byte> file) {
	BinaryData s(file);
	s.skip("II\x2A\x00");
    Image16 image;
    for(;;) {
        s.index = s.read32();
        if(!s.index) break;
        function<void(BinaryData& s)> readIFD = [&](BinaryData& s) -> void {
            uint16 entryCount = s.read();
            for(uint unused i : range(entryCount)) {
                struct Entry { uint16 tag, type; uint count; uint value; } entry = s.read<Entry>();
                BinaryData value (s.data); value.index = entry.value;
                // 254: NewSubfileType
                if(entry.tag == 256) image.size.x = entry.value;
                if(entry.tag == 257) image.size.y = entry.value;
                if(entry.tag == 258) assert_(entry.value == 16);
                if(entry.tag == 259) assert_(entry.value == 1);
                //262: Photometric Interpretation
                //271: Manufacturer, Model
                if(entry.tag == 273) {
                    uint32 offset;
                    if(entry.count==1) {
                        offset = value.index;
                        image.stride = image.size.x;
                    } else {
                        assert_(entry.count>1);
                        offset = value.read32();
                        uint32 lastOffset = offset;
                        for(uint unused i : range(1, entry.count)) {
                            uint32 nextOffset = value.read32();
                            uint32 stride = (nextOffset - lastOffset)/2;
                            if(!image.stride) image.stride = stride; //16bit
                            else assert_(image.stride == stride);
                            break;
                        }
                        assert_(offset+image.size.y*image.size.x*2<= file.size, offset, image.size, file.size);
                    }
                    ((ref<int16>&)image) = cast<int16>(s.slice(offset, image.size.y*image.size.x*2));
                }
                if(entry.tag == 278) assert_(entry.value == 1 || entry.value == image.height); // 1 row per trip or single strip
                //306: DateTime
                if(entry.tag == 330) { // SubIFDs
                    assert_(entry.count == 1);
                    readIFD(value);
                }
                // {DNG 50xxx} 706: Version, Backward, Model; 721: ColorMatrix; 740: Private; 778: CalibrationIlluminant, 827: OriginalName
                // 931,2: Camera,Profile Calibration Signature; 936: ProfileName; 940-2: Profile Tone Curve, Embed Policy, Copyright, 964: ForwardMatrix, 981-2: Look Table
            }
        };
        readIFD(s);
    }
    assert_(image);
	return image;
}
