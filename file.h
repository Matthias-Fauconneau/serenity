#pragma once
#include "array.h"

/// Returns a file descriptor to the root folder
int root();

/// Reads exactly \a size bytes from \a fd
array<byte> read(int fd, uint size);
/// Reads up to \a capacity bytes from \a fd
array<byte> readUpTo(int fd, uint capacity);

/// File is a file descriptor which close itself in the destructor
struct File {
    no_copy(File)
    int fd;
    explicit File(int fd):fd(fd){}
    File(File&& o) { fd=o.fd; o.fd=0; }
    ~File();
    operator int() { return fd; }
};

/// Returns wether \a file exists (as a file)
bool existsFile(const ref<byte>& file, int at=root());
/// Opens \a file for reading, fails if not existing
File openFile(const ref<byte>& file, int at=root());
/// Reads \a file content into an heap buffer
array<byte> readFile(const ref<byte>& file, int at=root());

struct Map {
    no_copy(Map)
    const byte* data=0; uint size=0;
    Map(){}
    Map(const byte* data, uint size):data(data),size(size){}
    Map(Map&& o):data(o.data),size(o.size){o.data=0,o.size=0;}
    Map& operator=(Map&& o){this->~Map();data=o.data,size=o.size;o.data=0,o.size=0;return*this;}
    ~Map();
    /// Returns a reference to the map, valid only while the map exists.
    operator ref<byte>() { return ref<byte>(data,size); } //TODO: escape analysis
    explicit operator bool() { return data && size; }
};

/// Maps \a file as read-only memory pages
Map mapFile(const ref<byte>& file, int at=root());
/// Maps \a fd as read-only memory pages
Map mapFile(int fd);

/// Opens \a file for writing
/// \note if \a overwrite is set, any existing file will be truncated
File createFile(const ref<byte>& file, int at=root(), bool overwrite=false);
/// Opens \a file for writing, append if existing
File appendFile(const ref<byte>& file, int at=root());
/// Writes \a content into \a file
/// \note if \a overwrite is set, any existing file will be replaced
void writeFile(const ref<byte>& file, const ref<byte>& content, int at=root(), bool overwrite=true);

// File system

/// Returns whether \a folder exists (as a folder)
bool existsFolder(const ref<byte>& folder, int at=root());
/// Opens \a folder
int openFolder(const ref<byte>& folder, int at=root(), bool create=false);
/// Creates a new \a folder
void createFolder(const ref<byte>& folder, int at=root());
/// Returns whether \a path is a folder
bool isFolder(const ref<byte>& path, int at=root());

/// Creates a symbolic link to \a target at \a name, replacing any existing files or links
void symlink(const ref<byte>& target,const ref<byte>& name, int at=root());
/// Returns the last modified time for \a path
long modifiedTime(const ref<byte>& path, int at=root());
/// Sets the last modified time for \a path to current time
void touchFile(const ref<byte>& path, int at=root());

enum Flags { Recursive=1, Sort=2, Folders=4, Files=8 }; inline Flags operator |(Flags a, Flags b) { return Flags(int(a)|int(b)); }
array<string> listFiles(const ref<byte>& folder, Flags flags, int at=root());
