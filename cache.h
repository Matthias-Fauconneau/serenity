/// \file cache.h Persistent cache
#pragma once
#include "file.h"
#include "function.h"
#include "time.h"

/// Caches results of \a generate on file system as folder/name.key
/// \note All results of a given \name and any \a key older than \a sourceTime or \a version are removed
inline File cacheFile(const Folder& folder, string name, string key, int64 sourceTime,
           function<void(File&)> write, string version = __DATE__ " " __TIME__) {
	auto files = filter(folder.list(Files), [&](string fileName){ return section(fileName,'.',0,-2) != name; });
    for(string fileName: files) {
		File file (fileName, folder);
        int64 cacheTime = file.modifiedTime();
        string fileKey = section(fileName,'.',-2,-1);
        if(fileKey && cacheTime > sourceTime && (version ? cacheTime > parseDate(version)*1000000000l : true)) {
            if(fileKey==key) return file;
        } else { // Removes any invalidated files (of any key)
			assert_(find(folder.name(),"/Pictures/"_) && !fileName.contains('/')); // Safeguards
            //log('-', fileName/*, Date(cacheTime), Date(sourceTime), Date(parseDate(version)*1000000000l)*/);
			remove(fileName, folder);
        }
    }
	File file(name, folder, ::Flags(ReadWrite|Create|Truncate));
    Time time;
    static String prefix;
	log(prefix+folder.name(), name, key);
    prefix.append(' ');
    write(file);
    prefix.pop();
    if(time>0.1) log(prefix+str(time));
	rename(name, name+'.'+key, folder);
    return file;
}

// Read/Write string cache
inline String cache(const Folder& folder, string name, string key, int64 sourceTime,
						 function<String()> evaluate, bool noCacheWrite = false, string version = __DATE__ " " __TIME__) {
	if(noCacheWrite) return evaluate();
	File file = cacheFile(folder, name, key, sourceTime, [&evaluate](File& target) {
		target.write(evaluate()); target.seek(0);
	}, version);
	return file.read(file.size());
}

// Read/Write raw value cache
template<Type T> T cache(const Folder& folder, string name, string key, int64 sourceTime,
						 function<T()> evaluate, bool noCacheWrite = false, string version = __DATE__ " " __TIME__) {
	if(noCacheWrite) return evaluate();
	return cacheFile(folder, name, key, sourceTime, [&evaluate](File& target) {
        target.writeRaw<T>(evaluate()); target.seek(0);
    }, version).template read<T>();
}

// Read/Write array value cache (FIXME: buffer)
template<Type T> array<T> cache(const Folder& folder, string name, string key, int64 sourceTime,
						 function<array<T>()> evaluate, bool noCacheWrite = false, string version = __DATE__ " " __TIME__) {
	if(noCacheWrite) return evaluate();
	File file = cacheFile(folder, name, key, sourceTime, [&evaluate](File& target) {
		target.write(cast<byte>(evaluate())); target.seek(0);
	}, version);
	assert_( file.size()%sizeof(T) == 0 );
	file.template read<T>(file.size()/sizeof(T));
}

// Mapped image cache
generic struct ImageMapSource : T {
    Map map;
    ImageMapSource() {}
    ImageMapSource(T&& image) : T(move(image)) { assert_(T::capacity); /*Heap allocated image*/ }
    ImageMapSource(const File& file, int2 size) : map(file) { (T&)*this = T(unsafeReference(cast<typename T::type>(map)), size, size.x); }
};
generic auto share(ref<ImageMapSource<T>> ref) -> buffer<decltype(share(ref[0]))> {
	return apply(ref,  [](const ImageMapSource<T>& x){ return share(x); });
}

/// Maps results to be generated or read from cache
template<Type T> ImageMapSource<T> cache(const Folder& folder, string name, int2 size, int64 sourceTime,
										 function<void(const T&)> evaluate, bool noCacheWrite = false, string version = __DATE__ " " __TIME__) {
	if(noCacheWrite) { T target(size); evaluate(target); return move(target); }
	return {cacheFile(folder, name, strx(size), sourceTime, [size,&evaluate](File& file) {
        file.resize(size.y*size.x*sizeof(typename T::type));
        Map map(file, Map::Write);
        evaluate(T(unsafeReference(cast<typename T::type>(map)), size, size.x));
    }, version), size};
}
