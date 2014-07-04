#include "thread.h"
#include "variant.h"
#include "synthetic.h"
#include "png.h"

struct Compare {
    map<string, Variant> parameters = parseParameters(arguments(),{"ui"_,"reference"_,"volumeSize"_,"projectionSize"_,"photonCounts"_,"projectionCounts"_,"method"_});
    Compare() {
        const int3 size = parameters.value("volumeSize"_, int3(256)); // Reconstruction sample count along each dimensions
        PorousRock rock (size);
        VolumeF reference = rock.volume();

        Folder results = "Results"_;
#if 0
        array<uint> bestSlices;
        Time time;
        for(string name: results.list() ) {
            if(!endsWith(name,".best"_)) continue;
            Map map (name, results);
            VolumeF reconstruction (size, buffer<float>(map), "x"_);
            uint bestZ = 0; float bestSSE = 0;
            for(uint z: range(size.z)) {
                float SSE=0;
                const float* x0 = slice(reference,z).data;
                const float* x1 = slice(reconstruction,z).data;
                for(uint i: range(size.y*size.x)) SSE += sq(x1[i] - x0[i]);
                if(SSE > bestSSE) { bestSSE = SSE; bestZ = z; }
            }
            bestSlices += bestZ;
        }
        log(time);
#else
        buffer<uint> bestSlices = apply(range(8),[&](uint z){return size.z*z/8; });
        log(bestSlices);
#endif
        Folder folder ("Difference"_, currentWorkingDirectory(), true);
        ImageF error ( reference.size.xy() );
        Image target ( reference.size.xy() );
        for(string name: results.list() ) {
            if(!endsWith(name,".best"_)) continue;
            Map map (name, results);
            VolumeF reconstruction (size, buffer<float>(map), "x"_);
            for(uint z: bestSlices) {
                const float* x0 = slice(reference,z).data;
                const float* x1 = slice(reconstruction,z).data;
                float* e = error.data;
                for(uint i: range(size.y*size.x)) e[i] = sq(x1[i] - x0[i]);
                convert(target, error); // Normalizes each slice by its maximum value
                writeFile(section(name,'.')+"."_+dec(z), encodePNG(target), folder);
            }
        }
    }
} app;
