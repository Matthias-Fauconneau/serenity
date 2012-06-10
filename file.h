#pragma once
#include "string.h"
#include "debug.h"

/// Input/Output
extern "C" ssize_t read(int fd, void* buf, size_t size);
extern "C" ssize_t write(int fd, const void* buf, size_t size);
extern "C" int close(int fd);

inline array<byte> readUpTo(int fd, uint capacity) {
    array<byte> buffer(capacity);
    int size = read(fd,(byte*)buffer.data(),(size_t)capacity);
    buffer.setSize(size);
    return buffer;
}
inline array<byte> read(int fd, uint capacity) {
    array<byte> buffer(capacity);
    int size = read(fd,(byte*)buffer.data(),(size_t)capacity);
    assert((uint)size==capacity,size);
    buffer.setSize(size);
    return buffer;
}
template<class T> T read(int fd) {
    T t;
    int unused size = read(fd,(byte*)&t,sizeof(T));
    assert(size==sizeof(T),size,sizeof(T));
    return t;
}

template<class T> inline void write(int fd, const array<T>& s) {
    uint unused wrote = write(fd,s.data(),(size_t)s.size()*sizeof(T));
    assert(wrote==s.size());
}

/// File
const int CWD = -100;
/// Open file for reading
int openFile(const string& path, int at=CWD);
/// Open file for writing, overwrite if existing
int createFile(const string& path, int at=CWD, bool overwrite=false);
/// Open file for writing, append if existing
int appendFile(const string& path, int at=CWD);

array<byte> readFile(const string& path, int at=CWD);

struct Map {
    no_copy(Map)
    const byte* data=0; uint size=0;
    Map(){}
    Map(const byte* data, uint size):data(data),size(size){}
    Map(Map&& o):data(o.data),size(o.size){o.data=0,o.size=0;}
    Map& operator=(Map&& o){data=o.data,size=o.size;o.data=0,o.size=0;return*this;}
    ~Map();
    /// Returns an /a array reference to the map, valid only while the map exists.
    operator array<byte>() { return array<byte>(data,size); }
};

Map mapFile(const string& path, int at=CWD);

void writeFile(const string& path, const array<byte>& content, int at=CWD, bool overwrite=false);

/// File system
int openFolder(const string& path, int at=CWD);
int home();
bool exists(const string& path, int at=CWD);
bool createFolder(const string& path, int at);
bool isFolder(const string& path, int at=CWD);
void symlink(const string& target,const string& name, int at=CWD);
long modifiedTime(const string& path, int at=CWD);
enum Flags { Recursive=1, Sort=2, Folders=4, Files=8 }; inline Flags operator |(Flags a, Flags b) { return Flags(int(a)|int(b)); }
array<string> listFiles(const string& folder, Flags flags);
string findFile(const string& folder, const string& file);
