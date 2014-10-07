#pragma once
#include "file.h"
#include "variant.h" // fromInt2
#include "image.h"
#include "function.h"
#include "time.h"
// -> cache.h

generic struct Target : T {
    Folder folder;
    String name;
    File file; // Keeps file descriptor open for file locking
    Map map;

    Target(string name, const Folder& folder) : folder("."_, folder), name(name), file(name, folder, ::Flags(ReadWrite|Create)) {}
    ~Target() { rename(name, name+"."_+strx(T::size), folder);/*FIXME: asserts unique TargetImage instance for this 'folder/name'*/ }
    const Target& resize(int2 size) {
        file.resize(size.y*size.x*sizeof(typename T::type));
        map = Map(file, Map::Write);
        (T&)*this = T(unsafeReference(cast<typename T::type>((Map&)*this)), size);
        return *this;
    }
};

generic struct Source : Map, T {
    using T::size;
    Source(){}
    Source(const string name, const Folder& folder) :
        Map(name, folder),
        T(unsafeReference(cast<typename T::type>((Map&)*this)), fromInt2(section(name,'.',-2,-1))) {}
};

generic Source<T> cache(string name, string operation, const Folder& folder, function<void(Target<T>&&)> generate,
                int64 sourceTime, string version = __TIMESTAMP__ ""_) {
    Folder cache(operation, folder);
    removeIfExisting(name, cache);
    File target;
    auto files = filter(cache.list(Files), [&](string fileName){ return !startsWith(fileName, name); });
    if(files) {
        assert_(files.size == 1);
        target = File(files[0], cache, ::Flags(ReadWrite));
        int64 cacheTime = target.modifiedTime();
        if(sourceTime < cacheTime && parseDate(version)*1000000000l < cacheTime) return Source<T>(name, cache);
    }
    log(name);
    generate(Target<T>(name, cache));
    return Source<T>(name, cache);
}

typedef Target<ImageF> TargetImage;
typedef Source<ImageF> SourceImage;
typedef Target<Image> TargetImageRGB;
typedef Source<Image> SourceImageRGB;

/// Collection of images
struct ImageSource {
    Folder folder;
    ImageSource(Folder&& folder) : folder(move(folder)) {}
    virtual size_t size() const abstract;
    virtual String name(size_t index) const abstract;
    virtual int64 time(size_t index) const abstract;
    virtual int2 size(size_t index) const abstract;
    virtual const map<String, String>& properties(size_t index) const abstract;
    virtual SourceImage image(size_t /*index*/, uint /*component*/) const abstract;
    virtual SourceImageRGB image(size_t /*index*/) const abstract;
};
