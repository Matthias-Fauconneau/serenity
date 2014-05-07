#include "netcdf.h"

string NetCDF::readName() {
    uint nameLength = s.read();
    string name = s.read<byte>(nameLength);
    s.align(4);
    return name;
}

map<string, Dimension> NetCDF::parseDimensions() {
    enum Tag { ABSENT, DIMENSION=0xA, VARIABLE, ATTRIBUTE };
    uint32 tag = s.read();
    uint32 elementCount = s.read();
    assert_(tag==DIMENSION || (tag == ABSENT && elementCount==0));
    map<string, Dimension> dimensions(elementCount);
    for(auto entry: dimensions) {
        entry.key = readName();
        entry.value = s.read();
    }
    return dimensions;
}

map<string, Attribute> NetCDF::parseAttributes() {
    enum Tag { ABSENT, DIMENSION=0xA, VARIABLE, ATTRIBUTE };
    uint32 tag = s.read();
    uint32 elementCount = s.read();
    assert_(tag==ATTRIBUTE || (tag == ABSENT && elementCount==0));
    map<string, Attribute> attributes(elementCount);
    for(auto entry: attributes) {
        entry.key = readName();
        Attribute& attribute = entry.value;
        attribute.type = s.read();
        assert_(attribute.type<=DOUBLE);
        uint32 elementCount = s.read();
        attribute.data = buffer<byte>( s.read<byte>(elementCount * attribute.elementSize()) );
        s.align(4);
    }
    return attributes;
}

map<string, Variable> NetCDF::parseVariables() {
    enum Tag { ABSENT, DIMENSION=0xA, VARIABLE, ATTRIBUTE };
    uint32 tag = s.read();
    uint32 elementCount = s.read();
    assert_(tag==VARIABLE || (tag == ABSENT && elementCount==0));
    map<string, Variable> variables(elementCount);
    for(auto entry: variables) {
        entry.key = readName();
        Variable& variable = entry.value;
        uint32 dimensionCount = s.read();
        variable.dimensions = map<string,Dimension>(dimensionCount);
        for(auto entry: variable.dimensions) {
            uint dimensionIndex = s.read();
            entry.key = dimensions.keys[dimensionIndex];
            entry.value = dimensions.values[dimensionIndex];
        }
        variable.attributes = parseAttributes();
        variable.type = s.read();
        uint32 size = s.read();
        uint32 offset = s.read();
        variable.data = buffer<byte>( s.buffer.slice(offset, size) );
        if(variable.elementSize()==1) {}
        else if(variable.elementSize()==4) variable.data = cast<byte>( bswap(cast<int32>(variable.data)) ); // NetCDF is big endian ...
        else if(variable.elementSize()==8) variable.data = cast<byte>( bswap(cast<int64>(variable.data)) ); // NetCDF is big endian ...
        else error(variable.type);
    }
    return variables;
}

NetCDF::NetCDF(const ref<byte>& data) : s(data, true) {
    // Header
    if(s.read<byte>(4)!="CDF\x01"_) { error("Invalid CDF"); } // Magic
    uint32 unused recordCount = s.read();
    dimensions = parseDimensions();
    attributes = parseAttributes();
    variables = parseVariables();
}
