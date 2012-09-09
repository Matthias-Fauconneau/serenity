#pragma once
#include "array.h"

/// Handle is a Unix file descriptor
struct Handle {
    no_copy(Handle)
    int fd;
    Handle(int fd):fd(fd){}
    Handle(Handle&& o):fd(o.fd){ o.fd=0; }
    Handle& operator=(Handle&& o) { this->~Handle(); fd=o.fd; o.fd=0; return *this; }
    /// Closes the descriptor
    ~Handle();
    explicit operator bool() { return fd; }
};

struct Folder;
///// Returns a file descriptor to the root folder
const Folder& root();

struct Folder : Handle {
    /// Opens \a folder
    Folder(const ref<byte>& folder, const Folder& at=root(), bool create=false);
};
/// Returns whether this \a folder exists (as a folder)
bool existsFolder(const ref<byte>& folder, const Folder& at=root());

/// Stream is an handle to an Unix I/O stream
struct Stream : Handle {
    Stream(int fd):Handle(fd){}
    Stream(Handle&& fd):Handle(move(fd)){}
    /// Reads exactly \a size bytes into \a buffer
    void read(void* buffer, uint size);
    /// Reads up to \a size bytes into \a buffer
    int readUpTo(void* buffer, uint size);
    /// Reads exactly \a size bytes
    array<byte> read(uint size);
    /// Reads up to \a size bytes
    array<byte> readUpTo(uint size);
    /// Reads a raw value
    template<class T> T read() { T t; read((byte*)&t,sizeof(T)); return t; }
    /// Reads \a size raw values
    template<class T> array<T> read(uint size) {
        array<T> buffer(size); buffer.setSize(size); uint byteSize=size*sizeof(T);
        for(uint i=0;i<byteSize;) i+=readUpTo(buffer.data()+i, byteSize-i);
        return buffer;
    }
    /// Writes \a buffer
    void write(const ref<byte>& buffer);
    /// Sends \a request with \a arguments
    int ioctl(uint request, void* arguments);
};

struct Socket : Stream {
    Socket(int domain, int type);
};

struct File : Stream {
    File(int fd):Stream(fd){}
    enum Flags {ReadOnly, WriteOnly, ReadWrite, Create=0100, Truncate=01000, Append=02000};
    /// Opens \a file
    /// If read only, fails if not existing
    /// If write only, fails if existing
    File(const ref<byte>& file, const Folder& at=root(), Flags flags=ReadOnly);
    /// Returns file size
    int size() const;
    /// Seeks to \a index
    void seek(int index);
};
inline File::Flags operator |(File::Flags a, File::Flags b) { return File::Flags(int(a)|int(b)); }
/// Returns whether \a file exists (as a file)
bool existsFile(const ref<byte>& file, const Folder& at=root());
/// Reads whole \a file content
array<byte> readFile(const ref<byte>& file, const Folder& at=root());
/// Writes \a content into \a file (overwrites any existing file)
void writeFile(const ref<byte>& file, const ref<byte>& content, const Folder& at=root());

typedef File Device;
constexpr uint IO(uint major, uint minor) { return major<<8 | minor; }
template<class T> constexpr uint IOW(uint major, uint minor) { return 1<<30 | sizeof(T)<<16 | major<<8 | minor; }
template<class T> constexpr uint IOR(uint major, uint minor) { return 2<<30 | sizeof(T)<<16 | major<<8 | minor; }
template<class T> constexpr uint IOWR(uint major, uint minor) { return 3<<30 | sizeof(T)<<16 | major<<8 | minor; }

struct Map : ref<byte> {
    no_copy(Map)
    Map(){}
    Map(const File& file);
    Map(const ref<byte>& file, const Folder& at=root()):Map(File(file,at)){}
    Map(Map&& o):ref<byte>(o.data,o.size){o.data=0,o.size=0;}
    Map& operator=(Map&& o){this->~Map();data=o.data,size=o.size;o.data=0,o.size=0;return*this;}
    ~Map();
    explicit operator bool() { return data && size; }
    /// Locks memory map in RAM
    void lock(uint size) const;
};

/// Creates a symbolic link to \a target at \a name, replacing any existing files or links
void symlink(const ref<byte>& target,const ref<byte>& name, const Folder& at=root());
/// Returns the last modified time for \a path
long modifiedTime(const ref<byte>& path, const Folder& at=root());
/// Sets the last modified time for \a path to current time
void touchFile(const ref<byte>& path, const Folder& at=root());

enum ListFlags { Recursive=1, Sort=2, Folders=4, Files=8 }; inline ListFlags operator |(ListFlags a, ListFlags b) { return ListFlags(int(a)|int(b)); }
array<string> listFiles(const ref<byte>& folder, ListFlags flags, const Folder& at=root());
