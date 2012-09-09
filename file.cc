#include "file.h"
#include "linux.h"
#include "debug.h"

/// Handle

Handle::~Handle() { if(fd>0) close(fd); }

/// Folder

const Folder& root() { const int AT_FDCWD=-100; static const Folder root = Folder("/"_,(const Folder&)AT_FDCWD); return root; }
Folder::Folder(const ref<byte>& folder, const Folder& at, bool create):Handle(0){
    if(create && !existsFolder(folder,at))  check_(mkdirat(at.fd, strz(folder), 0666), folder);
    fd=check( openat(at.fd, strz(folder), O_RDONLY|O_DIRECTORY, 0), folder);
}
array<string> listFiles(const ref<byte>& folder, ListFlags flags, const Folder& at) {
    Folder fd(folder,at);
    array<string> list; byte buffer[0x1000];
    for(int size;(size=check(getdents(fd.fd,&buffer,sizeof(buffer))))>0;) {
        for(byte* i=buffer,*end=buffer+size;i<end;i+=((dirent*)i)->len) { const dirent& entry=*(dirent*)i;
            ref<byte> name = str(entry.name);
            if(name=="."_||name==".."_) continue;
            int type = *((byte*)&entry + entry.len - 1);
            if(type==DT_DIR && flags&Recursive) {
                array<string> files = listFiles(folder?string(folder+"/"_+name):string(name),flags,at);
                if(flags&Sort) for(string& e: files) list.insertSorted(move(e));
                else list << move(files);
            } else if((type==DT_DIR && flags&Folders) || (type==DT_REG && flags&Files)) {
                string path = folder?string(folder+"/"_+name):string(name);
                if(flags&Sort) list.insertSorted(move(path));
                else list << move(path);
            }
        }
    }
    return list;
}
bool existsFolder(const ref<byte>& folder, const Folder& at) { return Handle( openat(at.fd, strz(folder), O_RDONLY|O_DIRECTORY, 0) ).fd > 0; }

/// Stream

void Stream::read(void* buffer, uint size) { int unused read=check( ::read(fd,buffer,size) ); assert(read==(int)size); }
int Stream::readUpTo(void* buffer, uint size) { return check( ::read(fd, buffer, size) ); }

array<byte> Stream::read(uint capacity) {
    array<byte> buffer(capacity);
    int size = check( ::read(fd,buffer.data(),capacity) );
    assert((uint)size==capacity,size,capacity);
    buffer.setSize(size);
    return buffer;
}

array<byte> Stream::readUpTo(uint capacity) {
    array<byte> buffer(capacity);
    int size = check( ::read(fd,buffer.data(),capacity) );
    if(size) { buffer.setCapacity(size); buffer.setSize(size); }
    return buffer;
}

void Stream::write(const ref<byte>& buffer) { int unused wrote=check( ::write(fd,buffer.data,buffer.size) ); assert(wrote==(int)buffer.size); }

int Stream::ioctl(uint request, void* arguments) { return check(::ioctl(fd, request, arguments)); }

/// File

File::File(const ref<byte>& file, const Folder& at, Flags flags):Stream(check(openat(at.fd, strz(file), flags, 0666),file)){}
int File::size() const { stat sb; fstat(fd, &sb); return sb.size; }
void File::seek(int index) { check_(::lseek(fd,index,0)); }

bool existsFile(const ref<byte>& folder, const Folder& at) { return Handle( openat(at.fd, strz(folder), O_RDONLY, 0) ).fd > 0; }
array<byte> readFile(const ref<byte>& path, const Folder& at) {
    File file(path,at);
    debug(if(file.size()>1<<16) { trace(); warn("use mapFile to avoid copying "_+dec(file.size()>>10)+"KB"_); })
    return file.read(file.size());
}
void writeFile(const ref<byte>& file, const ref<byte>& content, const Folder& at) { File(file,at,File::Truncate).write(content); }

/// Map

Map::Map(const File& file) { data = (byte*)mmap(0,size=file.size(),PROT_READ,MAP_PRIVATE,file.fd,0); assert(data); }
Map::~Map() { log("unmap",data); if(data) munmap((void*)data,size); }

/// File system

void symlink(const ref<byte>& target,const ref<byte>& name, const Folder& at) {
    assert(target!=name);
    unlinkat(at.fd,strz(name),0);
    check_(symlinkat(strz(target),at.fd,strz(name)), name,"->",target);
}
stat statFile(const ref<byte>& path, const Folder& at) { stat file; check_( fstat(File(path,at).fd, &file) ); return file; }
long modifiedTime(const ref<byte>& path, const Folder& at) { return statFile(path,at).mtime.sec; }
void touchFile(const ref<byte>& path, const Folder& at) { utimensat(at.fd, strz(path), 0, 0); }
