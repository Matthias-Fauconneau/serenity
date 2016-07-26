#include "cr2.h"

CR2::CR2(const ref<byte> file, bool onlyParse) : onlyParse(onlyParse)  {
 int compression = 0;
 BinaryData TIFF(file);
 TIFF.skip("II\x2A\x00");
 for(;;) {
  TIFF.index = TIFF.read32();
  if(!TIFF.index) break;
  uint16 entryCount = TIFF.read16();
  for(const CR2::Entry& entry : TIFF.read<CR2::Entry>(entryCount)) {
   /**/ if(entry.tag == 0x103) { // Compression
    assert_(entry.value == 1 || entry.value == 6 /*JPEG*/ || entry.value == 0x879C /*rANS4*/);
    compression = entry.value;
   }
   else if(entry.tag == 0x111) { // StripOffset
    assert_(entry.count == 1);
    tiffHeaderSize = entry.value;
   }
   else if(entry.tag == 0x117) { // StripByteCount
    dataSize = entry.value;
    assert_(dataSize <= TIFF.data.size);
   }
   else if(entry.tag == 0x8769) { // EXIF
    BinaryData EXIF (TIFF.data); EXIF.index = entry.value;
    uint16 entryCount = EXIF.read();
    for(const CR2::Entry& entry : EXIF.read<CR2::Entry>(entryCount)) {
     BinaryData value (EXIF.data); value.index = entry.value;
     if(entry.tag == 0x829A) { // ExposureTime
      assert_(entry.type == 5 && entry.count == 1);
      unused uint num = value.read32();
      unused uint den = value.read32();
      //log("ExposureTime", num,"/",den, "s");
     }
     else if(entry.tag == 0x829D) { // fNumber
      assert_(entry.type == 5 && entry.count == 1);
      unused uint num = value.read32();
      unused uint den = value.read32();
      //log("fNumber", num,"/",den);
     }
     else if(entry.tag == 0x8827) { // ISOSpeedRatings
      assert_(entry.type == 3 && entry.count == 1);
      //log("ISO", entry.value);
     }
     else if(entry.tag == 0x9003) { // DateTimeOriginal
      assert_(entry.type == 2);
      //log("DateTimeOriginal", value.peek(entry.count));
     }
     else if(entry.tag == 0x9201) { // ShutterSpeedValue
      assert_(entry.type == 10 && entry.count == 1);
      unused int32 num = value.read32();
      unused int32 den = value.read32();
      //log("ShutterSpeedValue", num,"/",den);
     }
     else if(entry.tag == 0x9202) { // ApertureValue
      assert_(entry.type == 5 && entry.count == 1);
      unused uint32 num = value.read32();
      unused uint32 den = value.read32();
      //log("ApertureValue", num,"/",den);
     }
     else if(entry.tag == 0x9204) { // ExposureBiasValue
      assert_(entry.type == 10 && entry.count == 1);
      unused uint32 num = value.read32();
      unused uint32 den = value.read32();
      //log("ExposureBiasValue", num,"/",den);
     }
     else if(entry.tag == 0x9209) { // Flash
      assert_(entry.type == 3 && entry.count == 1);
      //log("Flash", entry.value); // 16: Off
     }
     else if(entry.tag == 0x920A) { // FocalLength
      assert_(entry.type == 5 && entry.count == 1);
      unused uint32 num = value.read32();
      unused uint32 den = value.read32();
      //log("FocalLength", num,"/",den);
     }
     else if(entry.tag == 0x927C) { // MakerNote
      assert_(entry.type == 7);
      BinaryData makerNote (EXIF.data); makerNote.index = entry.value;
      uint16 entryCount = makerNote.read();
      for(const CR2::Entry& entry : makerNote.read<CR2::Entry>(entryCount)) {
       if(entry.tag == 0x4001) { // ColorBalance
        ref<uint16> RGGB = cast<uint16>(makerNote.slice(entry.value+142, 4*sizeof(uint16)));
        whiteBalance.R = RGGB[0];
        whiteBalance.G = RGGB[1];
        whiteBalance.B = RGGB[3];
       }
      }
     }
    }
   }
  }
 }
 ljpeg.parse(file.slice(tiffHeaderSize));
 if(onlyParse) return;
 image = Image16(ljpeg.width*2, ljpeg.height);
 if(compression == 6) { // JPEG
  ljpeg.decode(image, file.slice(tiffHeaderSize+ljpeg.headerSize));
 } else { // rANS4
  assert_(compression == 0x879C);
  decodeRANS4(image, file.slice(tiffHeaderSize+ljpeg.headerSize));
 }
}
