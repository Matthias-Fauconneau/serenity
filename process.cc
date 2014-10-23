#include "process.h"

// UnaryImageSource

SourceImage UnaryImageSource::image(size_t imageIndex, size_t outputIndex, int2 size, bool noCacheWrite) {
	SourceImage target = ::cache<ImageF>(folder(), elementName(imageIndex), size?:this->size(imageIndex), time(imageIndex),
				 [&](const ImageF& target) {
		assert_(operation.outputs() == 1);
		assert_(outputIndex == 0);
		auto inputs = apply(operation.inputs(), [&](size_t inputIndex) { return source.image(imageIndex, inputIndex, target.size, noCacheWrite); });
		for(auto& input: inputs) assert_(isNumber(input[0]));
		operation.apply({share(target)}, share(inputs));
	}, noCacheWrite);
	assert_(isNumber(target[0]), target[0], target.size, name());
	return target;
}

// ProcessedRGBSource

SourceImageRGB sRGBImageSource::image(size_t imageIndex, int2 size, bool noCacheWrite) {
	array<SourceImage> inputs;
	for(size_t inputIndex: range(source.outputs())) inputs.append(source.image(imageIndex, inputIndex, size, noCacheWrite));
	return ::cache<Image>(folder(), elementName(imageIndex), inputs[0].size, UnaryGenericImageSource::time(imageIndex),
				 [&](Image& target) {
		if(target.size != inputs[0].size) {
			error("Resize");
			assert_(target.size > inputs[0].size);
			target.Image::size = inputs[0].ImageF::size;
		}
		log(toString());
		if(inputs.size==1) ::sRGB(target, inputs[0]);
		else if(inputs.size==3) ::sRGB(target, inputs[0], inputs[1], inputs[2]);
		else error(inputs.size);
	}, noCacheWrite);
}

// UnaryGroupImageSource

SourceImage UnaryGroupImageSource::image(size_t groupIndex, size_t outputIndex, int2 size, bool noCacheWrite) {
	assert_(operation.outputs()==1 || source.outputs()==1);
	array<SourceImage> inputs = source.images(groupIndex, operation.outputs()==1?outputIndex:0, size, noCacheWrite); // FIXME
	return ::cache<ImageF>(folder(), elementName(groupIndex)+'['+str(outputIndex)+']', inputs[0].size, time(groupIndex), [&](ImageF& target) {
		if(target.size != inputs[0].size) {
			error("Resize");
			assert_(target.size > inputs[0].size);
			target.size = inputs[0].size;
		}
		assert_(operation.inputs() == 0 || operation.inputs() == inputs.size);
		assert_(inputs[0].size == target.size);
		if(operation.outputs()==1) { // outputIndex selects source output index
			operation.apply({share(target)}, share(inputs));
		} else {
			array<SourceImage> outputs;
			for(size_t unused index: range(operation.outputs())) outputs.append( inputs[0].size );
			operation.apply(share(outputs), share(inputs));
			assert_(outputIndex < outputs.size);
			assert_(target.size == outputs[outputIndex].size && target.stride == outputs[outputIndex].stride);
			target.copy(outputs[outputIndex]);
		}
	}, noCacheWrite);
}

// ProcessedImageGroupSource

String GroupImageGroupSource::elementName(size_t groupIndex) const {
	return str(apply(groups(groupIndex), [this](const size_t imageIndex) { return source.elementName(imageIndex); }));
}

int2 GroupImageGroupSource::size(size_t groupIndex) const {
	auto sizes = apply(groups(groupIndex), [this](size_t imageIndex) { return source.size(imageIndex); });
	for(auto size: sizes) assert_(size == sizes[0], sizes);
	return sizes[0];
}

array<SourceImage> GroupImageGroupSource::images(size_t groupIndex, size_t outputIndex, int2 size, bool noCacheWrite) {
	if(!size) size=this->size(groupIndex);
	return apply(groups(groupIndex), [&](const size_t imageIndex) { return source.image(imageIndex, outputIndex, size, noCacheWrite); });
}

// UnaryImageGroupSource

array<SourceImage> UnaryImageGroupSource::images(size_t groupIndex, size_t outputIndex, int2 size, bool noCacheWrite) {
	assert_(outputIndex == 0, outputIndex, operation.name(), operation.inputs(), operation.outputs());
	if(!size) size=this->size(groupIndex);
	if(operation.outputs() == 0) { // Process all images at once
		assert_(operation.inputs()==0);
		auto inputs = source.images(groupIndex, outputIndex, size, noCacheWrite);
		array<SourceImage> outputs;
		for(size_t unused index: range(inputs.size)) outputs.append( inputs[0].size );
		operation.apply(share(outputs), share(inputs));
		return outputs;
	} else { // Process every image separately
		assert_(operation.inputs() >= 1, operation.name());
		array<array<SourceImage>> groupInputs;
		for(size_t inputIndex: range(operation.inputs())) groupInputs.append( source.images(groupIndex, inputIndex, size, noCacheWrite) );
		array<SourceImage> allOutputs;
		for(size_t imageIndex: range(groupInputs[0].size)) {
			array<ImageF> inputs;
			for(size_t inputIndex: range(groupInputs.size)) inputs.append( share(groupInputs[inputIndex][imageIndex]) );
			array<SourceImage> outputs;
			for(size_t unused index: range(operation.outputs())) outputs.append( inputs[0].size );
			operation.apply(share(outputs), inputs);
			allOutputs.append(outputs);
		}
		return allOutputs;
	}
	error(operation.outputs());
}

// sRGBGroupSource

array<SourceImageRGB> sRGBImageGroupSource::images(size_t groupIndex, int2 size, bool noCacheWrite) {
	if(!size) size=this->size(groupIndex);
	assert_(source.outputs() == 1); // Process every image separately
	assert_(operation.outputs() == 1);
	assert_(operation.inputs() == 1);
	array<array<SourceImage>> groupInputs;
	for(size_t inputIndex: range(operation.inputs())) groupInputs.append( source.images(groupIndex, inputIndex, size, noCacheWrite) );
	array<SourceImageRGB> allOutputs;
	for(size_t imageIndex: range(groupInputs[0].size)) {
		array<ImageF> inputs;
		for(size_t inputIndex: range(groupInputs.size)) inputs.append( share(groupInputs[inputIndex][imageIndex]) );
		assert_(groupInputs.size == 1, groupInputs.size);
		SourceImageRGB target( inputs[0].size );
		::sRGB(target, inputs[0]);
		allOutputs.append(move(target));
	}
	return allOutputs;
}

// BinaryImageGroupSource

array<SourceImage> BinaryImageGroupSource::images(size_t groupIndex, size_t outputIndex, int2 size, bool noCacheWrite) {
	// Distributes binary operator on every output of B
	assert_(A.outputs() == 1, operation.name());
	assert_(operation.inputs() >= 2, operation.name());
	array<array<SourceImage>> groupInputs;
	for(size_t inputIndex: range(operation.inputs()-1)) groupInputs.append( A.images(groupIndex, inputIndex, size, noCacheWrite) );
	auto b = B.images(groupIndex, outputIndex, size, noCacheWrite);
	array<SourceImage> allOutputs;
	for(size_t imageIndex: range(groupInputs[0].size)) {
		array<ImageF> inputs;
		for(size_t inputIndex: range(groupInputs.size)) inputs.append(share(groupInputs[inputIndex][imageIndex]));
		inputs.append( share(b[imageIndex]) );
		for(auto& x: inputs) assert_(x.size == inputs[0].size, inputs, name());
		array<SourceImage> outputs;
		for(size_t unused index: range(operation.outputs())) outputs.append( inputs[0].size );
		operation.apply(share(outputs), inputs);
		allOutputs.append(outputs);
	}
	return allOutputs;
}
