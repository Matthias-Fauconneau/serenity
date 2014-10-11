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

    Target(string name, const Folder& folder) : folder(".", folder), name(name), file(fileName(), folder, ::Flags(ReadWrite|Create)) {}
    String fileName() const { return name+'.'+strx(T::size); }
    const Target& resize(int2 size) {
        file.resize(size.y*size.x*sizeof(typename T::type));
        map = Map(file, Map::Write);
        (T&)*this = T(unsafeReference(cast<typename T::type>(map)), size);
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

generic Source<T> cache(string name, string operation, const Folder& folder, function<void(Target<T>&)> generate,
                int64 unused sourceTime, string unused version = __TIMESTAMP__) {
    Folder cache(operation, folder, true);
    removeIfExisting(name, cache);
    auto files = filter(cache.list(Files), [&](string fileName){ return !startsWith(fileName, name); });
    if(files) {
        assert_(files.size == 1);
        int64 cacheTime = File(files[0], cache, ::Flags(ReadWrite)).modifiedTime();
        int2 size = fromInt2(section(files[0],'.',-2,-1));
        if(size>int2(0) && sourceTime < cacheTime && parseDate(version)*1000000000l < cacheTime) return Source<T>(files[0], cache);
        else {
            // Safeguards
            assert_(find(cache.name(),"/Pictures/"_));
            assert_(find(cache.name(),".0"_) || find(cache.name(),".1"_) || find(cache.name(),".2"_) || find(cache.name(),".sRGB"_));
            assert_(!files[0].contains('/'));
            remove(files[0], cache);
        }
    }
    Target<T> target(name, cache);
    String oldName = target.fileName();
    generate(target);
    assert_(target.size>int2(0));
    rename(oldName, target.fileName(), cache);
    return Source<T>(target.fileName(), cache);
}

typedef Target<ImageF> TargetImage;
typedef Source<ImageF> SourceImage;
typedef Target<Image> TargetImageRGB;
typedef Source<Image> SourceImageRGB;

/// Collection of images
struct ImageSource {
    Folder folder;
    ImageSource(Folder&& folder) : folder(move(folder)) {}
    virtual String name() const abstract;
    virtual size_t size() const abstract;
    virtual String name(size_t index) const abstract;
    virtual int64 time(size_t index) const abstract;
    virtual int2 size(size_t index) const abstract;
    virtual const map<String, String>& properties(size_t index) const abstract;
    virtual SourceImage image(size_t /*index*/, uint /*component*/) const abstract;
    virtual SourceImageRGB image(size_t /*index*/) const abstract;
};
