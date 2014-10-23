#include "operation.h"

// ImageOperation

SourceImage ImageOperation::image(size_t imageIndex, size_t componentIndex, int2 size, bool noCacheWrite) {
	SourceImage target = ::cache<ImageF>(folder(), elementName(imageIndex), size?:this->size(imageIndex), time(imageIndex),
				 [&](const ImageF& target) {
		assert_(operation.outputs() == 1);
		assert_(componentIndex == 0);
		auto inputs = apply(operation.inputs(), [&](size_t inputIndex) { return source.image(imageIndex, inputIndex, target.size, noCacheWrite); });
		for(auto& input: inputs) assert_(isNumber(input[0]));
		operation.apply({share(target)}, share(inputs));
	}, noCacheWrite);
	assert_(isNumber(target[0]), target[0], target.size, name());
	return target;
}

// sRGBOperation

SourceImageRGB sRGBOperation::image(size_t imageIndex, int2 size, bool noCacheWrite) {
	array<SourceImage> inputs;
	for(size_t inputIndex: range(source.outputs())) inputs.append(source.image(imageIndex, inputIndex, size, noCacheWrite));
	return ::cache<Image>(folder(), elementName(imageIndex), inputs[0].size, GenericImageOperation::time(imageIndex),
				 [&](Image& target) {
		if(target.size != inputs[0].size) {
			error("Resize");
			assert_(target.size > inputs[0].size);
			target.Image::size = inputs[0].ImageF::size;
		}
		if(inputs.size==1) ::sRGB(target, inputs[0]);
		else if(inputs.size==3) ::sRGB(target, inputs[0], inputs[1], inputs[2]);
		else error(inputs.size);
	}, noCacheWrite);
}

// ImageGroupFold

SourceImage ImageGroupFold::image(size_t groupIndex, size_t componentIndex, int2 size, bool noCacheWrite) {
	assert_(operation.outputs()==1 || source.outputs()==1);
	array<SourceImage> inputs = source.images(groupIndex, operation.outputs()==1?componentIndex:0, size, noCacheWrite); // FIXME
	return ::cache<ImageF>(folder(), elementName(groupIndex)+'['+str(componentIndex)+']', inputs[0].size, time(groupIndex), [&](ImageF& target) {
		if(target.size != inputs[0].size) {
			error("Resize");
			assert_(target.size > inputs[0].size);
			target.size = inputs[0].size;
		}
		assert_(operation.inputs() == 0 || operation.inputs() == inputs.size);
		assert_(inputs[0].size == target.size);
		if(operation.outputs()==1) { // componentIndex selects source output index
			operation.apply({share(target)}, share(inputs));
		} else {
			array<SourceImage> outputs;
			for(size_t unused index: range(operation.outputs())) outputs.append( inputs[0].size );
			operation.apply(share(outputs), share(inputs));
			assert_(componentIndex < outputs.size, componentIndex, outputs.size, operation.name());
			assert_(target.size == outputs[componentIndex].size && target.stride == outputs[componentIndex].stride);
			target.copy(outputs[componentIndex]);
		}
	}, noCacheWrite);
}

// GroupImageOperation

String GroupImageOperation::elementName(size_t groupIndex) const {
	return str(apply(groups(groupIndex), [this](const size_t imageIndex) { return source.elementName(imageIndex); }));
}

int2 GroupImageOperation::size(size_t groupIndex) const {
	auto sizes = apply(groups(groupIndex), [this](size_t imageIndex) { return source.size(imageIndex); });
	for(auto size: sizes) assert_(size == sizes[0], sizes);
	return sizes[0];
}

array<SourceImage> GroupImageOperation::images(size_t groupIndex, size_t componentIndex, int2 size, bool noCacheWrite) {
	if(!size) size=this->size(groupIndex);
	return apply(groups(groupIndex), [&](const size_t imageIndex) { return source.image(imageIndex, componentIndex, size, noCacheWrite); });
}

// ImageGroupOperation

array<SourceImage> ImageGroupOperation::images(size_t groupIndex, size_t componentIndex, int2 size, bool noCacheWrite) {
	//assert_(componentIndex == 0, componentIndex, operation.name(), operation.inputs(), operation.outputs(), outputs());
	if(!size) size=this->size(groupIndex);
	if(operation.inputs() == 0 && operation.outputs()==0) { // Process all images at once (Forwards componentIndex)
		auto inputs = source.images(groupIndex, componentIndex, size, noCacheWrite);
		array<SourceImage> outputs;
		for(size_t unused index: range(inputs.size)) outputs.append( inputs[0].size );
		operation.apply(share(outputs), share(inputs));
		return outputs;
	} else { // Process every image separately
		array<array<SourceImage>> groupInputs;
		for(size_t inputIndex: range(operation.inputs()?:source.outputs()))
			groupInputs.append( source.images(groupIndex, inputIndex, size, noCacheWrite) );
		array<SourceImage> allOutputs;
		for(size_t imageIndex: range(groupInputs[0].size)) {
			array<ImageF> inputs;
			for(size_t inputIndex: range(groupInputs.size)) inputs.append( share(groupInputs[inputIndex][imageIndex]) );
			array<SourceImage> outputs;
			for(size_t unused index: range(operation.outputs())) outputs.append( inputs[0].size );
			operation.apply(share(outputs), inputs);
			assert_(componentIndex < outputs.size, componentIndex, outputs.size, operation.name());
			allOutputs.append( move( outputs[componentIndex] ) );
			// FIXME: cache before discarding
		}
		return allOutputs;
	}
	error(operation.outputs());
}

// sRGBGroupOperation

array<SourceImageRGB> sRGBGroupOperation::images(size_t groupIndex, int2 size, bool noCacheWrite) {
	if(!size) size=this->size(groupIndex);
	assert_(source.outputs() == 1 || source.outputs() == 3, source.outputs()); // Process every image separately
	assert_(operation.inputs() == 1 && operation.outputs() == 1);
	array<array<SourceImage>> groupInputs;
	for(size_t inputIndex: range(source.outputs())) groupInputs.append( source.images(groupIndex, inputIndex, size, noCacheWrite) );
	array<SourceImageRGB> allOutputs;
	for(size_t imageIndex: range(groupInputs[0].size)) {
		array<ImageF> inputs;
		for(size_t inputIndex: range(groupInputs.size)) inputs.append( share(groupInputs[inputIndex][imageIndex]) );
		SourceImageRGB target( inputs[0].size );
		/**/  if(inputs.size==1) ::sRGB(target, inputs[0]);
		else if(inputs.size==3) ::sRGB(target, inputs[0], inputs[1], inputs[2]);
		else error(inputs.size);
		allOutputs.append(move(target));
	}
	return allOutputs;
}

// BinaryImageGroupOperation

array<SourceImage> BinaryImageGroupOperation::images(size_t groupIndex, size_t componentIndex, int2 size, bool noCacheWrite) {
	// Distributes binary operator on every output of B
	assert_(A.outputs() == 1 || operation.inputs()==0, operation.name());
	assert_(operation.inputs() >= 2 || operation.inputs()==0, operation.name());
	array<array<SourceImage>> groupInputs;
	if(operation.inputs()) assert_(operation.inputs()-1 <= A.outputs(), operation.inputs(), A.outputs());
	for(size_t inputIndex: range(operation.inputs() ? operation.inputs()-1 : A.outputs()))
		groupInputs.append( A.images(groupIndex, inputIndex, size, noCacheWrite) );
	auto b = B.images(groupIndex, componentIndex, size, noCacheWrite);
	array<SourceImage> allOutputs;
	/*if(operation.inputs() == 0) { // For each image of the group, operates on A[*], B[componentIndex]
	} else*/ { // For each image of the group, operates on A[*], B[componentIndex]
		assert_(A.groupSize(groupIndex) == B.groupSize(groupIndex));
		assert_(groupInputs.size);
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
	}
	return allOutputs;
}
