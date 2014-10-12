/// \file cache.h Persistent cache
#pragma once
#include "file.h"
#include "function.h"

/// Caches results of \a generate on file system as folder/operation/name.key
/// \note All results of a given \name and any \a key older than \a sourceTime or \a version are removed
template<Type Source, Type Target> Source cache(const Folder& folder, string operation, string name, string key, int64 sourceTime,
                                                function<void(Target&)> generate, string version = __DATE__ " " __TIME__) {
    Folder cache(operation, folder, true);
    removeIfExisting(name, cache);
    auto files = filter(cache.list(Files), [&](string fileName){ return section(fileName,'.',0,-2) != name; });
    for(string file: files) {
        int64 cacheTime = File(file, cache, ::Flags(ReadWrite)).modifiedTime();
        string fileKey = section(file,'.',-2,-1);
        if(fileKey && sourceTime < cacheTime && (version ? parseDate(version)*1000000000l < cacheTime : true)) {
            if(fileKey==key) return Source(cache, file);
        } else { // Removes any invalidated files (of any key)
            // Safeguards
            assert_(find(cache.name(),"/Pictures/"_));
            assert_(!file.contains('/'));
            remove(file, cache);
        }
    }
    String fileName;
    {Target target(cache, name);
        generate(target);
        fileName=target.fileName();
    }
    return Source(cache, fileName);
}

// Read/Write cache

generic struct WriteTarget {
    Folder folder;
    String name;
    String properties;

    WriteTarget(const Folder& folder, string name) : folder(".", folder), name(name) {}
    String fileName() const { return name+'.'+properties; }
};

generic struct ReadSource : T {
    ReadSource(const Folder& folder, string name) : T(*(const T*)readFile(name, folder).data) {}
};

template<Type T> T cache(const Folder& folder, string operation, string name, string key, int64 sourceTime,
                         function<T()> generate, string version = __DATE__ " " __TIME__) {
    return cache<ReadSource<T>,WriteTarget<T>>(folder, operation, name, key, sourceTime, [&](WriteTarget<T>& target) {
        T value = generate();
        target.properties = String(key);
        writeFile(target.fileName(), raw(value), folder);
    }, version);
}

// Mapped cache

struct MapTarget {
    Folder folder;
    String name;
    String properties;

    File file;
    Map map;

    MapTarget(const Folder& folder, string name) : folder(".", folder), name(name), file(name, folder, ::Flags(ReadWrite|Create)) {}
    String fileName() const { return name+'.'+properties; }
    ~MapTarget() { rename(name, fileName(), folder); }
};

//TODO: define generic mapped cache
/*generic struct MapSource : Map, T {
    using T::size;
    MapSource(){}
    MapSource(const string name, const Folder& folder) :
        Map(name, folder),
        T(unsafeReference(cast<typename T::type>((Map&)*this)), fromInt2(section(name,'.',-2,-1))) {}
};

/// Maps results to be generated or read from cache
template<Type T> MapSource<T> cache(string operation, string name, const Folder& folder, function<void(T&)> generate,
                int64 sourceTime, int2 requestedSize = 0, string version = __DATE__ " " __TIME__) {
    return cache(name, operation, folder, generate, sourceTime, requestedSize, version);
}*/

// Image cache

generic struct ImageMapTarget : MapTarget, T {
    int2 size;

    using MapTarget::MapTarget;

    const T& resize(int2 size) {
        this->size = size;
        properties = strx(size);
        file.resize(size.y*size.x*sizeof(typename T::type));
        map = Map(file, Map::Write);
        (T&)*this = T(unsafeReference(cast<typename T::type>(map)), size);
        return *this;
    }
};

generic struct ImageMapSource : Map, T {
    using T::size;
    ImageMapSource(){}
    ImageMapSource(const Folder& folder, const string fileName) :
        Map(fileName, folder),
        T(unsafeReference(cast<typename T::type>((Map&)*this)), fromInt2(section(fileName,'.',-2,-1))) {}
};

/// Maps results to be generated or read from cache
template<Type T> ImageMapSource<T> cache(const Folder& folder, string operation, string name, int2 size, int64 sourceTime,
                                         function<void(ImageMapTarget<T>&)> generate, string version = __DATE__ " " __TIME__) {
    return cache<ImageMapSource<T>,ImageMapTarget<T>>(folder, operation, name, strx(size), sourceTime, generate, version);
}
