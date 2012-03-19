#pragma once
#include "string.h"

/// Input/Output
extern "C" ssize_t read(int fd, void* buf, size_t size);
extern "C" ssize_t write(int fd, const void* buf, size_t size);
extern "C" int close(int fd);

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

/// File
const int CWD = -100;
int openFile(const string& path, int at=CWD);
int createFile(const string& path, int at=CWD);
array<byte> readFile(const string& path, int at=CWD);
struct Map { const byte* data; int size; ~Map(); };
Map mapFile(const string& path, int at=CWD);
void writeFile(const string& path, const array<byte>& content, int at=CWD);

/// File system
int openFolder(const string& path, int at=CWD);
bool exists(const string& path, int at=CWD);
bool createFolder(const string& path, int at);
bool isFolder(const string& path, int at=CWD);
enum Flags { Recursive=1, Sort=2, Folders=4, Files=8 }; inline Flags operator |(Flags a, Flags b) { return Flags(int(a)|int(b)); }
array<string> listFiles(const string& folder, Flags flags);
