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
        //map<String, array<String>> occurences;
        for(String& fileName: fileNames) {
            Map file = Map(fileName, sourceFolder);
            if(imageFileFormat(file)!="JPEG"_) continue; // Only JPEG images
            auto tags = parseExifTags(file);
            //if(tags.at("Exif.Photo.FNumber"_).real() != 6.3) continue; // Only same aperture //FIXME: -> DustRemoval
            //TODO: if(source.size != imageSize) { log("Warning: inconsistent source image size"); continue; }
            //for(auto tag: tags) occurences[tag.key] += str(tag.value); // Aggregates occurences for each tag
            insert(move(fileName), {tags.keys, apply(tags.values, [](const Variant& o){return str(o);})});
        }
        //occurences.filter( [this](const string&, const ref<String>& values) { return values.size == 1 || values.size == size(); } );
        //log(strn(occurences));
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
        //log(strn(*this));
    }

    //Image sRGB(string imageName) const { return decodeImage(Map(imageName, sourceFolder)); }
    ImageSourceRGB scaledRGB(string imageName) const {
        // Caches conversion from sRGB JPEGs to raw (mmap'able) linear float images
        String id = section(imageName,'.')+".rgb"_;
        if(skipCache || !existsFile(id, cacheFolder)) { //FIXME: automatic invalidation
            log_("("_+imageName+" "_);
            Image source = decodeImage(Map(imageName, sourceFolder));
            log_("->"_+id+") "_);
            ImageTargetRGB target (id, cacheFolder, imageSize);
            target.pixels.clear();
            resize(share(target), source);
            assert_(max(target.pixels));
        }
        return ImageSourceRGB(id, cacheFolder, imageSize);
    }

    /// Loads linear float image
    ImageSource image(string imageName, Component component) const {
        // Caches conversion from sRGB JPEGs to raw (mmap'able) linear float images
        String id = section(imageName,'.')+"."_+str(component);
        if(skipCache || !existsFile(id, cacheFolder)) { //FIXME: automatic invalidation
            log_("("_+section(imageName,'.')+" "_);
            //Image source = sRGB(imageName) //Slower but exact
            ImageSourceRGB source = scaledRGB(imageName); // Faster but slightly inaccurate
            assert_(max(source.pixels), component);
            log_("->"_+id+") "_);
            ImageTarget target (id, cacheFolder, imageSize);
            if(imageSize==source.Image::size) linear(share(target), source, component);
            else error("Slow"); //resize(share(target), linear(source, component));
            assert_(max(target.pixels), component);
        }
        return ImageSource(id, cacheFolder, imageSize);
    }
};

struct Filter {
    /// Returns filtered image
    virtual ImageSource image(const ImageFolder& folder, string imageName, Component component) const abstract;
};
