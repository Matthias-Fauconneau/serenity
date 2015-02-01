#include "tiff.h"
#include "data.h"

Image16 parseTIFF(ref<byte> file) {
	BinaryData s(file);
	s.skip("II\x2A\x00");
	s.index = s.read32();
	uint16 entryCount = s.read();
	Image16 image;
	for(uint unused i : range(entryCount)) {
		struct Entry { uint16 tag, type; uint count; uint value; } entry = s.read<Entry>();
		BinaryData value (s.data); value.index = entry.value;
		if(entry.tag == 257) image.size.y = entry.value;
		if(entry.tag == 256) image.size.x = entry.value;
		if(entry.tag == 258) assert_(entry.value == 16);
		if(entry.tag == 259) assert_(entry.value == 1);
		if(entry.tag == 273) {
			uint32 offset = value.read32();
			uint32 lastOffset = offset;
			for(uint unused i : range(1, entry.count)) {
				uint32 nextOffset = value.read32();
				uint32 stride = (nextOffset - lastOffset)/2;
				//if(!image.stride) image.stride = (nextOffset - lastOffset)/2; //16bit
				assert_(stride == uint(image.size.x));
				break;
				//assert_(nextOffset - lastOffset == image.stride*2, lastOffset, nextOffset, nextOffset-offset, image.stride);
				//lastOffset = nextOffset;
			}
			assert_(offset+image.size.y*image.size.x*2<= file.size);
			((ref<int16>&)image) = cast<int16>(s.slice(offset, image.size.y*image.size.x*2));
		}
		if(entry.tag == 278) assert_(entry.value == 1); // 1 row per trip // assert_(entry.value == image.height, entry.value, image.height); // single strip
	}
	return image;
}
