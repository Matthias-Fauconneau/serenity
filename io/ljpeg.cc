#include "ljpeg.h"
#include "data.h"

LJPEG::LJPEG(ref<byte> data) {
    BinaryData s (data, true);
    {const uint16 marker = s.read16();
        assert_(marker == 0xFFD8); // Start Of Image
    }
    while(s) {
        const uint16 marker = s.read16();
        const uint start = s.index;
        const uint16 length = s.read16(); // 14
        /**/ if(marker == 0xFFC3) { // Start of Frame Lossless
            sampleSize = s.read8();
            assert(sampleSize == 16);
            height = s.read16();
            width = s.read16();
            const uint8 componentCount = s.read8();
            assert_(componentCount == 2);
            for(const uint i: range(componentCount)) {
                const uint8 componentIndex = s.read8();
                assert_(componentIndex == i);
                const uint8 HV = s.read8();
                assert_(HV == 0x11);
                const uint8 quantizationTableSelector = s.read8();
                assert_(quantizationTableSelector == 0); // Not using quantization in lossless mode
            }
        }
        else if(marker == 0xFFC4) { // Define Huffman Table
            for(;s.index < start+length;) {
                const uint8 tableClass_Index = s.read8();
                assert_((tableClass_Index&0xF0) == 0, tableClass_Index);
                assert_((tableClass_Index&0x0F) < 2, tableClass_Index);
                Table& t = tables[tableClass_Index];
                mref<uint8>(t.symbolCountsForLength).copy(s.read<uint8>(16));
                t.maxLength=16; for(; t.maxLength && !t.symbolCountsForLength[t.maxLength-1]; t.maxLength--);
                int totalSymbolCount = 0; for(int count: t.symbolCountsForLength) totalSymbolCount += count;
                assert_(totalSymbolCount < 16, totalSymbolCount, t.symbolCountsForLength);
                mref<uint8>(t.symbols).slice(0, totalSymbolCount).copy(s.read<uint8>(totalSymbolCount));
                {
                    int h=0;
                    for(int length: range(1, t.maxLength+1))
                        for(auto_: range(t.symbolCountsForLength[length-1]))
                            for(auto_: range(1 << (t.maxLength-length)))
                                h++;
                    assert_(h<=512);
                }
                int p=0, h=0; for(int length: range(1, t.maxLength+1)) {
                    for(auto_: range(t.symbolCountsForLength[length-1])) {
                        for(auto_: range(1 << (t.maxLength-length))) {
                            t.lengthSymbolForCode[h++] = {uint8(length), t.symbols[p]};
                        }
                        p++;
                    }
                }
            }
        }
        else if(marker == 0xFFDA) { // Start Of Scan
            const uint8 componentCount = s.read8();
            for(const uint i: range(componentCount)) {
                const uint8 scanComponentSelector = s.read8();
                assert_(scanComponentSelector == i);
                const uint8 DCACindex = s.read8();
                assert_(DCACindex == scanComponentSelector<<4, scanComponentSelector, DCACindex);
            }
            const uint8 predictor = s.read8();
            assert_(predictor == 1, predictor);
            const uint8 endOfSpectralSelection = s.read8();
            assert_(endOfSpectralSelection == 0);
            const uint8 pointTransform = s.read8();
            assert_(pointTransform == 0);
            assert_(s.index == start+length, start, length, start+length, s.index, hex(marker));
            break;
        }
        //else if(marker == 0xFFE2) return; // APP2 (ICC?) from JPEG thumbnail (FIXME: skip)
        else error("JPEG", hex(marker));
        assert_(s.index == start+length, start, length, start+length, s.index, hex(marker));
    }
    headerSize = s.index;
}

void LJPEG::decode(const Image16& target, ref<byte> data) {
    const uint8* pointer = reinterpret_cast<const uint8*>(data.begin());
    uint bitbuf = 0;
    int bitLeftCount = 0;
    int predictor[2] = {0,0};
    for(const uint y: range(height)) {
        for(const uint c: range(2)) predictor[c] = y>0 ? target(c, y-1) : 1<<(sampleSize-1);
        for(const uint x: range(width)) {
            for(const uint c: range(2)) {
                int length; /*readHuffman*/ {
                    const Table& t = tables[c];
                    while(bitLeftCount < t.maxLength) {
                        uint8 byte = *pointer; pointer++;
                        if(byte == 0xFF) {
                            uint8 v = *pointer; pointer++;
                            if(v == 0xD9) /*End of Image*/ {
                                assert_(x==width-1 && y==height-1);
                                assert_((const ::byte*)pointer == data.end());
                                return;
                            }
                            assert_(v == 0x00);
                        }
                        bitbuf <<= 8;
                        bitbuf |= byte;
                        bitLeftCount += 8;
                    }
                    uint code = (bitbuf << (32-bitLeftCount)) >> (32-t.maxLength);
                    bitLeftCount -= t.lengthSymbolForCode[code].length;
                    length = t.lengthSymbolForCode[code].symbol;
                }
                int signMagnitude;
                if(length==0) signMagnitude = 0;
                else {
                    assert_(length<16);
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
                uint16 value = uint16(predictor[c] + residual);
                target(x*2+c, y) = value;
                //log(predictor[c], residual, value);
                //assert_(value <= 4118, predictor[c], residual, value);
                predictor[c] = value;
            }
        }
    }
    assert_(*pointer == 0xFF); pointer++;
    assert_(*pointer == 0xD9); pointer++;
    assert_((const byte*)pointer == data.end(), data.end()-(const byte*)pointer);
}

#if 0
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
                            *reinterpret_cast<uint64*>(pointer) = __builtin_bswap64(bitbuf); // MSB msb
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
                        *reinterpret_cast<uint64*>(pointer) = __builtin_bswap64(bitbuf); // MSB msb
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
#endif
