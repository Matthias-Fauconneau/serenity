#include "file.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dirent.h>
#include <unistd.h>

/// Input/Output

array<byte> readUpTo(int fd, uint capacity) {
    array<byte> buffer(capacity);
    int size = read(fd,(byte*)buffer.data(),(size_t)capacity);
    buffer.setSize(size);
    return buffer;
}

array<byte> read(int fd, uint capacity) {
    array<byte> buffer(capacity);
    int size = read(fd,(byte*)buffer.data(),(size_t)capacity);
    assert((uint)size==capacity,size);
    buffer.setSize(size);
    return buffer;
}


/// File
int openFile(const string& path, int at) {
    int fd = openat(at, strz(path), O_RDONLY);
    if(fd < 0) error("File not found"_,"'"_+path+"'"_);
    return fd;
}

int createFile(const string& path, int at, bool overwrite) {
    if(!overwrite && exists(path,at)) error("exists",path);
    return openat(at, strz(path),O_CREAT|O_WRONLY|O_TRUNC,0666);
}

int appendFile(const string& path, int at) {
    return openat(at, strz(path),O_CREAT|O_WRONLY|O_APPEND,0666);
}

array<byte> readFile(const string& path, int at) {
    int fd = openat(at, strz(path), O_RDONLY);
    if(fd < 0) error("File not found"_,"'"_+path+"'"_);
    struct stat sb; fstat(fd, &sb);
    array<byte> file = read(fd,sb.st_size);
    close(fd);
    debug( if(file.size()>1<<20) warn("use mapFile to avoid copying "_+dec(file.size()>>10)+"KB"_) );
    return file;
}

Map mapFile(const string& path, int at) {
    int fd = openat(at, strz(path), O_RDONLY);
    if(fd < 0) error("File not found"_,"'"_+path+"'"_);
    struct stat sb; fstat(fd, &sb);
    const byte* data = (byte*)mmap(0,(size_t)sb.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    close(fd);
    return Map(data,(int)sb.st_size);
}
Map::~Map() { munmap((void*)data,size); }

void writeFile(const string& path, const array<byte>& content, int at, bool overwrite) {
    int fd = createFile(path,at,overwrite);
    if(fd < 0) { warn("Creation failed",path,fd,at); return; }
    write(fd,content);
    close(fd);
}

/// File system

int openFolder(const string& path, int at) {
    int fd = openat(at, path?strz(path):".", O_RDONLY|O_DIRECTORY);
    if(fd < 0) error("Folder not found"_,"'"_+path+"'"_);
    return fd;
}

extern "C" char* getenv(const char* key);
int home() {
    static int fd = openFolder(strz(getenv("HOME")));
    return fd;
}

bool createFolder(const string& path, int at) { return mkdirat(at, strz(path), 0666)==0; }

bool exists(const string& path, int at) {
    int fd = openat(at, strz(path), O_RDONLY);
    if(fd >= 0) { close(fd); return true; }
    return false;
}

void symlink(const string& target,const string& name, int at) {
    unlinkat(at,strz(name),0);
    if(symlinkat(strz(target),at,strz(name))<0) warn("symlink failed",name,"->",target);
}

struct stat statFile(const string& path, int at) { struct stat file; fstatat(at, strz(path), &file, 0); return file; }
bool isFolder(const string& path, int at) { return statFile(path,at).st_mode&S_IFDIR; }

long modifiedTime(const string& path, int at) { return statFile(path,at).st_mtime; }

#include "syscall.h"
struct linux_dirent { long ino, off; short len; char name[]; };

array<string> listFiles(const string& folder, Flags flags, int at) {
    int fd = openFolder(folder,at);
    assert(fd, "Folder not found"_, folder);
    array<string> list;
    int i=0; for(linux_dirent entry; syscall(SYS_getdents,fd,&entry,sizeof(entry))>0;i++) { if(i<2) continue;
        string name = strz(entry.name);
        string path = folder+"/"_+name;
        int type = *((byte*)&entry + entry.len - 1);
        if(type==DT_DIR && flags&Recursive) {
            if(flags&Sort) for(auto&& e: listFiles(path,flags,at)) insertSorted(list, move(e));
            else list << move(listFiles(path,flags,at));
        } else if((type==DT_DIR && flags&Folders) || (type==DT_REG && flags&Files)) {
            if(flags&Sort) insertSorted(list, move(path));
            else list << move(path);
        }
    }
    close(fd);
    return list;
}

string findFile(const string& folder, const string& file, int at) {
    int fd = openFolder(folder,at);
    assert(fd, "Folder not found"_, folder);
    int i=0; for(linux_dirent entry; syscall(SYS_getdents,fd,&entry,sizeof(entry))>0;i++) { if(i<2) continue;
        string name = strz(entry.name);
        int type = *((byte*)&entry + entry.len - 1);
        if(type==DT_DIR) { string path=findFile(name,file,fd); if(path) return folder+"/"_+path; }
        else if(type==DT_REG && file==name) return folder+"/"_+name;
    }
    close(fd);
    return ""_;
}
