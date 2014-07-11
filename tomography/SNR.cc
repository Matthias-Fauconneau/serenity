#include "thread.h"
#include "variant.h"
#include "synthetic.h"
#include "deflate.h"

inline float sum(const ref<float>& A) { double sum=0; for(float a: A) sum+=a; return sum; }
inline float sum(const VolumeF& volume) { return sum(volume.data); }
inline float mean(const ref<float>& A) { return sum(A) / A.size; }
inline float mean(const VolumeF& volume) { return mean(volume.data); }

struct SNR {
    SNR() {
        const int3 volumeSize = int3(256);
        PorousRock rock (volumeSize);
        const VolumeF referenceVolume = rock.volume();
        const float referenceMean = mean(referenceVolume);
        const float centerSSQ = sq(sub(referenceVolume,  int3(0,0,volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/2)));
        const float extremeSSQ = sq(sub(referenceVolume, int3(0,0,0), int3(volumeSize.xy(), volumeSize.z/4))) + sq(sub(referenceVolume, int3(0,0, 3*volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/4)));

        Folder results = "Results"_;
        string ext = ".nmse"_; //".snr"_;
        array<String> names = filter(results.list(), [&](const string name){ return !endsWith(name,".best"_) || (existsFile(section(name,'.',0,-2)+ext, results) && File(section(name,'.',0,-2)+ext, results).size()>0); }); for(uint index: range(names.size)) {
            string name = names[index];
            Map map (name, results);
            buffer<float> data (map);
            int3 size = volumeSize;
            if(data.size != (size_t)size.x*size.y*size.z) {
                data = cast<float>(inflate(map));
                if(data.size != (size_t)size.x*size.y*size.z) {
                    log("Expected", size.x*size.y*size.z, "voxels, got", data.size, "for", name);
                    continue;
                }
            }
            VolumeF volume (size, move(data), "x"_);
#if 1 // MSE of normalized volume
            const float scale = referenceMean / mean(volume);
            const ref<float>& a = referenceVolume.data;
            const ref<float>& b = volume.data;
            double SSE = 0;
            for(uint i: range(a.size)) SSE += sq(a[i] - scale*b[i]);
            double NMSE = SSE / (centerSSQ+extremeSSQ);
            String result = "{Normalized NMSE %:"_+str(100*NMSE)+"}"_;
            writeFile(section(name,'.',0,-2)+".nmse"_, result, results);
            log(index+1,"/", names.size, name, result);
#else // SNR
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
            log(index+1,"/", names.size, name, SNR);
#endif
        }
    }
} app;
