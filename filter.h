#pragma once
#include "file.h"
#include "exif.h"
#include "jpeg.h"

struct ImageTarget : Map, ImageF {
    using ImageF::size;
    Folder folder;
    String name;
    ImageTarget(string name, const Folder& folder, int2 size) :
        Map(File(name, folder, ::Flags(ReadWrite|Create)).resize(size.x*size.y*sizeof(float)), Map::Write),
        ImageF(unsafeReference(cast<float>((Map&)*this)), size),
        folder("."_, folder), name(name) {}
    ~ImageTarget() { rename(name, name+"."_+strx(size), folder);/*FIXME: asserts unique ImageTarget instance for this 'folder/name'*/ }
};

// -> variant.h
/// Parses 2 integers separated by 'x', ' ', or ',' to an \a int2
int2 fromInt2(TextData& s) {
    int x=s.integer(); // Assigns a single value to all components
    if(!s) return int2(x);
    s.whileAny("x, "_); int y=s.integer();
    assert_(!s); return int2(x,y);
}
/// Parses 2 integers separated by 'x', ' ', or ',' to an \a int2
inline int2 fromInt2(string str) { TextData s(str); return fromInt2(s); }

struct ImageSource : ImageF {
    Map map;
    ImageSource(const string& name, const Folder& folder) {
        auto match = filter(folder.list(Files), [&](string fileName){ return !startsWith(fileName, name); });
        assert_(match.size == 1);
        map = Map(match[0], folder);
        (ImageF&)*this = ImageF(unsafeReference(cast<float>(map)), fromInt2(section(match[0],'.',-2,-1)));
    }
};

struct ImageTargetRGB : Map, Image {
    using Image::size;
    ImageTargetRGB(const string& path, const Folder& at, int2 size) :
        Map(File(path, at, ::Flags(ReadWrite|Create)).resize(size.x*size.y*sizeof(byte4)), Map::Write),
        Image(unsafeReference(cast<byte4>((Map&)*this)), size) {}
};

struct ImageSourceRGB : Map, Image {
    using Image::size;
    ImageSourceRGB() {}
    ImageSourceRGB(const string& path, const Folder& at, int2 size) :
        Map (path, at),
        Image(unsafeReference(cast<byte4>((Map&)*this)), size) {}
};

/// Collection of images
struct ImageFolder : map<String, map<String, String>> {
    Folder sourceFolder;
    Folder cacheFolder {".cache"_, sourceFolder, true};
    int2 imageSize;

    ImageFolder(Folder&& folder, function<bool(const String& name, const map<String, String>& tags)> predicate={}) : sourceFolder(move(folder)) {
        {// Lists images, evaluates target image size and load tags
            int2 maximumImageSize = 0;

            for(String& fileName: sourceFolder.list(Files|Sorted)) {
                Map file = Map(fileName, sourceFolder);
                if(imageFileFormat(file)!="JPEG"_) continue; // Only JPEG images
                int2 imageSize = ::imageSize(file);
                maximumImageSize = max(maximumImageSize, imageSize);

                auto tags = parseExifTags(file);
                tags.insert(String("Size"_), str(imageSize));
                insert(move(fileName), {tags.keys, apply(tags.values, [](const Variant& o){return str(o);})});


            }
            // Sets target image size
            imageSize = maximumImageSize / 4;
        }

        {// Filters useless tags and renames to short names
            map<string, array<string>> occurences;
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

                for(auto tag: tags) occurences[tag.key] += tag.value; // Aggregates occurences for each tag
            }
            for(auto tag: occurences) log(tag.key,':', sort(tag.value));
        }

        // Applies application specific filter
       if(predicate) filter(predicate);
    }

    ImageSourceRGB scaledRGB(string imageName) {
        // Caches conversion from sRGB JPEGs to raw (mmap'able) linear float images
        String id = section(imageName,'.')+".rgb"_;

        if(!existsFile(id, cacheFolder)) { //FIXME: automatic invalidation
            log_(section(imageName,'.'));
            Image source = decodeImage(Map(imageName, sourceFolder));
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
        if(!existsFile(id, cacheFolder)) { //FIXME: automatic invalidation
            ImageSourceRGB source = scaledRGB(imageName); // Faster but slightly inaccurate
            ImageTarget target (id, cacheFolder, imageSize);
            assert_(imageSize==source.Image::size);
            linear(share(target), source, component);
        }
        return ImageSource(id, cacheFolder);
    }
};

struct Filter {
    /// Returns filtered image
    virtual ImageSource image(ImageFolder& folder, string imageName, Component component) const abstract;
};
