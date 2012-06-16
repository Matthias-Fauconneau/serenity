#pragma once
#include "string.h"

extern "C" int close(int fd);

/// Input/Output

array<byte> readUpTo(int fd, uint capacity);
array<byte> read(int fd, uint capacity);

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
array<string> listFiles(const string& folder, Flags flags, int at=CWD);
string findFile(const string& folder, const string& file, int at=CWD);
