#include "thread.h"
#include "variant.h"
#include "synthetic.h"
#include "png.h"

struct SNR {
    SNR() {
        const int3 size = int3(256);
        PorousRock rock (size);
        Folder results = "Results"_;
        array<String> names = filter(results.list(), [&](const string name){ return !endsWith(name,".best"_) || existsFile(section(name,'.',0,-2)+".snr"_, results); });
        for(uint index: range(names.size)) {
            string name = names[index];
            Map map (name, results);
            assert_(map.size == size.x*size.y*size.z*sizeof(float), name, map.size, size.x*size.y*size.z*sizeof(float));
            VolumeF reconstruction (size, buffer<float>(map), "x"_);

            float SNRsum = 0; uint SNRcount=0;
            for(vec4 grain: rock.types[2].grains) {
                if(grain.w < rock.largestGrain.w/2) continue;
                vec3 center = grain.xyz(); float r = grain.w;
                int3 size = 2*ceil(r); int3 origin = int3(round(center))-size/2;
                float sum=0; float count=0;
                for(int z: range(size.z)) for(int y: range(size.y)) for(int x: range(size.x))  if(sq((vec3(origin)+vec3(x,y,z))-center)<=sq(r-1./2)) sum += reconstruction(origin+int3(x,y,z)), count++;
                float mean = sum / count; float deviation = 0;
                for(int z: range(size.z)) for(int y: range(size.y)) for(int x: range(size.x))  if(sq((vec3(origin)+vec3(x,y,z))-center)<=sq(r-1./2)) deviation += abs(reconstruction(origin+int3(x,y,z))-mean);
                deviation /= count;
                float SNR = mean/deviation;
                SNRsum += SNR; SNRcount++;
            }
            float SNR = SNRsum / SNRcount;
            writeFile(section(name,'.',0,-2)+".snr"_, "{SNR:"_+str(SNR)+"}"_, results);
            log(index,"/", names.size, name, SNR);
        }
    }
} app;
