#include "operation.h"

// ImageOperation

SourceImage ImageOperation::image(size_t imageIndex, size_t componentIndex, int2 hint, bool noCacheWrite) {
	if(operation.inputs()==0 && operation.outputs()==0) {
		auto inputs = apply(source.outputs(), [&](size_t inputIndex) { return source.image(imageIndex, inputIndex, hint, noCacheWrite); });
		array<SourceImage> outputs;
		for(size_t unused index: range(operation.outputs()?:inputs.size)) outputs.append( inputs[0].size );
		operation.apply({share(outputs)}, share(inputs));
		return move( outputs[componentIndex] );
	}
	assert_(operation.outputs() == 1);
	SourceImage target = ::cache<ImageF>(folder(), elementName(imageIndex)+'['+str(componentIndex)+']', this->size(imageIndex, hint),
										 time(imageIndex), [&](const ImageF& target) {
		array<SourceImage> inputs;
		if(operation.inputs()==1 && source.outputs()!=1) { // Component wise
			inputs.append( source.image(imageIndex, componentIndex, hint, noCacheWrite) );
		} else {
			assert_(componentIndex == 0);
			inputs = apply(operation.inputs()?:source.outputs(),
						   [&](size_t inputIndex) { return source.image(imageIndex, inputIndex, hint, noCacheWrite); });
		}
		operation.apply({share(target)}, share(inputs));
	}, noCacheWrite);
	return target;
}

// sRGBOperation

SourceImageRGB sRGBOperation::image(size_t imageIndex, int2 hint, bool noCacheWrite) {
	return ::cache<Image>(folder(), elementName(imageIndex), this->size(imageIndex, hint), GenericImageOperation::time(imageIndex), [&](Image& target) {
		array<SourceImage> inputs;
		for(size_t inputIndex: range(source.outputs())) inputs.append(source.image(imageIndex, inputIndex, hint, noCacheWrite));
		if(inputs.size==1) ::sRGB(target, inputs[0]);
		else if(inputs.size==3) ::sRGB(target, inputs[0], inputs[1], inputs[2]);
		else error(inputs.size, "SourceImageRGB");
	}, noCacheWrite);
}

// ImageGroupFold

SourceImage ImageGroupFold::image(size_t groupIndex, size_t componentIndex, int2 hint, bool noCacheWrite) {
	int2 size = this->size(groupIndex, hint);
	return ::cache<ImageF>(folder(), elementName(groupIndex)+'['+str(componentIndex)+']', size, time(groupIndex),
			[&](ImageF& target) {
		array<SourceImage> inputs = source.images(groupIndex, operation.outputs()==1?componentIndex:0, hint, noCacheWrite);
		assert_(operation.inputs() == 0 || operation.inputs() == inputs.size);
		assert_(inputs[0].size == target.size, inputs[0].size, target.size, source.name());
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

int2 GroupImageOperation::size(size_t groupIndex, int2 size) const {
	auto sizes = apply(groups(groupIndex), [this,size](size_t imageIndex) { return source.size(imageIndex, size); });
	for(auto size: sizes) assert_(size == sizes[0], sizes);
	return sizes[0];
}

array<SourceImage> GroupImageOperation::images(size_t groupIndex, size_t componentIndex, int2 hint, bool noCacheWrite) {
	return apply(groups(groupIndex), [&](const size_t imageIndex) { return source.image(imageIndex, componentIndex, hint, noCacheWrite); });
}

// ImageGroupOperation

// FIXME: factorize and proper cache
array<SourceImage> ImageGroupOperation::images(size_t groupIndex, size_t componentIndex, int2 hint, bool noCacheWrite) {
	//assert_(componentIndex == 0, componentIndex, operation.name(), operation.inputs(), operation.outputs(), outputs());
	array<SourceImage> allOutputs;
	if(operation.inputs() == 0 && operation.outputs()==0) { // Process all images at once (forwards componentIndex)
		auto inputs = source.images(groupIndex, componentIndex, hint, noCacheWrite); //FIXME: not if all cached
		for(size_t imageIndex: range(inputs.size)) {
			allOutputs.append( ::cache<ImageF>(folder(), elementName(groupIndex)+'{'+str(imageIndex)+'}'+'['+str(componentIndex)+']',
											   this->size(groupIndex, hint), time(groupIndex), [&](ImageF& target) {
				array<SourceImage> outputs;
				for(size_t unused index: range(inputs.size)) outputs.append( inputs[0].size );
				operation.apply(share(outputs), share(inputs));
				assert_(imageIndex < outputs.size);
				assert_(target.size == outputs[imageIndex].size);
				target.copy(outputs[imageIndex]);
				for(size_t otherImageIndex: range(outputs.size)) { // Writes all outputs
					if(otherImageIndex != imageIndex)
					::cache<ImageF>(folder(), elementName(groupIndex)+'{'+str(otherImageIndex)+'}'+'['+str(componentIndex)+']',
									inputs[0].size, time(groupIndex), [&](ImageF& target) {
						assert_(otherImageIndex < outputs.size);
						assert_(target.size == outputs[otherImageIndex].size);
						target.copy(outputs[otherImageIndex]);
					});
				}
			}));
		}
	} else { // Process every image separately
		array<array<SourceImage>> groupInputs;
		for(size_t inputIndex: range(operation.inputs()?:source.outputs()))
			groupInputs.append( source.images(groupIndex, inputIndex, hint, noCacheWrite) ); //FIXME: not if all cached
		for(size_t imageIndex: range(groupInputs[0].size)) {
			array<ImageF> inputs;
			for(size_t inputIndex: range(groupInputs.size)) inputs.append( share(groupInputs[inputIndex][imageIndex]) );
			allOutputs.append( ::cache<ImageF>(folder(), elementName(groupIndex)+'{'+str(imageIndex)+'}'+'['+str(componentIndex)+']',
											   this->size(groupIndex, hint), time(groupIndex),
					[&](ImageF& target) {
				array<SourceImage> outputs;
				for(size_t unused index: range(operation.outputs())) outputs.append( inputs[0].size );
				log(operation.name(), elementName(groupIndex)+'{'+str(imageIndex)+'}'+'['+str(componentIndex)+']');
				operation.apply(share(outputs), inputs);
				assert_(componentIndex < outputs.size);
				assert_(target.size == outputs[componentIndex].size);
				target.copy(outputs[componentIndex]);
				for(size_t otherComponentIndex: range(outputs.size)) { // Writes all outputs
					if(otherComponentIndex != componentIndex) {
						::cache<ImageF>(folder(), elementName(groupIndex)+'{'+str(imageIndex)+'}'+'['+str(otherComponentIndex)+']',
										inputs[0].size, time(groupIndex), [&](ImageF& target) {
							assert_(otherComponentIndex < outputs.size);
							assert_(target.size == outputs[otherComponentIndex].size);
							target.copy(outputs[otherComponentIndex]);
						});
					}
				}
			}) );
		}
	}
	return allOutputs;
}

// sRGBGroupOperation

array<SourceImageRGB> sRGBGroupOperation::images(size_t groupIndex, int2 hint, bool noCacheWrite) {
	assert_(source.outputs() == 1 || source.outputs() == 3, source.outputs()); // Process every image separately
	assert_(operation.inputs() == 1 && operation.outputs() == 1);
	array<array<SourceImage>> groupInputs;
	for(size_t inputIndex: range(source.outputs())) // FIXME: not if all cached
		groupInputs.append( source.images(groupIndex, inputIndex, hint, noCacheWrite) );
	array<SourceImageRGB> allOutputs;
	for(size_t imageIndex: range(groupInputs[0].size)) {
		//FIXME: cache this->size(groupIndex, hint)
		array<ImageF> inputs;
		for(size_t inputIndex: range(groupInputs.size)) inputs.append( share(groupInputs[inputIndex][imageIndex]) );
		SourceImageRGB target( this->size(imageIndex, hint) );
		/**/  if(inputs.size==1) ::sRGB(target, inputs[0]);
		else if(inputs.size==3) ::sRGB(target, inputs[0], inputs[1], inputs[2]);
		else error(inputs.size, "sRGBGroupOperation");
		allOutputs.append(move(target));
	}
	return allOutputs;
}

// BinaryImageOperation

SourceImage BinaryImageOperation::image(size_t imageIndex, size_t componentIndex, int2 hint, bool noCacheWrite) {
	//FIXME: cache
	if(A.outputs() == B.outputs()) {
		assert_(operation.inputs() == 2, A.outputs(), B.outputs(), operation.inputs(), operation.name());
		array<SourceImage> inputs;
		inputs.append( A.image(imageIndex, componentIndex, hint, noCacheWrite) );
		inputs.append( B.image(imageIndex, componentIndex, hint, noCacheWrite) );
		SourceImage output ( this->size(imageIndex, hint) );
		operation.apply({share(output)}, share(inputs));
		return output;
	}
	assert_(A.outputs() == 1 && operation.inputs() == 2, A.outputs(), B.outputs(), operation.inputs(), operation.name());
	array<SourceImage> inputs;
	inputs.append( A.image(imageIndex, 0, hint, noCacheWrite) );
	inputs.append( B.image(imageIndex, componentIndex, hint, noCacheWrite) );
	SourceImage output ( this->size(imageIndex, hint) );
	operation.apply({share(output)}, share(inputs));
	return output;
}

// BinaryImageGroupOperation

array<SourceImage> BinaryImageGroupOperation::images(size_t groupIndex, size_t componentIndex, int2 hint, bool noCacheWrite) {
	assert_(A.outputs() == 1 || operation.inputs()==0 || A.outputs() == B.outputs(), operation.name(), A.outputs(), B.outputs());
	assert_(operation.inputs() >= 2 || operation.inputs()==0, operation.name());
	array<SourceImage> allOutputs;
	if(A.outputs() == B.outputs()) {  // For each image of the group, operates on A[componentIndex], B[componentIndex]
		assert_(A.groupSize(groupIndex) == B.groupSize(groupIndex));
		// FIXME: not if all cached
		array<SourceImage> a = A.images(groupIndex, componentIndex, hint, noCacheWrite);
		array<SourceImage> b = B.images(groupIndex, componentIndex, hint, noCacheWrite);
		for(size_t imageIndex: range(a.size)) {
			// FIXME: cache
			array<ImageF> inputs;
			inputs.append( share( a[imageIndex] ) );
			inputs.append( share( b[imageIndex] ) );
			array<SourceImage> outputs;
			for(size_t unused index: range(operation.outputs())) outputs.append( this->size(groupIndex, hint) );
			operation.apply(share(outputs), inputs);
			allOutputs.append(outputs);
		}
	} else { // For each image of the group, operates on A[*], B[componentIndex]
		// FIXME: not if all cached
		array<array<SourceImage>> groupInputs;
		if(operation.inputs()) assert_(operation.inputs()-1 <= A.outputs(), operation.inputs(), A.outputs());
		for(size_t inputIndex: range(operation.inputs() ? operation.inputs()-1 : A.outputs()))
			groupInputs.append( A.images(groupIndex, inputIndex, hint, noCacheWrite) );
		auto b = B.images(groupIndex, componentIndex, hint, noCacheWrite);
		assert_(A.groupSize(groupIndex) == B.groupSize(groupIndex));
		assert_(groupInputs.size);
		for(size_t imageIndex: range(groupInputs[0].size)) {
			// FIXME: cache
			array<ImageF> inputs;
			for(size_t inputIndex: range(groupInputs.size)) inputs.append(share(groupInputs[inputIndex][imageIndex]));
			inputs.append( share(b[imageIndex]) );
			for(auto& x: inputs) assert_(x.size == this->size(groupIndex, hint), inputs, name());
			array<SourceImage> outputs;
			for(size_t unused index: range(operation.outputs())) outputs.append( this->size(groupIndex, hint) );
			operation.apply(share(outputs), inputs);
			allOutputs.append(outputs);
		}
	}
	return allOutputs;
}
