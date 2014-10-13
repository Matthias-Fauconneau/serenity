#include "calibration.h"
#include "math.h"

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
                parallel::apply(target, [scale](float sum, byte4 source) {
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

        float factor = 1/parallel::max(target);
        parallel::apply(target, [=](float v) { return min(1.f, factor*v); }, target);
    });
}
