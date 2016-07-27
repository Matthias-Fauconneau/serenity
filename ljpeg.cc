#include "ljpeg.h"
#include "data.h"

void LJPEG::parse(ref<byte> data) {
 BinaryData s (data, true);
 {uint16 marker = s.read16();
  assert_(marker == 0xFFD8); // Start Of Image
 }
 {uint16 marker = s.read16();
  if(marker == 0xFFE2) return; // APP2 (ICC?) from JPEG thumbnail
  assert_(marker == 0xFFC4); // Define Huffman Table
  unused uint16 length = s.read16();
  for(uint c: range(2)) {
   unused uint8 huffmanTableInfo = s.read8();
   mref<uint8>(symbolCountsForLength[c]).copy(s.read<uint8>(16));
   maxLength[c]=16; for(; maxLength[c] && !symbolCountsForLength[c][maxLength[c]-1]; maxLength[c]--);
   int totalSymbolCount = 0; for(int count: symbolCountsForLength[c]) totalSymbolCount += count;
   assert_(totalSymbolCount < 16);
   mref<uint8>(symbols[c]).slice(0, totalSymbolCount).copy(s.read<uint8>(totalSymbolCount));
   for(int p=0, h=0, length=1; length <= maxLength[c]; length++) {
    for(int i=0; i < symbolCountsForLength[c][length-1]; i++, p++) {
     for(int j=0; j < (1 << (maxLength[c]-length)); j++) {
      lengthSymbolForCode[c][h++] = {uint8(length), symbols[c][p]};
     }
    }
   }
  }
 }
 {
  unused uint16 marker = s.read16(); // 0xFFC3: Start Of Frame (LJPEG)
  unused uint16 length = s.read16(); // 14
  sampleSize = s.read8();
  height = s.read16();
  width = s.read16();
  uint8 componentCount = s.read8();
  for(unused uint c: range(componentCount)) {
   unused uint8 index = s.read8();
   unused uint8 HV = s.read8();
   unused uint8 quantizationTable = s.read8();
  }
 }
 {
  unused uint16 marker = s.read16(); // 0xFFDA: Start Of Scan
  unused uint16 length = s.read16();
  uint8 componentCount = s.read8();
  for(unused uint c: range(componentCount)) {
   unused uint8 index = s.read8();
   unused uint8 DCACindex = s.read8();
  }
  unused uint8 predictor = s.read8();
  unused uint8 endOfSpectralSelection = s.read8();
  unused uint8 successiveApproximation = s.read8();
  assert_(successiveApproximation == 0);
 }
 headerSize = s.index;
}

void LJPEG::decode(const mref<int16> image, ref<byte> data) {
 const uint8* pointer = (uint8*)data.begin();
 uint bitbuf = 0;
 int bitLeftCount = 0;
 int16* target = image.begin();
 int predictor[2] = {0,0};
 for(uint unused y: range(height)) {
  for(uint c: range(2)) predictor[c] = 1<<(sampleSize-1);
  for(uint unused x: range(width)) {
   for(uint c: range(2)) {
    int length; /*readHuffman*/ {
     const int maxLength = this->maxLength[c];
     while(bitLeftCount < maxLength) {
      uint byte = *pointer; pointer++;
      if(byte == 0xFF) {
       uint8 v = *pointer; pointer++;
       if(v == 0xD9) { target[0]=0; return; }
       assert(v == 0x00);
      }
      bitbuf <<= 8;
      bitbuf |= byte;
      bitLeftCount += 8;
     }
     uint code = (bitbuf << (32-bitLeftCount)) >> (32-maxLength);
     bitLeftCount -= lengthSymbolForCode[c][code].length;
     length = lengthSymbolForCode[c][code].symbol;
    }
    uint signMagnitude;
    if(length==0) signMagnitude = 0;
    else {
     while(bitLeftCount < length) {
      uint byte = *pointer; pointer++;
      if(byte == 0xFF) { unused uint8 v = *pointer; pointer++; assert(v == 0x00); }
      bitbuf <<= 8;
      bitbuf |= byte;
      bitLeftCount += 8;
     }
     signMagnitude = (bitbuf << (32-bitLeftCount)) >> (32-length);
     bitLeftCount -= length;
    }
    int sign = signMagnitude & (1<<(length-1));
    int residual = sign ? signMagnitude : signMagnitude-((1<<length)-1);
    int value = predictor[c] + residual;
    /*image(x*2+c, y)*/ *target = value;
    target++;
    predictor[c] = value;
   }
  }
 }
}

size_t encode(const LJPEG& ljpeg, const mref<byte> target, const ref<int16> source) {
 struct LengthCode { uint8 length = -1; uint16 code = -1; };
 LengthCode lengthCodeForSymbol[2][16];
 for(uint c: range(2)) {
  assert_(ljpeg.maxLength[c] <= 16);
  for(int p=0, code=0, length=1; length <= ljpeg.maxLength[c]; length++) {
   for(int i=0; i < ljpeg.symbolCountsForLength[c][length-1]; i++, p++) {
    assert_(length < 0xFF && code < 0xFFFF);
    uint8 symbol = ljpeg.symbols[c][p];
    lengthCodeForSymbol[c][symbol] = {uint8(length), uint16(code>>(ljpeg.maxLength[c]-length))};
    code += (1 << (ljpeg.maxLength[c]-length));
   }
  }
 }

 const int16* s = source.begin();
 ::buffer<uint8> buffer (source.size);
 uint8* pointer = buffer.begin(); // Not writing directly to target as we need to replace FF with FF 00 (JPEG sync)
 uint64 bitbuf = 0;
 uint bitLeftCount = sizeof(bitbuf)*8;
 int predictor[2] = {0,0};
 for(uint unused y: range(ljpeg.height)) {
  for(uint c: range(2)) predictor[c] = 1<<(ljpeg.sampleSize-1);
  for(uint unused x: range(ljpeg.width)) {
   for(uint c: range(2)) {
    uint value = *s; /*source(x*2+c, y)*/;
    s++;
    int residual = value - predictor[c];
    predictor[c] = value;
    uint signMagnitude, length;
    if(residual<0) {
     length = ((sizeof(residual)*8) - __builtin_clz(-residual));
     signMagnitude = residual+((1<<length)-1);
     if(signMagnitude&(1<<(length-1))) length++; // Ensures leading zero
    } else if(residual>0) {
     signMagnitude = residual;
     length = ((sizeof(signMagnitude)*8) - __builtin_clz(signMagnitude)); // Sign bit is also leading significant bit
    } else {
     signMagnitude = 0;
     length = 0;
    }
    {
     uint symbol = length;
     LengthCode lengthCode = lengthCodeForSymbol[c][symbol];
     {
      uint size = lengthCode.length;
      uint value = lengthCode.code;
      if(size < bitLeftCount) {
       bitbuf <<= size;
       bitbuf |= value;
      } else {
       bitbuf <<= bitLeftCount;
       bitbuf |= value >> (size - bitLeftCount); // Puts leftmost bits in remaining space
       bitLeftCount += sizeof(bitbuf)*8;
       *(uint64*)pointer = __builtin_bswap64(bitbuf); // MSB msb
       bitbuf = value; // Already stored leftmost bits will be pushed out eventually
       pointer += sizeof(bitbuf);
      }
      bitLeftCount -= size;
     }
    }
    if(length) {
     uint size = length;
     uint value = signMagnitude;
     if(size < bitLeftCount) {
      bitbuf <<= size;
      bitbuf |= value;
     } else {
      bitbuf <<= bitLeftCount;
      bitbuf |= value >> (size - bitLeftCount); // Puts leftmost bits in remaining space
      bitLeftCount += sizeof(bitbuf)*8;
      *(uint64*)pointer = __builtin_bswap64(bitbuf); // MSB msb
      bitbuf = value; // Already stored leftmost bits will be pushed out eventually
      pointer += sizeof(bitbuf);
     }
     bitLeftCount -= size;
    }
   }
  }
 }
 // Flush
 if(bitLeftCount<sizeof(bitbuf)*8) bitbuf <<= bitLeftCount;
 while(bitLeftCount<sizeof(bitbuf)*8) {
  assert_(pointer < buffer.end());
  *pointer++ = bitbuf>>(sizeof(bitbuf)*8-8);
  bitbuf <<= 8;
  bitLeftCount += 8;
 }
 for(bitLeftCount--;bitLeftCount>=64;bitLeftCount--) *(pointer-1) |= 1<<(bitLeftCount-64);
 assert_(s == source.end());
 buffer.size = pointer-buffer.begin();

 // Replaces FF with FF 00 (JPEG sync)
 byte* ptr = target.begin();
 for(uint8 b: buffer) {
  *ptr++ = b;
  if(b==0xFF) *ptr++ = 0x00;
 }
 // Restores End of Image marker
 *ptr++ = 0xFF; *ptr++ = 0xD9;
 assert_(ptr <= target.end());
 return ptr-target.begin();
}
