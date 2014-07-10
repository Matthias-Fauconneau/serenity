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
            VolumeF volume (size, buffer<float>(map), "x"_);
            float* volumeData = volume.data;
            const int XY = size.x*size.x;
            const int X = size.x;

            float SNRsum = 0; uint SNRcount=0;
            for(vec4 grain: rock.types[2].grains) {
                if(grain.w < rock.largestGrain.w/2) continue;
                float cx = grain.x, cy=grain.y, cz=grain.z, r=grain.w;
                float innerR2 = sq(r-1.f/2);
                int iz = cz-r-1./2, iy = cy-r-1./2, ix = cx-r-1./2; // ⌊Sphere bounding box⌋
                float fz = cz-iz, fy=cy-iy, fx=cx-ix; // Relative center coordinates
                uint grainSize = ceil(2*(r+1./2)); // Bounding box size
                float* volume0 = volumeData + iz*XY + iy*X + ix; // Pointer to bounding box samples
                float sum=0; float count=0;
                for(uint z=0; z<grainSize; z++) {
                    float* volumeZ = volume0 + z*XY;
                    float rz = float(z) - fz;
                    float RZ = rz*rz;
                    for(uint y=0; y<grainSize; y++) {
                        float* volumeZY = volumeZ + y*X;
                        float ry = float(y) - fy;
                        float RZY = RZ + ry*ry;
                        for(uint x=0; x<grainSize; x++) {
                            float rx = float(x) - fx;
                            float r2 = RZY + rx*rx;
                            if(r2 <= innerR2) sum += volumeZY[x], count++;
                        }
                    }
                }
                float mean = sum / count; float deviation = 0;
                for(uint z=0; z<grainSize; z++) {
                    float* volumeZ = volume0 + z*XY;
                    float rz = float(z) - fz;
                    float RZ = rz*rz;
                    for(uint y=0; y<grainSize; y++) {
                        float* volumeZY = volumeZ + y*X;
                        float ry = float(y) - fy;
                        float RZY = RZ + ry*ry;
                        for(uint x=0; x<grainSize; x++) {
                            float rx = float(x) - fx;
                            float r2 = RZY + rx*rx;
                            if(r2 <= innerR2) deviation += abs(volumeZY[x]-mean);
                        }
                    }
                }
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
