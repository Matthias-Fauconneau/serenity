#pragma once
#include "string.h"

bool exists(const string& path);
bool isDirectory(const string& path);
enum Flags { Recursive=1, Sort=2 }; inline Flags operator |(Flags a, Flags b) { return Flags(int(a)|int(b)); }
array<string> listFiles(const string& folder, Flags flags=Sort);
int createFile(const string& path);
string mapFile(const string& path);
template <class T> void write(int fd, const T& t) { write(fd,&t,sizeof(T)); }
