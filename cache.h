/// \file cache.h Persistent cache
#pragma once
#include "file.h"
#include "function.h"
#include "time.h"

/// Caches results of \a write on file system as folder/name.key
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
	write(file);
	rename(name, name+'.'+key, folder);
    return file;
}

// Read/Write string cache
inline String cache(const Folder& folder, string name, string key, int64 sourceTime,
						 function<String()> evaluate, bool noCacheWrite = false, string version = __DATE__ " " __TIME__) {
	if(noCacheWrite) return evaluate();
	File file = cacheFile(folder, name, key, sourceTime, [&evaluate,&key](File& target) { target.write(evaluate()); target.seek(0); }, version);
	return file.read(file.size());
}

// Mapped image cache
generic struct ImageMapSource : T {
    Map map;
	ImageMapSource(T&& image=T(), Map&& map=Map()) : T(move(image)), map(move(map)) {}
};
generic auto share(ref<ImageMapSource<T>> ref) -> buffer<decltype(share(ref[0]))> {
	return apply(ref,  [](const ImageMapSource<T>& x){ return share(x); });
}

/// Maps results to be generated or read from cache
template<Type T> ImageMapSource<T> cache(const Folder& folder, string name, int2 size, int64 sourceTime,
										 function<void(const T&)> evaluate, bool noCacheWrite = false, string version = __DATE__ " " __TIME__) {
	if(noCacheWrite) { T target(size); evaluate(target); return move(target); }
	File file = cacheFile(folder, name, strx(size), sourceTime, [size,&evaluate](File& file) {
        file.resize(size.y*size.x*sizeof(typename T::type));
        Map map(file, Map::Write);
		T image(unsafeReference(cast<typename T::type>(map)), size, size.x);
		evaluate(image);
	}, version);
	Map map (file);
	T t (unsafeReference(cast<typename T::type>(map)), size, size.x);
	return ImageMapSource<T>(move(t), move(map));
}

/// Caches results of \a write on file system as folder/name.key/
/// \note All results of a given \name and any \a key older than \a sourceTime or \a version are removed
inline Folder cacheFolder(const Folder& parent, string name, string key, int64 sourceTime,
		   function<void(const Folder&)> write, string version = __DATE__ " " __TIME__) {
	auto folders = filter(parent.list(Folders), [&](string folderName){ return section(folderName,'.',0,-2) != name; });
	for(string folderName: folders) {
		Folder folder (folderName, parent);
		int64 cacheTime = folder.modifiedTime();
		string folderKey = section(folderName,'.',-2,-1);
		if(folderKey && cacheTime > sourceTime && (version ? cacheTime > parseDate(version)*1000000000l : true)) {
			if(folderKey==key) return folder;
		} else { // Removes any invalidated folders (of any key)
			assert_(find(parent.name(),"/Pictures/"_) && !folderName.contains('/')); // Safeguards
			//log('-', folderName/*, Date(cacheTime), Date(sourceTime), Date(parseDate(version)*1000000000l)*/);
			for(string file: folder.list(Files)) remove(file, folder);
			removeFolder(folderName, parent);
		}
	}
	Folder folder(name, parent, true);
	//FIXME: clear any existing files
	write(folder);
	rename(name, name+'.'+key, parent);
	return folder;
}

/// Maps results to be generated or read from cache
// TODO: recursion
template<Type T> array<ImageMapSource<T>> cacheGroup(const Folder& parent, string name, int2 size, size_t groupSize, int64 sourceTime,
										 function<void(ref<T>)> evaluate, bool noCacheWrite = false, string version = __DATE__ " " __TIME__) {
	//if(noCacheWrite) { array<ImageMapSource<T>> target(size); evaluate(target); return move(target); }
	assert_(!noCacheWrite);
	Folder folder = cacheFolder(parent, name, strx(size), sourceTime, [groupSize, size, &evaluate](const Folder& folder) {
		array<Map> maps = apply(groupSize, [&folder, size](size_t index) {
				File file(str(index), folder, Flags(ReadWrite|Create|Truncate));
				file.resize(size.y*size.x*sizeof(typename T::type));
				return Map(file, Map::Write);
			});
		array<T> images = apply(maps, [size](const Map& map) { return T(unsafeReference(cast<typename T::type>(map)), size, size.x); });
		evaluate(images);
	}, version);
	return apply(groupSize, [&folder, size](size_t index) {
		Map map(str(index), folder);
		T t (unsafeReference(cast<typename T::type>(map)), size, size.x);
		return ImageMapSource<T>(move(t), move(map));
	});
}
