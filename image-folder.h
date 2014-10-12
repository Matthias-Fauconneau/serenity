#pragma once
#include "image-source.h"
#include "variant.h"
#include "exif.h"
#include "jpeg.h"

/// Cached collection of images backed by a source folder
struct ImageFolder : ImageSource, map<String, map<String, String>> {
    int2 maximumImageSize = 0;

    ImageFolder(const Folder& source, function<bool(const String& name, const map<String, String>& properties)> predicate={})
        : ImageSource(Folder(".",source)) {
        {// Lists images and their properties
            for(String& fileName: folder.list(Files|Sorted)) {
                Map file = Map(fileName, folder);
                if(imageFileFormat(file)!="JPEG") continue; // Only JPEG images
                int2 imageSize = ::imageSize(file);

                auto properties = parseExifTags(file);
                properties.insert("Size"_, strx(imageSize));
                properties.insert("Path"_, copy(fileName));
                properties.at("Exif.Photo.ExposureTime"_).number *= 1000; // Scales seconds to milliseconds
                insert(String(section(fileName,'.')), {properties.keys, apply(properties.values, [](const Variant& o){return str(o);})});

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
            for(auto property: occurences) if(property.value.size!=count()) log(property.key,':', sort(property.value));
        }

        // Applies application specific filter
       if(predicate) filter(predicate);
    }

    String name() const override { return String(section(folder.name(),'/',-2,-1)); }
    size_t count() const override { return map::size(); }
    int2 maximumSize() const override { return maximumImageSize; }
    String name(size_t index) const override { assert_(index<count()); return copy(keys[index]); }
    int64 time(size_t index) const override { return File(properties(index).at("Path"_), folder).modifiedTime(); }
    const map<String, String>& properties(size_t index) const override { return values[index]; }
    int2 size(size_t index) const override { return fromInt2(properties(index).at("Size"_)); }

    /// Converts encoded sRGB images to raw (mmap'able) sRGB images
    SourceImageRGB image(size_t index) const {
        assert_(index  < count());
        File sourceFile (properties(index).at("Path"_), folder);
        return cache<Image>(name(index), ".sRGB", folder, [&](TargetImageRGB& target){
            Time time; log_(str("source.decode", size(index), ""));
            Image source = decodeImage(Map(sourceFile));
            log(time);
            target.resize(source.size);
            target.copy(source);
        }, sourceFile.modifiedTime(), size(index), "" /*Disable version invalidation to avoid redecoding on header changes*/);
    }

    /// Resizes sRGB images
    SourceImageRGB image(size_t index, int2 size) const override {
        assert_(index  < count());
        File sourceFile (properties(index).at("Path"_), folder);
        if(size==this->size(index)) return image(index);
        return cache<Image>(name(index), "Resize.sRGB", folder, [&](TargetImageRGB& target){
            SourceImageRGB source = image(index);
            target.resize(size);
            assert_(target.size <= source.size, target.size, source.size);
            resize(share(target), source);
        }, sourceFile.modifiedTime(), size);
    }

    /// Converts sRGB images to linear float images
    SourceImage image(size_t index, uint component, int2 size) const override {
        assert_(index  < count());
        //int2 size = fit(this->size(index), hint);
        return cache<ImageF>(name(index), '.'+str(component), folder, [&](TargetImage& target) {
            SourceImageRGB source = image(index, size); // Faster but slightly inaccurate
            target.resize(size);
            linear(share(target), source, component);
        }, time(index), size);
    }
};
