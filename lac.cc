#include "process.h"
#include "file.h"
#include "stream.h"
#include "sys/mman.h"

/// Codec for unary/binary encoding
/// \note words are filled lsb first
struct BitWriter : array<byte> {
    //write buffer (with enough capacity)
    uint64* pos; //current position
    uint64 w=0; //current word
    const uint W=64; //word size
    uint64 bits=0; //used bits in current word
    BitWriter(int capacity) : array(capacity), pos((uint64*)data) {}

    /// Write \a size 0s followed by a one
    /// \note \a size is limited to 64
    inline void unary(uint size) {
        bits+=size; //write size 0s
        if(bits>=W) bits-=W, *pos++=w, w=0; //flush
        assert(bits<W,size);
        w |= 1ul<<bits; bits++; //write 1
    }

    /// Writes \a size bits in LSB lsb encoding
    inline void binary(uint size, uint64 v) {
        assert(size<=W);
        if(bits>=W) bits-=W, *pos++=w, w=v>>(size-bits); //flush (shift operand is %64)
        w |= v<<bits; bits += size; //write value
        if(bits>=W) bits-=W, *pos++=w, w=v>>(size-bits); //flush
        assert(bits<W);
    }

    /// Flush current word and return size in bytes
    int flush() { *pos=w; return ((byte*)pos-data)+(bits+7)/8; }
};

int encode(const array<byte>& in, BitWriter& out) { //550
    const uint N=8;
    int last[2]={0,0}, previous[2]={0,0}; uint mean[2]={1<<8,1<<8};
    for(const byte* raw=&in, *end=raw+in.size;raw<end;) {
        for(int c=0;c<2;c++) {
            int s = *(int*)(raw-1)>>8; raw+=3;
            int e = s - (2*last[c]-previous[c]);
            previous[c] = last[c];
            last[c] = s;
            uint u = (e<<1) ^ (e>>31);
            uint m = mean[c];
            mean[c] = ((N-1)*m+u)/N;
            uint k = m ? 32-__builtin_clz(m) : 1;
            if((u>>k)>=64) error("Unary>64"_,u,k,m,int(raw-&in));
            out.unary(u >> k);
            out.binary(k, u & ((1<<k)-1));
        }
    }
    return out.flush();
}

void decode(BitReader& in, array<byte>& out) {
    for(byte* raw=(byte*)&out, *end=raw+out.size;raw<end;) {
        for(int c=0;c<2;c++) {
            *(int*)(raw) = s; raw+=3;
        }
    }
}

#if 0
struct Test : Application {
    void start(array<string>&& args) override {
        bool convert = args[0]=="convert"_, verify = args[0]=="verify"_;
        for(auto& path: slice(args,1)) {
            string target = section(path,'.',0,-2)+".lac"_;
            if(!verify && exists(target)) continue;

            array<byte> wav = slice(mapFile(path),44);
            if(convert) {
                int time=getCPUTime();
                BitWriter lac(wav.size/2);
                lac.binary(32, wav.size);
                lac.size = encode(wav, lac);
                int fd = createFile(target);
                write(fd,lac);
                close(fd);
                time=getCPUTime()-time;
                log("\tEncode Speed:"_,(lac.size/time)/1024,"MB/s\t"_,str((lac.size/48)/time)+"x"_);
            }
            if(verify) {
                BitReader lac = mapFile(target);
                array<byte> raw(lac.binary(32));
                int time=getCPUTime();
                decode(lac, raw);
                time=getCPUTime()-time;
                munmap((void*)lac.data,lac.size);
                if(raw!=wav) log("File Corrupted");
                log(section(section(path,'/',-2,-1),'.',0,-2), "\tRatio:"_,str(100.f*lac.size/wav.size)+"%"_,"\tFLAC:"_, str((194500.0f*lac.size/wav.size)/738)+"%"_,
                    "\tSalamander (MB):"_, 1945.0f*lac.size/wav.size,
                    "\tDecode Speed:"_,(lac.size/time)/1024,"MB/s\t"_,str((lac.size/48)/time)+"x"_);
            }
            munmap((void*)(wav.data-44),wav.size);
        }
    }
} test;
#endif
