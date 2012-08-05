#pragma once
#include "array.h"

array<byte> read(int fd, uint capacity);
array<byte> readUpTo(int fd, uint capacity);

int root();

/// File is a file descriptor which close itself in the destructor
struct File {
    no_copy(File)
    int fd;
    explicit File(int fd=0):fd(fd){};
    File(File&& o) { fd=o.fd; o.fd=0; }
    ~File();
    operator int() { return fd; }
};

/// Open file for reading, fails if not existing
File openFile(const ref<byte>& path, int at=root());
/// Open file for writing, overwrite if existing
File createFile(const ref<byte>& path, int at=root(), bool overwrite=false);
/// Open file for writing, append if existing
File appendFile(const ref<byte>& path, int at=root());

array<byte> readFile(const ref<byte>& path, int at=root());
void writeFile(const ref<byte>& path, const ref<byte>& content, int at=root(), bool overwrite=true);

struct Map {
    no_copy(Map)
    const byte* data=0; uint size=0;
    Map(){}
    Map(const byte* data, uint size):data(data),size(size){}
    Map(Map&& o):data(o.data),size(o.size){o.data=0,o.size=0;}
    Map& operator=(Map&& o){this->~Map();data=o.data,size=o.size;o.data=0,o.size=0;return*this;}
    ~Map();
    /// Returns a reference to the map, valid only while the map exists.
    operator ref<byte>() { return ref<byte>(data,size); } //TODO: escape analysis
};

Map mapFile(const ref<byte>& path, int at=root());
Map mapFile(int fd);

int openFolder(const ref<byte>& path, int at=root());
bool exists(const ref<byte>& path, int at=root());
void createFolder(const ref<byte>& path, int at);
bool isFolder(const ref<byte>& path, int at=root());
void symlink(const ref<byte>& target,const ref<byte>& name, int at=root());
long modifiedTime(const ref<byte>& path, int at=root());
enum Flags { Recursive=1, Sort=2, Folders=4, Files=8 }; inline Flags operator |(Flags a, Flags b) { return Flags(int(a)|int(b)); }
//array<string> listFiles(const ref<byte>& folder, Flags flags, int at=root());
//string findFile(const ref<byte>& folder, const ref<byte>& file, int at=root());
