#include "process.h"

SourceImage ProcessedSource::image(size_t imageIndex, int outputIndex, int2 size, bool noCacheWrite) {
	assert_(operation.outputs() == 1);
	assert_(outputIndex == 0);
	array<SourceImage> inputs;
	for(size_t inputIndex: range(operation.inputs())) inputs.append(source.image(imageIndex, inputIndex, size, noCacheWrite));
	ImageF output( size?:this->size(imageIndex) );
	operation.apply({share(output)},  apply(inputs, [](const SourceImage& x){ return share(x); }));
	return move(output);
}

SourceImageRGB ProcessedSource::image(size_t imageIndex, int2 size, bool noCacheWrite) {
	return ::cache<Image>(folder(), elementName(imageIndex), size?:this->size(imageIndex), time(imageIndex),
				 [&](const Image& target) {
		if(operation.outputs()==1) sRGB(target, image(imageIndex, 0, size, noCacheWrite));
		else if(operation.outputs()==2) {
			array<SourceImage> inputs;
			for(size_t inputIndex: range(operation.inputs())) inputs.append(source.image(imageIndex, inputIndex, size, noCacheWrite));
			array<ImageF> outputs;
			for(size_t unused index: range(operation.outputs())) outputs.append(ImageF(target.size));
			operation.apply(outputs, apply(inputs, [](const SourceImage& x){ return share(x); }));
			sRGB(target, outputs[0], outputs[1], outputs[2]);
		} else error(operation.outputs());
	}, noCacheWrite);
}

// ProcessedGroupImageSource

SourceImage ProcessedGroupImageSource::image(size_t groupIndex, int outputIndex, int2 unused size, bool unused noCacheWrite) {
	assert_(operation.outputs() == 1);
	array<size_t> group = groups(groupIndex);
	array<SourceImage> inputs;
	for(size_t imageIndex: group) inputs.append(source.image(imageIndex, outputIndex, size, noCacheWrite));
	ImageF output( size?:this->size(groupIndex) );
	operation.apply({share(output)}, apply(inputs, [](const SourceImage& x){ return share(x); }));
	return move(output);
}

SourceImageRGB ProcessedGroupImageSource::image(size_t groupIndex, int2 size, bool noCacheWrite) {
	assert_(source.outputs() == 3, name());
	assert_(operation.outputs() == 1);
	array<size_t> group = groups(groupIndex);
	array<ImageF> outputs;
	for(size_t inputIndex: range(3)) {
		array<SourceImage> inputs;
		for(size_t imageIndex: group) inputs.append(source.image(imageIndex, inputIndex, size, noCacheWrite));
		outputs.append(ImageF( size?:this->size(groupIndex )));
		operation.apply({share(outputs.last())}, apply(inputs, [](const SourceImage& x){ return share(x); }));
	}
	return sRGB(outputs[0], outputs[1], outputs[2]);
}
