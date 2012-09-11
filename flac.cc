#include "flac.h"
#include "process.h"
#include "debug.h"

typedef double double2 __attribute__ ((vector_size(16)));
#if __clang__
#define extract(vec, i) vec[i]
#else
#define extract(vec, i) __builtin_ia32_vec_ext_v2df(vec,i)
#endif
enum Round { Even, Down, Up, Zero };
void setRoundMode(Round round) { int r; asm volatile("stmxcsr %0":"=m"(*&r)); r &= ~(0b11<<13); r |= (round&0b11) << 13; asm volatile("ldmxcsr %0" : : "m" (*&r)); }
enum { Invalid=1<<0, Denormal=1<<1, DivisionByZero=1<<2, Overflow=1<<3, Underflow=1<<4, Precision=1<<5 };
void setExceptions(int except) { int r; asm volatile("stmxcsr %0":"=m"(*&r)); r|=0b111111<<7; r &= ~((except&0b111111)<<7); asm volatile("ldmxcsr %0" : : "m" (*&r)); }
#define swap64 __builtin_bswap64

void BitReader::setData(const ref<byte>& buffer) { data=buffer.data; bsize=8*buffer.size; index=0; }

void BitReader::skip(int count) { index+=count; }

uint BitReader::bit() { uint8 bit = uint8(data[index/8]<<(index&7))>>7; index++; return bit; }

uint BitReader::binary(int size) {
    assert(size<=32);//48
    assert(index<bsize);
    uint64 word = swap64(*(uint64*)(data+index/8)) << (index&7);
    index += size;
    return word>>int8(64-size);
}

int BitReader::sbinary(int size) {
    assert(size<=32);//48
    assert(index<bsize);
    int64 word = swap64(*(uint64*)(data+index/8)) << (index&7);
    index += size;
    return word>>int8(64-size);
}

static uint8 log2[256];
static_this { int i=1; for(int l=0;l<=7;l++) for(int r=0;r<1<<l;r++) log2[i++]=7-l; assert(i==256); }
uint BitReader::unary() {
    assert(index<bsize);
    // 64bit word optimization of "uint size=0; while(!bit()) size++; assert(size<(64-8)+64);"
    uint64 w = swap64(*(uint64*)(data+index/8)) << (index&7);
    uint size=0;
    if(!w) {
        size+=64-(index&7);
        w = swap64(*(uint64*)(data+index/8+8));
    }
    assert(w);
    uint8 b=w>>(64-8);
    while(!b) size+=8, w<<=8, b=w>>(64-8);
    size += log2[b];
    index += size+1;
    return size;
}

/// Reads a byte-aligned UTF-8 encoded value
uint BitReader::utf8() {
    assert(index%8==0);
    const byte* pointer = &data[index/8];
    byte code = pointer[0];
    /**/  if((code&0b10000000)==0b00000000) { index+=8; return code; }
    else if((code&0b11100000)==0b11000000) { index+=16; return(code&0b11111)<<6  |(pointer[1]&0b111111); }
    else if((code&0b11110000)==0b11100000) { index+=24; return(code&0b01111)<<12|(pointer[1]&0b111111)<<6  |(pointer[2]&0b111111); }
    else if((code&0b11111000)==0b11110000) { index+=32; return(code&0b00111)<<18|(pointer[1]&0b111111)<<12|(pointer[2]&0b111111)<<6|(pointer[3]&0b111111); }
    error("");
}

FLAC::FLAC(const ref<byte>& data) {
    BitReader::setData(data);
    assert(startsWith(data,"fLaC"_)); skip(32);
    for(;;) { //METADATA_BLOCK*
        bool last=bit();
        enum BlockType { STREAMINFO, PADDING, APPLICATION, SEEKTABLE, VORBIS_COMMENT, CUESHEET, PICTURE };
        uint blockType = binary(7); assert(blockType<=PICTURE, bin(blockType));
        int size = binary(24);
        if(blockType==STREAMINFO) {
            assert(size=0x22);
            uint unused minBlockSize = binary(16); assert(minBlockSize>=16);
            uint maxBlockSize = binary(16); assert(minBlockSize<=maxBlockSize && maxBlockSize<=32768);
            int unused minFrameSize = binary(24), unused maxFrameSize = binary(24);
            rate = binary(20); assert(rate == 44100 || rate==48000,rate);
            uint channels = binary(3)+1; assert(channels==2);
            uint unused sampleSize = binary(5)+1; assert(sampleSize==16 || sampleSize==24);
            duration = (binary(36-24)<<24) | binary(24); //time = binary(36);
            skip(128); //MD5

            buffer = Buffer(channels*maxBlockSize);
        } else skip(size*8);
        if(last) break;
    };
    readFrame();
}

//FIXME: clang doesn't seem to keep the predictor in registers
template<int unroll> inline void unroll_predictor(uint order, double* predictor, double* even, double* odd, int* signal, int* end, int shift) {
    assert(order<=2*unroll,order,unroll);
    //ensure enough warmup before using unrolled version
    for(uint i=order;i<2*unroll;i++) { //scalar compute first sample for odd orders
        double sum=0;
        for(uint i=0;i<order;i++) sum += predictor[i] * even[i];
        int sample = (int64(sum)>>shift) + *signal; //add residual to prediction
        even[i]=odd[i]= (double)sample; //write out context
        *signal = sample; signal++; //write out decoded sample
    }
    for(int i=order-1;i>=0;i--) predictor[2*unroll-order+i]=predictor[i]; //move predictors to right place for unrolled loop
    for(uint i=0;i<2*unroll-order;i++) predictor[i]=0; //clear extra predictors used because of unrolling
    double2 kernel[unroll];
    for(uint i=0;i<unroll;i++) kernel[i] = *(double2*)&predictor[2*i] /*/(1<<shift)*/; //load predictor in registers
    for(;signal<end;) {
#define INTEGER 1
#if INTEGER
#define FILTER(aligned, misaligned) /*TODO: check if an inline function would also keep predictor in registers*/ ({ \
    double2 sum = {0,0}; \
    for(uint i=0;i<unroll;i++) sum += kernel[i] * *(double2*)(aligned+2*i); /*unrolled loop in registers*/ \
    int sample = (int64(extract(sum,0)+extract(sum,1))>>shift); /*add residual to prediction SD2SI=10*/\
    const double c = 0x1.0p52f; \
    int unused sample2 = int32(((((extract(sum,0)+extract(sum,1))/(1<<shift))-c)+c));\
    assert((sample2-sample)==0, sample2-sample, sample2, sample, int32(2*((extract(sum,0)+extract(sum,1))/(1<<shift))));\
    sample += *signal;\
    aligned[2*unroll]= misaligned[2*unroll]= double(sample); aligned++; misaligned++; /*SI2SD=9, write out contexts, misalign align, align misalign*/\
    *signal = sample; signal++; /*write out decoded sample*/ })
#else
#define FILTER(aligned, misaligned) /*TODO: check if an inline function would also keep predictor in registers*/ ({ \
    double2 sum = {0,0}; \
    for(uint i=0;i<unroll;i++) sum += kernel[i] * *(double2*)(aligned+2*i); /*unrolled loop in registers*/ \
    /*const float c = 3*2^(24-1); FIXME: force integer truncation*/ \
    //double sample = (extract(sum,0)+extract(sum,1)) + double(*signal); /*add residual to prediction SI2SD=12*/
        float sample = float(*signal) + float(extract(sum,0)+extract(sum,1)); /*add residual to prediction SD2SS=8, SI2SS=14*/
        /*OPTI: convert signal to single right after decoding (SI2SD=12 | SI2SS=14 -> SS2SD=2)*/ \
        aligned[2*unroll]= misaligned[2*unroll]= sample; even++; odd++; /*write out contexts, misalign align, align misalign*/\
        *signal = int(sample); signal++; /*write out decoded sample SD2SI=10*//*16bit audio could keep float (full 24bit audio would need 25bit)*/\
    })
#endif
    FILTER(even, odd);
    FILTER(odd, even);
}
}

uint64 rice=0, predict=0, order=0;
void FLAC::readFrame() {
    int unused sync = binary(15); assert(sync==0b111111111111100,bin(sync));
    bool unused variable = bit();
    int blockSize_[16] = {0, 192, 576,1152,2304,4608, -8,-16, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
    int blockSize = blockSize_[binary(4)];
    int sampleRate_[16] = {0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000, -1, -2, -20 };
    uint unused rate = sampleRate_[binary(4)]; assert(rate==this->rate);
    enum { Independent=1, LeftSide=8, RightSide=9, MidSide=10 };
    int channels = binary(4); assert(channels==Independent||channels==LeftSide||channels==MidSide||channels==RightSide,channels);
    int sampleSize_[8] = {0, 8, 12, 0, 16, 20, 24, 0};
    uint sampleSize = sampleSize_[binary(3)];
    int unused zero = bit(); assert(zero==0);
    uint unused frame = utf8();
    if(blockSize<0) blockSize = binary(-blockSize)+1;
    skip(8);

    int block[2][blockSize];
    setRoundMode(Down); setExceptions(Invalid|Denormal|Overflow|Underflow|DivisionByZero);
    for(int channel=0;channel<2;channel++) {
        int* signal = block[channel];
        int* end = signal;
        double buffer1[blockSize]; double* even = buffer1;
        double buffer2[blockSize+1]; double* odd = buffer2+1;
        if(ptr(even)%16) odd=buffer1, even=buffer2+1;

        int rawSampleSize = sampleSize; //might need one bit more to be able to substract full range from other channel (1 sign bit + bits per sample)
        if(channel == 0) { if(channels==RightSide) rawSampleSize++; }
        if(channel == 1) { if(channels==LeftSide || channels == MidSide) rawSampleSize++; }

        //Subframe
        int unused zero = bit(); assert(zero==0);
        int type = binary(6);
        int unused wasted = bit(); assert(wasted==0);
        if (type == 0) { //constant
            int constant = sbinary(rawSampleSize); //sbinary?
            for(int i = 0;i<blockSize;i++) *signal++ = constant;
            continue;
        }
        if (type == 1) { //verbatim
            error("TODO");
            continue;
        }

        uint i=0;
        uint order;
        int shift;
        double predictor[32];

        if (type >= 32) { //LPC
            order = (type&~0x20)+1; assert(order>0 && order<=32,order);
            for(;i<order;i++) even[i]=odd[i]= *signal++ = sbinary(rawSampleSize);
            int precision = binary(4)+1; assert(precision<=15,precision);
            shift = sbinary(5); assert(shift>=0);
            for(uint i=0;i<order;i++) predictor[order-1-i]= double(sbinary(precision));
        } else if(type>=8 && type <=12) { //Fixed
            order = type & ~0x8; assert(order>=0 && order<=4,order);
            for(;i<order;i++) even[i]=odd[i]= *signal++ = sbinary(rawSampleSize);
            if(order==1) predictor[0]=1;
            else if(order==2) predictor[0]=-1, predictor[1]=2;
            else if(order==3) predictor[0]=1, predictor[1]=-3, predictor[2]=3;
            else if(order==4) predictor[0]=-1, predictor[1]=4, predictor[2]=-6, predictor[3]=4;
            shift=0;
        } else error("Unknown type");

        //Residual
        int method = binary(2); assert(method<=1,method);
        int parameterSize = method == 0 ? 4 : 5;
        int escapeCode = (1<<parameterSize)-1;
        int partitionOrder = binary(4);
        uint size = blockSize >> partitionOrder;
        int partitionCount = 1<<partitionOrder;

        tsc rice;
        for(int p=0;p<partitionCount;p++) {
            end += size;
            int k = binary( parameterSize );
            // 5 registers [signal, end, k, data, index]
            if(k==0) {
                for(;signal<end;) {
                    uint u = unary() << k;
                    *signal++ = (u >> 1) ^ (-(u & 1));
                }
            } else if(k < escapeCode) { //TODO: static dispatch on k?
                //disasm(
                for(;signal<end;) {
                    uint u = unary() << k;
                    u |= binary(k);
                    *signal++ = (u >> 1) ^ (-(u & 1));
                }
                //)
            } else {
                int n = binary(5); assert(n<=16,n);
                for(;signal<end;) {
                    int u = binary(n);
                    *signal++ = (u >> 1) ^ (-(u & 1));
                }
            }
        }
        ::rice += rice;

        tsc predict;
        signal=block[channel]+order;
#define UNROLL 1
#if UNROLL
#define o(n) case n: unroll_predictor<n>(order,predictor,even,odd,signal,end,shift); break;
        switch((order+1)/2) {
         o(1)o(2)o(3)o(4)o(5)o(6)o(7)o(8)o(9)o(10)o(11)o(12)o(13)o(14) //keep predictor in 14 SSE registers until order=28
         o(15)o(16) //order>28 will spill the predictor out of registers
        }
#else
        double* context = even;
        for(;signal<end;) {
            double sum=0;
            for(uint i=0;i<order;i++) sum += predictor[i] * context[i];
            int sample = int(sum) + *signal; //add residual to prediction
            context[order] = (double)sample; context++;  //write out context
            *signal = sample; signal++; //write out decoded sample
        }
#endif
        ::predict += predict;
        ::order += order*blockSize;
    }
    setRoundMode(Even);
    index=align(8,index);
    skip(16);
    for(int i=0;i<blockSize;i++) {
        int a=block[0][i], b=block[1][i];
        if(channels==Independent) buffer[i]=__(float(a),float(b));
        if(channels==LeftSide) buffer[i]=__(float(a),float(a-b));
        if(channels==MidSide) a-=b>>1, buffer[i]=__(float(a+b),float(a));
        if(channels==RightSide) buffer[i]=__(float(a+b), float(b));
    }
    this->blockSize=blockSize; blockIndex=buffer, blockEnd=buffer+blockSize;
    setExceptions(Overflow|DivisionByZero); //-Invalid,-Underflow,-Denormal
}
