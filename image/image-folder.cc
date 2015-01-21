#include "image-folder.h"

ImageFolder::ImageFolder(const Folder& source, function<bool(string name, const map<string, String>& properties)> predicate, const int downsample)
	: source(Folder(".",source)), downsample(downsample) {
	{// Lists images and their properties
		for(String& fileName: source.list(Files|Sorted)) {
			Map file = Map(fileName, source);
			if(!ref<string>{"JPEG"_,"PNG"_}.contains(imageFileFormat(file))) continue; // Only JPEG and PNG images
			int2 imageSize = ::imageSize(file);

			map<String, Variant> exif = parseExifTags(file);

			if(exif.contains("Exif.Image.Orientation"_) && (string)exif.at("Exif.Image.Orientation"_) == "6") imageSize = int2(imageSize.y, imageSize.x);
			Variant date = ""_;
			if(exif.contains("Exif.Image.DateTime"_)) date = move(exif.at("Exif.Image.DateTime"));
			date = section((string)date, ' '); // Discards time to group by date using PropertyGroup
			if(exif.contains("Exif.Photo.ExposureTime"_)) exif.at("Exif.Photo.ExposureTime"_).number *= 1000; // Scales seconds to milliseconds

			map<string, String> properties;
			properties.insert("Size", strx(imageSize));
			properties.insert("Path", copy(fileName));
			if(exif.contains("Exif.Image.DateTime"_)) properties.insert("Date", str(date));
			if(exif.contains("Exif.Image.Orientation"_)) properties.insert("Orientation", str(exif.at("Exif.Image.Orientation"_)));
			if(exif.contains("Exif.Photo.FocalLength"_)) properties.insert("Focal", str(exif.at("Exif.Photo.FocalLength"_)));
			if(exif.contains("Exif.Photo.FNumber"_)) properties.insert("Aperture", str(exif.at("Exif.Photo.FNumber"_)));
			if(exif.contains("Exif.Photo.ExposureBiasValue"_)) properties.insert("Bias", str(exif.at("Exif.Photo.ExposureBiasValue"_)));
			if(exif.contains("Exif.Photo.ISOSpeedRatings"_)) properties.insert("Gain", str(exif.at("Exif.Photo.ISOSpeedRatings"_)));
			if(exif.contains("Exif.Photo.ExposureTime"_)) properties.insert("Time", str(exif.at("Exif.Photo.ExposureTime"_)));

			string name = section(fileName,'.');
			if(predicate && predicate(name, properties)) continue;

			insert(copyRef(name), move(properties));

			maximumImageSize = max(maximumImageSize, imageSize);
		}
	}
	// Applies application specific filter
   if(predicate) filter(predicate);
}

/// Converts encoded sRGB images to raw (mmap'able) sRGB images
SourceImageRGB ImageFolder::image(size_t index, string) {
	assert_(index  < count());
	File sourceFile (values[index].at("Path"_), source);
	return cache<Image>(path()+"/Source", elementName(index), parse<int2>(values[index].at("Size"_)), sourceFile.modifiedTime(), [&](const Image& target) {
		Image source = decodeImage(Map(sourceFile));
		assert_(source.size);
		if(values[index].contains("Orientation") && values[index].at("Orientation") == "6") rotate(target, source);
		else target.copy(source);
	}, true /*Disables full size source cache*/, "" /*Disables version invalidation to avoid redecoding on header changes*/);
}

/// Resizes sRGB images
/// \note Resizing after linear float conversion would be more accurate but less efficient
SourceImageRGB ImageFolder::image(size_t index, int2 hint, string parameters) {
	assert_(index  < count());
	File sourceFile (values[index].at("Path"_), source);
	int2 sourceSize = parse<int2>(values[index].at("Size"_));
	int2 size = this->size(index, hint);
	if(size==sourceSize) return image(index, parameters);
	return cache<Image>(path()+"/Resize", elementName(index), size, sourceFile.modifiedTime(), [&](const Image& target) {
		SourceImageRGB source = image(index);
		assert_(size==target.size && size >= int2(12) && size <= source.size, size, hint);
		resize(target, source);
	}, false, "" /*Disables version invalidation to avoid redecoding and resizing on header changes*/);
}

/// Converts sRGB images to linear float images
SourceImage ImageFolder::image(size_t index, size_t componentIndex, int2 size, string parameters) {
	assert_(index  < count());
	int2 targetSize = size?:this->size(index);
	return cache<ImageF>(path()+"/Linear["+str(componentIndex)+']', elementName(index), targetSize, time(index),
						 [&](const ImageF& target) {
		SourceImageRGB source = image(index, targetSize, parameters);
		assert_(source.size == target.size, source.size, target.size, size, targetSize);
		linear(target, source, componentIndex); } );
}
