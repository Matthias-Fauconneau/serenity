/// \file cache.h Persistent cache
#pragma once
#include "file.h"
#include "function.h"
#include "time.h"

/// Caches results of \a generate on file system as folder/name.key
/// \note All results of a given \name and any \a key older than \a sourceTime or \a version are removed
inline File cacheFile(const Folder& folder, string name, string key, int64 sourceTime,
		   function<String(File&)> write, string version = __DATE__ " " __TIME__) {
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
	//log(prefix+folder.name(), name, key);
    prefix.append(' ');
	String newKey = write(file);
	if(newKey != key) {
		log("Expected", key, ", evaluated", newKey);
		key = move(newKey);
	}
    prefix.pop();
	//if(time>0.1) log(prefix+str(time));
	rename(name, name+'.'+key, folder);
    return file;
}

// Read/Write string cache
inline String cache(const Folder& folder, string name, string key, int64 sourceTime,
						 function<String()> evaluate, bool noCacheWrite = false, string version = __DATE__ " " __TIME__) {
	if(noCacheWrite) return evaluate();
	File file = cacheFile(folder, name, key, sourceTime, [&evaluate,&key](File& target) {
		target.write(evaluate()); target.seek(0); return String(key);
	}, version);
	return file.read(file.size());
}

// Mapped image cache
generic struct ImageMapSource : T {
    Map map;
    ImageMapSource() {}
	ImageMapSource(T&& image) : T(move(image)) {}
    ImageMapSource(const File& file, int2 size) : map(file) { (T&)*this = T(unsafeReference(cast<typename T::type>(map)), size, size.x); }
};
generic auto share(ref<ImageMapSource<T>> ref) -> buffer<decltype(share(ref[0]))> {
	return apply(ref,  [](const ImageMapSource<T>& x){ return share(x); });
}

/// Maps results to be generated or read from cache
template<Type T> ImageMapSource<T> cache(const Folder& folder, string name, int2 size, int64 sourceTime,
										 function<void(T&)> evaluate, bool noCacheWrite = false, string version = __DATE__ " " __TIME__) {
	if(noCacheWrite) { T target(size); evaluate(target); return move(target); }
	return {cacheFile(folder, name, strx(size), sourceTime, [size,&evaluate](File& file) {
        file.resize(size.y*size.x*sizeof(typename T::type));
        Map map(file, Map::Write);
		T image(unsafeReference(cast<typename T::type>(map)), size, size.x);
		evaluate(image);
		if(image.size != size) {
			error(image.size);
			assert_(image.size < size);
			file.resize(image.size.y*image.size.x*sizeof(typename T::type));
		}
		return strx(size);
    }, version), size};
}
