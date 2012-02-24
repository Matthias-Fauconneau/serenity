#pragma once
#include "string.h"

extern "C" ssize_t read(int fd, void* buf, size_t size);
extern "C" ssize_t write(int fd, const void* buf, size_t size);
extern "C" int close(int fd);

//inline void read(int fd, array<byte>& s) { s.size=read(fd,(byte*)&s,(size_t)s.capacity); }
inline array<byte> read(int fd, int capacity) {
    array<byte> buffer(capacity);
    buffer.size=read(fd,(byte*)&buffer,(size_t)buffer.capacity);
    return buffer;
}

int createFile(const string& path);
string mapFile(const string& path);

bool exists(const string& path);
bool isDirectory(const string& path);

enum Flags { Recursive=1, Sort=2, Folders=4, Files=8 }; inline Flags operator |(Flags a, Flags b) { return Flags(int(a)|int(b)); }
array<string> listFiles(const string& folder, Flags flags);
