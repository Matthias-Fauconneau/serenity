#include "file.h"
#include "data.h"
#include "time.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <sys/sendfile.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <pwd.h>

// -- Handle

void Handle::close() { if(fd>0) ::close(fd); fd=0; }

String Handle::name() const {
 if(fd==AT_FDCWD) return "."__;
 static Folder procSelfFD("/proc/self/fd/");
 String s (256); s.size=check(readlinkat(procSelfFD.fd, strz(str((int)fd)), s.begin(), s.capacity), (int)fd);
 return s;
}

// -- Folder

const Folder& currentWorkingDirectory() { static const int cwd = AT_FDCWD; return (const Folder&)cwd; }

Folder::Folder(string folder, const Folder& at, bool create) {
 if(create && !existsFolder(folder,at)) {
  if(folder != "/"_ && folder.contains('/') && !existsFolder(section(folder,'/',0,-2), at))
   Folder(section(folder,'/',0,-2), at, true); // Recursively creates parents
  check(mkdirat(at.fd, strz(folder), 0777), at.name(), folder);
 }
 fd = check( openat(at.fd, strz(folder?:"."), O_RDONLY|O_DIRECTORY, 0), '\''+folder+'\'', at.name() );
}

struct stat Folder::properties() const { struct stat s; check( fstat(fd, &s) ); return s; }

int64 Folder::accessTime() const {
 struct stat s = Folder::properties();
 return s.st_atim.tv_sec*1000000000ull + s.st_atim.tv_nsec;
}

int64 Folder::modifiedTime() const {
 struct stat s = Folder::properties();
 return s.st_mtim.tv_sec*1000000000ull + s.st_mtim.tv_nsec;
}

static int getdents(int fd, void* entry, long size) { return syscall(SYS_getdents, fd, entry, size); }
struct dirent { long ino, off; short len; char name[]; };
enum { DT_UNKNOWN, DT_FIFO, DT_CHR, DT_DIR = 4, DT_BLK = 6, DT_REG = 8, DT_LNK = 10, DT_SOCK = 12, DT_WHT = 14 };
buffer<String> Folder::list(uint flags) const {
    Folder fd(".",*this);
    array<String> list (0x8000); byte buffer[0x8000*sizeof(dirent)];
    for(int size; (size=check(getdents(fd.fd,&buffer,sizeof(buffer))))>0;) {
        for(byte* i = buffer, *end = buffer+size; i < end; i += ((dirent*)i)->len) {
            const dirent& entry=*(dirent*)i;
            string name = str(entry.name);
            if(!(flags&Hidden) && name[0]=='.') continue;
            if(name=="." || name=="..") continue;
            int type = *((byte*)&entry + entry.len - 1);
            if((flags&Files && (type==DT_REG||type==DT_LNK||type==DT_UNKNOWN))
                    || (flags&Folders && type==DT_DIR)
                    || (flags&Devices && type==DT_CHR)
                    || (flags&Drives && type==DT_BLK) ) {
                if(flags&Sorted) list.insertSorted( copyRef(name) );
                else list.append( copyRef(name) );
            }
            if(flags&Recursive && type==DT_DIR) {
                for(const String& file: Folder(name,*this).list(flags)) {
                    if(flags&Sorted) list.insertSorted( name+'/'+file );
                    else list.append( name+'/'+file );
                }
            }
        }
    }
    return move(list);
}

bool existsFolder(const string folder, const Folder& at) { return Handle( openat(at.fd, strz(folder), O_RDONLY|O_DIRECTORY, 0) ).fd > 0; }

// -- Stream

int64 Stream::readUpTo(mref<byte> target) { return check( ::read(fd, target.begin(), target.size), (int)fd); }

void Stream::read(mref<byte> target) {
 int unused read=check( ::read(fd, target.begin(), target.size) ); assert(read==(int)target.size,"Expected", target.size, "got", read);
}

buffer<byte> Stream::readUpTo(size_t capacity) {
 buffer<byte> buffer(capacity);
 buffer.size = check( ::read(fd, (void*)buffer.data, capacity) );
 assert_(buffer.size < buffer.capacity, buffer.size, buffer.capacity);
 return buffer;
}

buffer<byte> Stream::readUpToLoop(size_t capacity) {
 buffer<byte> buffer(capacity);
 size_t offset=0; while(offset<capacity) {
  int size = check(::read(fd, buffer.begin()+offset, capacity-offset));
  if(!size) break;
  offset += size;
 }
 buffer.size = offset;
 return buffer;
}

buffer<byte> Stream::read(size_t size) {
 buffer<byte> buffer(size);
 size_t offset=0; while(offset<size) offset+=check(::read(fd, buffer.begin()+offset, size-offset));
 assert(offset==size);
 return buffer;
}

bool Stream::poll(int timeout) { assert(fd); pollfd pollfd{fd,POLLIN,0}; return ::poll(&pollfd,1,timeout)==1 && (pollfd.revents&POLLIN); }

buffer<byte> Stream::readAll() {
 array<byte> buffer;
 while(poll()) {
  buffer.reserve(buffer.size+(1<<20));
  int size = check(::read(fd, buffer.begin()+buffer.size, buffer.capacity-buffer.size));
  if(!size) break;
  buffer.size += size;
 }
 return move(buffer);
}

size_t Stream::write(const byte* data, size_t size) {
 assert(data); size_t offset=0; while(offset<size) offset+=check(::write(fd, data+offset, size-offset), name()); return offset;
}

size_t Stream::write(const ref<byte> buffer) { return write(buffer.data, buffer.size); }

Socket::Socket(int domain, int type):Stream(check(socket(domain,type|SOCK_CLOEXEC,0))){}

// -- File

static int open(const string path, const Folder& at, Flags flags, int permissions) {
 int fd = openat(at.fd, strz(path), flags, permissions);
 if(flags&NonBlocking && -*__errno_location()==int(LinuxError::Busy)) return 0;
 return check(fd, at.name(), path/*, at.list(Files)*/);
}
File::File(const string path, const Folder& at, Flags flags, int permissions)
 : Stream(open(path, at, flags, permissions)) {}

struct stat File::stat() const { struct stat stat; check( fstat(fd, &stat) ); return stat; }

//FileType File::type() const { return FileType(stat().st_mode&__S_IFMT); }

size_t File::size() const { return stat().st_size; }

int64 File::accessTime() const { struct stat stat = File::stat(); return stat.st_atim.tv_sec*1000000000ull + stat.st_atim.tv_nsec; }

int64 File::modifiedTime() const { struct stat stat = File::stat(); return stat.st_mtim.tv_sec*1000000000ull + stat.st_mtim.tv_nsec;  }

void File::touch(int64 time) {
 timespec times[]={{0,0}, {time/second,time?time%second:UTIME_NOW}};
 check(futimens(fd, times));
}

const File& File::resize(size_t size) { check(ftruncate(fd, size), fd.pointer, size); return *this; }

void File::seek(int index) { check(::lseek(fd,index,0)); }

bool existsFile(const string path, const Folder& at) { int fd = openat(at.fd, strz(path), /*O_PATH*/010000000, 0); if(fd>0) close(fd); return fd>0; }

bool writableFile(const string path, const Folder& at) {
 int fd = openat(at.fd, strz(path), O_WRONLY|O_NONBLOCK, 0); if(fd>0) close(fd); return fd>0;
}

buffer<byte> readFile(const string path, const Folder& at) { return File(path,at).readAll(); }

int64 writeFile(const string path, const ref<byte> content, const Folder& at, bool overwrite) {
 assert_(overwrite || !existsFile(path, at));
 return File(path,at,Flags(WriteOnly|Create|Truncate)).write(content);
}

// -- Device

int Device::ioctl(uint request, void* arguments, int pass) {
 int status = ::ioctl(fd, request, arguments);
 if(-*__errno_location()==pass) return status;
 return check(status, request>>30, (request>>16)&((1<<14)-1), (request>>8)&((1<<8)-1), request&((1<<8)-1));
}

// -- Map

Map::Map(const File& file, Prot prot, Flags flags) {
 size = file.size();
 data = size?(byte*)check(mmap(0,size,prot,flags,file.fd,0)):0;
}

Map::Map(uint fd, uint64 offset, uint64 size, Prot prot, Flags flags){
 this->size=size;
 data=(byte*)check(mmap(0,size, prot, flags, fd, offset), fd);
}

Map::~Map() { unmap(); }

int Map::lock(size_t size) const { return mlock(data, min<size_t>(this->size,size)); }

void Map::unmap() {
 if(data) munmap((void*)data, size);
 data=0, size=0;
}

// -- File system

void rename(const Folder& oldFolder, const string oldName, const Folder& newFolder, const string newName) {
 assert_(existsFile(oldName,oldFolder), oldFolder.name(), oldName, newName);
 assert_(!existsFile(newName,newFolder), oldName, newFolder.name(), newName);
 assert_(newName.size<0x100);
 check(renameat(oldFolder.fd, strz(oldName), newFolder.fd, strz(newName)));
}

void rename(const string oldName,const string newName, const Folder& at) {
 assert(oldName!=newName);
 rename(at, oldName, at, newName);
}


void remove(const string name, const Folder& at) { check( unlinkat(at.fd, strz(name), 0), name); }

void removeFolder(const string& name, const Folder& at) { check( unlinkat(at.fd, strz(name), AT_REMOVEDIR), name); }

void symlink(const string from,const string to, const Folder& at) {
 assert(from!=to);
 remove(from, at);
 check(symlinkat(strz(from), at.fd, strz(to)), from,"->",to);
}

void touchFile(const string path, const Folder& at, int64 time) {
 timespec times[]={{0,0}, {time/second,time?time%second:UTIME_NOW}};
 check(utimensat(at.fd, strz(path), times, 0), path);
}

void copy(const Folder& oldAt, const string oldName, const Folder& newAt, const string newName, bool overwrite) {
 //FIXME: preserve executable flag
 File oldFile(oldName, oldAt);
 assert_(overwrite || !existsFile(newName, newAt));
 File newFile(newName, newAt, Flags(WriteOnly|Create|Truncate), oldFile.stat().st_mode);
 for(size_t offset=0, size=oldFile.size(); offset<size;)
  offset+=check(sendfile(newFile.fd, oldFile.fd, (off_t*)offset, size-offset), (int)newFile.fd, (int)oldFile.fd, offset, size-offset, size);
 assert(newFile.size() == oldFile.size());
}

void link(const Folder& oldAt, const string oldName, const Folder& newAt, const string newName) {
 check(linkat(oldAt.fd, strz(oldName), newAt.fd, strz(newName), 0));
}


int64 availableCapacity(const Handle& file) { struct statvfs statvfs; check( fstatvfs(file.fd, &statvfs) ); return statvfs.f_bavail*statvfs.f_frsize; }

int64 availableCapacity(const string path, const Folder& at) { return availableCapacity(File(path,at)); }


int64 capacity(const Handle& file) { struct statvfs statvfs; check( fstatvfs(file.fd, &statvfs) ); return statvfs.f_blocks*statvfs.f_frsize; }

int64 capacity(const string path, const Folder& at) { return capacity(File(path,at)); }

string environmentVariable(const string name, string value) {
 static auto environ = File("/proc/self/environ").readUpTo/*<4096>*/(8192);
 for(TextData s(environ);s;) {
  string key=s.until('='); string value=s.until('\0');
  if(key==name) return value;
 }
 return value;
}

ref<string> cmdline() {
 static String cmdline = File("/proc/self/cmdline").readUpTo(512);
 assert(cmdline.size<4096);
 static array<string> arguments = split(cmdline,"\0");
 return arguments;
}
ref<string> arguments() { return cmdline().slice(1); }

const string user() { static string user = str((const char*)getpwuid(geteuid())->pw_name); return user; }
const Folder& home() { static Folder home(environmentVariable("HOME",str((const char*)getpwuid(geteuid())->pw_dir))); return home; }
