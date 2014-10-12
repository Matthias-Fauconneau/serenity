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
    Target& resize(int2 size) {
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
                int64 sourceTime, int2 sizeHint = 0, string version = __DATE__ " " __TIME__) {
    Folder cache(operation, folder, true);
    removeIfExisting(name, cache);
    auto files = filter(cache.list(Files), [&](string fileName){ return section(fileName,'.',0,-2) != name; });
    for(string file: files) {
        int64 cacheTime = File(file, cache, ::Flags(ReadWrite)).modifiedTime();
        int2 size = fromInt2(section(file,'.',-2,-1));
        if(size>int2(0) && sourceTime < cacheTime && (version ? parseDate(version)*1000000000l < cacheTime : true)) {
            if(size==sizeHint) return Source<T>(file, cache);
        } else { // Removes any invalidated files (of any size)
            // Safeguards
            assert_(find(cache.name(),"/Pictures/"_));
            assert_(!file.contains('/'));
            remove(file, cache);
        }
    }
    Target<T> target(name, cache);
    String oldName = target.fileName();
    generate(target);
    assert_(target.size>int2(0));
    assert_(!existsFile(target.fileName(), cache), cache.name(), files, target.fileName(), sizeHint);
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
    virtual size_t count() const abstract;
    virtual int2 maximumSize() const abstract;
    virtual String name(size_t index) const abstract;
    virtual int64 time(size_t index) const abstract;
    virtual int2 size(size_t index) const abstract;
    virtual const map<String, String>& properties(size_t index) const abstract;
    virtual SourceImage image(size_t /*index*/, uint /*component*/, int2 unused hint=0) const { error("Unimplemented"); }
    virtual SourceImageRGB image(size_t /*index*/, int2 unused hint = 0) const { error("Unimplemented"); }
};

/*// Fits size to hint
inline int2 fit(int2 size, int2 hint) {
    if(!hint) return size; // No hint
    if(hint >= size) return size; // Larger hint
    return hint.x*size.y < hint.y*size.x ?
                int2(hint.x, ((hint.x+size.x-1)/size.x)*size.y) : // Fits width
                int2(((hint.y+size.y-1)/size.y)*size.x, hint.y) ; // Fits height
}*/
