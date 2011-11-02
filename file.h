#pragma once
#include "string.h"

bool exists(const string& path);
bool isDirectory(const string& path);
array<string> listFiles(const string& folder, bool recursive=false);
int createFile(const string& path);
string mapFile(const string& path);
template <class T> void write(int fd, const T& t) { write(fd,&t,sizeof(T)); }
