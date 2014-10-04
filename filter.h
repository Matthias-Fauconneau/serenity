#pragma once
#include "file.h"
#include "exif.h"
#include "jpeg.h"

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
    const int2 imageSize = int2(4000, 3000); //FIXME: = ::imageSize(readFile(imageNames.first()));

    /// Loads linear float image
    ImageSource image(string imageName) const {
        // Caches conversion from sRGB JPEGs to raw (mmap'able) linear float images
        string baseName = section(imageName,'.');
        if(/*1 ||*/ !existsFile(baseName, cacheFolder)) { //FIXME: automatic invalidation
            log_(imageName);
            Image source = decodeImage(Map(imageName, sourceFolder));

            log(" ->",baseName);
            ImageTarget target (baseName, cacheFolder, source.size);
            chunk_parallel(source.pixels.size, [&](uint, uint start, uint size) {
                for(uint index: range(start, start+size)) {
                    byte4 sRGB = source.pixels[index];
                    float b = sRGB_reverse[sRGB.b];
                    float g = sRGB_reverse[sRGB.g];
                    float r = sRGB_reverse[sRGB.r];
                    float intensity = (b+g+r)/3; // Assumes dust affects all components equally
                    target.pixels[index] = intensity;
                }
            });
            assert_(::sum(target.pixels));
        }
        return ImageSource(baseName, cacheFolder, imageSize);
    }
};

struct Filter {
    /// Returns filtered image
    virtual ImageSource image(const ImageFolder& folder, string imageName) const abstract;
};
