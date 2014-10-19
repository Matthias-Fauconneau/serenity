#include "processed-source.h"

SourceImage ProcessedSource::image(size_t index, int component, int2 size, bool noCacheWrite) const {
	assert_(operation.inputs() == 3);
	assert_(operation.outputs() == 1);
	assert_(component == 0);
	ImageF target( size?:this->size(index) );
	operation.apply1(target,
				source.image(index, 0, size, noCacheWrite),
				source.image(index, 1, size, noCacheWrite),
				source.image(index, 2, size, noCacheWrite));
	return move(target);
}

SourceImageRGB ProcessedSource::image(size_t index, int2 size, bool noCacheWrite) const {
	assert_(operation.inputs()==3);
	int outputs = operation.outputs();
	return cache<Image>(source.folder, operation.name(), name(index), size?:this->size(index), time(index),
				 [&](const Image& target) {
		if(outputs==1) sRGB(target, image(index, 0, size, noCacheWrite));
		else if(outputs==3) {
			auto images = operation.apply3(
						source.image(index, 0, size, noCacheWrite),
						source.image(index, 1, size, noCacheWrite),
						source.image(index, 2, size, noCacheWrite));
			sRGB(target, images[0], images[1], images[2]);
		} else error(outputs);
	}, noCacheWrite);
}

SourceImage ProcessedSequence::image(size_t index, int component, int2 size, bool noCacheWrite) const {
	assert_(operation.inputs()==2);
	assert_(operation.outputs()==1);
	assert_(component == 0);
	return cache<ImageF>(source.folder, operation.name(), name(index), size?:this->size(index), time(index),
						[&](const ImageF& target) {
		operation.apply1(target, source.image(index, 0, size, noCacheWrite), source.image(index+1, 0, size, noCacheWrite));
	});
}

SourceImageRGB ProcessedSequence::image(size_t index, int2 size, bool noCacheWrite) const {
	assert_(operation.inputs()==2);
	assert_(operation.outputs()==1);
	return cache<Image>(source.folder, operation.name(), name(index), size?:this->size(index), time(index),
						[&](const Image& target) {
		sRGB(target, image(index, 0, size, noCacheWrite));
	});
}
