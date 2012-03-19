#include "flac.h"
#include "file.h"
#include "sys/mman.h"
#include "process.h"

#include <sys/resource.h>
#include <sched.h>

struct : Application {
    void start(array<string>&& args) override {
        setPriority(-20);
        for(auto& path: args) {
            string base = section(path,'.',0,-2);
            FLAC flac = readFile(base+".flac"_);
#define VERIFY 0
#if VERIFY
            string raw = slice(readFile(section(base,'/',0,-3)+"/wav/"_+section(base,'/',-2,-1)+".wav"_),44);
            mlock(raw.data(),raw.size());
            byte* out = raw.data()-1;
#endif
            sched_yield();
            rice=0, predict=0, order=0;
            rusage usage; getrusage(RUSAGE_SELF,&usage);
#define getCPUTime() ((usage.ru_stime.tv_sec*1000+usage.ru_stime.tv_usec/1000) + (usage.ru_utime.tv_sec*1000+usage.ru_utime.tv_usec/1000))
            int ru_nivcsw=usage.ru_nivcsw;
            int time = getCPUTime();
            tsc total;
            for(uint i=0;i<flac.time;) {
                flac.readFrame();
#if VERIFY
                for(;flac.position<flac.blockSize;flac.position++) {
                    int2 s = flac.buffer[flac.position];
                    int x=(*(int*)(out)>>8); out+=3;
                    if(x!=s.x) error("i="_,i,"wav[i]="_,x,"flac[i]="_,s.x);
                    int y=(*(int*)(out)>>8); out+=3;
                    if(y!=s.y) error("i="_,i,"wav[i]="_,y,"flac[i]="_,s.y);
                }
#endif
                i += flac.blockSize;
            }
            int cycles=total;
            getrusage(RUSAGE_SELF,&usage);
            ru_nivcsw = usage.ru_nivcsw-ru_nivcsw;
            time = getCPUTime()-time;
            cycles = cycles/time;
#if VERIFY
            munmap((void*)(raw.data()-44),raw.size());
#endif
            munmap((void*)flac.array::data(),flac.size());
            log(section(base,'/',-2,-1)+"\t"_,str(flac.sampleRate)+"Hz"_,str(flac.channels)+"ch"_,str(flac.bitsPerSample)+"bit"_,str(flac.time/flac.sampleRate)+"s"_,
                str(100.f*flac.size()/(flac.time*6))+"%\t"_,(flac.size()/time)*1000/1024/1024,"MiB/s\t"_,str((flac.time/48)/time)+"x"_,
                "\trice"_,rice/float(2*flac.time),"\tpredict"_,predict/float(2*flac.time),"\tFIR"_,predict/float(order),"\torder"_,order/float(2*flac.time),
                "\t"_+str(cycles/1000)+"Hz"_,ru_nivcsw,time/ru_nivcsw,time);
        }
    }
} codec;

