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
struct ImageFolder {
    Folder sourceFolder;
    Folder cacheFolder {".cache"_, sourceFolder, true};

    ImageFolder(Folder&& sourceFolder) : sourceFolder(move(sourceFolder)) {}

    /// Lists matching images
    array<String> listImages() {
        array<String> imageNames;
        array<String> fileNames = sourceFolder.list(Files|Sorted);
        for(String& fileName: fileNames) {
            Map file = Map(fileName, sourceFolder);
            if(imageFileFormat(file)!="JPEG"_) continue; // Only JPEG images
            if(parseExifTags(file).at("Exif.Photo.FNumber"_).real() != 6.3) continue; // Only same aperture //FIXME: -> DustRemoval
            //TODO: if(source.size != imageSize) { log("Warning: inconsistent source image size"); continue; }
            imageNames << move(fileName);
        }
        return imageNames;
    }

    array<String> imageNames = listImages();
    const int2 imageSize = int2(4000, 3000)/4; //FIXME: = ::imageSize(readFile(imageNames.first()));

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
