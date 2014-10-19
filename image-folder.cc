#include "image-folder.h"

/// Converts encoded sRGB images to raw (mmap'able) sRGB images
SourceImageRGB ImageFolder::image(size_t index, bool noCacheWrite) {
	assert_(index  < count());
	File sourceFile (properties(index).at("Path"_), source);
	if(noCacheWrite) return decodeImage(Map(sourceFile));
	return cache<Image>({"Source", source}, elementName(index), size(index), sourceFile.modifiedTime(), [&](const Image& target) {
		target.copy(decodeImage(Map(sourceFile)));
	}, "" /*Disable version invalidation to avoid redecoding on header changes*/);
}

/// Resizes sRGB images
/// \note Resizing after linear float conversion would be more accurate but less efficient
SourceImageRGB ImageFolder::image(size_t index, int2 size, bool noCacheWrite) {
	assert_(index  < count());
	File sourceFile (properties(index).at("Path"_), source);
	if(!size || size>=this->size(index)) return image(index, noCacheWrite);
	if(noCacheWrite) return resize(size, image(index));
	return cache<Image>({"Resize", source}, elementName(index), size, sourceFile.modifiedTime(), [&](const Image& target){
		SourceImageRGB source = image(index);
		assert_(target.size <= source.size, target.size, source.size);
		resize(target, source);
	});
}

/// Converts sRGB images to linear float images
SourceImage ImageFolder::image(size_t index, int outputIndex, int2 size, bool noCacheWrite) {
	assert_(index  < count());
	if(noCacheWrite) return linear(image(index, size, noCacheWrite), outputIndex);
	return cache<ImageF>({"Linear["+str(outputIndex)+']', source}, elementName(index), size?:this->size(index), time(index),
						 [&](const ImageF& target) { linear(target, image(index, size), outputIndex); } );
}
