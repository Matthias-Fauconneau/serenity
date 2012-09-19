#include "flac.h"
#include "debug.h"
#include "process.h"

typedef double double2 __attribute((vector_size(16)));
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
            uint unused maxBlockSize = binary(16); assert(minBlockSize<=maxBlockSize && maxBlockSize<=32768);
            int unused minFrameSize = binary(24), unused maxFrameSize = binary(24);
            rate = binary(20); assert(rate == 44100 || rate==48000,rate);
            uint unused channels = binary(3)+1; assert(channels==2);
            uint unused sampleSize = binary(5)+1; assert(sampleSize==16 || sampleSize==24);
            duration = (binary(36-24)<<24) | binary(24); //time = binary(36);
            skip(128); //MD5
        } else skip(size*8);
        if(last) break;
    };
    parseFrame();
}

enum { Independent=1, LeftSide=8, RightSide=9, MidSide=10 };
void FLAC::parseFrame() {
    int unused sync = binary(15); assert(sync==0b111111111111100,bin(sync));
    bool unused variable = bit();
    int blockSize_[16] = {0, 192, 576,1152,2304,4608, -8,-16, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
    int blockSize = blockSize_[binary(4)];
    int sampleRate_[16] = {0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000, -1, -2, -20 };
    uint unused rate = sampleRate_[binary(4)]; assert(rate==this->rate);
    channelMode = binary(4); assert(channelMode==Independent||channelMode==LeftSide||channelMode==MidSide||channelMode==RightSide,channelMode);
    int sampleSize_[8] = {0, 8, 12, 0, 16, 20, 24, 0};
    sampleSize = sampleSize_[binary(3)]; assert(sampleSize==16 || sampleSize==24);
    int unused zero = bit(); assert(zero==0);
    uint unused frameNumber = utf8();
    if(blockSize<0) blockSize = binary(-blockSize)+1;
    skip(8);
    this->blockSize = blockSize;
}

inline double round(double x) { //setRoundMode(Down) to round towards negative infinity
    const double lead = 0x1.0p52f+0x1.0p51f; //add leading bit 2**(52-16) to force rounding //+p51 to also force negative numbers
    return x+lead-lead;
}

template<int unroll> inline void filter(double2 kernel[unroll], double*& aligned, double*& misaligned, float*& signal) {
    double2 sum = {0,0};
    for(uint i=0;i<unroll;i++) sum += kernel[i] * *(double2*)(aligned+2*i); //unrolled loop in registers
    double sample = round(extract(sum,0)+extract(sum,1))+double(*signal); //add residue to prediction [SS2SD=2]
    aligned[2*unroll]= misaligned[2*unroll]= sample; aligned++; misaligned++; //write out contexts, misalign align, align misalign
    *signal = sample; signal++; //write out decoded sample SD2SS=8
}

template<int unroll> inline void convolve(double* predictor, double* even, double* odd, float* signal, float* end) {
    double2 kernel[unroll];
    for(uint i=0;i<unroll;i++) kernel[i] = *(double2*)&predictor[2*i]; //load predictor in registers
    for(;signal<end;) {
        filter<unroll>(kernel, even, odd, signal);
        filter<unroll>(kernel, odd, even, signal);
    }
}

template<int unroll,int channelMode> inline void interleave(const float* A, const float* B, float2* ptr, float2* end) {
    for(;ptr<end;) {
        __builtin_prefetch(A+32,0,0);
        __builtin_prefetch(B+32,0,0);
        __builtin_prefetch(ptr+32,0,0);
        for(uint i=0;i<unroll;i++) {
            float a=A[i], b=B[i];
            if(channelMode==Independent) ptr[i]=(float2)__(a,b);
            else if(channelMode==LeftSide) ptr[i]=(float2)__(a,a-b);
            else if(channelMode==MidSide) a-=b/2, ptr[i]=(float2)__(a+b,a);
            else if(channelMode==RightSide) ptr[i]=(float2)__(a+b, b);
        }
        A+=unroll; B+=unroll; ptr+=unroll;
    }
}
template<int unroll> inline void interleave(const int channelMode,const float* A, const float* B, float2* ptr, float2* end) {
    #define o(n) case n: interleave<unroll,n>(A,B,ptr,end); break;
    switch(channelMode) { o(Independent) o(LeftSide) o(MidSide) o(RightSide) }
    #undef o
}

uint64 rice=0, predict=0, order=0;
void FLAC::decodeFrame() {
    assert(blockSize && blockSize<buffer.capacity);
    int allocSize = align(4096,blockSize);
    float* block[2] = {allocate16<float>(allocSize),allocate16<float>(allocSize)};
    setRoundMode(Down);
    for(int channel=0;channel<2;channel++) {
        int rawSampleSize = sampleSize; //one bit more to be able to substract full range from other channel (1 sign bit + bits per sample)
        if(channel == 0) { if(channelMode==RightSide) rawSampleSize++; }
        if(channel == 1) { if(channelMode==LeftSide || channelMode == MidSide) rawSampleSize++; }

        //Subframe
        int unused zero = bit(); assert(zero==0,channel,blockSize,index,bsize);
        uint type = binary(6);
        int unused wasted = bit(); assert(wasted==0,type);
        if (type == 0) { //constant
            int constant = sbinary(rawSampleSize); //sbinary?
            for(int i = 0;i<blockSize;i++) block[channel][i] = constant;
            continue;
        } else if (type == 1) { //verbatim
            error("TODO");
            continue;
        }

        double predictor[32]; uint order;
        double* even = allocate16<double>(allocSize);
        double* odd = allocate16<double>(allocSize+1)+1;
        float* signal = block[channel];

        if (type >= 32) { //LPC
            order = (type&~0x20)+1; assert(order>0 && order<=32,order);
            for(uint i=0;i<order;i++) even[i]=odd[i]=signal[i]= sbinary(rawSampleSize);
            int precision = binary(4)+1; assert(precision<=15,precision);
            int shift = sbinary(5); assert(shift>=0);
            if(order%2) { predictor[0]=0; for(uint i=0;i<order;i++) predictor[order-i]= double(sbinary(precision))/(1<<shift); } //right align odd order
            else { for(uint i=0;i<order;i++) predictor[order-1-i]= double(sbinary(precision))/(1<<shift); }
        } else if(type>=8 && type <=12) { //Fixed
            order = type & ~0x8; assert(order<=4,order);
            for(uint i=0;i<order;i++) even[i]=odd[i]=signal[i]= sbinary(rawSampleSize);
            if(order==1) predictor[0]=0, predictor[1]=1;
            else if(order==2) predictor[0]=-1, predictor[1]=2;
            else if(order==3) predictor[0]=0, predictor[1]=1, predictor[2]=-3, predictor[3]=3;
            else if(order==4) predictor[0]=-1, predictor[1]=4, predictor[2]=-6, predictor[3]=4;
        } else error("Unknown type",type,channel,blockSize,index,bsize);
        float* end=signal; signal += order;

        //Residual
        int method = binary(2); assert(method<=1,method);
        int parameterSize = method == 0 ? 4 : 5;
        int escapeCode = (1<<parameterSize)-1;
        int partitionOrder = binary(4);
        uint size = blockSize >> partitionOrder;
        int partitionCount = 1<<partitionOrder;

        //yield(); tsc rice;
        for(int p=0;p<partitionCount;p++) {
            end += size;
            int k = binary( parameterSize );
            // 5 registers [signal, end, k, data, index]
            if(k==0) {
                for(;signal<end;) {
                    uint u = unary() << k;
                    int s = (u >> 1) ^ (-(u & 1));
                    *signal++ = s;
                }
            } else if(k < escapeCode) {
                for(;signal<end;) {
                    uint u = unary() << k;
                    u |= binary(k);
                    int s = (u >> 1) ^ (-(u & 1));
                    *signal++ = s;
                }
            } else {
                int n = binary(5); assert(n<=16,n);
                for(;signal<end;) {
                    uint u = binary(n);
                    int s = (u >> 1) ^ (-(u & 1));
                    *signal++ = s;
                }
            }
        }
        //::rice += rice; yield(); tsc predict;
        signal=block[channel]+order;
        if(order%2) { //for odd order: compute first sample with 'right' predictor without advancing context to begin unrolled loops with even context
            double sum=0;
            for(uint i=0;i<order;i++) sum += predictor[i+1] * even[i];
            double sample = round(sum)+*signal;
            even[order]=odd[order]= sample; //write out context
            *signal = sample; signal++; //write out decoded sample //TODO: float
        }
        #define o(n) case n: convolve<n>(predictor,even,odd,signal,end); break;
        switch((order+1)/2) {o(1)o(2)o(3)o(4)o(5)o(6)o(7)o(8)o(9)o(10)o(11)o(12)o(13)o(14)/*fit order<=28 in 14 double2 registers*/o(15)o(16)/*order>28 will spill*/}
        #undef o
        int t=rice*48000/2000000000; if(t>16) log("predict",t); //::predict += predict, ::order += order*blockSize;
        unallocate(even,allocSize);
        odd-=1; unallocate(odd,allocSize+1);
    }
    setRoundMode(Even);
    index=align(8,index);
    skip(16);
    assert(blockSize<=readIndex+buffer.capacity-writeIndex); buffer.size+=blockSize;
    uint beforeWrap=buffer.capacity-writeIndex;
    if(blockSize>beforeWrap) {
        interleave<4>(channelMode,block[0],block[1],buffer+writeIndex,buffer+buffer.capacity);
        interleave<4>(channelMode,block[0]+beforeWrap,block[1]+beforeWrap,buffer,buffer+blockSize-beforeWrap);
        writeIndex = blockSize-beforeWrap;
    } else {
        interleave<4>(channelMode,block[0],block[1],buffer+writeIndex,buffer+writeIndex+blockSize);
        writeIndex += blockSize;
    }
    unallocate(block[0],allocSize); unallocate(block[1],allocSize);
    if(index<bsize) parseFrame(); else blockSize=0;
    //log(::predict/::order); // GCC~4 / Clang~8 [in cycles/(sample*order) on Athlon64 3200]
}

int FLAC::read(float2 *out, uint size) {
    while(buffer.size<size){ if(blockSize==0) { size=buffer.size; break; } decodeFrame(); }
    uint beforeWrap = buffer.capacity-readIndex;
    if(size>beforeWrap) {
        copy(out,buffer+readIndex,beforeWrap);
        copy(out+beforeWrap,buffer+0,size-beforeWrap);
        readIndex=size-beforeWrap;
    } else {
        copy(out,buffer+readIndex,size);
        readIndex+=size;
    }
    buffer.size-=size; position+=size;
    return size;
}
