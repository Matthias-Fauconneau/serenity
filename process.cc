#include "process.h"

SourceImage ProcessedSource::image(size_t imageIndex, int outputIndex, int2 size, bool noCacheWrite) {
	SourceImage target = ::cache<ImageF>(folder(), elementName(imageIndex), size?:this->size(imageIndex), time(imageIndex),
				 [&](const ImageF& target) {
		assert_(operation.outputs() == 1);
		assert_(outputIndex == 0);
		auto inputs = apply(operation.inputs(), [&](size_t inputIndex) { return source.image(imageIndex, inputIndex, size, noCacheWrite); });
		for(auto& input: inputs) assert_(isNumber(input[0]));
		operation.apply({share(target)}, share(inputs));
	}, noCacheWrite||true);
	assert_(isNumber(target[0]), target[0], target.size, name());
	return target;
}

SourceImageRGB ProcessedSource::image(size_t imageIndex, int2 size, bool noCacheWrite) {
	return ::cache<Image>(folder(), elementName(imageIndex), size?:this->size(imageIndex), time(imageIndex),
				 [&](const Image& target) {
		array<SourceImage> inputs;
		for(size_t inputIndex: range(operation.inputs())) inputs.append(source.image(imageIndex, inputIndex, size, noCacheWrite));
		array<ImageF> outputs;
		for(size_t unused index: range(operation.outputs())) outputs.append(ImageF(target.size));
		operation.apply(outputs, apply(inputs, [](const SourceImage& x){ return share(x); }));
		if(operation.outputs()==1) sRGB(target, outputs[0]);
		else if(operation.outputs()==3) sRGB(target, outputs[0], outputs[1], outputs[2]);
		else error(operation.outputs());
	}, noCacheWrite);
}

// ProcessedGroupImageSource

SourceImage ProcessedGroupImageSource::image(size_t groupIndex, int outputIndex, int2 size, bool noCacheWrite) {
	return  ::cache<ImageF>(folder(), elementName(groupIndex), size?:this->size(groupIndex), time(groupIndex),
						   [&](const ImageF& target) {
		assert_(operation.inputs() == 1);
		assert_(operation.outputs() == 1);
		operation.apply({share(target)}, share(source.images(groupIndex, outputIndex, size, noCacheWrite)));
	}, noCacheWrite);
}

SourceImageRGB ProcessedGroupImageSource::image(size_t groupIndex, int2 size, bool noCacheWrite) {
	return  ::cache<Image>(folder(), elementName(groupIndex), size?:this->size(groupIndex), time(groupIndex),
						   [&](const Image& target) {
		array<ImageF> outputs;
		if(operation.inputs()==1) {
			assert_(operation.outputs() == 1);
			for(size_t inputIndex: range(source.outputs())) {
				outputs.append(ImageF( size ? : this->size(groupIndex) ));
				operation.apply({share(outputs.last())}, share(source.images(groupIndex, inputIndex, size, noCacheWrite)));
			}
		} else error(operation.inputs(), source.outputs());
		/**/  if(source.outputs() == 1) sRGB(target, outputs[0]);
		else if(source.outputs() == 3) sRGB(target, outputs[0], outputs[1], outputs[2]);
		else error(source.outputs());
	}, noCacheWrite);
}
