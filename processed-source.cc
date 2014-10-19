#include "processed-source.h"

SourceImageRGB ProcessedSource::image(size_t index, int2 size, bool noCacheWrite) const {
	int type = operation.type();
    if(noCacheWrite) {
		/**/ if(type==1)
			return sRGB(operation.apply1(
							source.image(index, 0, size, noCacheWrite),
							source.image(index, 1, size, noCacheWrite),
							source.image(index, 2, size, noCacheWrite)));
		else if(type==3) {
			auto images = operation.apply3(
						source.image(index, 0, size, noCacheWrite),
						source.image(index, 1, size, noCacheWrite),
						source.image(index, 2, size, noCacheWrite));
			return sRGB(images[0], images[1], images[2]);
		} else error(type);
    }
    return cache<Image>(source.folder, operation.name(), name(index), size?:this->size(index), time(index),
                 [&](const Image& target) {
		if(type==1) sRGB(target, operation.apply1(source.image(index, 0, size), source.image(index, 1, size), source.image(index, 2, size)));
		else if(type==3) {
			auto images = operation.apply3(source.image(index, 0, size), source.image(index, 1, size), source.image(index, 2, size));
			sRGB(target, images[0], images[1], images[2]);
		} else error(type);
    });
}
