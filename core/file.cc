#include "file.h"
#include "linux.h"
#include "string.h"

#include <sys/syscall.h>
static int getdents(int fd, void* entry, long size) { return syscall(SYS_getdents, fd, entry, size); }
struct dirent { long ino, off; short len; char name[]; };
enum {DT_DIR=4, DT_REG=8};

#include <stdio.h> // renameat
#include <sys/sendfile.h>
#include <sys/statvfs.h>

// Handle
Handle::~Handle() { if(fd>0) close(fd); }
String Handle::name() const { if(fd==AT_FDCWD) return String("."_); String s(256); s.size=check(readlink(strz("/proc/self/fd/"_+str((int)fd)), s.begin(), s.capacity), (int)fd); return s; }

// Folder
const Folder& currentWorkingDirectory() { static const int cwd = AT_FDCWD; return (const Folder&)cwd; }
const Folder& root() { static const Folder root("/"_,currentWorkingDirectory()); return root; }
Folder::Folder(const string& folder, const Folder& at, bool create):Handle(0){
    if(create && !existsFolder(folder,at)) {
        assert_(isASCII(folder));
        check_(mkdirat(at.fd, strz(folder), 0777), at.name(), folder);
    }
    fd=check(openat(at.fd, strz(folder?:"."_), O_RDONLY|O_DIRECTORY, 0), "'"_+folder+"'"_);
}
struct stat Folder::stat() const { struct stat stat; check_( fstat(fd, &stat) ); return stat; }
int64 Folder::accessTime() const { struct stat stat = Folder::stat(); return stat.st_atim.tv_sec*1000000000ul + stat.st_atim.tv_nsec; }
int64 Folder::modifiedTime() const { struct stat stat = Folder::stat(); return stat.st_mtim.tv_sec*1000000000ul + stat.st_mtim.tv_nsec;  }
array<String> Folder::list(uint flags) const {
    Folder fd(""_,*this);
    array<String> list; byte buffer[0x1000];
    for(int size;(size=check(getdents(fd.fd,&buffer,sizeof(buffer))))>0;) {
        for(byte* i=buffer,*end=buffer+size;i<end;i+=((dirent*)i)->len) { const dirent& entry=*(dirent*)i;
            string name = str(entry.name);
            assert(name);
            if(!(flags&Hidden) && name[0]=='.') continue;
            if(name=="."_||name==".."_) continue;
            int type = *((byte*)&entry + entry.len - 1);
            if((type==DT_DIR && flags&Folders) || (type==DT_REG && flags&Files)) { if(flags&Sorted) list.insertSorted(String(name)); else list << String(name); }
            if(type==DT_DIR && flags&Recursive) for(const String& file: Folder(name,*this).list(flags)) { if(flags&Sorted) list.insertSorted(name+"/"_+file); else list << name+"/"_+file; }
        }
    }
    return list;
}
bool existsFolder(const string& folder, const Folder& at) { return Handle( openat(at.fd, strz(folder), O_RDONLY|O_DIRECTORY, 0) ).fd > 0; }

// Stream
void Stream::read(void* buffer, size_t size) { int unused read=check( ::read(fd,buffer,size) ); assert(read==(int)size); }
int64 Stream::readUpTo(void* buffer, size_t size) { return check( ::read(fd, buffer, size), (int)fd, buffer, size); }
buffer<byte> Stream::read(size_t size) {
    buffer<byte> buffer(size);
    size_t offset=0; for(; offset<size;) offset+=check(::read(fd, buffer.begin()+offset, size-offset));
    assert(offset==size);
    return buffer;
}
buffer<byte> Stream::readUpTo(size_t capacity) {
    buffer<byte> buffer(capacity);
    buffer.size = check( ::read(fd, (void*)buffer.data, capacity) );
    return buffer;
}
bool Stream::poll(int timeout) { assert(fd); pollfd pollfd{fd,POLLIN,0}; return ::poll(&pollfd,1,timeout)==1 && (pollfd.revents&POLLIN); }
void Stream::write(const byte* data, size_t size) { assert(data); for(size_t offset=0; offset<size;) offset+=check(::write(fd, data+offset, size-offset), name()); }
void Stream::write(const ref<byte>& buffer) { write(buffer.data, buffer.size); }
Socket::Socket(int domain, int type):Stream(check(socket(domain,type,0))){}

// File
File::File(const string& path, const Folder& at, Flags flags):Stream(check(openat(at.fd, strz(path), flags, 0666),at.name()+"/"_+path+"'"_, (int)flags, path.size)){ assert_(path.size<0x100); }
struct stat File::stat() const { struct stat stat; check_( fstat(fd, &stat) ); return stat; }
int64 File::size() const { return stat().st_size; }
int64 File::accessTime() const { struct stat stat = File::stat(); return stat.st_atim.tv_sec*1000000000ul + stat.st_atim.tv_nsec; }
int64 File::modifiedTime() const { struct stat stat = File::stat(); return stat.st_mtim.tv_sec*1000000000ul + stat.st_mtim.tv_nsec;  }
void File::resize(int64 size) { check_(ftruncate(fd, size), fd.pointer, size); }
void File::seek(int index) { check_(::lseek(fd,index,0)); }

bool existsFile(const string& path, const Folder& at) { return Handle( openat(at.fd, strz(path), O_RDONLY, 0) ).fd > 0; }
buffer<byte> readFile(const string& path, const Folder& at) { File file(path,at); return file.read( file.size() ); }
void writeFile(const string& path, const ref<byte>& content, const Folder& at) { File(path,at,Flags(WriteOnly|Create|Truncate)).write(content); }

// Device
int Device::ioctl(uint request, void* arguments) { return check(::ioctl(fd, request, arguments)); }

// Map
Map::Map(const File& file, Prot prot, Flags flags) { size=file.size(); data = size?(byte*)check(mmap(0,size,prot,flags,file.fd,0)):0; }
Map::Map(uint fd, uint offset, uint size, Prot prot, Flags flags){ this->size=size; data=(byte*)check(mmap(0,size,prot,flags,fd,offset)); }
Map::~Map() { unmap(); }
void Map::lock(uint size) const { check_(mlock(data, min<size_t>(this->size,size))); }
void Map::unmap() { if(data) munmap((void*)data,size); data=0, size=0; }

// File system
void rename(const Folder& oldAt, const string& oldName, const Folder& newAt, const string& newName) {
    assert_(oldName != newName);
    assert_(existsFile(oldName,oldAt), oldName, newName, existsFile(newName,newAt));
    assert_(!existsFile(newName,newAt), oldName, newName);
    assert_(isUTF8(newName) && isASCII(newName), newName);
    check_(renameat(oldAt.fd,strz(oldName),newAt.fd,strz(newName)), oldName, newName);
}
void rename(const string& oldName,const string& newName, const Folder& at) { rename(at, oldName, at, newName); }
void remove(const string& name, const Folder& at) { check_( unlinkat(at.fd,strz(name),0), name); }
void removeFolder(const string& name, const Folder& at) { check_( unlinkat(at.fd,strz(name),AT_REMOVEDIR), name); }
void remove(const Folder& folder) { int fd=check(openat(folder.fd, strz("."_), O_WRONLY|O_DIRECTORY, 0), folder.name()); check_( unlinkat(fd,".",AT_REMOVEDIR), folder.name()); close(fd); }
void symlink(const string& from,const string& to, const Folder& at) {
    assert(from!=to);
    remove(from,at);
    check_(symlinkat(strz(from),at.fd,strz(to)), from,"->",to);
}
void touchFile(const string& path, const Folder& at, bool setModified) { timespec times[]={{0,0}, {0,setModified?UTIME_NOW:UTIME_OMIT}}; check_(utimensat(at.fd, strz(path), times, 0)); }
void copy(const Folder& oldAt, const string& oldName, const Folder& newAt, const string& newName) {
    File oldFile(oldName, oldAt), newFile(newName, newAt, Flags(WriteOnly|Create|Truncate));
    for(size_t offset=0, size=oldFile.size(); offset<size;) offset+=check(sendfile(newFile.fd, oldFile.fd, (off_t*)offset, size-offset), (int)newFile.fd, (int)oldFile.fd, offset, size-offset, size);
    assert_(newFile.size() == oldFile.size(), oldFile.size(), newFile.size());
}

int64 freeSpace(const Handle& file) { struct statvfs statvfs; check_( fstatvfs(file.fd, &statvfs) ); return statvfs.f_bavail*statvfs.f_frsize; }
int64 freeSpace(const string& path, const Folder& at) { return freeSpace(File(path,at)); }
