#pragma once
/// \file file.h Unix stream I/O and file system abstraction (Handle, Folder, Stream, Socket, File, Device, Map)
#include "array.h"

/// Unix file descriptor
struct Handle {
    no_copy(Handle);
    int fd;
    Handle(int fd):fd(fd){}
    move_operator(Handle):fd(o.fd){ o.fd=0; }
    /// Closes the descriptor
    ~Handle();
    explicit operator bool() { return fd; }
};

struct Folder;
/// Returns a file descriptor to the current working directory
const Folder& cwd();
/// Returns a file descriptor to the root folder
const Folder& root();

enum { Files=1<<0, Folders=1<<1, Recursive=1<<2 };
struct Folder : Handle {
    /// Opens \a folder
    Folder(const ref<byte>& folder, const Folder& at=root(), bool create=false);
    /// Lists all files in \a folder
    array<string> list(uint flags);
};
/// Returns whether this \a folder exists (as a folder)
bool existsFolder(const ref<byte>& folder, const Folder& at=root());

#if NOLIBC
enum { POLLIN = 1, POLLOUT=4, POLLERR=8, POLLHUP = 16, POLLNVAL=32, IDLE=64 };
struct pollfd { int fd; short events, revents; };
#else
enum { IDLE=64 };
#include <poll.h>
#endif

/// Handle to an Unix I/O stream
struct Stream : Handle {
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
        for(uint i=0;i<byteSize;) i+=readUpTo((byte*)buffer.data()+i, byteSize-i);
        return buffer;
    }
    /// Polls whether reading would block
    bool poll(int timeout=0);
    /// Writes \a buffer
    void write(const ref<byte>& buffer);
};

/// Handle to a socket
struct Socket : Stream {
    Socket(Handle&& fd):Stream(move(fd)){}
    enum {PF_LOCAL=1, PF_INET};
    enum {SOCK_STREAM=1, SOCK_DGRAM, O_NONBLOCK=04000};
    Socket(int domain, int type);
};

enum {ReadOnly, WriteOnly, ReadWrite, Create=0100, Truncate=01000, Append=02000, Asynchronous=020000};
/// Handle to a file
struct File : Stream {
    File(int fd):Stream(fd){}
    /// Opens \a file
    /// If read only, fails if not existing
    /// If write only, fails if existing
    File(const ref<byte>& file, const Folder& at=root(), uint flags=ReadOnly);
    /// Returns file size
    int size() const;
    /// Seeks to \a index
    //void seek(int index);
};
/// Returns whether \a file exists (as a file)
bool existsFile(const ref<byte>& file, const Folder& at=root());
/// Reads whole \a file content
array<byte> readFile(const ref<byte>& file, const Folder& at=root());
/// Writes \a content into \a file (overwrites any existing file)
void writeFile(const ref<byte>& file, const ref<byte>& content, const Folder& at=root());

template<uint major, uint minor> struct IO { static constexpr uint io = major<<8 | minor; };
template<uint major, uint minor, class T> struct IOW { typedef T Type; static constexpr uint iow = 1<<30 | sizeof(T)<<16 | major<<8 | minor; };
template<uint major, uint minor, class T> struct IOR { typedef T Type; static constexpr uint ior = 2<<30 | sizeof(T)<<16 | major<<8 | minor; };
template<uint major, uint minor, class T> struct IOWR { typedef T Type; static constexpr uint iowr = 3u<<30 | sizeof(T)<<16 | major<<8 | minor; };
/// Handle to a device
struct Device : File {
    Device(const ref<byte>& file, int flags=ReadWrite):File(file,root(),flags){}
    /// Sends ioctl \a request with untyped \a arguments
    int ioctl(uint request, void* arguments);
    /// Sends ioctl request with neither input/outputs arguments
    template<class IO> int io() { return ioctl(IO::io, 0); }
    /// Sends ioctl request with \a input arguments
    template<class IOW> int iow(const typename IOW::Type& input) { return ioctl(IOW::iow, &input); }
    /// Sends ioctl request with output arguments
    template<class IOR> typename IOR::Type ior() { typename IOR::Type output; ioctl(IOR::ior, &output); return output; }
    /// Sends ioctl request with \a reference argument
    template<class IOWR> int iowr(typename IOWR::Type& reference) { return ioctl(IOWR::iowr, &reference); }
};

/// Managed memory mapping
struct Map : ref<byte> {
    no_copy(Map);
    Map(){}
    Map(const File& file);
    Map(const ref<byte>& file, const Folder& at=root()):Map(File(file,at)){}
    enum {Read=1, Write=2}; enum {Shared=1, Private=2, Anonymous=32};
    Map(uint fd, uint offset, uint size, uint prot, uint flags=Shared);
    move_operator(Map):ref<byte>(o.data,o.size){o.data=0,o.size=0;}
    ~Map();
    /// Locks memory map in RAM
    void lock(uint size=-1) const;
};

/// Creates a symbolic link to \a target at \a name, replacing any existing files or links
void symlink(const ref<byte>& target,const ref<byte>& name, const Folder& at=root());
/// Returns the last modified time for \a path
long modifiedTime(const ref<byte>& path, const Folder& at=root());
/// Sets the last modified time for \a path to current time
void touchFile(const ref<byte>& path, const Folder& at=root());
