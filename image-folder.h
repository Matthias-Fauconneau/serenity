#include "image-source.h"
#include "exif.h"
#include "jpeg.h"

/// Cached collection of images backed by a source folder
struct ImageFolder : ImageSource, map<String, map<String, String>> {
    int2 imageSize = 0;

    ImageFolder(Folder&& source, function<bool(const String& name, const map<String, String>& properties)> predicate={})
        : ImageSource(move(source)) {
        {// Lists images, evaluates target image size and load properties
            int2 maximumImageSize = 0;

            for(String& fileName: folder.list(Files|Sorted)) {
                Map file = Map(fileName, folder);
                if(imageFileFormat(file)!="JPEG") continue; // Only JPEG images
                int2 imageSize = ::imageSize(file);
                maximumImageSize = max(maximumImageSize, imageSize);

                auto properties = parseExifTags(file);
                properties.insert(String("Size"), str(imageSize));
                properties.insert(String("Path"), copy(fileName));
                insert(String(section(fileName,'.')), {properties.keys, apply(properties.values, [](const Variant& o){return str(o);})});
            }
            // Sets target image size
            imageSize = maximumImageSize / 4;
            assert_(imageSize);
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
                properties.keys.replace("Exif.Photo.ExposureTime"_, "Time"_);

                for(auto property: properties) occurences[property.key].add( property.value ); // Aggregates occuring values for each property
            }
            for(auto property: occurences) if(property.value.size!=size()) log(property.key,':', sort(property.value));
        }

        // Applies application specific filter
       if(predicate) filter(predicate);
    }

    size_t size() const override { return map::size(); }
    String name(size_t index) const override { return copy(keys[index]); }
    int64 time(size_t index) const override { return File(values[index].at("Path"_), folder).modifiedTime(); }
    const map<String, String>& properties(size_t index) const override { return values[index]; }
    int2 size(size_t) const override { return imageSize; }

    /// Converts encoded sRGB images to raw (mmap'able) sRGB images
    SourceImageRGB image(size_t index) const override {
        File sourceFile (values[index].at("Path"_), folder);
        return cache<Image>(name(index), ".sRGB", folder, [&](TargetImageRGB& target){
            Image source = decodeImage(Map(sourceFile));
            target.resize(imageSize);
            target.buffer::clear();
            assert_(target.size < source.size);
            resize(share(target), source);
        }, sourceFile.modifiedTime());
    }

    /// Converts sRGB images to linear float images
    SourceImage image(size_t index, uint component) const override {
        return cache<ImageF>(name(index), '.'+str(component), folder, [&](TargetImage& target) {
            SourceImageRGB source = image(index); // Faster but slightly inaccurate
            target.resize(source.size);
            linear(share(target), source, component);
        }, time(index));
    }
};
