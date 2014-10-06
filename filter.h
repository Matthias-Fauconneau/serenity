#pragma once
#include "file.h"
#include "exif.h"
#include "jpeg.h"

static constexpr bool skipCache = false;

struct ImageTarget : Map, ImageF {
    ImageTarget(const string& path, const Folder& at, int2 size) :
        Map(File(path, at, ::Flags(ReadWrite|Create)).resize(size.x*size.y*sizeof(float)), Map::Write),
        ImageF(unsafeReference(cast<float>((Map&)*this)), size) {}
};

struct ImageSource : Map, ImageF {
    ImageSource(const string& path, const Folder& at, int2 size) :
        Map (path, at),
        ImageF(unsafeReference(cast<float>((Map&)*this)), size) {}
};

struct ImageTargetRGB : Map, Image {
    ImageTargetRGB(const string& path, const Folder& at, int2 size) :
        Map(File(path, at, ::Flags(ReadWrite|Create)).resize(size.x*size.y*sizeof(byte4)), Map::Write),
        Image(unsafeReference(cast<byte4>((Map&)*this)), size) {}
};

struct ImageSourceRGB : Map, Image {
    ImageSourceRGB() {}
    ImageSourceRGB(const string& path, const Folder& at, int2 size) :
        Map (path, at),
        Image(unsafeReference(cast<byte4>((Map&)*this)), size) {}
};

/// Collection of images
struct ImageFolder : map<String, map<String, String>> {
    Folder sourceFolder;
    Folder cacheFolder {".cache"_, sourceFolder, true};
    const int2 imageSize = int2(4000, 3000)/4; //FIXME: = ::imageSize(readFile(keys.at(0));

    ImageFolder(Folder&& folder) : sourceFolder(move(folder)) {
        array<String> fileNames = sourceFolder.list(Files|Sorted);
        for(String& fileName: fileNames) {
            Map file = Map(fileName, sourceFolder);
            if(imageFileFormat(file)!="JPEG"_) continue; // Only JPEG images
            auto tags = parseExifTags(file);
            insert(move(fileName), {tags.keys, apply(tags.values, [](const Variant& o){return str(o);})});
        }
        for(auto& tags: values) {
            tags.filter([this](const string& key, const string&) { return !ref<string>{
                            "Exif.Photo.FocalLength"_,
                            "Exif.Photo.FNumber"_,
                            "Exif.Photo.ExposureBiasValue"_,
                            "Exif.Photo.ISOSpeedRatings"_,
                            "Exif.Photo.ExposureTime"_ }.contains(key);
            });
            replace(tags.keys, "Exif.Photo.FocalLength"_, "Focal"_);
            replace(tags.keys, "Exif.Photo.FNumber"_, "Aperture"_);
            replace(tags.keys, "Exif.Photo.ExposureBiasValue"_, "Bias"_);
            replace(tags.keys, "Exif.Photo.ISOSpeedRatings"_, "Gain"_);
            replace(tags.keys, "Exif.Photo.ExposureTime"_, "Time"_);
        }
    }

    ImageSourceRGB scaledRGB(string imageName) {
        // Caches conversion from sRGB JPEGs to raw (mmap'able) linear float images
        String id = section(imageName,'.')+".rgb"_;
        Image source;
        if(!at(imageName).contains("Size"_)) {
            if(!source) source = decodeImage(Map(imageName, sourceFolder)); //FIXME: get size without decoding
            at(imageName).insert(String("Size"_), str(source.size));
        }
        if(skipCache || !existsFile(id, cacheFolder)) { //FIXME: automatic invalidation
            log_("("_+imageName+" "_);
            if(!source) source = decodeImage(Map(imageName, sourceFolder));
            log_("->"_+id+") "_);
            ImageTargetRGB target (id, cacheFolder, imageSize);
            target.pixels.clear();
            resize(share(target), source);
        }
        return ImageSourceRGB(id, cacheFolder, imageSize);
    }

    /// Loads linear float image
    ImageSource image(string imageName, Component component) {
        // Caches conversion from sRGB JPEGs to raw (mmap'able) linear float images
        String id = section(imageName,'.')+"."_+str(component);
        if(skipCache || !existsFile(id, cacheFolder)) { //FIXME: automatic invalidation
            log_("("_+section(imageName,'.')+" "_);
            ImageSourceRGB source = scaledRGB(imageName); // Faster but slightly inaccurate
            log_("->"_+id+") "_);
            ImageTarget target (id, cacheFolder, imageSize);
            assert_(imageSize==source.Image::size);
            linear(share(target), source, component);
        }
        return ImageSource(id, cacheFolder, imageSize);
    }
};

struct Filter {
    /// Returns filtered image
    virtual ImageSource image(ImageFolder& folder, string imageName, Component component) const abstract;
};
