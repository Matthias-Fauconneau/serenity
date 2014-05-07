#pragma once
#include "data.h"
#include "map.h"

typedef uint32 Dimension;
enum NCType { BYTE=1, CHAR, SHORT, INT, FLOAT, DOUBLE };

struct VariantArray {
    uint32 type; buffer<byte> data; //ref<byte> data;
    uint elementSize() { return ref<uint>({0,1,1,2,4,4,8})[type]; }
    operator ref<byte>() const { assert_(type==BYTE || type==CHAR); return cast<byte>(data); }
    operator ref<short>() const { assert_(type==SHORT); return cast<short>(data); }
    operator ref<int>() const { assert_(type==INT); return cast<int>(data); }
    operator ref<float>() const { assert_(type==FLOAT); return cast<float>(data); }
    operator ref<double>() const { assert_(type==DOUBLE); return cast<double>(data); }
};

typedef VariantArray Attribute;
inline String str(const Attribute& a) {
    /**/  if(a.type==BYTE||a.type==CHAR) return str((ref<byte>)a);
    else if(a.type==SHORT) return str((ref<short>)a);
    else if(a.type==INT) return str((ref<short>)a);
    else if(a.type==FLOAT) return str((ref<float>)a);
    else if(a.type==DOUBLE) return str((ref<double>)a);
    else error(int(a.type));
}

struct Variable : VariantArray {
    map<string, Dimension> dimensions;
    map<string, Attribute> attributes;
};
inline String str(const Variable& v) { return str(ref<string>({""_,"byte"_,"char"_,"short"_,"int"_,"float"_,"double"_})[v.type], v.dimensions,v.attributes); }

struct NetCDF {
    NetCDF(const ref<byte>& data);

    map<string, Dimension> dimensions;
    map<string, Attribute> attributes;
    map<string, Variable> variables;

private:
    string readName();
    map<string, Dimension> parseDimensions();
    map<string, Attribute> parseAttributes();
    map<string, Variable> parseVariables();

    BinaryData s;
};
