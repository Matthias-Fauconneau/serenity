#pragma once
/// \file file.h Unix stream I/O and file system abstraction (Handle, Folder, Stream, Socket, File, Device, Map)
#include "array.h"

/// Linux error code names
enum class LinuxError { Interrupted=4 };
constexpr string linuxErrors[] = {"OK", "PERM", "NOENT", "SRCH", "INTR", "IO", "NXIO", "TOOBIG", "NOEXEC", "BADF", "CHILD", "AGAIN", "NOMEM", "ACCES", "FAULT", "NOTBLK", "BUSY", "EXIST", "XDEV", "NODEV", "NOTDIR", "ISDIR", "INVAL", "NFILE", "MFILE", "NOTTY", "TXTBSY", "FBIG", "NOSPC", "SPIPE", "ROFS", "MLINK", "PIPE", "DOM", "RANGE", "DEADLK", "NAMETOOLONG", "NOLCK", "NOSYS", "NOTEMPTY", "LOOP", "WOULDBLOCK", "NOMSG", "IDRM", "CHRNG", "L2NSYNC", "L3HLT", "L3RST", "LNRNG", "UNATCH", "NOCSI", "L2HLT", "BADE", "BADR", "XFULL", "NOANO", "BADRQC", "BADSLT", "DEADLOCK", "EBFONT", "NOSTR", "NODATA", "TIME", "NOSR", "NONET", "NOPKG", "REMOTE", "NOLINK", "ADV", "SRMNT", "COMM", "PROTO", "MULTIHO", "DOTDOT", "BADMSG", "OVERFLOW", "NOTUNIQ", "BADFD", "REMCHG", "LIBACC", "LIBBAD", "LIBSCN", "LIBMAX", "LIBEXEC", "ILSEQ", "RESTART", "STRPIPE", "USERS", "NOTSOCK", "DESTADDRREQ", "MSGSIZE", "PROTOTYPE", "NOPROTOOPT", "PROTONOSUPPORT", "SOCKTNOSUPPORT", "OPNOTSUPP", "PFNOSUPPORT", "AFNOSUPPORT", "ADDRINUSE", "ADDRNOTAVAIL", "NETDOWN", "NETUNREACH", "NETRESET", "CONNABORTED", "CONNRESET", "NOBUFS", "ISCONN", "NOTCONN", "SHUTDOWN", "TOOMANYREFS", "TIMEDOUT", "CONNREFUSED", "HOSTDOWN", "HOSTUNREACH", "ALREADY", "INPROGRESS", "STALE", "UCLEAN", "NOTNAM", "NAVAIL", "ISNAM", "REMOTEIO", "DQUOT", "NOMEDIUM", "MEDIUMTYPE", "CANCELED", "NOKEY", "KEYEXPIRED", "KEYREVOKED", "KEYREJECTED", "OWNERDEAD", "NOTRECOVERABLE", "RFKILL", "HWPOISON"};
extern "C" int* __errno_location() noexcept __attribute((const));

/// Aborts if \a expr is negative and logs corresponding error code
#define check(expr, message...) ({ auto e = expr; if(long(e)<0 && size_t(-long(e)) < ref<string>(linuxErrors).size) error(#expr, linuxErrors[*__errno_location()], ##message); e; })
/// Aborts if \a expr is negative and logs corresponding error code (unused result)
#define check_(expr, message...) ({ auto e=expr; if(long(e)<0 && size_t(-long(e)) < ref<string>(linuxErrors).size) error(#expr, linuxErrors[*__errno_location()], ##message); })
/// Does not abort if \a expr is negative and logs corresponding error code (unused result)
#define check__(expr, message...) ({ auto e=expr; if(long(e)<0 && size_t(-long(e)) < ref<string>(linuxErrors).size) log(#expr, linuxErrors[*__errno_location()], ##message); })

/// Unix file descriptor
struct Handle {
    handle<int> fd;

    Handle():fd(0){}
    Handle(int fd):fd(fd){}
    default_move(Handle);
    ~Handle() { close(); }
    explicit operator bool() const { return fd; }
    /// Closes file descriptor
    void close();
    /// Returns file descriptor name
    String name() const;
};

struct Folder;
/// Returns a file descriptor to the current working directory
const Folder& currentWorkingDirectory();
/// Returns a file descriptor to the root folder
const Folder& root();

enum { Drives=1<<0, Devices=1<<1, Folders=1<<2, Files=1<<3, Recursive=1<<4, Sorted=1<<5, Hidden=1<<6 };
struct Folder : Handle {
    Folder() : Handle(0) {}
    /// Opens \a folderPath
    Folder(const string folderPath, const Folder& at=currentWorkingDirectory(), bool create=false);
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
bool existsFolder(const string folder, const Folder& at=currentWorkingDirectory());

/// Handle to an Unix I/O stream
struct Stream : Handle {
    Stream(){}
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
    void write(const ref<byte> buffer);
};

/// Handle to a socket
struct Socket : Stream {
    enum {PF_LOCAL=1, PF_INET};
    enum {SOCK_STREAM=1, SOCK_DGRAM, O_NONBLOCK=04000};
    Socket(int domain, int type);
};

enum Flags {ReadOnly, WriteOnly, ReadWrite, Create=0100, Truncate=01000, Append=02000, NonBlocking=04000, Descriptor=010000000};
enum class FileType { Folder=0040000, Device=0020000, Drive=0060000, File=0100000 };
/// Handle to a file
struct File : Stream {
    File(){}
    File(int fd):Stream(fd){}
    /// Opens \a path
    File(const string path, const Folder& at=currentWorkingDirectory(), Flags flags=ReadOnly);
    /// Returns file properties
    struct stat stat() const;
    /// Returns file type
    FileType type() const;
    /// Returns file size
    int64 size() const;
    /// Returns the last access Unix timestamp (in nanoseconds)
    int64 accessTime() const;
    /// Returns the last modified Unix timestamp (in nanoseconds)
    int64 modifiedTime() const;

    /// Resizes file
    const File& resize(int64 size) const;
    /// Seeks to \a index
    void seek(int index);
};
/// Returns whether \a path exists (as a file or a folder)
bool existsFile(const string path, const Folder& at=currentWorkingDirectory());
/// Returns whether \a path is writable (as a file or a folder)
bool writableFile(const string path, const Folder& at);
/// Reads whole \a file content
buffer<byte> readFile(const string path, const Folder& at=currentWorkingDirectory());
/// Writes \a content into \a file (overwrites any existing file)
void writeFile(const string path, const ref<byte> content, const Folder& at=currentWorkingDirectory());

template<uint major, uint minor> struct IO { static constexpr uint io = major<<8 | minor; };
template<uint major, uint minor, Type T> struct IOW { typedef T Args; static constexpr uint iow = 1<<30 | sizeof(T)<<16 | major<<8 | minor; };
template<uint major, uint minor, Type T> struct IOR { typedef T Args; static constexpr uint ior = 2<<30 | sizeof(T)<<16 | major<<8 | minor; };
template<uint major, uint minor, Type T> struct IOWR { typedef T Args; static constexpr uint iowr = 3u<<30 | sizeof(T)<<16 | major<<8 | minor; };
/// Handle to a device
struct Device : File {
    Device(){}
    Device(const string path, const Folder& at=root(), Flags flags=ReadWrite):File(path, at, flags){}
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
struct Map : mref<byte> {
    enum Prot {Read=1, Write=2};
    enum Flags {Shared=1, Private=2, Anonymous=0x20, Populate=0x8000};

    Map(){}
    Map(Map&& o) : mref(o) { o.data=0, o.size=0; }
    Map& operator=(Map&& o) { this->~Map(); new (this) Map(::move(o)); return *this; }

    explicit Map(const File& file, Prot prot=Read, Flags flags=Shared);
    explicit Map(const string path, const Folder& at=root(), Prot prot=Read) : Map(File(path,at),prot) {}
    Map(uint fd, uint offset, uint size, Prot prot, Flags flags=Shared);
    ~Map();

    /// Locks memory map in RAM
    void lock(uint size=-1) const;
    /// Unmaps memory map
    void unmap();

    using mref::data;
    using mref::size;
};

/// Renames a file
void rename(const Folder& oldAt, const string oldName, const Folder& newAt, const string newName);
/// Renames a file
void rename(const string oldName, const string newName, const Folder& at=currentWorkingDirectory());
/// Removes file
void remove(const string name, const Folder& at=currentWorkingDirectory());
/// Removes file if it exists
void removeIfExisting(const string name, const Folder& at=currentWorkingDirectory());

/// Creates a symbolic link to \a target at \a name, replacing any existing files or links
void symlink(const string target,const string name, const Folder& at=currentWorkingDirectory());
/// Sets the last modified time for \a path to current time
void touchFile(const string path, const Folder& at=currentWorkingDirectory(), bool setModified=false);
/// Copies a file replacing any existing files or links
void copy(const Folder& oldAt, const string oldName, const Folder& newAt, const string newName);

/// Returns available free space in bytes for the file system containing \a file
int64 available(const Handle& file);
/// Returns available free space in bytes for the file system containing \a path
int64 available(const string path, const Folder& at=root());

/// Returns capacity in bytes for the file system containing \a file
int64 capacity(const Handle& file);
/// Returns capacity in bytes for the file system containing \a path
int64 capacity(const string path, const Folder& at=root());
