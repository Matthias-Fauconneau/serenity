#pragma once
#include "stream.h"
#include "map.h"
#include "inflate.h"

struct ZipFile {
    array<byte> data;
    bool compressed;
    ZipFile(array<byte>&& data, bool compressed=false) : data(move(data)), compressed(compressed) {}
    operator array<byte>() { return compressed ? inflate(data, false) : copy(data); }
};

map<string, ZipFile> readZip(DataBuffer);
