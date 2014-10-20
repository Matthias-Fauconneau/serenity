#include "process.h"

SourceImage ProcessedSource::image(size_t imageIndex, int outputIndex, int2 size, bool noCacheWrite) {
	return ::cache<ImageF>(folder(), elementName(imageIndex), size?:this->size(imageIndex), time(imageIndex),
				 [&](const ImageF& target) {
		assert_(operation.outputs() == 1);
		assert_(outputIndex == 0);
		operation.apply({share(target)},
						share(apply(operation.inputs(), [&](size_t inputIndex) { return source.image(imageIndex, inputIndex, size, noCacheWrite); })));
	}, noCacheWrite);
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

ProcessedGroupImageSource::ProcessedGroupImageSource(ImageGroupSource& source, ImageGroupOperation& operation)
	: source(source), operation(operation), cacheFolder(operation.name(), source.folder(), true) {}

int ProcessedGroupImageSource::outputs() const { return operation.outputs(); }
const Folder& ProcessedGroupImageSource::folder() const { return cacheFolder; }
String ProcessedGroupImageSource::name() const { return str(operation.name(), source.name()); }
size_t ProcessedGroupImageSource::count(size_t need) { return source.count(need); }
int2 ProcessedGroupImageSource::maximumSize() const { return source.maximumSize(); }
String ProcessedGroupImageSource::elementName(size_t groupIndex) const { return source.elementName(groupIndex); }
int64 ProcessedGroupImageSource::time(size_t groupIndex) { return max(operation.time(), source.time(groupIndex)); }
const map<String, String>& ProcessedGroupImageSource::properties(size_t unused groupIndex) const { error("Unimplemented"); }
int2 ProcessedGroupImageSource::size(size_t groupIndex) const { return source.size(groupIndex); }

SourceImage ProcessedGroupImageSource::image(size_t groupIndex, int outputIndex, int2 size, bool noCacheWrite) {
	return  ::cache<ImageF>(folder(), elementName(groupIndex), size?:this->size(groupIndex), time(groupIndex),
						   [&](const ImageF& target) {
		assert_(operation.outputs() == 1);
		operation.apply({share(target)}, share(source.images(groupIndex, outputIndex, size, noCacheWrite)));
	}, noCacheWrite);
}

SourceImageRGB ProcessedGroupImageSource::image(size_t groupIndex, int2 size, bool noCacheWrite) {
	return  ::cache<Image>(folder(), elementName(groupIndex), size?:this->size(groupIndex), time(groupIndex),
						   [&](const Image& target) {
		assert_(operation.outputs() == 1);
		array<ImageF> outputs;
		for(size_t inputIndex: range(source.outputs())) {
			outputs.append(ImageF( size?:this->size(groupIndex )));
			operation.apply({share(outputs.last())}, share(source.images(groupIndex, inputIndex, size, noCacheWrite)));
		}
		/**/  if(source.outputs() == 1) sRGB(target, outputs[0]);
		else if(source.outputs() == 3) sRGB(target, outputs[0], outputs[1], outputs[2]);
		else error(source.outputs());
	}, noCacheWrite);
}
