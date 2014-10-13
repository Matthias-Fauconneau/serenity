#include "calibration.h"
#include "math.h"

int2 argmin(const ImageF& source) {
    struct C { int2 position; float value=inf; };
    C minimums[threadCount];
    mref<C>(minimums).clear(); // Some threads may not iterate
    parallel_chunk(source.size.y, [&](uint id, uint64 start, uint64 chunkSize) {
        C min;
        for(size_t y: range(start, start+chunkSize)) {
            for(size_t x: range(source.size.x)) { float v = source(x,y); if(v < min.value) min = C{int2(x,y), v}; }
        }
        minimums[id] = min;
    });
    C min;
    for(C v: minimums) { if(v.value < min.value) min = v; }
    return min.position;
}

int64 Calibration::time() const {
    int64 sourceTime = max(::apply(source.count(), [&](size_t index) { return source.time(index); }));
    int64 version = parseDate(__DATE__ " " __TIME__)*1000000000l;
    return max(sourceTime, version);
}

// Sums all images
SourceImage Calibration::sum(int2 size) const {
    return cache<ImageF>(source.folder, "Calibration", "sum", size, time(), [&](const ImageF& target) {
            target.buffer::clear();
            float scale = 1./(3*source.count());
            for(size_t index: range(source.count())) {
                SourceImageRGB source = Calibration::source.image(index, size);
                parallel_apply(target, [scale](float sum, byte4 source) {
                    return sum + scale*(sRGB_reverse[source.b]+sRGB_reverse[source.g]+sRGB_reverse[source.r]);
                }, target, source);
                debug(break;)
            }
    });
}

int2 Calibration::spotPosition(int2 size) const {
    return cache<int2>(source.folder, "Calibration", "spotPosition", strx(size), time(), [&]() {
        int2 spotSize = Calibration::spotSize(size); // Reverse dependency but ensures spot is found inside enough
        return spotSize/2+argmin(crop(sum(size), spotSize/2, size-spotSize));
    });
}

/// Returns spot size
int2 Calibration::spotSize(int2 size) const { return int2(min(size.x, size.y)/8); }

SourceImage Calibration::attenuation(int2 size) const {
    return cache<ImageF>(source.folder, "Calibration", "attenuation", spotSize(size), time(), [&](const ImageF& target) {
        SourceImage source = Calibration::sum(size);

        int2 spotPosition = Calibration::spotPosition(size);
        int2 spotSize = Calibration::spotSize(size);
        assert_(spotPosition-spotSize/2 >= int2(0)  && spotPosition+spotSize/2 < source.size, spotPosition, spotSize, source.size);

        // Crop
        parallel_chunk(target.size.y, [&](uint, uint64 start, uint64 chunkSize) {
            int2 spotTopLeft = spotPosition-spotSize/2;
            size_t offset = spotTopLeft.y*source.stride + spotTopLeft.x;
            for(size_t y: range(start, start+chunkSize)) {
                for(size_t x: range(target.size.x)) target[y*target.stride + x] = source[offset + y*source.stride + x];
            }
        });

        // Normalizes sum by mean (DC) and clips values over 1
        float DC = mean(target);
        float factor = 1/DC;
        parallel_apply(target, [=](float v) { return min(1.f, factor*v); }, target);
    });
}

/*SourceImage Calibration::blendFactor(int2 size) const {
    return cache<ImageF>(source.folder, "Calibration", "blendFactor", spotSize(size), time(), [&](const ImageF& target) {
        SourceImage source = Calibration::attenuation(size);
        parallel_apply(target, [=](float value) { return 1-value; }, source);
    });
}*/

/*Region Calibration::regionOfInterest(int2 size) const {
    return cache<Region>(source.folder, "Calibration", "regionOfInterest", strx(size), time(), [&]() {
       SourceImage source = blendFactor(size);
    });
}*/
/*Region bound()
int2 minimums[threadCount], maximums[threadCount];
mref<int2>(minimums).clear(size); mref<int2>(maximums).clear(0); // Some threads may not iterate
parallel_chunk(size.y, [&](uint id, uint64 start, uint64 chunkSize) {
    int2 min = size, max = 0;
    for(size_t y: range(start, start+chunkSize)) {
        for(size_t x: range(size.x)) {
            if(source(x,y) > 1./2) {
                min = ::min(min, int2(x,y));
                max = ::max(max, int2(x,y));
            }
        }
    }
    minimums[id] = min, maximums[id] = max;
});
int2 min = ::min(minimums), max = ::max(maximums);
return Region{::min(min,max), max};*/
