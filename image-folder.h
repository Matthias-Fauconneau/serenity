#pragma once
#include "source.h"
#include "serialization.h"
#include "exif.h"
#include "jpeg.h"

/// Cached collection of images backed by a source folder
struct ImageFolder : ImageSource, ImageRGBSource, map<String, map<String, String>> {
	Folder source;
    int2 maximumImageSize = 0;

    ImageFolder(const Folder& source, function<bool(const String& name, const map<String, String>& properties)> predicate={})
		: source(Folder(".",source)) {
        {// Lists images and their properties
			for(String& fileName: source.list(Files|Sorted)) {
				Map file = Map(fileName, source);
                if(imageFileFormat(file)!="JPEG") continue; // Only JPEG images
                int2 imageSize = ::imageSize(file);

                auto properties = parseExifTags(file);
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
                        "Exif.Photo.FocalLength",
                        "Exif.Photo.FNumber",
                        "Exif.Photo.ExposureBiasValue",
                        "Exif.Photo.ISOSpeedRatings",
                        "Exif.Photo.ExposureTime" }.contains(key);
                });
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

	const Folder& folder() const override { return source; }
	String name() const override { return String(section(source.name(),'/',-2,-1)); }
	size_t count(size_t) override { return map::size(); }
	size_t count() const { return map::size(); }
	size_t outputs() const override { return 3; }
    int2 maximumSize() const override { return maximumImageSize; }
	String elementName(size_t index) const override { assert_(index<count()); return copy(keys[index]); }
	int64 time(size_t index) override { return File(values[index].at("Path"_), source).modifiedTime(); }
	//const map<String, String>& properties(size_t index) const override { return values[index]; }
	int2 size(size_t index, int2 size=0) const override { return size ?: parse<int2>(values[index].at("Size"_)); }

    /// Converts encoded sRGB images to raw (mmap'able) sRGB images
	SourceImageRGB image(size_t index, string parameters = "");

    /// Resizes sRGB images
    /// \note Resizing after linear float conversion would be more accurate but less efficient
	SourceImageRGB image(size_t index, int2 size, string parameters = "") override;

    /// Converts sRGB images to linear float images
	SourceImage image(size_t index, size_t componentIndex, int2 size, string parameters = "") override;
};
