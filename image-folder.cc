#include "image-folder.h"

/// Converts encoded sRGB images to raw (mmap'able) sRGB images
SourceImageRGB ImageFolder::image(size_t index, string) {
	assert_(index  < count());
	File sourceFile (values[index].at("Path"_), source);
	return cache<Image>({"Source", source, true}, elementName(index), size(index), sourceFile.modifiedTime(), [&](const Image& target) {
		target.copy(decodeImage(Map(sourceFile)));
	}, "" /*Disable version invalidation to avoid redecoding on header changes*/);
}

/// Resizes sRGB images
/// \note Resizing after linear float conversion would be more accurate but less efficient
SourceImageRGB ImageFolder::image(size_t index, int2 size, string parameters) {
	assert_(index  < count());
	File sourceFile (values[index].at("Path"_), source);
	int2 sourceSize = this->size(index);
	//size = min(sourceSize*size.x/sourceSize.x, sourceSize*size.y/sourceSize.y); // Fits aspect ratio
	if(!size || size>=sourceSize) return image(index, parameters);
	return cache<Image>({"Resize", source, true}, elementName(index), size, sourceFile.modifiedTime(), [&](const Image& target) {
		SourceImageRGB source = image(index);
		assert_(target.size <= source.size, target.size, source.size);
		//assert_(target.size.x*source.size.y == source.size.x*target.size.y, source.size, target.size);
		resize(target, source);
	});
}

/// Converts sRGB images to linear float images
SourceImage ImageFolder::image(size_t index, size_t componentIndex, int2 size, string parameters) {
	assert_(index  < count());
	return cache<ImageF>({"Linear["+str(componentIndex)+']', source, true}, elementName(index), size?:this->size(index), time(index),
						 [&](const ImageF& target) { linear(target, image(index, size, parameters), componentIndex); } );
}
