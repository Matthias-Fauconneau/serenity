#include "file.h"
#include "linux.h"
#include "string.h"

#include <sys/syscall.h>
static int getdents(int fd, void* entry, long size) { return syscall(SYS_getdents, fd, entry, size); }
struct dirent { long ino, off; short len; char name[]; };
enum {DT_DIR=4, DT_REG=8};

// Handle
Handle::~Handle() { if(fd>0) close(fd); }

// Folder
const Folder& cwd() { static const int cwd = AT_FDCWD; return (const Folder&)cwd; }
const Folder& root() { static const Folder root = Folder("/"_,cwd()); return root; }
Folder::Folder(const ref<byte>& folder, const Folder& at, bool create):Handle(0){
    if(create && !existsFolder(folder,at)) check_(mkdirat(at.fd, strz(folder), 0666), folder);
    fd=check(openat(at.fd, strz(folder?:"."_), O_RDONLY|O_DIRECTORY, 0), folder);
}
array<string> Folder::list(uint flags) {
    Folder fd(""_,*this);
    array<string> list; byte buffer[0x1000];
    for(int size;(size=check(getdents(fd.fd,&buffer,sizeof(buffer))))>0;) {
        for(byte* i=buffer,*end=buffer+size;i<end;i+=((dirent*)i)->len) { const dirent& entry=*(dirent*)i;
            ref<byte> name = str(entry.name);
            if(name=="."_||name==".."_) continue;
            int type = *((byte*)&entry + entry.len - 1);
            if(type==DT_DIR && flags&Recursive) {
                array<string> files = Folder(name,*this).list(flags);
                for(const string& file: files) list.insertSorted(name+"/"_+file);
            } else if((type==DT_DIR && flags&Folders) || (type==DT_REG && flags&Files)) {
                list.insertSorted(string(name));
            }
        }
    }
    return list;
}
bool existsFolder(const ref<byte>& folder, const Folder& at) { return Handle( openat(at.fd, strz(folder), O_RDONLY|O_DIRECTORY, 0) ).fd > 0; }

// Stream
void Stream::read(void* buffer, uint size) { int unused read=check( ::read(fd,buffer,size) ); assert(read==(int)size); }
int Stream::readUpTo(void* buffer, uint size) { return check( ::read(fd, buffer, size), fd, buffer, size); }
array<byte> Stream::read(uint capacity) {
    array<byte> buffer(capacity);
    buffer.size = check( ::read(fd, buffer.data, capacity) );
    assert(buffer.size==capacity, buffer.size, capacity);
    return buffer;
}
array<byte> Stream::readUpTo(uint capacity) {
    array<byte> buffer(capacity);
    buffer.size = check( ::read(fd, buffer.data, capacity) );
    return buffer;
}
bool Stream::poll(int timeout) { assert(fd); pollfd pollfd __(fd,POLLIN); return ::poll(&pollfd,1,timeout)==1 && (pollfd.revents&POLLIN); }
void Stream::write(const byte* data, uint size) { int unused wrote=check(::write(fd,data,size)); assert(wrote==(int)size); }
void Stream::write(const ref<byte>& buffer) { write(buffer.data, buffer.size); }
Socket::Socket(int domain, int type):Stream(check(socket(domain,type,0))){}

// File
File::File(const ref<byte>& path, const Folder& at, uint flags):Stream(check(openat(at.fd, strz(path), flags, 0666),path)){}
int File::size() const { struct stat sb={}; check_(fstat(fd, &sb)); return sb.st_size; }
void File::seek(int index) { check_(::lseek(fd,index,0)); }
int Device::ioctl(uint request, void* arguments) { return check(::ioctl(fd, request, arguments)); }
bool existsFile(const ref<byte>& folder, const Folder& at) { return Handle( openat(at.fd, strz(folder), O_RDONLY, 0) ).fd > 0; }
array<byte> readFile(const ref<byte>& path, const Folder& at) {
    File file(path,at);
    uint size=file.size();
    if(size>1<<24) log(path,"use mapFile to avoid copying "_+dec(file.size()>>10)+"KB"_);
    return file.read(size);
}
void writeFile(const ref<byte>& path, const ref<byte>& content, const Folder& at) { File(path,at,WriteOnly|Create|Truncate).write(content); }

// Map
Map::Map(const File& file, uint prot) { size=file.size(); data = size?(byte*)check(mmap(0,size,prot,Private,file.fd,0)):0; }
Map::Map(uint fd, uint offset, uint size, uint prot, uint flags){ this->size=size; data=(byte*)check(mmap(0,size,prot,flags,fd,offset)); }
Map::~Map() { if(data) munmap((void*)data,size); }
void Map::lock(uint size) const { check_(mlock(data, min(this->size,size))); }

// File system
void symlink(const ref<byte>& target,const ref<byte>& name, const Folder& at) {
    assert(target!=name);
    unlinkat(at.fd,strz(name),0);
    check_(symlinkat(strz(target),at.fd,strz(name)), name,"->",target);
}
struct stat statFile(const ref<byte>& path, const Folder& at) { struct stat file; check_( fstat(File(path,at).fd, &file) ); return file; }
long modifiedTime(const ref<byte>& path, const Folder& at) { return statFile(path,at).st_mtime; }
void touchFile(const ref<byte>& path, const Folder& at) { utimensat(at.fd, strz(path), 0, 0); }
