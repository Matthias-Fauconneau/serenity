#include "image-folder.h"

ImageFolder::ImageFolder(const Folder& source, function<bool(const String& name, const map<String, String>& properties)> predicate)
	: source(Folder(".",source)) {
	{// Lists images and their properties
		for(String& fileName: source.list(Files|Sorted)) {
			Map file = Map(fileName, source);
			if(imageFileFormat(file)!="JPEG") continue; // Only JPEG images
			int2 imageSize = ::imageSize(file);

			map<String, Variant> properties = parseExifTags(file);

			if((string)properties.at("Exif.Image.Orientation"_) == "6") imageSize = int2(imageSize.y, imageSize.x);
			properties.insert("Size"_, strx(imageSize));
			properties.insert("Path"_, copy(fileName));
			properties.at("Exif.Photo.ExposureTime"_).number *= 1000; // Scales seconds to milliseconds
			insert(String(section(fileName,'.')), {move(properties.keys), apply(properties.values, [](const Variant& o){return str(o);})});

			maximumImageSize = max(maximumImageSize, imageSize);
		}
	}

	{// Filters useless tags and renames to short names
		map<string, array<string>> occurences;
		for(auto& properties: values) {
			properties.filter([this](const string key, const string) { return !ref<string>{
					"Size", "Path",
					"Exif.Image.Orientation",
					"Exif.Photo.FocalLength",
					"Exif.Photo.FNumber",
					"Exif.Photo.ExposureBiasValue",
					"Exif.Photo.ISOSpeedRatings",
					"Exif.Photo.ExposureTime" }.contains(key);
			});
			properties.keys.replace("Exif.Image.Orientation"_, "Orientation"_);
			properties.keys.replace("Exif.Photo.FocalLength"_, "Focal"_);
			properties.keys.replace("Exif.Photo.FNumber"_, "Aperture"_);
			properties.keys.replace("Exif.Photo.ExposureBiasValue"_, "Bias"_);
			properties.keys.replace("Exif.Photo.ISOSpeedRatings"_, "Gain"_);
			properties.keys.replace("Exif.Photo.ExposureTime"_, "Time (ms)"_);

			for(auto property: properties) occurences[property.key].add( property.value ); // Aggregates occuring values for each property
		}
		//for(auto property: occurences) if(property.value.size!=count()) log(property.key,':', sort(property.value));
	}

	// Applies application specific filter
   if(predicate) filter(predicate);
}

/// Converts encoded sRGB images to raw (mmap'able) sRGB images
SourceImageRGB ImageFolder::image(size_t index, string) {
	assert_(index  < count());
	File sourceFile (values[index].at("Path"_), source);
	return cache<Image>({"Source", source, true}, elementName(index), size(index), sourceFile.modifiedTime(), [&](const Image& target) {
		auto source = decodeImage(Map(sourceFile));
		if(values[index].at("Orientation") == "6") rotate(target, source);
		else target.copy(source);
	}, "" /*Disable version invalidation to avoid redecoding on header changes*/);
}

/// Resizes sRGB images
/// \note Resizing after linear float conversion would be more accurate but less efficient
SourceImageRGB ImageFolder::image(size_t index, int2 hint, string parameters) {
	assert_(index  < count());
	File sourceFile (values[index].at("Path"_), source);
	int2 sourceSize = parse<int2>(values[index].at("Size"_));
	int2 size = this->size(index, hint);
	if(size==sourceSize) return image(index, parameters);
	return cache<Image>({"Resize", source, true}, elementName(index), size, sourceFile.modifiedTime(), [&](const Image& target) {
		SourceImageRGB source = image(index);
		assert_(target.size <= source.size, target.size, source.size);
		assert_(target.size.x/source.size.x == target.size.y/source.size.y, target.size, source.size);
		assert_(source.size.x/target.size.x == source.size.y/target.size.y, target.size, source.size);
		resize(target, source);
	});
}

/// Converts sRGB images to linear float images
SourceImage ImageFolder::image(size_t index, size_t componentIndex, int2 size, string parameters) {
	assert_(index  < count());
	return cache<ImageF>({"Linear["+str(componentIndex)+']', source, true}, elementName(index), size?:this->size(index), time(index),
						 [&](const ImageF& target) { linear(target, image(index, size, parameters), componentIndex); } );
}
