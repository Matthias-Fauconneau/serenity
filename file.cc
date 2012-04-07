#include "file.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dirent.h>
#include <unistd.h>

/// File
int openFile(const string& path, int at) {
    int fd = openat(at, strz(path).data(), O_RDONLY);
    if(fd < 0) error("File not found"_,"'"_+path+"'"_);
    return fd;
}

int createFile(const string& path, int at, bool overwrite) {
    if(!overwrite && exists(path,at)) error("exists",path);
    return openat(at, strz(path).data(),O_CREAT|O_WRONLY|O_TRUNC,0666);
}

int appendFile(const string& path, int at) {
    return openat(at, strz(path).data(),O_CREAT|O_WRONLY|O_APPEND,0666);
}

array<byte> readFile(const string& path, int at) {
    int fd = openat(at, strz(path).data(), O_RDONLY);
    if(fd < 0) error("File not found"_,"'"_+path+"'"_);
    struct stat sb; fstat(fd, &sb);
    array<byte> file = read(fd,sb.st_size);
    close(fd);
    debug( if(file.size()>1<<18) ("use mapFile to avoid copying "_+dec(file.size()>>10)+"KB"_) );
    return file;
}

Map mapFile(const string& path, int at) {
    int fd = openat(at, strz(path).data(), O_RDONLY);
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
    int fd = openat(at, strz(path).data(), O_RDONLY|O_DIRECTORY);
    if(fd < 0) error("Folder not found"_,"'"_+path+"'"_);
    return fd;
}

extern "C" char* getenv(const char* key);
int home() {
    static int fd = openFolder(strz(getenv("HOME")));
    return fd;
}

bool createFolder(const string& path, int at) { return mkdirat(at, strz(path).data(), 0666)==0; }

bool exists(const string& path, int at) {
    int fd = openat(at, strz(path).data(), O_RDONLY);
    if(fd >= 0) { close(fd); return true; }
    return false;
}

void symlink(const string& target,const string& name, int at) {
    unlinkat(at,strz(name).data(),0);
    if(symlinkat(strz(target).data(),at,strz(name).data())<0) warn("symlink failed",name,"->",target);
}

struct stat statFile(const string& path, int at) { struct stat file; fstatat(at, strz(path).data(), &file, 0); return file; }
bool isFolder(const string& path, int at) { return statFile(path,at).st_mode&S_IFDIR; }

long modifiedTime(const string& path, int at) { return statFile(path,at).st_mtime; }

array<string> listFiles(const string& folder, Flags flags) {
    array<string> list;
    DIR* dir = opendir(folder?strz(folder).data():".");
    assert(dir, "Folder not found"_, folder);
    for(dirent* dirent; (dirent=readdir(dir));) {
        string name = strz(dirent->d_name);
        if(name!="."_ && name!=".."_) {
            string path = folder+"/"_+name;
            bool isFolder = ::isFolder(path);
            if(isFolder && flags&Recursive) {
                if(flags&Sort) for(auto&& e: listFiles(path,flags)) insertSorted(list, move(e));
                else list << move(listFiles(path,flags));
            } else if((isFolder && flags&Folders) || (!isFolder && flags&Files)) {
                if(flags&Sort) insertSorted(list, move(path));
                else list << move(path);
            }
        }
    }
    closedir(dir);
    return list;
}

string findFile(const string& folder, const string& file) {
    DIR* dir = opendir(folder?strz(folder).data():".");
    assert(dir, "Folder not found"_, folder);
    for(dirent* dirent; (dirent=readdir(dir));) {
        string name = strz(dirent->d_name);
        if(name!="."_ && name!=".."_) {
            string path = folder+"/"_+name;
            if(::isFolder(path)) { path=findFile(path,file); if(path) return path; }
            else if(file==name) return path;
        }
    }
    closedir(dir);
    return ""_;
}
