#include "cr2.h"
#include "bit.h"
#include "sort.h"

typedef float m128 __attribute__((__vector_size__(16)));
typedef int __attribute((__vector_size__(16))) v4si;
typedef char v16qi __attribute__((__vector_size__(16)));
inline v4si set1(int i) { return (v4si){i,i,i,i}; }
#include <smmintrin.h>

uint CR2::readBits(const int nbits) {
 if(nbits==0) return 0u;
 while(vbits < nbits) {
  uint byte = *pointer; pointer++;
  if(byte == 0xFF) { unused uint8 v = *pointer; pointer++; assert(v == 0x00); }
  bitbuf <<= 8;
  bitbuf |= byte;
  vbits += 8;
 }
 uint value = (bitbuf << (32-vbits)) >> (32-nbits);
 vbits -= nbits;
 return value;
}

int CR2::readHuffman(uint i) {
 const int nbits = maxLength[i];
 while(vbits < nbits) {
  uint byte = *pointer; pointer++;
  if(byte == 0xFF) {
   uint8 v = *pointer; pointer++;
   if(v == 0xD9) return -1;
   assert(v == 0x00);
  }
  bitbuf <<= 8;
  bitbuf |= byte;
  vbits += 8;
 }
 uint code = (bitbuf << (32-vbits)) >> (32-nbits);
 uint8 length = lengthSymbolForCode[i][code].length;
 uint8 symbol = lengthSymbolForCode[i][code].symbol;
 vbits -= length;
 return symbol;
}

void CR2::readIFD(BinaryData& s) {
 size_t ifdStart = s.index;
 int compression = 0;
 size = {}; data = {};
 uint16 entryCount = s.read();
 size_t lastReference = ifdStart;
 for(uint unused i : range(entryCount)) {
  const Entry& entry = s.read<Entry>(1)[0];
  BinaryData value (s.data); value.index = entry.value;
  if((entry.type==2||entry.type==5||entry.type==10||entry.count>1)&&value.index) {
   size_t size = 0;
   if(entry.type==1||entry.type==2) size=entry.count;
   else if(entry.type==3) size=entry.count*2;
   else if(entry.type==4) size=entry.count*4;
   else if(entry.type==5||entry.type==10) size=8;
   assert_(size, entry.type, entry.count);
   lastReference = ::max(lastReference, value.index+size);
  }
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
    entriesToFix.append((Entry*)&entry);
   }
  }
  else if(entry.tag == 0x103) { // Compression
   assert_(entry.value == 1 || entry.value == 6 /*JPEG*/ || entry.value == 0x879C /*rANS4*/);
   compression = entry.value;
   entriesToFix.append((Entry*)&entry);
  }
  else if(entry.tag == 0x10E) {} // ImageDescription
  else if(entry.tag == 0x10F) {} // Manufacturer
  else if(entry.tag == 0x110) {} // Model
  else if(entry.tag == 0x106) assert_(entry.value == 2); // PhotometricInterpretation
  else if(entry.tag == 0x111) { // StripOffset
   assert_(entry.count == 1);
   size_t offset = value.index;
   stride = size.x;
   assert_(compression==1 || compression==6 || compression==0x879C);
   assert_(!data.size);
   data.data = s.data.data+offset;
   entriesToFix.append((Entry*)&entry);
  }
  else if(entry.tag == 0x112) assert_(entry.value==1 || entry.value == 6, "Orientation", entry.value); // Orientation
  else if(entry.tag == 0x115) assert_(entry.value == 3, entry.value); // PhotometricInterpretation
  else if(entry.tag == 0x116) {} // RowPerStrip
  else if(entry.tag == 0x117) { // StripByteCount
   assert_(entry.value == 1 || entry.value == (uint)size.y || entry.value==(uint)size.x*size.y*3*2 || compression>1, entry.value, size); // 1 row per trip or single strip
   if(!size) data.size = entry.value;
   // else ignore first IFD (EXIF), only set data reference to RAW
   assert_(data.end() <= s.data.end());
   if(!size) entriesToFix.append((Entry*)&entry); // Only sets size of RAW IFD (FIXME: should zero size of removed JPEG)
  }
  else if(entry.tag == 0x11A) assert_(entry.type==5 && entry.count==1); // xResolution (72)
  else if(entry.tag == 0x11B) assert_(entry.type==5 && entry.count==1); // yResolution (72)
  else if(entry.tag == 0x11C) assert_(entry.type==3 && entry.value==1); // PlanarConfiguration
  else if(entry.tag == 0x128) assert_(entry.type==3 && entry.value==2); // resolutionUnit (ppi)
  else if(entry.tag == 0x132) assert_(entry.type==2); // DateTime
  else if(entry.tag == 0x13B) assert_(entry.type==2); // Artist
  else if(entry.tag == 0x201) { // JPEGInterchangeFormat (deprecated)
   assert_(!data);
   data = s.slice(entry.value, 0);
   assert_(!compression);
   compression = 6;
   //entriesToFix.append((Entry*)&entry); // Removed anyway
  }
  else if(entry.tag == 0x202) { // JPEGInterchangeFormatLength (deprecated)
   assert_(!data.size, data.size);
   data.size = entry.value;
   //entriesToFix.append((Entry*)&entry); // Removed anyway
  }
  else if(entry.tag == 0x2BC) { // XML_Packet (XMP)
   entriesToFix.append((Entry*)&entry); // Removed anyway
  }
  else if(entry.tag == 0x8298) {} // Copyright
  else if(entry.tag == 0x8769) { // EXIF
   BinaryData& s = value; // Renames value -> s
   unused size_t exifStart = s.index;
   size_t referencesSize = 0;
   size_t lastEXIFReference = 0;
   uint16 entryCount = s.read();
   for(uint unused i : range(entryCount)) {
    Entry entry = s.read<Entry>();
    BinaryData value (s.data); value.index = entry.value;
    if((entry.type==2||entry.type==5||entry.type==10||(entry.count>1&&entry.type!=7))&&value.index) {
     lastEXIFReference = ::max(lastEXIFReference, value.index);
    }
    if(entry.tag == 0x829A) { // ExposureTime
     assert_(entry.type == 5 && entry.count == 1);
     unused uint num = value.read32();
     unused uint den = value.read32();
     //log("ExposureTime", num,"/",den, "s");
     referencesSize += 8;
    }
    else if(entry.tag == 0x829D) { // fNumber
     assert_(entry.type == 5 && entry.count == 1);
     unused uint num = value.read32();
     unused uint den = value.read32();
     //log("fNumber", num,"/",den);
     referencesSize += 8;
    }
    else if(entry.tag == 0x8827) { // ISOSpeedRatings
     assert_(entry.type == 3 && entry.count == 1);
     //log("ISO", entry.value);
    }
    else if(entry.tag == 0x8830) { // SensitivityType
     assert_(entry.type == 3 && entry.count == 1);
     //log("Type", entry.value); // 2: Recommended Exposure Index
    }
    else if(entry.tag == 0x8832) { // RecommendedExposureIndex
     assert_(entry.type == 4 && entry.count == 1);
     //log("RecommendedExposureIndex", entry.value);
    }
    else if(entry.tag == 0x9000) { assert_(entry.type == 7 && entry.count == 4); } // ExifVersion
    else if(entry.tag == 0x9003) { // DateTimeOriginal
     assert_(entry.type == 2);
     //log("DateTimeOriginal", value.peek(entry.count));
    }
    else if(entry.tag == 0x9004) { assert_(entry.type == 2); } // DateTimeDigitized
    else if(entry.tag == 0x9101) { assert_(entry.type == 7 && entry.count == 4); } // ComponentsConfiguration
    else if(entry.tag == 0x9102) { assert_(entry.type == 5 && entry.count == 1); } // CompressedBitsPerPixel
    else if(entry.tag == 0x9201) { // ShutterSpeedValue
     assert_(entry.type == 10 && entry.count == 1);
     unused int32 num = value.read32();
     unused int32 den = value.read32();
     //log("ShutterSpeedValue", num,"/",den);
     referencesSize += 8;
    }
    else if(entry.tag == 0x9202) { // ApertureValue
     assert_(entry.type == 5 && entry.count == 1);
     unused uint32 num = value.read32();
     unused uint32 den = value.read32();
     //log("ApertureValue", num,"/",den);
     referencesSize += 8;
    }
    else if(entry.tag == 0x9204) { // ExposureBiasValue
     assert_(entry.type == 10 && entry.count == 1);
     unused uint32 num = value.read32();
     unused uint32 den = value.read32();
     //log("ExposureBiasValue", num,"/",den);
     referencesSize += 8;
    }
    else if(entry.tag == 0x9205) { // MaxApertureValue
     assert_(entry.type == 5 && entry.count == 1);
     unused uint32 num = value.read32();
     unused uint32 den = value.read32();
     //log("MaxApertureValue", num,"/",den);
     referencesSize += 8;
    }
    else if(entry.tag == 0x9207) { assert_(entry.type == 3 && entry.count == 1); } // MeteringMode
    else if(entry.tag == 0x9209) { // Flash
     assert_(entry.type == 3 && entry.count == 1);
     //log("Flash", entry.value); // 16: Off
    }
    else if(entry.tag == 0x920A) { // FocalLength
     assert_(entry.type == 5 && entry.count == 1);
     unused uint32 num = value.read32();
     unused uint32 den = value.read32();
     //log("FocalLength", num,"/",den);
     referencesSize += 8;
    }
    else if(entry.tag == 0x927C) { // MakerNote
     assert_(entry.type == 7);
     BinaryData& s = value; // Renames value -> s
     unused size_t makerNoteStart = s.index;
     size_t lastMakerNoteReference = 0;
     uint16 entryCount = s.read();
     for(uint unused i : range(entryCount)) {
      Entry entry = s.read<Entry>();
      BinaryData value (s.data); value.index = entry.value;
      if(entry.type==2||entry.type==5||entry.type==10||entry.count>1) {
       //log("MakerNote@", hex(value.index), entry.count);
       lastMakerNoteReference = ::max(lastMakerNoteReference, value.index);
      }
      if(entry.tag == 0x0001) {  assert_(entry.type == 3 && entry.count == 50); } // CameraSettings
      else if(entry.tag <= 0x0004) {}
      else if(entry.tag == 0x0006) { assert_(entry.type==2); } // ImageType
      else if(entry.tag >= 0x0007 && entry.tag <= 0x00D0) {}
      else if(entry.tag == 0x00E0) {
       assert_(entry.type == 3 && entry.count == 17);
       struct SensorInfo {
        uint16 size;
        uint16 width, height;
        uint16 left, top, right, bottom;
        struct { uint16 left, top, right, bottom; } blackMask;
       } info = value.read<SensorInfo>();
       assert_(info.width == 5632 && info.height == 3710);
       //log(info.left, info.top, info.right, info.bottom);
       //log(info.blackMask.left, info.blackMask.top, info.blackMask.right, info.blackMask.bottom);
       referencesSize += sizeof(SensorInfo);
      }
      else if(entry.tag == 0x4001) { // ColorBalance
       assert_(entry.type == 7 && entry.count == 5120);
       value.advance(142);
       ref<uint16> RGGB = value.read<uint16>(4);
       whiteBalance.R = RGGB[0];
       whiteBalance.G = RGGB[1];
       assert_(whiteBalance.G == RGGB[2]);
       whiteBalance.B = RGGB[3];
       //log("RGGB", RGGB);
       referencesSize += entry.count;
      }
      else if(entry.tag == 0x4015) { assert_(entry.type == 7 && entry.count == 1012); } // VignettingCorrection
      else error(entry.tag, hex(entry.tag), entry.type, entry.count, entry.value);
     }
     //referencesSize += s.index - makerNoteStart;
     //sections.append({makerNoteStart, lastReference-makerNoteStart, "EXIF.MakerNote"__});
     lastEXIFReference = ::max(lastEXIFReference, lastMakerNoteReference);
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
   assert_(entry.type == 3);
   if(entry.count == 1) assert_(entry.value == 5632);
   else {
    assert_(entry.count==3);
    int A = value.read16();
    int B = value.read16();
    int C = value.read16();
    assert_(A==0 && B==0 && C==5632);
    //entriesToFix.append((Entry*)&entry);
    assert_(entry.value+6 < 0x3800);
   }
  }
  else if(entry.tag == 0xFFC3) error("Thumb");
  else error(entry.tag, hex(entry.tag));
 }
 if(size) data={}; // Thumbnail
 if(data) {
  BinaryData s (data, true);
  {uint16 marker = s.read16();
   assert_(marker == 0xFFD8, hex(marker)); // Start Of Image
  }
  {uint16 marker = s.read16();
   if(marker == 0xFFE2) return; // APP2 (ICC?) from JPEG thumbnail
   assert_(marker == 0xFFC4, hex(marker)); // Define Huffman Table
   unused uint16 length = s.read16();
   for(uint c: range(2)) {
    uint8 huffmanTableInfo = s.read8();
    unused uint tableIndex = huffmanTableInfo&0b1111;
    assert_(c==tableIndex);
    assert_((huffmanTableInfo&0b10000) == 0, huffmanTableInfo); // DC
    symbolCountsForLength[c] = s.read<uint8>(16);
    maxLength[c]=16; for(; maxLength[c] && !symbolCountsForLength[c][maxLength[c]-1]; maxLength[c]--);
    int totalSymbolCount = 0; for(int count: symbolCountsForLength[c]) totalSymbolCount += count;
    assert_(maxLength[c] <= 9);
    symbols[c] = s.read<uint8>(totalSymbolCount);
    for(int p=0, h=0, length=1; length <= maxLength[c]; length++) {
     for(int i=0; i < symbolCountsForLength[c][length-1]; i++, p++) {
      //log(str(h>>(maxLength[c]-length),uint(length),'0',2u), symbols[c][p]);
      for(int j=0; j < (1 << (maxLength[c]-length)); j++) {
       lengthSymbolForCode[c][h++] = {uint8(length), symbols[c][p]};
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
  assert_(sampleSize > 8 && sampleSize <= 16);
  assert_(!image);
  begin = (uint8*)s.data.begin()+s.index;
  if(onlyParse) return;
  image = Image16(width*2, height);
  if(compression == 6) { // JPEG
   pointer = begin;
   int16* target = image.begin();
   int predictor[2] = {0,0};
   for(uint unused y: range(height)) {
    for(uint c: range(2)) predictor[c] = 1<<(sampleSize-1);
    for(uint unused x: range(width)) {
     for(uint c: range(2)) {
      int length = readHuffman(c);
      if(length == -1) { assert_(y==height-1 && x==width-1 && c==1, y, x, c); target[0]=0; target++; pointer-=2; goto break2; }
      assert_(length < 16);
      uint signMagnitude = readBits(length);
      int sign = signMagnitude & (1<<(length-1));
      int residual = sign ? signMagnitude : signMagnitude-((1<<length)-1); // Remove offset
      int value = predictor[c] + residual;
      /*image(x*2+c, y)*/ *target = value;
      target++;
      predictor[c] = value;
     }
    }
   }
   break2:;
   assert_(target == image.end());
   end = pointer;
   s.index = (const byte*)pointer-s.data.begin();
   //if(s.data.end() == file.end()-2) s.data.size+=2; // StripByteCount size failed to include End Of Image marker
   unused uint16 marker = s.read16();
   assert(marker == 0xFFD9); // End Of Image
  } else { // rANS4
   assert_(compression == 0x879C);
   const uint16* ptr = (uint16*)begin;
   for(size_t i: range(4)) {
    int16 min = *ptr; ptr++;
    int16 max = *ptr; ptr++;
    ref<uint16> freqM (ptr, max+1-min);
    ptr += freqM.size;
    ::buffer<uint16> cumulativeM(1+freqM.size);
    cumulativeM[0] = 0;
    for(size_t i: range(freqM.size)) cumulativeM[i+1] = cumulativeM[i] + freqM[i];

    int16 reverse[M]; // 64K
    uint slots[M]; // 128K
    for(uint16 sym: range(freqM.size)) {
     for(uint16 i: range(freqM[sym])) {
      uint slot = cumulativeM[sym]+i;
      reverse[slot] = min+sym;
      slots[slot] = uint(freqM[sym]) | (uint(i) << 16); // uint16 freq, bias;
     }
    }

    v4si x = (v4si)_mm_loadu_si128((const __m128i*)ptr);
    ptr += 8; // !! not 16
    uint W = image.width/2;
    assert_(W%4 == 0);
    int16* plane = image.begin() + (i&2)*W + (i&1);
    for(uint y: range(image.height/2)) {
     const int16* const up = plane + (y-1)*2*W*2;
     int16* const row = plane + y*2*W*2;
     for(uint X=0; X<W; X+=4) {
      const v4si slot = x & set1(M-1);
      for(uint k: range(4)) { // FIXME: SIMD
       uint x = X+k;
       uint top = y>0 ? up[x*2] : 0;
       uint left = x>0 ? row[x*2-2] : 0;
       int predictor = (left+top)/2;
       int r = reverse[slot[k]];
       uint value = predictor + r;
       row[x*2] = value;
      }

      const v4si freq_bias {(int)slots[slot[0]], (int)slots[slot[1]], (int)slots[slot[2]], (int)slots[slot[3]]};
      const v4si xscaled = __builtin_ia32_psrldi128(x, scaleBits);
      v4si freq = freq_bias & set1(0xffff);
      v4si bias = __builtin_ia32_psrldi128(freq_bias, 16);
      x = xscaled * freq + bias;

      static v16qi const shuffles[16] = {
#define _ -1
       { _,_,_,_, _,_,_,_, _,_,_,_, _,_,_,_ }, // 0000
       { 0,1,_,_, _,_,_,_, _,_,_,_, _,_,_,_ }, // 0001
       { _,_,_,_, 0,1,_,_, _,_,_,_, _,_,_,_ }, // 0010
       { 0,1,_,_, 2,3,_,_, _,_,_,_, _,_,_,_ }, // 0011
       { _,_,_,_, _,_,_,_, 0,1,_,_, _,_,_,_ }, // 0100
       { 0,1,_,_, _,_,_,_, 2,3,_,_, _,_,_,_ }, // 0101
       { _,_,_,_, 0,1,_,_, 2,3,_,_, _,_,_,_ }, // 0110
       { 0,1,_,_, 2,3,_,_, 4,5,_,_, _,_,_,_ }, // 0111
       { _,_,_,_, _,_,_,_, _,_,_,_, 0,1,_,_ }, // 1000
       { 0,1,_,_, _,_,_,_, _,_,_,_, 2,3,_,_ }, // 1001
       { _,_,_,_, 0,1,_,_, _,_,_,_, 2,3,_,_ }, // 1010
       { 0,1,_,_, 2,3,_,_, _,_,_,_, 4,5,_,_ }, // 1011
       { _,_,_,_, _,_,_,_, 0,1,_,_, 2,3,_,_ }, // 1100
       { 0,1,_,_, _,_,_,_, 2,3,_,_, 4,5,_,_ }, // 1101
       { _,_,_,_, 0,1,_,_, 2,3,_,_, 4,5,_,_ }, // 1110
       { 0,1,_,_, 2,3,_,_, 4,5,_,_, 6,7,_,_ }, // 1111
#undef _
      };
      static uint8 const numBytes[16] = { 0,1,1,2, 1,2,2,3, 1,2,2,3, 2,3,3,4 };
      v4si x_biased = x ^ set1(0x80000000);
      v4si greater = set1(L - 0x80000000) > x_biased;
      uint mask = __builtin_ia32_movmskps((m128)greater);
      v4si xshifted = __builtin_ia32_pslldi128(x, 16);
      v4si newx = xshifted | (v4si)__builtin_ia32_pshufb128((v16qi)_mm_loadl_epi64((const __m128i*)ptr), shuffles[mask]);
      x = (v4si)__builtin_ia32_pblendvb128((v16qi)x, (v16qi)newx, (v16qi)greater);
      ptr += numBytes[mask];
     }
    }
   }
   end = (uint8*)ptr;
   s.index = ((byte*)ptr)-s.data.begin();
  }
  assert_(!s);
 }
}

CR2::CR2(const ref<byte> file, bool onlyParse) : onlyParse(onlyParse)  {
 BinaryData s(file);
 s.skip("II\x2A\x00");
 for(;;) {
  ifdOffset.append((uint*)cast<uint>(s.slice(s.index,4)).begin());
  s.index = s.read32();
  if(!s.index) break;
  readIFD(s);
 }
}
