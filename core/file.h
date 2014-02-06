#pragma once
/// \file file.h Unix stream I/O and file system abstraction (Handle, Folder, Stream, Socket, File, Device, Map)
#include "array.h"

/// Unix file descriptor
struct Handle {
    handle<int> fd;
    Handle(int fd):fd(fd){}
    default_move(Handle);
    ~Handle();
    explicit operator bool() const { return fd; }
    /// Returns file descriptor name
    String name() const;
};

struct Folder;
/// Returns a file descriptor to the current working directory
const Folder& currentWorkingDirectory();
/// Returns a file descriptor to the root folder
const Folder& root();

enum { Devices=1<<0, Files=1<<1, Folders=1<<2, Recursive=1<<3, Sorted=1<<4, Hidden=1<<5 };
struct Folder : Handle {
    /// Opens \a folderPath
    Folder(const string& folderPath, const Folder& at=root(), bool create=false);
    /// Returns folder properties
    struct stat stat() const;
    /// Returns the last access Unix timestamp (in nanoseconds)
    int64 accessTime() const;
    /// Returns the last modified Unix timestamp (in nanoseconds)
    int64 modifiedTime() const;
    /// Lists all files in this folder
    array<String> list(uint flags) const;
};
/// Returns whether this \a folder exists (as a folder)
bool existsFolder(const string& folder, const Folder& at=root());

/// Handle to an Unix I/O stream
struct Stream : Handle {
    Stream(int fd):Handle(fd){}
    /// Reads exactly \a size bytes into \a buffer
    void read(byte* buffer, size_t size);
    /// Reads up to \a size bytes into \a buffer
    int64 readUpTo(byte* buffer, size_t size);
    /// Reads exactly \a size bytes
    buffer<byte> read(size_t size);
    /// Reads up to \a size bytes
    buffer<byte> readUpTo(size_t size);
    /// Reads a raw value
    generic T read() { T t; read((byte*)&t,sizeof(T)); return t; }
    /// Reads \a size raw values
    generic buffer<T> read(size_t size) {
        ::buffer<T> buffer(size); size_t byteSize=size*sizeof(T);
        size_t offset=0; while(offset<byteSize) offset+=readUpTo((byte*)buffer.begin()+offset, byteSize-offset);
        assert(offset==byteSize);
        return buffer;
    }
    /// Polls whether reading would block
    bool poll(int timeout=0);
    /// Writes \a buffer of \a size bytes
    void write(const byte* data, size_t size);
    /// Writes \a buffer
    void write(const ref<byte>& buffer);
};

/// Handle to a socket
struct Socket : Stream {
    enum {PF_LOCAL=1, PF_INET};
    enum {SOCK_STREAM=1, SOCK_DGRAM, O_NONBLOCK=04000};
    Socket(int domain, int type);
};

enum Flags {ReadOnly, WriteOnly, ReadWrite, Create=0100, Truncate=01000, Append=02000, Asynchronous=020000};
/// Handle to a file
struct File : Stream {
    File(int fd):Stream(fd){}
    /// Opens \a path
    /// If read only, fails if not existing
    /// If write only, fails if existing
    File(const string& path, const Folder& at=root(), Flags flags=ReadOnly);
    /// Returns file properties
    struct stat stat() const;
    /// Returns file size
    int64 size() const;
    /// Returns the last access Unix timestamp (in nanoseconds)
    int64 accessTime() const;
    /// Returns the last modified Unix timestamp (in nanoseconds)
    int64 modifiedTime() const;
    /// Returns file name
    String name() const;

    /// Resizes file
    void resize(int64 size);
    /// Seeks to \a index
    void seek(int index);
};
/// Returns whether \a path exists (as a file or a folder)
bool existsFile(const string& path, const Folder& at=root());
/// Reads whole \a file content
buffer<byte> readFile(const string& path, const Folder& at=root());
/// Writes \a content into \a file (overwrites any existing file)
void writeFile(const string& path, const ref<byte>& content, const Folder& at=root());

template<uint major, uint minor> struct IO { static constexpr uint io = major<<8 | minor; };
template<uint major, uint minor, Type T> struct IOW { typedef T Args; static constexpr uint iow = 1<<30 | sizeof(T)<<16 | major<<8 | minor; };
template<uint major, uint minor, Type T> struct IOR { typedef T Args; static constexpr uint ior = 2<<30 | sizeof(T)<<16 | major<<8 | minor; };
template<uint major, uint minor, Type T> struct IOWR { typedef T Args; static constexpr uint iowr = 3u<<30 | sizeof(T)<<16 | major<<8 | minor; };
/// Handle to a device
struct Device : File {
    Device(const string& path, const Folder& at=root(), Flags flags=ReadWrite):File(path, at, flags){}
    /// Sends ioctl \a request with untyped \a arguments
    int ioctl(uint request, void* arguments);
    /// Sends ioctl request with neither input/outputs arguments
    template<Type IO> int io() { return ioctl(IO::io, 0); }
    /// Sends ioctl request with \a input arguments
    template<Type IOW> int iow(const typename IOW::Args& input) { return ioctl(IOW::iow, &input); }
    /// Sends ioctl request with output arguments
    template<Type IOR> typename IOR::Args ior() { typename IOR::Args output; ioctl(IOR::ior, &output); return output; }
    /// Sends ioctl request with \a reference argument
    template<Type IOWR> int iowr(typename IOWR::Args& reference) { return ioctl(IOWR::iowr, &reference); }
};

/// Managed memory mapping
struct Map {
    enum Prot {Read=1, Write=2};
    enum Flags {Shared=1, Private=2, Anonymous=0x20, Populate=0x8000};

    Map(){}
    explicit Map(const File& file, Prot prot=Read, Flags flags=Shared);
    explicit Map(const string& path, const Folder& at=root(), Prot prot=Read):Map(File(path,at),prot){}
    Map(uint fd, uint offset, uint size, Prot prot, Flags flags=Shared);
    default_move(Map);
    ~Map();

    explicit operator bool() const { return data; }
    operator ref<byte>() const { return ref<byte>(data, size); }

    /// Locks memory map in RAM
    void lock(uint size=-1) const;
    /// Unmaps memory map
    void unmap();

    handle<byte*> data;
    size_t size=0;
};

/// Renames a file replacing any existing files or links
void rename(const Folder& oldAt, const string& oldName, const Folder& newAt, const string& newName);
/// Renames a file replacing any existing files or links
void rename(const string& oldName, const string& newName, const Folder& at=root());
/// Removes file
void remove(const string& name, const Folder& at=root());
/// Removes folder
void remove(const Folder& folder);
/// Removes folder
void removeFolder(const string& name, const Folder& at=root());
/// Removes file or folder
void removeFileOrFolder(const string& name, const Folder& at);
/// Creates a symbolic link to \a target at \a name, replacing any existing files or links
void symlink(const string& target,const string& name, const Folder& at=root());
/// Sets the last modified time for \a path to current time
void touchFile(const string& path, const Folder& at=root(), bool setModified=false);
/// Copies a file replacing any existing files or links
void copy(const Folder& oldAt, const string& oldName, const Folder& newAt, const string& newName);

/// Returns available free space in bytes for the file system containing \a file
int64 available(const Handle& file);
/// Returns available free space in bytes for the file system containing \a path
int64 available(const string& path, const Folder& at=root());

/// Returns capacity in bytes for the file system containing \a file
int64 capacity(const Handle& file);
/// Returns capacity in bytes for the file system containing \a path
int64 capacity(const string& path, const Folder& at=root());
