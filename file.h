#pragma once
#include "string.h"

int root();

/// Open file for reading
int openFile(const string& path, int at=root());
/// Open file for writing, overwrite if existing
int createFile(const string& path, int at=root(), bool overwrite=false);
/// Open file for writing, append if existing
int appendFile(const string& path, int at=root());

array<byte> read(int fd, uint capacity);
array<byte> readFile(const string& path, int at=root());
void writeFile(const string& path, const array<byte>& content, int at=root(), bool overwrite=false);

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

Map mapFile(const string& path, int at=root());

int openFolder(const string& path, int at=root());
bool exists(const string& path, int at=root());
bool createFolder(const string& path, int at);
bool isFolder(const string& path, int at=root());
void symlink(const string& target,const string& name, int at=root());
long modifiedTime(const string& path, int at=root());
enum Flags { Recursive=1, Sort=2, Folders=4, Files=8 }; inline Flags operator |(Flags a, Flags b) { return Flags(int(a)|int(b)); }
array<string> listFiles(const string& folder, Flags flags, int at=root());
string findFile(const string& folder, const string& file, int at=root());
