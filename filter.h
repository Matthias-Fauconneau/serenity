#pragma once
#include "file.h"
#include "exif.h"
#include "jpeg.h"

// -> variant.h
/// Parses 2 integers separated by 'x', ' ', or ',' to an \a int2
int2 fromInt2(TextData& s) {
    int x = s.integer(); // Assigns a single value to all components
    if(!s) return int2(x);
    s.whileAny("x, "_); int y=s.integer();
    assert_(!s); return int2(x,y);
}
/// Parses 2 integers separated by 'x', ' ', or ',' to an \a int2
inline int2 fromInt2(string str) { TextData s(str); return fromInt2(s); }

/// Returns the file starting with name in folder
String fileStartingWith(const string& name, const Folder& folder) {
    auto match = filter(folder.list(Files), [&](string fileName){ return !startsWith(fileName, name); });
    assert_(match.size == 1);
    return move(match[0]);
}

bool existsFileStartingWith(const string& name, const Folder& folder) {
    return filter(folder.list(Files), [&](string fileName){ return !startsWith(fileName, name); }).size == 1;
}

generic struct Target : Map, T {
    using T::size;
    Folder folder;
    String name;
    Target(string name, const Folder& folder, int2 size) :
        Map(File(name, folder, ::Flags(ReadWrite|Create)).resize(size.y*size.x*sizeof(typename T::type)), Map::Write),
        T(unsafeReference(cast<typename T::type>((Map&)*this)), size),
        folder("."_, folder), name(name) {}
    ~Target() { rename(name, name+"."_+strx(size), folder);/*FIXME: asserts unique ImageTarget instance for this 'folder/name'*/ }
};

generic struct Source : Map, T {
    using T::size;
    Source(){}
    Source(const string& name, const Folder& folder) :
        Map(fileStartingWith(name, folder), folder),
        T(unsafeReference(cast<typename T::type>((Map&)*this)), fromInt2(section(Map::name,'.',-2,-1))) {}
};

typedef Target<ImageF> ImageTarget;
typedef Source<ImageF> ImageSource;
typedef Target<Image> ImageTargetRGB;
typedef Source<Image> ImageSourceRGB;

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
        removeIfExisting(id, cacheFolder);
        if(!existsFileStartingWith(id, cacheFolder)) { //FIXME: automatic invalidation
            log_(section(imageName,'.'));
            Image source = decodeImage(Map(imageName, sourceFolder));
            ImageTargetRGB target (id, cacheFolder, imageSize);
            target.buffer::clear();
            resize(share(target), source);
        }
        return ImageSourceRGB(id, cacheFolder);
    }

    /// Loads linear float image
    ImageSource image(string imageName, Component component) {
        // Caches conversion from sRGB JPEGs to raw (mmap'able) linear float images
        String id = section(imageName,'.')+"."_+str(component);
        removeIfExisting(id, cacheFolder);
        if(!existsFileStartingWith(id, cacheFolder)) { //FIXME: automatic invalidation
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
