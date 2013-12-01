#pragma once
#include "array.h"

struct Family : array<uint64> {
    Family(uint64 root):root(root){}
    uint64 root;
};
String str(const Family& o) { return str(o.root)/*+":"_+str((const array<uint64>&)o)*/; }
array<Family> parseFamilies(const string& data);
