#pragma once
/// \file file.h Unix stream I/O and file system abstraction (Handle, Folder, Stream, Socket, File, Device, Map)
#include "string.h"

/// Linux error code names
enum LinuxError { OK, Interrupted=-4, Again=-11, Busy=-16, Invalid=-22 };
constexpr string linuxErrors[] = {
 "OK", "PERM", "NOENT", "SRCH", "INTR", "IO", "NXIO", "TOOBIG", "NOEXEC", "BADF", "CHILD", "AGAIN", "NOMEM", "ACCES", "FAULT", "NOTBLK",
 "BUSY", "EXIST", "XDEV", "NODEV", "NOTDIR", "ISDIR", "INVAL", "NFILE", "MFILE", "NOTTY", "TXTBSY", "FBIG", "NOSPC", "SPIPE", "ROFS", "MLINK",
 "PIPE", "DOM", "RANGE", "DEADLK", "NAMETOOLONG", "NOLCK", "NOSYS", "NOTEMPTY", "LOOP", "WOULDBLOCK", "NOMSG", "IDRM", "CHRNG",
 "L2NSYNC", "L3HLT", "L3RST", "LNRNG", "UNATCH", "NOCSI", "L2HLT", "BADE", "BADR", "XFULL", "NOANO", "BADRQC", "BADSLT", "DEADLOCK",
 "EBFONT", "NOSTR", "NODATA", "TIME", "NOSR", "NONET", "NOPKG", "REMOTE", "NOLINK", "ADV", "SRMNT", "COMM", "PROTO", "MULTIHO",
 "DOTDOT", "BADMSG", "OVERFLOW", "NOTUNIQ", "BADFD", "REMCHG", "LIBACC", "LIBBAD", "LIBSCN", "LIBMAX", "LIBEXEC", "ILSEQ", "RESTART",
 "STRPIPE", "USERS", "NOTSOCK", "DESTADDRREQ", "MSGSIZE", "PROTOTYPE", "NOPROTOOPT", "PROTONOSUPPORT", "SOCKTNOSUPPORT",
 "OPNOTSUPP", "PFNOSUPPORT", "AFNOSUPPORT", "ADDRINUSE", "ADDRNOTAVAIL", "NETDOWN", "NETUNREACH", "NETRESET",
 "CONNABORTED","CONNRESET", "NOBUFS", "ISCONN", "NOTCONN", "SHUTDOWN", "TOOMANYREFS", "TIMEDOUT", "CONNREFUSED",
 "HOSTDOWN","HOSTUNREACH","ALREADY", "INPROGRESS", "STALE", "UCLEAN", "NOTNAM", "NAVAIL", "ISNAM", "REMOTEIO", "DQUOT",
 "NOMEDIUM","MEDIUMTYPE", "CANCELED", "NOKEY", "KEYEXPIRED", "KEYREVOKED", "KEYREJECTED", "OWNERDEAD", "NOTRECOVERABLE"};
extern "C" /*const*/ int* __errno_location() noexcept;

/// Aborts if \a expr is negative and logs corresponding error code
#define check(expr, args...) ({ \
 auto e = expr; \
 if(long(e)<0 && size_t(-long(e)) < ref<string>(linuxErrors).size) error(#expr ""_, linuxErrors[*__errno_location()], ##args); \
 e; })

/// Unix file descriptor
struct Handle {
 handle<int> fd;

 Handle() : fd(0){}
 explicit Handle(int fd):fd(fd){}
 default_move(Handle);
 //Handle(Handle&& o) : fd(::move(o.fd)) {}
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

enum { Drives=1<<0, Devices=1<<1, Folders=1<<2, Files=1<<3, Recursive=1<<4, Sorted=1<<5, Hidden=1<<6 };
struct stat;
struct Folder : Handle {
 //using Handle::Handle;
 //Folder() : Handle(0) {}
 /// Opens \a folderPath
 Folder(string folderPath, const Folder& at=currentWorkingDirectory(), bool create=false);
 /// Returns folder properties
 stat properties() const;
 /// Returns the last access Unix timestamp (in nanoseconds)
 int64 accessTime() const;
 /// Returns the last modified Unix timestamp (in nanoseconds)
 int64 modifiedTime() const;
 /// Lists all files in this folder
 buffer<String> list(uint flags) const;
};
/// Returns whether this \a folder exists (as a folder)
bool existsFolder(const string folder, const Folder& at=currentWorkingDirectory());

/// Handle to an Unix I/O stream
struct Stream : Handle { //FIXME: overlaps with Data/BinaryData
 Stream(){}
 Stream(int fd) : Handle(fd) {}
 /// Reads up to \a size bytes into \a buffer
 int64 readUpTo(mref<byte> target);
 /// Reads exactly \a size bytes into \a buffer
 void read(mref<byte> target);
 /// Reads up to \a size bytes
 buffer<byte> readUpTo(size_t size);
 buffer<byte> readUpToLoop(size_t size);
 /// Reads exactly \a size bytes
 buffer<byte> read(size_t size);
 buffer<byte> readAll();
 /// Reads a raw value
 generic T read() { T t; read(mref<byte>((byte*)&t,sizeof(T))); return t; }
 /// Reads \a size raw values
 generic buffer<T> read(size_t size) {
  ::buffer<byte> buffer(size*sizeof(T));
  size_t offset=0; while(offset<buffer.size) offset+=readUpTo(buffer.slice(offset));
  assert(offset==buffer.size);
  return cast<T>(move(buffer));
 }
 /// Polls whether reading would block
 bool poll(int timeout=0);
 /// Writes \a buffer of \a size bytes
 size_t write(const byte* data, size_t size);
 /// Writes \a buffer
 size_t write(const ref<byte> buffer);
 /// Writes a raw value
 generic size_t writeRaw(T t) { return write((byte*)&t,sizeof(T)); }
};

enum Flags {ReadOnly, WriteOnly, ReadWrite, Create=0100, Truncate=01000, Append=02000, NonBlocking=04000, Descriptor=010000000};

/// Handle to a socket
struct Socket : Stream {
 enum {PF_LOCAL=1, PF_INET};
 enum {SOCK_STREAM=1, SOCK_DGRAM};
 Socket(int domain=PF_INET, int type=SOCK_STREAM);
};

/// Handle to a file
struct File : Stream {
 File(){}
 File(int fd):Stream(fd){}
 /// Opens \a path
 explicit File(string path, const Folder& at=currentWorkingDirectory(), Flags flags=ReadOnly,
               int permissions=0666);
 /// Returns file properties
 struct stat stat() const;
 /// Returns file type
 //enum FileType { Folder=0040000, Device=0020000, Drive=0060000, File=0100000 } type() const;
 /// Returns file size
 size_t size() const;
 /// Returns the last access Unix timestamp (in nanoseconds)
 int64 accessTime() const;
 /// Returns the last modified Unix timestamp (in nanoseconds)
 int64 modifiedTime() const;
 void touch(int64 time = 0);

 /// Resizes file
 const File& resize(size_t size);
 /// Seeks to \a index
 void seek(int index);
};
/// Returns whether \a path exists (as a file or a folder)
bool existsFile(const string path, const Folder& at=currentWorkingDirectory());
/// Returns whether \a path is writable (as a file or a folder)
bool writableFile(const string path, const Folder& at);
/// Reads whole \a file content
buffer<byte> readFile(const string path, const Folder& at=currentWorkingDirectory());
/// Writes \a content into \a file
int64 writeFile(const string path, const ref<byte> content, const Folder& at=currentWorkingDirectory(), bool overwrite=false);

template<uint major, uint minor> struct IO { static constexpr uint io = major<<8 | minor; };
template<uint major, uint minor, Type T> struct IOW { typedef T Args; static constexpr uint iow = 1<<30 | sizeof(T)<<16 | major<<8 | minor; };
template<uint major, uint minor, Type T> struct IOR { typedef T Args; static constexpr uint ior = 2u<<30 | sizeof(T)<<16 | major<<8 | minor; };
template<uint major, uint minor, Type T> struct IOWR { typedef T Args; static constexpr uint iowr = 3u<<30|sizeof(T)<<16|major<<8|minor; };
/// Handle to a device
struct Device : File {
 Device(){}
 Device(const string path, const Folder& at=currentWorkingDirectory(), Flags flags=ReadWrite) : File(path, at, flags){}
 /// Sends ioctl \a request with untyped \a arguments
 int ioctl(uint request, void* arguments, int pass=0);
 /// Sends ioctl request with neither input/outputs arguments
 template<Type IO> int io(int pass=0) { return ioctl(IO::io, 0, pass); }
 /// Sends ioctl request with \a input arguments
 template<Type IOW> int iow(const Type IOW::Args& input) { return ioctl(IOW::iow, (void*)&input); }
 /// Sends ioctl request with output arguments
 template<Type IOR> Type IOR::Args ior() { Type IOR::Args output; ioctl(IOR::ior, &output); return output; }
 /// Sends ioctl request with \a reference argument
 template<Type IOWR> int iowr(Type IOWR::Args& reference, int pass=0) { return ioctl(IOWR::iowr, &reference, pass); }
};

/// Managed memory mapping
struct Map : mref<byte> {
 using mref::data;
 using mref::size;

 enum Prot {Read=1, Write=2};
 enum Flags {Shared=1, Private=2, Anonymous=0x20, Populate=0x8000, HugeTLB=0x40000};

 Map(){}
 Map(Map&& o) : mref(o) { o.data=0; o.size=0; }
 Map& operator=(Map&& o) { this->~Map(); new (this) Map(::move(o)); return *this; }

 explicit Map(const File& file, Prot prot=Read, Flags flags=Shared);
 explicit Map(const string path, const Folder& at=currentWorkingDirectory(), Prot prot=Read, Flags flags=Shared) : Map(File(path,at), prot, flags) {}
 Map(uint fd, uint64 offset, uint64 size, Prot prot=Prot(Read|Write), Flags flags=Shared);
 ~Map();

 /// Locks memory map in RAM
 int lock(size_t size=~0) const;

 /// Unmaps memory map
 void unmap();
};

/// Renames a file
void rename(const Folder& oldAt, const string oldName, const Folder& newAt, const string newName);
/// Renames a file
void rename(const string oldName, const string newName, const Folder& at=currentWorkingDirectory());
/// Removes a file
void remove(const string name, const Folder& at=currentWorkingDirectory());
/// Removes a folder
void removeFolder(const string& name, const Folder& at);

/// Creates a symbolic link to \a target at \a name, replacing any existing files or links
void symlink(const string target,const string name, const Folder& at=currentWorkingDirectory());
/// Sets the last modified time for \a path to current time
void touchFile(const string path, const Folder& at=currentWorkingDirectory(), int64 time=0);
/// Copies a file replacing any existing files or links
void copy(const Folder& oldAt, const string oldName, const Folder& newAt, const string newName, bool overwrite=false);

void link(const Folder& oldAt, const string oldName, const Folder& newAt, const string newName);

/// Returns available free space in bytes for the file system containing \a file
int64 availableCapacity(const Handle& file);
/// Returns available free space in bytes for the file system containing \a path
int64 availableCapacity(const string path, const Folder& at=currentWorkingDirectory());

/// Returns capacity in bytes for the file system containing \a file
int64 capacity(const Handle& file);
/// Returns capacity in bytes for the file system containing \a path
int64 capacity(const string path, const Folder& at=currentWorkingDirectory());

/// Returns command line arguments
ref<string> arguments();
ref<string> cmdline();

/// Returns value for environment variable \a name
string environmentVariable(const string name, string value=""_);

/// Returns user informations
const string user();
const Folder& home(); //$HOME ?: pwuid->pw_dir
