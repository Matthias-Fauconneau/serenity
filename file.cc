#include "file.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dirent.h>

bool exists(const string& path) {
    int fd = open(&strz(path), O_RDONLY);
    if(fd >= 0) { close(fd); return true; }
    return false;
}

struct stat statFile(const string& path) { struct stat file; stat(&strz(path), &file); return file; }
bool isDirectory(const string& path) { return statFile(path).st_mode&S_IFDIR; }

array<string> listFiles(const string& folder, Flags flags) {
    array<string> list;
    DIR* dir = opendir(folder?&strz(folder):".");
    assert(dir, "Folder not found"_, folder);
    for(dirent* dirent; (dirent=readdir(dir));) {
        string path = strz(dirent->d_name);
        if(path!="."_ && path!=".."_) {
            if(folder) path = folder+"/"_+path;
            bool isFolder = isDirectory(path);
            if(isFolder && flags&Recursive) {
                if(flags&Sort) list.insertSorted(move(listFiles(path,flags)));
                else list << move(listFiles(path,flags));
            } else if((isFolder && flags&Folders) || (!isFolder && flags&Files)) {
                if(flags&Sort) list.insertSorted(move(path));
                else list << move(path);
            }
        }
    }
    closedir(dir);
    return list;
}

int createFile(const string& path) {
    return open(&strz(path),O_CREAT|O_WRONLY|O_TRUNC,0666);
}

string mapFile(const string& path) {
    int fd = open(&strz(path), O_RDONLY);
    if(fd < 0) error("File not found"_,"'"_+path+"'"_);
    struct stat sb; fstat(fd, &sb);
    const void* data = mmap(0,(size_t)sb.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    close(fd);
    return string((const char*)data,(int)sb.st_size);
}
