/// \file cache.h Persistent cache
#pragma once
#include "file.h"
#include "function.h"
#include "time.h"

/// Caches results of \a generate on file system as folder/operation/name.key
/// \note All results of a given \name and any \a key older than \a sourceTime or \a version are removed
inline File cache(const Folder& folder, string operation, string name, string key, int64 sourceTime,
           function<void(File&)> write, string version = __DATE__ " " __TIME__) {
    Folder cache(operation, folder, true);
    auto files = filter(cache.list(Files), [&](string fileName){ return section(fileName,'.',0,-2) != name; });
    for(string fileName: files) {
        File file (fileName, cache);
        int64 cacheTime = file.modifiedTime();
        string fileKey = section(fileName,'.',-2,-1);
        if(fileKey && cacheTime > sourceTime && (version ? cacheTime > parseDate(version)*1000000000l : true)) {
            if(fileKey==key) return file;
        } else { // Removes any invalidated files (of any key)
            assert_(find(cache.name(),"/Pictures/"_) && !fileName.contains('/')); // Safeguards
            //log('-', fileName/*, Date(cacheTime), Date(sourceTime), Date(parseDate(version)*1000000000l)*/);
            remove(fileName, cache);
        }
    }
    File file(name, cache, ::Flags(ReadWrite|Create|Truncate));
    Time time;
    static String prefix;
    log(prefix+operation, name, key);
    prefix.append(' ');
    write(file);
    prefix.pop();
    if(time>0.1) log(prefix+str(time));
    rename(name, name+'.'+key, cache);
    return file;
}

// Read/Write value cache
template<Type T> T cache(const Folder& folder, string operation, string name, string key, int64 sourceTime,
                         function<T()> evaluate, string version = __DATE__ " " __TIME__) {
    return cache(folder, operation, name, key, sourceTime, [&evaluate](File& target) {
        target.writeRaw<T>(evaluate()); target.seek(0);
    }, version).template read<T>();
}

// Mapped image cache
generic struct ImageMapSource : T {
    Map map;
    ImageMapSource() {}
    ImageMapSource(T&& image) : T(move(image)) { assert_(T::capacity); /*Heap allocated image*/ }
    ImageMapSource(const File& file, int2 size) : map(file) { (T&)*this = T(unsafeReference(cast<typename T::type>(map)), size, size.x); }
};

/// Maps results to be generated or read from cache
template<Type T> ImageMapSource<T> cache(const Folder& folder, string operation, string name, int2 size, int64 sourceTime,
                                         function<void(const T&)> evaluate, string version = __DATE__ " " __TIME__) {
    return {cache(folder, operation, name, strx(size), sourceTime, [size,&evaluate](File& file) {
        file.resize(size.y*size.x*sizeof(typename T::type));
        Map map(file, Map::Write);
        evaluate(T(unsafeReference(cast<typename T::type>(map)), size, size.x));
    }, version), size};
}
