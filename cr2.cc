#include "cr2.h"
#include "bit.h"

uint CR2::readBits(const int nbits) {
 if(nbits==0) return 0u;
 while(vbits < nbits) {
  uint byte = *pointer; pointer++;
  if(byte == 0xFF) { uint8 v = *pointer; pointer++; assert_(v == 0x00); }
  bitbuf <<= 8;
  bitbuf |= byte;
  vbits += 8;
 }
 uint value = (bitbuf << (32-vbits)) >> (32-nbits);
 vbits -= nbits;
 return value;
}

uint8 CR2::readHuffman(uint i) {
 const int nbits = maxLength[i];
 while(vbits < nbits) {
  uint byte = *pointer; pointer++;
  if(byte == 0xFF) { uint8 v = *pointer; pointer++; assert_(v == 0x00); }
  bitbuf <<= 8;
  bitbuf |= byte;
  vbits += 8;
 }
 uint code = (bitbuf << (32-vbits)) >> (32-nbits);
 //assert_(lengthSymbolForCode[i][code].length <= maxLength[i]);
 vbits -= lengthSymbolForCode[i][code].length;
 return lengthSymbolForCode[i][code].symbol;
}

void CR2::readIFD(BinaryData& s) {
 int compression = 0;
 ref<byte> data;
 uint16 entryCount = s.read();
 int2 size = 0; size_t stride = 0;
 for(uint unused i : range(entryCount)) {
  struct Entry { uint16 tag, type; uint count; uint value; } entry = s.read<Entry>();
  BinaryData value (s.data); value.index = entry.value;
  // 0xFE: NewSubfileType
  /**/  if(entry.tag == 0x100) size.x = entry.value; // Width
  else if(entry.tag == 0x101) size.y = entry.value; // Height
  else if(entry.tag == 0x102) { // BitsPerSample
   if(entry.count==1) {
    assert_(entry.value == 16);
   } else if(entry.count==3) {
    assert_(entry.type == 3);
    int R = value.read16();
    int G = value.read16();
    int B = value.read16();
    assert_((R==8 && G==8 && B==8) || (R==16 && G==16 && B==16), R, G, B);
   }
  }
  else if(entry.tag == 0x103) { // Compression
   assert_(entry.value == 1 || entry.value == 6 /*JPEG*/);
   compression = entry.value;
  }
  else if(entry.tag == 0x10E) {} // ImageDescription
  else if(entry.tag == 0x10F) {} // Manufacturer
  else if(entry.tag == 0x110) {} // Model
  else if(entry.tag == 0x106) assert_(entry.value == 2); // PhotometricInterpretation
  else if(entry.tag == 0x111) { // StripOffset
   assert_(entry.count == 1);
   size_t offset;
   if(entry.count==1) {
    offset = value.index;
    stride = size.x;
   } else {
    assert_(entry.count>1);
    offset = value.read32();
    uint32 lastOffset = offset;
    for(uint unused i : range(1, entry.count)) {
     uint32 nextOffset = value.read32();
     uint32 nextStride = (nextOffset - lastOffset)/2;
     if(!stride) stride = nextStride; //16bit
     else assert_(stride == nextStride);
     break;
    }
    assert(compression==1 || compression==6);
   }
   assert_(!data.size);
   data.data = s.data.data+offset;
   //((ref<int16>&)image) = cast<int16>(s.slice(offset, image.size.y*image.size.x*2));
  }
  else if(entry.tag == 0x112) assert_(entry.value==1 || entry.value == 6, "Orientation", entry.value); // Orientation
  else if(entry.tag == 0x115) assert_(entry.value == 3, entry.value); // PhotometricInterpretation
  else if(entry.tag == 0x116) {} //log("RowPerStrip", entry.value); // RowPerStrip
  else if(entry.tag == 0x117) { // StripByteCount
   assert_(entry.value == 1 || entry.value == (uint)size.y || entry.value==(uint)size.x*size.y*3*2 || compression>1, entry.value, size); // 1 row per trip or single strip
   data.size = entry.value;
   assert_(data.end() <= s.data.end());
  }
  else if(entry.tag == 0x11A) assert_(entry.type==5 && entry.count==1); // xResolution (72)
  else if(entry.tag == 0x11B) assert_(entry.type==5 && entry.count==1); // yResolution (72)
  else if(entry.tag == 0x11C) assert_(entry.type==3 && entry.value==1); // PlanarConfiguration
  else if(entry.tag == 0x128) assert_(entry.type==3 && entry.value==2); // resolutionUnit (ppi)
  else if(entry.tag == 0x132) assert_(entry.type==2); // DateTime
  else if(entry.tag == 0x13B) assert_(entry.type==2); // Artist
  else if(entry.tag == 0x14A) { // SubIFDs
   assert_(entry.count == 1);
   readIFD(value);
  }
  else if(entry.tag == 0x201) {} // JPEGInterchangeFormat (deprecated)
  else if(entry.tag == 0x202) {} // JPEGInterchangeFormatLength (deprecated)
  else if(entry.tag == 0x2BC) {} // XML_Packet (XMP)
  else if(entry.tag == 0x8298) {} // Copyright
  else if(entry.tag == 0x8769) { // EXIF
   BinaryData& s = value; // Renames value -> s
   uint16 entryCount = s.read();
   for(uint unused i : range(entryCount)) {
    Entry entry = s.read<Entry>();
    BinaryData value (s.data); value.index = entry.value;
    if(entry.tag == 0x829A) { // ExposureTime
     assert_(entry.type == 5 && entry.count == 1);
     uint num = value.read32();
     uint den = value.read32();
     log("ExposureTime", num,"/",den, "s");
    }
    else if(entry.tag == 0x829D) { // fNumber
     assert_(entry.type == 5 && entry.count == 1);
     uint num = value.read32();
     uint den = value.read32();
     log("fNumber", num,"/",den);
    }
    else if(entry.tag == 0x8827) { // ISOSpeedRatings
     assert_(entry.type == 3 && entry.count == 1);
     log("ISO", entry.value);
    }
    else if(entry.tag == 0x8830) { // SensitivityType
     assert_(entry.type == 3 && entry.count == 1);
     log("Type", entry.value); // 2: Recommended Exposure Index
    }
    else if(entry.tag == 0x8832) { // RecommendedExposureIndex
     assert_(entry.type == 4 && entry.count == 1);
     log("RecommendedExposureIndex", entry.value);
    }
    else if(entry.tag == 0x9000) { // ExifVersion
     assert_(entry.type == 7 && entry.count == 4);
     log("ExifVersion");
    }
    else if(entry.tag == 0x9003) { // DateTimeOriginal
     assert_(entry.type == 2);
     log("DateTimeOriginal", value.peek(entry.count));
    }
    else if(entry.tag == 0x9004) { // DateTimeDigitized
     assert_(entry.type == 2);
     log("DateTimeDigitized", value.peek(entry.count));
    }
    else if(entry.tag == 0x9101) { // ExifVersion
     assert_(entry.type == 7 && entry.count == 4);
     log("ComponentsConfiguration");
    }
    else if(entry.tag == 0x9102) { // CompressedBitsPerPixel
     assert_(entry.type == 5 && entry.count == 1);
     log("CompressedBitsPerPixel");
    }
    else if(entry.tag == 0x9201) { // ShutterSpeedValue
     assert_(entry.type == 10 && entry.count == 1);
     int32 num = value.read32();
     int32 den = value.read32();
     log("ShutterSpeedValue", num,"/",den);
    }
    else if(entry.tag == 0x9202) { // ApertureValue
     assert_(entry.type == 5 && entry.count == 1);
     uint32 num = value.read32();
     uint32 den = value.read32();
     log("ApertureValue", num,"/",den);
    }
    else if(entry.tag == 0x9204) { // ExposureBiasValue
     assert_(entry.type == 10 && entry.count == 1);
     uint32 num = value.read32();
     uint32 den = value.read32();
     log("ExposureBiasValue", num,"/",den);
    }
    else if(entry.tag == 0x9205) { // MaxApertureValue
     assert_(entry.type == 5 && entry.count == 1);
     uint32 num = value.read32();
     uint32 den = value.read32();
     log("MaxApertureValue", num,"/",den);
    }
    else if(entry.tag == 0x9207) { // MeteringMode
     assert_(entry.type == 3 && entry.count == 1);
     log("MeteringMode", entry.value); // 5: Multi-segment
    }
    else if(entry.tag == 0x9209) { // Flash
     assert_(entry.type == 3 && entry.count == 1);
     log("Flash", entry.value); // 16: Off
    }
    else if(entry.tag == 0x920A) { // FocalLength
     assert_(entry.type == 5 && entry.count == 1);
     uint32 num = value.read32();
     uint32 den = value.read32();
     log("FocalLength", num,"/",den);
    }
    else if(entry.tag == 0x927C) { // MakerNote
     assert_(entry.type == 7);
     BinaryData& s = value; // Renames value -> s
     uint16 entryCount = s.read();
     log(entryCount);
     for(uint unused i : range(entryCount)) {
      Entry entry = s.read<Entry>();
      BinaryData value (s.data); value.index = entry.value;
      if(entry.tag == 0x0001) { // CameraSettings
       assert_(entry.type == 3 && entry.count == 50);
       log("CameraSettings");
      }
      else if(entry.tag <= 0x0004) {}
      else if(entry.tag == 0x0006) {
       assert_(entry.type==2);
       log("ImageType", value.peek(entry.count));
      }
      else if(entry.tag >= 0x0007 && entry.tag <= 0x00D0) {}
      else if(entry.tag == 0x00E0) {
       assert_(entry.type == 3 && entry.count == 17);
       struct SensorInfo {
        uint16 size;
        uint16 width, height;
        uint16 left, top, right, bottom;
        struct { uint16 left, top, right, bottom; } blackMask;
       } info = value.read<SensorInfo>();
       log("SensorInfo", info.width, info.height);
       //log(info.left, info.top, info.right, info.bottom);
       //log(info.blackMask.left, info.blackMask.top, info.blackMask.right, info.blackMask.bottom);
      }
      else if(entry.tag == 0x4001) { // ColorBalance
       assert_(entry.type == 7 && entry.count == 5120);
       value.advance(142);
       ref<uint16> RGGB = value.read<uint16>(4);
       whiteBalance.R = RGGB[0];
       whiteBalance.G = RGGB[1];
       assert_(whiteBalance.G == RGGB[2]);
       whiteBalance.B = RGGB[3];
       log("RGGB", RGGB);
      }
      else if(entry.tag == 0x4015) { // VignettingCorrection
       assert_(entry.type == 7 && entry.count == 1012);
       log("VignettingCorrection");
      }
      else log(entry.tag, hex(entry.tag), entry.type, entry.count, entry.value);
     }
    }
    else if(entry.tag == 0x9286) {} // UserComment
    else if(entry.tag == 0xA000) {} // FlashpixVersion
    else if(entry.tag == 0xA001) {} // ColorSpace
    else if(entry.tag == 0xA002) {} // PixelXDimension
    else if(entry.tag == 0xA003) {} // PixelYDimension
    else if(entry.tag == 0xA005) {} // InteropOffset
    else if(entry.tag == 0xA20E) {} // FocalPlaneXResolution (px/FocalPlaneResolutionUnit)
    else if(entry.tag == 0xA20F) {} // FocalPlaneYResolution (px/FocalPlaneResolutionUnit)
    else if(entry.tag == 0xA210) {} // FocalPlaneResolutionUnit (2: inches)
    else if(entry.tag == 0xA217) {} // SensingMethod
    else if(entry.tag == 0xA300) {} // FileSource
    else if(entry.tag == 0xA401) {} // CustomRendered
    else if(entry.tag == 0xA402) {} // ExposureMode
    else if(entry.tag == 0xA403) {} // WhiteBalance
    else if(entry.tag == 0xA404) {} // DigitalZoomRatio
    else if(entry.tag == 0xA406) {} // SceneCaptureType
    else if(entry.tag == 0xA430) {} // OwnerName
    else error(entry.tag, hex(entry.tag), entry.type, entry.count, entry.value);
   }
  }
  else if(entry.tag == 0x8825) {} // GPS
  else if(entry.tag == 0xC5D8 || entry.tag == 0xC5D9 || entry.tag == 0xC5E0 || entry.tag == 0xC6C5 || entry.tag == 0xC6DC) {} // ?
  else if(entry.tag == 0xC640) {
   assert_(entry.type == 3 && entry.count == 3);
   int A = value.read16();
   int B = value.read16();
   int C = value.read16();
   assert_(A==0 && B==0 && C==5632);
  }
  else if(entry.tag == 0xFFC3) error("Thumb");
  else error(entry.tag, hex(entry.tag));
 }
 if(data && !size) {
  BinaryData s (data, true);
  {uint16 marker = s.read16();
   assert_(marker == 0xFFD8, hex(marker)); // Start Of Image
  }
  {uint16 marker = s.read16();
   assert_(marker == 0xFFC4); // Define Huffman Table
   //uint start = s.index;
   unused uint16 length = s.read16();
   for(uint index: range(2)) {
    uint8 huffmanTableInfo = s.read8();
    unused uint tableIndex = huffmanTableInfo&0b1111;
    assert_(index==tableIndex);
    assert_((huffmanTableInfo&0b10000) == 0, huffmanTableInfo); // DC
    ref<uint8> symbolCountsForLength = s.read<uint8>(16);
    maxLength[index]=16; for(; maxLength[index] && !symbolCountsForLength[maxLength[index]-1]; maxLength[index]--);
    int totalSymbolCount = 0; for(int count: symbolCountsForLength) totalSymbolCount += count;
    lengthSymbolForCode[index] = buffer<LengthSymbol>(1<<maxLength[index]);
    int p = 0;
    ref<uint8> symbols = s.read<uint8>(totalSymbolCount);
    for(int h=0, length=1; length <= maxLength[index]; length++) {
     for(int i=0; i < symbolCountsForLength[length-1]; i++, p++) {
      for(int j=0; j < (1 << (maxLength[tableIndex]-length)); j++) {
       lengthSymbolForCode[tableIndex][h++] = {uint8(length), symbols[p]};
      }
     }
    }
   }
  }
  uint sampleSize;
  uint width, height;
  {
   uint16 marker = s.read16();
   assert_(marker == 0xFFC3, hex(marker)); // Start Of Frame (Lossless)
   unused uint16 length = s.read16();
   assert_(length == 14, length);
   sampleSize = s.read8();
   assert_(sampleSize == 12, sampleSize);
   height = s.read16();
   assert_(height == 3710, height);
   width = s.read16();
   assert_(width == 2816, width);
   uint8 componentCount = s.read8();
   assert_(componentCount == 2, componentCount);
   for(uint c: range(componentCount)) {
    uint8 index = s.read8();
    assert_(index == 1+c, index, c);
    uint8 HV = s.read8();
    assert_(HV==0b00010001, HV);
    uint8 quantizationTable = s.read8();
    assert_(quantizationTable == 0);
   }
  }
  {
   uint16 marker = s.read16();
   assert_(marker == 0xFFDA, hex(marker)); // Start Of Scan
   uint16 length = s.read16();
   assert_(length == 10, length);
   uint8 componentCount = s.read8();
   assert_(componentCount == 2, componentCount);
   for(uint c: range(componentCount)) {
    uint8 index = s.read8();
    assert_(index == c+1);
    uint8 DCACindex = s.read8();
    assert_(DCACindex == c<<4, DCACindex);
   }
   uint8 predictor = s.read8();
   assert_(predictor == 1, predictor);
   uint8 endOfSpectralSelection = s.read8();
   assert_(endOfSpectralSelection == 0);
   uint8 successiveApproximation = s.read8();
   assert_(successiveApproximation == 0);
  }
  pointer = (uint8*)s.data.begin()+s.index;
  assert_(sampleSize > 8 && sampleSize <= 16);
  assert_(!image);
  image = Image16(width*2, height);
  int predictor[2];// = {1<<(sampleSize-1), 1<<(sampleSize-1)};
  for(uint unused y: range(height)) {
   for(uint c: range(2)) predictor[c] = 1<<(sampleSize-1); // ?
   for(uint unused x: range(width)) {
    for(uint c: range(2)) {
     int length = readHuffman(c);
     //assert_(length < 16);
     int residual = readBits(length);
     if((residual & (1 << (length-1))) == 0) residual -= (1 << length) - 1;
     int value = predictor[c] + residual;
     image(x*2+c, y) = value; // FIXME: components
     predictor[c] = value;
    }
   }
  }
  s.index = (const byte*)pointer-s.data.begin();
  {
   uint16 marker = s.read16();
   assert_(marker == 0xFFD9, hex(marker)); // End Of Image
  }
 }
}

CR2::CR2(const ref<byte> file) {
 BinaryData s(file);
 s.skip("II\x2A\x00");
 for(;;) {
  s.index = s.read32();
  if(!s.index) break;
  readIFD(s);
 }
}
