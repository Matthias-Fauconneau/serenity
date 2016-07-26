#include "cr2.h"
#include "time.h"
inline double log2(double x) { return __builtin_log2(x); }

struct Raw {
 Raw() {
  array<String> files = Folder(".").list(Files|Sorted);
  size_t cr2Count = 0;
  for(string name: files) {
   if(!endsWith(toLower(name), ".cr2")) continue;
   cr2Count++;
  }
  size_t imageCount = 0;
  for(string name: files) {
   if(!endsWith(toLower(name), ".cr2")) continue;
   if(0) {
    String jpgName = section(name,'.')+".JPG";
    if(files.contains(jpgName)) {
     assert_(existsFile(section(name,'.')+".CR2"));
     log("Removing", jpgName);
     remove(jpgName);
     //return;
    }
    //assert_(!files.contains(section(name,'.')+".JPG"), name);
   }
   if(0) {
    Map file(name);
    CR2 cr2(file, true);
    const size_t source = cr2.data.begin()-file.begin();
    const size_t target = 0x3800;
    if(cr2.ifdOffset.size <= 3 && source == target) continue;
   }
   if(0) if(CR2(Map(name), false).earlyEOF) continue;
   imageCount++;
   //log(imageCount, "/", cr2Count);
  }
  Time jpegDecTime, ransEncTime, ransDecTime, jpegEncTime, totalTime {true};
  size_t totalSize = 0, stripSize = 0, ransSize = {};
  size_t index = 0;
  for(string name: files) {
   if(!endsWith(toLower(name), ".cr2")) continue;
   if(0) {
    Map file(name);
    CR2 cr2(file, true);
    const size_t source = cr2.data.begin()-file.begin();
    const size_t target = 0x3800;
    if(cr2.ifdOffset.size <= 3 && source == target) continue;
   }
   index++;
   log(index,"/",imageCount,name);
   if(0) {
    buffer<byte> file = readFile(name);
    {
     CR2 cr2(file, true);
     const size_t source = cr2.data.begin()-file.begin();
     const size_t target = 0x3800;
     assert_(cr2.ifdOffset.size == 5 && source > target);
     for(CR2::Entry* entry: cr2.entriesToFix) {
      if(entry->tag==0x102) { entry->count=1; entry->value=16; } // BitsPerSample
      if(entry->tag == 0x111) { entry->value = target; } // StripOffset
      if(entry->tag == 0x2BC) { entry->count = 0; entry->value = 0; } // XMP
     }
     *cr2.ifdOffset[1] = *cr2.ifdOffset[3]; // Skips JPEG and RGB thumbs
     for(size_t i: range(cr2.data.size)) file[target+i] = file[source+i];
     size_t originalSize = file.size;
     file.size -= source-target;
     log(str(originalSize/1024/1024., 1u), "MB", str((source-target)/1024/1024., 1u), "MB", str(100.*(source-target)/file.size, 1u)+"%",
         str(file.size/1024/1024., 1u), "MB");
     stripSize += file.size;
    }
    {CR2 cr2(file);} // Verify
    writeFile(name, file, currentWorkingDirectory(), true);
   }
   if(1) {
    //Map file(name);
    array<byte> file = readFile(name);
    // Needs to be done before references are taken
    //file.grow(file.size*2); // rANS4 to JPEG expands
    file.reserve(file.size+1); // JPEG to JPEG may also expands if original had premature EOF
    bool earlyEOF = false;
    buffer<byte> original = copyRef(file);
    totalSize += file.size;
    Image16 source;
    array<Code> originalCodes;
    size_t pass = -1;
    {
     // Decode JPEG-LS
     jpegDecTime.start();
     CR2 cr2(file);
     if(0) if(cr2.earlyEOF) { ransSize+=file.size; continue; }
     earlyEOF = cr2.earlyEOF;
     source = ::move(cr2.image);
     originalCodes = ::move(cr2.codes);
     jpegDecTime.stop();

     if(0) {
      // Encode as rANS4
      ransEncTime.start();
      ::buffer<uint16> buffer;
      buffer.data = (uint16*)cr2.begin;
      buffer.size = 0;
      buffer.capacity = (uint16*)file.end()-(uint16*)cr2.begin; // HACK: to use append (capacity flags heap allocation to be freed which is not the case here)
      // buffer.capacity needs to be cleared to zero before destruction as it is not a heap allocation
      assert_(source.width%2==0 && source.height%2==0);
      Image16 residual (source.size/2);
      for(uint i: range(4)) {
       uint W = source.width/2;
       assert_(W%4 == 0);
       const int16* plane = source.data + (i&2)*W + (i&1);
       for(uint y: range(source.height/2)) {
        const int16* const up = plane + (y-1)*2*W*2;
        const int16* const row = plane + y*2*W*2;
        int16* const target = residual.begin() + y*W;
        for(uint x: range(W)) {
         uint top = y>0 ? up[x*2] : 0;
         uint left = x>0 ? row[x*2-2] : 0;
         int predictor = (left+top)/2;
         uint value = row[x*2];
         int r = value - predictor;
         assert_(-0x2000 <= r && r <= 0x2000, r, hex(r));
         target[x] = r;
        }
       }

       int16 min = 0x7FFF, max = 0;
       for(int16 value: residual) { min=::min(min, value); max=::max(max, value); }
       assert_(max+1-min <= 0x2000, min, max, max+1-min);
       ::buffer<uint32> histogram(max+1-min);
       histogram.clear(0);
       uint32* base = histogram.begin()-min;
       for(int value: residual) base[value]++;
       ::buffer<uint> cumulative(1+histogram.size);
       cumulative[0] = 0;
       for(size_t i: range(histogram.size)) cumulative[i+1] = cumulative[i] + histogram[i];

       ::buffer<uint16> freqM (histogram.size);
       ::buffer<uint16> cumulativeM (cumulative.size);
       cumulativeM[0] = 0;
       for(size_t i: range(1, cumulative.size)) cumulativeM[i] = (uint64)cumulative[i]*M/cumulative.last();
       for(size_t i: range(1, cumulative.size)) {
        if(histogram[i-1] && cumulativeM[i] == cumulativeM[i-1]) {
         uint32 bestFreq = 0;
         size_t bestIndex = invalid;
         for(int j: range(1, cumulative.size)) {
          uint32 freq = cumulativeM[j] - cumulativeM[j-1];
          if(freq > 1 && freq > bestFreq) { bestFreq = freq; bestIndex = j; }
         }
         assert_(bestIndex != invalid && bestIndex != i);
         if(bestIndex < i) for(size_t j: range(bestIndex, i)) cumulativeM[j]--;
         if(bestIndex > i) for(size_t j: range(i, bestIndex)) cumulativeM[j]++;
        }
       }
       assert_(cumulativeM[0] == 0 && cumulativeM.last() == M, cumulativeM.last());
       for(size_t i: range(histogram.size)) {
        if(histogram[i] == 0) assert_(cumulativeM[i+1] == cumulativeM[i]);
        else assert_(cumulativeM[i+1] > cumulativeM[i]);
        freqM[i] = cumulativeM[i+1] - cumulativeM[i];
       }

       uint16* const end = buffer.begin()+buffer.capacity;
       assert_((end-(uint16*)file.end())/2 == 0, end);
       uint16* begin; {
        uint16* ptr = end;
        uint32 rans[4];
        for(uint32& r: rans) r = L;
        for(int i: reverse_range(residual.ref::size)) {
         uint s = residual[i]-min;
         uint32 x = rans[i%4];
         uint freq = freqM[s];
         if(x >= ((L>>scaleBits)<<16) * freq) {
          ptr -= 1;
          *ptr = (uint16)(x&0xffff);
          x >>= 16;
         }
         rans[i%4] = ((x / freq) << scaleBits) + (x % freq) + cumulativeM[s];
        }
        for(int i: reverse_range(4)) {
         uint32 x = rans[i%4];
         ptr -= 2;
         ptr[0] = (uint16)(x>>0);
         ptr[1] = (uint16)(x>>16);
        }
        assert_(ptr >= buffer.begin());
        begin = ptr;
       }
       buffer.append(cast<uint16>(ref<int16>{min, max}));
       buffer.append(freqM);
       assert_(begin > buffer.end(), buffer.end(), begin, earlyEOF);
       buffer.append(ref<uint16>(begin, end-begin));

       if(0) {
        const uint32 total = residual.ref::size;
        double entropy = 0;
        for(uint32 count: histogram) if(count) entropy += count * log2(double(total)/double(count));
        log(str(100.*((end-begin)/(entropy/16)-1), 1u)+"%", str(100.*histogram.size/(end-begin), 1u)+"%");
       }
      }
      buffer.capacity = 0; // reference buffer
      size_t jpegFileSize = file.size;
      file.size = ((byte*)cr2.begin-file.begin())+buffer.size*2;
      for(CR2::Entry* entry: cr2.entriesToFix) {
       if(entry->tag==0x103) { assert_(entry->value==6); entry->value=0x879C; } // Compression
       if(entry->tag==0x117) {
        assert_(entry->value==jpegFileSize-(cr2.data.begin()-file.begin()),
                jpegFileSize-(cr2.data.begin()-file.begin()), entry->value, cr2.data.size);
        entry->value = file.size - (cr2.data.begin()-file.begin());
       }
      }
      ransEncTime.stop();
     }
    }
    size_t ransFileSize = file.size;
    ransSize += ransFileSize;
    {
#if 0
     // Decode rANS4
     ransDecTime.start();
     CR2 cr2(file);
     const Image16& source = cr2.image;
     ransDecTime.stop();
#else
     CR2 cr2(file, false);
     assert_(cr2.end);
     // Replaces FF 00 with FF (for bitstream verification)
     ::buffer<uint8> bitstream (cr2.end-cr2.begin, 0);
     for(const uint8* ptr=cr2.begin; ptr < (uint8*)file.end()-2; ptr++) {
      bitstream.append(*ptr);
      if(*ptr==0xFF) { ptr++; assert_(*ptr == 0x00, *ptr); }
     }
#endif

     // Encode image back to JPEG-LS
     jpegEncTime.start();
     // JPEG-LS headers (e.g Huffman table) were preserved by rANS converter, only need to regenerate image encoding

     ::buffer<uint8> buffer (source.ref::size);
     uint8* pointer = buffer.begin(); // Not writing directly to cr2.data as we need to replace FF with FF 00 (JPEG sync)
     uint64 bitbuf = 0;
     uint bitLeftCount = sizeof(bitbuf)*8;

     struct LengthCode { uint8 length = -1; uint16 code = -1; };
     LengthCode lengthCodeForSymbol[2][16];
     for(uint c: range(2)) {
      assert_(cr2.maxLength[c] <= 16);
      for(int p=0, code=0, length=1; length <= cr2.maxLength[c]; length++) {
       for(int i=0; i < cr2.symbolCountsForLength[c][length-1]; i++, p++) {
        assert_(length < 0xFF && code < 0xFFFF);
        uint8 symbol = cr2.symbols[c][p];
        lengthCodeForSymbol[c][symbol] = {uint8(length), uint16(code>>(cr2.maxLength[c]-length))};
        code += (1 << (cr2.maxLength[c]-length));
       }
      }
     }
     const int16* s = source.begin();
     int predictor[2] = {0,0};
     array<Code> codes;
     size_t index = 0;
     for(uint unused y: range(source.height)) {
      const uint sampleSize = 12;
      for(uint c: range(2)) predictor[c] = 1<<(sampleSize-1);
      for(uint unused x: range(source.width/2)) {
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
          assert_((value ? ((sizeof(value)*8) - __builtin_clz(value)) : 0) <= size && size <= 9);
          codes.append(uint8(size), uint16(value), uint8(symbol));
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
         assert_((value ? ((sizeof(value)*8) - __builtin_clz(value)) : 0) <= size && size <= 12);
         codes.append(uint8(size), uint16(value), uint8(0));
         if(size < bitLeftCount) {
          bitbuf <<= size;
          bitbuf |= value;
         } else {
          bitbuf <<= bitLeftCount;
          bitbuf |= value >> (size - bitLeftCount); // Puts leftmost bits in remaining space
          bitLeftCount += sizeof(bitbuf)*8;
          *(uint64*)pointer = __builtin_bswap64(bitbuf); // MSB msb
          if(!(*(uint64*)pointer == *(const uint64*)(bitstream.begin()+(pointer-buffer.begin()))) && 1) {
           log(index=codes.size-1);
           log(codes[codes.size-1]);
           log(originalCodes[codes.size-1]);
           assert_(codes[codes.size-1] != originalCodes[codes.size-1]);
           log((pointer-buffer.begin()), cr2.end-cr2.begin, "\n",
                  str(__builtin_bswap64(*(uint64*)pointer),64u,'0',2u), "\n",
                  str(__builtin_bswap64(*(uint64*)(bitstream.begin()+(pointer-buffer.begin()))),64u,'0',2u));
           //return;
          }
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

     //assert_(codes == originalCodes);
     assert_(codes.size == originalCodes.size
             || (earlyEOF && codes.size == originalCodes.size+2)
             , codes.size, originalCodes.size);
     log(codes.size, originalCodes.size);
     //log(index);
     //log(codes[index]);
     //log(originalCodes[index]);
     //assert_(codes[index] != originalCodes[index]);
     for(size_t i: range(originalCodes.size)) {
      //if(!(codes[i].length == originalCodes[i].length && codes[i].value == originalCodes[i].value && codes[i].symbol == originalCodes[i].symbol)) {
      if(codes[i] != originalCodes[i]) {
       log(i, "\n", codes[i], "\n", originalCodes[i]);
       return;
      }
     }
     // Replaces FF with FF 00 (JPEG sync)
     uint8* ptr = (uint8*)cr2.begin;
     for(uint8 b: buffer) {
      *ptr++ = b;
      if(b==0xFF) *ptr++ = 0x00;
     }
     // Restores End of Image marker
     *ptr++ = 0xFF; *ptr++ = 0xD9;

     size_t ransFileSize = file.size;
     file.size = (byte*)ptr-file.begin();
     assert_(file.size <= file.capacity, file.size, file.capacity);
     for(CR2::Entry* entry: cr2.entriesToFix) {
      if(entry->tag==0x103) { /*assert_(entry->value==0x879C);*/ entry->value=6; } // Compression
      if(entry->tag==0x117) {
       assert_(entry->value==ransFileSize-(cr2.data.begin()-file.begin()));
       entry->value = file.size - (cr2.data.begin()-file.begin()); // May introduce change when original was wrong
       pass = ((byte*)&entry->value)-file.begin();
      }
     }
    }

    Image16 target;
    {
     // Redecode JPEG-LS
     jpegDecTime.start();
     CR2 cr2(file);
     target = ::move(cr2.image);
     jpegDecTime.stop();
    }

    // Image verification
    assert_(source == target);

    // Bitstream verification
    /*if(0) for(size_t i: range(original.size)) {
     if(file[i] != original[i]) wrong++;
     if(wrong) log(i, "\n",
                   str((uint8)file[i-4],8u,'0',2u), str((uint8)original[i-4],8u,'0',2u), "\n",
                   str((uint8)file[i-3],8u,'0',2u), str((uint8)original[i-3],8u,'0',2u), "\n",
                   str((uint8)file[i-2],8u,'0',2u), str((uint8)original[i-2],8u,'0',2u), "\n",
                   str((uint8)file[i-1],8u,'0',2u), str((uint8)original[i-1],8u,'0',2u), "\n",
                   str((uint8)file[i+0],8u,'0',2u), str((uint8)original[i+0],8u,'0',2u), "\n",
                   str((uint8)file[i+1],8u,'0',2u), str((uint8)original[i+1],8u,'0',2u), "\n",
                   str((uint8)file[i+2],8u,'0',2u), str((uint8)original[i+2],8u,'0',2u));
     //assert_(wrong < 1);
     if(!(wrong < 1)) return;
    }*/

    // File verification
    //assert_(file == original);
    //if(file.size != original.size) log(file.size, original.size);
    // FIXME: "earlyEOF"
    assert_(file.size == original.size
            || (earlyEOF && file.size == original.size+2)
            || (earlyEOF && file.size == original.size+3)
            , earlyEOF, file.size, original.size);
    for(size_t i: range(original.size)) {
     if(file[i] != original[i] && !(earlyEOF && (i>=original.size-5 || (i==pass || i==pass+1 || i==pass+2 || i==pass+3)))) {
      log(i, "\n",
          str((uint8)file[i-4],8u,'0',2u), str((uint8)original[i-4],8u,'0',2u), "\n",
        str((uint8)file[i-3],8u,'0',2u), str((uint8)original[i-3],8u,'0',2u), "\n",
        str((uint8)file[i-2],8u,'0',2u), str((uint8)original[i-2],8u,'0',2u), "\n",
        str((uint8)file[i-1],8u,'0',2u), str((uint8)original[i-1],8u,'0',2u), "\n",
        str((uint8)file[i+0],8u,'0',2u), str((uint8)original[i+0],8u,'0',2u), "\n",
        str((uint8)file[i+1],8u,'0',2u), str((uint8)original[i+1],8u,'0',2u), "\n",
        str((uint8)file[i+2],8u,'0',2u), str((uint8)original[i+2],8u,'0',2u));
      return;
     }
    }

    log(str((file.size-              0)/1024/1024.,1u)+"MB", str(ransFileSize*2/1024/1024.,1u)+"MB",
        str((file.size-ransFileSize)/1024/1024.,1u)+"MB", str(100.*(file.size-ransFileSize)/file.size,1u)+"%");
   }
   log(jpegDecTime, ransEncTime, ransDecTime, jpegEncTime, totalTime); // 5min
   if(totalSize) log(str((totalSize-                0)/1024/1024.,1u)+"MB", str(ransSize/1024/1024.,1u)+"MB",
                                                  str((totalSize-ransSize)/1024/1024.,1u)+"MB", str(100.*(totalSize-ransSize)/totalSize, 1u)+"%");
   //break;
  }
 }
} app;
