#include "thread.h"
#include "data.h"
#include "image.h"
#include "png.h"

/// 2D array of 16bit integer samples
typedef ImageT<uint16> Image16;

struct Raw {
 Raw() {
  for(string name: Folder(".").list(Files|Sorted))
   if(endsWith(toLower(name), ".cr2")) {
    log(name);
    Map file (name);
    BinaryData s(file);
    s.skip("II\x2A\x00");
    Image16 image;
    for(;;) {
     s.index = s.read32();
     if(!s.index) break;
     function<void(BinaryData& s)> readIFD = [&](BinaryData& s) -> void {
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
         if(compression==1) assert_(offset+size.y*size.x*2<= file.size);
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
       // {DNG 50xxx} 2C2: Version, Backward, Model; 2D1: ColorMatrix; 2E4: Private; 30A: CalibrationIlluminant, 33B: OriginalName
       // 3A3: Camera, Profile Calibration, Signature; 3A8: ProfileName; 3AC: Profile Tone Curve, Embed Policy, Copyright, 3C4: ForwardMatrix, 3D5: Look Table
       else if(entry.tag == 0x8298) {} // Copyright
       else if(entry.tag == 0x8769) {} // EXIF
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
       struct LengthSymbol { uint8 length; uint8 symbol; };
       int maxLength[2];
       buffer<LengthSymbol> lengthSymbolForCode[2];
       {uint16 marker = s.read16();
        assert_(marker == 0xFFC4); // Define Huffman Table
        //uint start = s.index;
        unused uint16 length = s.read16();
        for(uint index: range(2)) {
         uint8 huffmanTableInfo = s.read8();
         unused uint tableIndex = huffmanTableInfo&0b1111;
         //log(index);
         assert_(index==tableIndex);
         assert_((huffmanTableInfo&0b10000) == 0, huffmanTableInfo); // DC
         ref<uint8> symbolCountsForLength = s.read<uint8>(16);
         //log(symbolCountsForLength);
         maxLength[index]=16; for(; maxLength[index] && !symbolCountsForLength[maxLength[index]-1]; maxLength[index]--);
         int totalSymbolCount = 0; for(int count: symbolCountsForLength) totalSymbolCount += count;
         lengthSymbolForCode[index] = buffer<LengthSymbol>(1<<maxLength[index]);
         int p = 0;
         ref<uint8> symbols = s.read<uint8>(totalSymbolCount);
         for(int h=0, length=1; length <= maxLength[index]; length++) {
          for(int i=0; i < symbolCountsForLength[length-1]; i++, p++) {
           //log(symbols[p]);
           for(int j=0; j < (1 << (maxLength[tableIndex]-length)); j++) {
            lengthSymbolForCode[tableIndex][h++] = {uint8(length), symbols[p]};
           }
          }
         }
         if(0) for(uint code=1; code<lengthSymbolForCode[index].size;) {
          LengthSymbol lengthSymbol = lengthSymbolForCode[index][code];
          uint length = lengthSymbol.length, symbol = lengthSymbol.symbol;
          log(str(code>>(maxLength[index]-length), length, '0', 2u), symbol);
          code += (1 << (maxLength[index]-length));
         }
        }
        assert(s.index == start+length, start, length, s.index, start+length);
        //s.index = start+length;
       }
       uint sampleSize;
       uint width, height;
       {
        uint16 marker = s.read16();
        assert_(marker == 0xFFC3, hex(marker)); // Start Of Frame (Lossless)
        unused size_t start = s.index;
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
        assert(s.index == start+length, start, length, s.index, start+length);
        //s.index = start+length;
       }
       {
        uint16 marker = s.read16();
        assert_(marker == 0xFFDA, hex(marker)); // Start Of Scan
        unused size_t start = s.index;
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
        assert(s.index == start+length, start, length, s.index, start+length);
        //s.index = start+length;
       }
       //log(hex(cast<uint8>(s.peek(16))));
       uint bitbuf = 0; int vbits = 0;
       auto readBits = [&](const int nbits) -> uint {
        if(nbits==0) return 0u;
        while(vbits < nbits) {
         uint c = s.read<uint8>();
         if(c == 0xff) assert_(s.read<uint8>() == 0x00);
         bitbuf <<= 8;
         bitbuf |= c;
         vbits += 8;
        }
        uint value = (bitbuf << (32-vbits)) >> (32-nbits);
        vbits -= nbits;
        return value;
       };
       auto readHuffman = [&](uint i) {
        const int nbits = maxLength[i];
        while(vbits < nbits) {
         uint byte = s.read8();
         if(byte == 0xff) { uint8 v = s.read8(); assert_(v == 0x00, v); }
         bitbuf <<= 8;
         bitbuf |= byte;
         vbits += 8;
        }
        uint code = (bitbuf << (32-vbits)) >> (32-nbits);
        assert_(lengthSymbolForCode[i][code].length <= maxLength[i]);
        vbits -= lengthSymbolForCode[i][code].length;
        return lengthSymbolForCode[i][code].symbol;
       };
       assert_(sampleSize > 8 && sampleSize <= 16);
       assert_(!image);
       image = Image16(width*2, height);
       int predictor[2] = {1<<(sampleSize-1), 1<<(sampleSize-1)};
       for(uint unused y: range(height)) {
        for(uint c: range(2)) predictor[c] = 1<<(sampleSize-1); // ?
        for(uint unused x: range(width)) {
         for(uint c: range(2)) {
          int length = readHuffman(c);
          assert_(length < 16);
          int residual = readBits(length);
          if((residual & (1 << (length-1))) == 0) residual -= (1 << length) - 1;
          int value = predictor[c] + residual;
          image(x*2+c, y) = value; // FIXME: components
          predictor[c] = value;
         }
        }
       }
       {
        uint16 marker = s.read16();
        assert_(marker == 0xFFD9, hex(marker)); // End Of Image
       }
      }
     };
     readIFD(s);
    }
    assert_(image);
    Image sRGB (image.size);
    assert_(sRGB.stride == image.stride);
    int min = 0xFFFF, max = 0;
    for(size_t i: range(image.ref::size)) min=::min(min,int(image[i])), max=::max(max,int(image[i]));
    //for(size_t i: range(image.ref::size)) sRGB[i] = byte3(image[i] >> (12-8));
    for(size_t i: range(image.ref::size)) sRGB[i] = byte3((int(image[i])-min)*0xFF/(max-min));
    writeFile(name+".png"_,encodePNG(sRGB), currentWorkingDirectory(), true);
    break;
   }
 }
} app;
