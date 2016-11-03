#include "scene.h"
#include "data.h"

#if 1 // .scene

//include "parse.h"
// Enforces exact match for overload resolution
generic T parse(TextData&) { static_assert(0&&sizeof(T), "No overload for parse<T>(TextData&)"); }
template<> inline float parse<float>(TextData& s) { return s.decimal(); }
template<Type V> V parseVec(TextData& s) {
    V value;
    for(uint index: range(V::_N)) { s.whileAny(" \t"); value[index] = parse<Type V::_T>(s); }
    return value;
}
template<> inline vec3 parse<vec3>(TextData& s) { return parseVec<vec3>(s); }


Scene parseScene(ref<byte> file) {
    TextData s (file);
    while(s.match('#')) s.until('\n');
    const vec3 viewpoint = parse<vec3>(s);
    s.skip('\n');
    array<Scene::Face> faces;
    while(s) {
        if(s.match('\n')) continue;
        if(s.match('#')) { s.until('\n'); continue; }
        array<vec3> polygon;
        while(s && !s.match('\n')) { // Empty line
            polygon.append(parse<vec3>(s));
            if(!s) break;
            s.skip('\n');
        }
        assert_(polygon.size == 4);
        //faces.append({{polygon[0], polygon[1], polygon[2], polygon[3]},{0,1,1,0},{0,0,1,1},1});
        faces.append({{polygon[3], polygon[2], polygon[1], polygon[0]},{0,1,1,0},{0,0,1,1},Image8()});
        /*assert_(polygon.size == 3 || polygon.size == 4);
        // Fan
        for(size_t i : range(1, polygon.size-1)) {
            //faces.append({{polygon[0], polygon[i], polygon[i+1]},{vec3(0,1,i==1), vec3(0,i==2,1)},1});
            faces.append({{polygon[i+1], polygon[i], polygon[0]},{vec3(i==1,1,0), vec3(1,i==2,0)},1});
        }*/
    }
    log(viewpoint, faces.size);
    return {viewpoint, ::move(faces)};
}

#elif 1 // Stanford .ply

//include "parse.h"
// Enforces exact match for overload resolution
generic T parse(TextData&) { static_assert(0&&sizeof(T), "No overload for parse<T>(TextData&)"); }
template<> inline uint parse<uint>(TextData& s) { return s.integer(false); }
template<> inline float parse<float>(TextData& s) { return s.decimal(); }
template<Type V> V parseVec(TextData& s) {
    V value;
    for(uint index: range(V::_N)) { s.whileAny(" \t"); value[index] = parse<Type V::_T>(s); }
    return value;
}
template<> inline uint3 parse<uint3>(TextData& s) { return parseVec<uint3>(s); }
template<> inline vec3 parse<vec3>(TextData& s) { return parseVec<vec3>(s); }

string basename(string x) {
    string name = x.contains('/') ? section(x,'/',-2,-1) : x;
    string basename = name.contains('.') ? section(name,'.',0,-2) : name;
    assert_(basename);
    return basename;
}

Scene parseScene(ref<byte> file) {
    TextData s (file);
    s.skip("ply\n");
    s.skip("format ascii 1.0\n");
    s.skip("comment "); s.until('\n');
    s.skip("element vertex ");
    const uint vertexCount = s.integer();
    s.skip('\n');
    while(s.match("property")) s.until("\n");
    s.skip("element face ");
    const uint faceCount = s.integer();
    s.skip('\n');
    while(s.match("property")) s.until("\n");
    s.skip("end_header\n");
    buffer<vec3> vertices (vertexCount);
    for(size_t i : range(vertexCount)) {
        vertices[i] = parse<vec3>(s);
        s.until('\n');
    }

    vec3 min = inff, max = -inff;
    for(vec3 p: vertices) { min = ::min(min, p); max = ::max(max, p); }
    log(min, max);

    buffer<uint3> indices (faceCount);
    for(size_t i : range(faceCount)) {
        int length = s.integer();
        indices[i] = parse<uint3>(s);
        s.until('\n');
    }

    vec3 viewpoint (0,0,-512);
    buffer<Scene::Face> faces (faceCount, 0);
    for(uint3 face: indices) {
        faces.append({{vertices[face[0]], vertices[face[1]], vertices[face[2]]},{vec3(1,1,0), vec3(1,0,0)},1});
        //faces.append({{vertices[face[2]], vertices[face[1]], vertices[face[0]]},{vec3(1,1,0), vec3(1,0,0)}, 0});
    }
    return {viewpoint, ::move(faces)};
}


#else // .blend

#include "sdna.h"

/// Used to fix pointers in .blend file-block sections
struct Block {
    uint64 begin; // First memory address used by pointers pointing in this block
    uint64 end; // Last memory address used by pointers pointing in this block
    int64 delta; // Target memory address where the block was loaded - begin
};
bool operator<(const Block& a, const Block& b) { return a.begin<b.begin; }
String str(const Block& b) { return str(hex(b.begin))+str("-")+str(hex(b.end)); }

/// SDNA type definitions
struct Struct {
    string name;
    uint size;
    struct Field {
        string typeName;
        uint reference;
        string name;
        uint count;
        const Struct* type;
    };
    array<Field> fields;
};
inline bool operator==(const Struct& a, const string& b) { return a.name==b; }

string elementType(const Struct& o, const Struct::Field& f){ // HACK: DNA doesn't define ListBase element types
    static array<string> listbases = split("bNodeTree.nodes:bNode bNode.inputs:bNodeSocket bNode.outputs:bNodeSocket ParticleSettings.dupliweights:ParticleDupliWeight "
                                           "Object.particlesystem:ParticleSystem Scene.base:Base"_," "_);
    for(string listbase: listbases) { TextData s(listbase); if(s.until('.')==o.name && s.until(':')==f.name) return s.untilEnd(); }
    return ""_;
}

String str(const Struct& o, const array<const Struct*>& defined={}) {
    String s = "struct "_+o.name+" {\n"_;
    for(const Struct::Field& f: o.fields)  {
        s.append("    "_+(defined.contains(f.type)?""_:"struct "_)+(f.typeName=="uint64_t"_?"uint64"_:f.typeName));
        if(f.typeName=="ListBase"_) s.append("<"_+elementType(o,f)+">"_);
        s.append(repeat("*"_,f.reference)+" "_+f.name);
        if(f.count!=1) s.append("["_+str(f.count)+"]"_);
        s.append(";\n"_);
    }
    return s+"};"_;
}

/// Lists all definitions needed to define a type (recursive)
void listDependencies(const Struct& type, array<const Struct*>& deps) {
    if(!deps.contains(&type)) {
        deps.append(&type);
        for(const Struct::Field& field: type.fields) if(!field.reference) listDependencies(*field.type, deps);
    }
}
array<const Struct*> listDependencies(const Struct& type) { array<const Struct*> deps; listDependencies(type,deps); return deps; }

/// Outputs structures DNA to be copied to a C header in order to update this parser to new Blender versions //FIXME: own class
String DNAtoC(const Struct& type, array<const Struct*>& defined, array<const Struct*>& defining, array<Struct>& structs) {
    String s;
    assert_(!defining.contains(&type));
    defining.append(&type);
    array<const Struct*> deferred;
    for(const Struct::Field& field: type.fields) {
        const Struct* fieldType = field.type;
        if(field.typeName=="ListBase"_) {
            string elementType = ::elementType(type,field);
            if(elementType) {
                assert(structs.contains(elementType), elementType);
                fieldType = &structs[structs.indexOf(elementType)];
            }
        }
        if(fieldType) {
            if(fieldType==&type) continue; // Self reference
            if(defined.contains(fieldType)) continue; // Already defined
            if(deferred.contains(fieldType)) continue; // Already deferred
            if(field.reference || field.typeName=="ListBase"_) { // Only defines references if we can
                array<const Struct*> deps = listDependencies(*fieldType);
                for(const Struct* d: deps) if(defining.contains(d)) { if(d==&type) deferred.append(fieldType); goto continue_2; }
            }
            s.append(DNAtoC(*fieldType, defined, defining, structs));
        }
       continue_2:;
    }
    s.append(str(type,defined)+"\n"_);
    defined.append(&type);
    defining.pop();
    for(const Struct* d: deferred) s.append(DNAtoC(*d, defined, defining, structs));
    return s;
}

/// Recursively fix all pointers in an SDNA structure
   void fix(const array<Block>& blocks, const Struct& type, const ref<byte>& buffer) {
       BinaryData data (buffer);
       for(const Struct::Field& field: type.fields) {
           if(!field.reference) {
               if(field.type->fields) for(uint i unused: range(field.count)) fix(blocks, *field.type, data.Data::read(field.type->size));
               else data.advance(field.count*field.type->size);
           } else for(uint i unused: range(field.count)) {
               uint64& pointer = (uint64&)data.read<uint64>();
               if(!pointer) continue;
               for(const Block& block: blocks/*.slice(blocks.binarySearch( Block{pointer,0,0} )-1)*/) {
                   if(pointer >= block.begin && pointer < block.end) {
                       pointer += block.delta;
                       if(field.reference>1) { // pointer arrays
                           assert(pointer==block.begin+block.delta);
                           assert((block.end-block.begin)%8==0);
                           uint64* array = (uint64*)pointer;
                           uint size = (block.end-block.begin)/8;
                           for(uint i : range(size)) {
                               uint64& pointer = array[i];
                               if(!pointer) continue;
                               for(const Block& block: blocks/*.slice(blocks.binarySearch( Block{pointer,0,0} )-1)*/) {
                                   if(pointer >= block.begin && pointer < block.end) {
                                       pointer += block.delta;
                                       goto found2;
                                   }
                               }
                               error("not found");
                               pointer = 0;
                               found2:;
                           }
                       }
                       goto found;
                   }
               }
               pointer = 0; //-1
               found:;
           }
       }
   }

   /// Parses SDNA to fix all pointers
   void load(const ref<byte> file_) {
       array<Block> blocks; // List of file blocks containing pointers to be fixed
       array<Struct> structs; // SDNA structure definitions

       BinaryData file(file_);
       file.skip("BLENDER-v278"); // - : 64bit, v : little endian
       blocks.reserve(32768);
       while(file) { // Parses SDNA
           const BlockHeader& header = file.read<BlockHeader>();
           string identifier(header.identifier,4);
           BinaryData data( file.Data::read(header.size) );
           assert(blocks.size<blocks.capacity); // Avoids large reallocations
           blocks.append( Block{header.address, header.address+header.size, int64(uint64(data.buffer.data)-header.address)} );
           log(header.size, header.address, header.type, header.count);
           if(identifier == "DNA1"_) {
               data.skip("SDNA"); //SDNA
               data.skip("NAME"); //NAME
               uint nameCount = data.read();
               assert_(nameCount < 4245, nameCount);
               log(nameCount);
               array< string > names;
               for(uint unused i: range(nameCount)) names.append(data.whileNot(0));
               data.align(4);
               data.advance(4); //TYPE
               uint typeCount = data.read();
               log(typeCount);
               assert_(typeCount < 8192, typeCount);
               array< string > types;
               for(uint unused i: range(typeCount)) types.append(data.whileNot(0));
               data.align(4);
               data.advance(4); //TLEN
               ref<uint16> lengths = data.read<uint16>(typeCount);
               data.align(4);
               data.advance(4); //STRC
               uint structCount = data.read();
               assert_(!structs);
               for(uint unused i: range(structCount)) {
                   uint16 index = data.read();
                   string name = types[index];
                   uint size = lengths[index];
                   uint16 fieldCount = data.read();
                   array<Struct::Field> fields;
                   for(uint unused i: range(fieldCount)) {
                       string type = types[(uint16)data.read()];
                       string name = names[(uint16)data.read()];
                       uint reference=0, count=1;
                       TextData s (name);
                       if(s.match("(*"_)) { // Parses function pointers
                           name.data+=2; name.size-=2+3;
                           type = "void"_; reference++;
                       } else {
                           while(s.match('*')) { // Parses references
                               name.data++; name.size--;
                               reference++;
                           }
                       }
                       s.whileNot('['); if(s.match('[')) { // Parses static arrays
                           name.size -= 1+(s.buffer.size-s.index);
                           count = s.integer();
                           s.match(']');
                           while(s.match('[')) { // Flattens multiple indices
                               count *= s.integer();
                               s.match(']');
                           }
                       }
                       fields.append(Struct::Field{type, reference, name, count, 0});
                   }
                   structs.append(Struct{name, size, move(fields)});
               }
               structs.append(Struct{"void"_,0,{}});
               structs.append(Struct{"char"_,1,{}});
               structs.append(Struct{"short"_,2,{}});
               structs.append(Struct{"int"_,4,{}});
               structs.append(Struct{"uint64_t"_,8,{}});
               structs.append(Struct{"float"_,4,{}});
               structs.append(Struct{"double"_,8,{}}); // Do not move this as DNA structs are indexed by BlockHeader
               for(Struct& s: structs) {
                   for(Struct::Field& f: s.fields) {
                       int index = structs.indexOf(f.typeName);
                       if(index>=0) f.type = &structs[index]; // Structs may not be reallocated after taking these references
                       else assert(f.reference);
                   }
               }
           }
           if(identifier == "ENDB"_) break;
       }
       //quicksort(blocks);

       file.seek(0);
       string version = file.read<byte>(12);
       if(/*version!=sdnaVersion || false*/1) {
           array<const Struct*> defined;
           defined.append(&structs[structs.indexOf("void"_)]);
           defined.append(&structs[structs.indexOf("char"_)]);
           defined.append(&structs[structs.indexOf("short"_)]);
           defined.append(&structs[structs.indexOf("int"_)]);
           defined.append(&structs[structs.indexOf("uint64_t"_)]);
           defined.append(&structs[structs.indexOf("float"_)]);
           defined.append(&structs[structs.indexOf("double"_)]);
           defined.append(&structs[structs.indexOf("ID"_)]);
           defined.append(&structs[structs.indexOf("ListBase"_)]); // ListBase is redefined with a template
           array<const Struct*> defining;
           String header = "#pragma once\n#include \"sdna.h\"\nconst string sdnaVersion = \""_+version+"\"_;\n"_;
           header.append(DNAtoC(structs[structs.indexOf("Scene"_)], defined, defining, structs));
           header.append(DNAtoC(structs[structs.indexOf("NodeTexImage"_)], defined, defining, structs));
           header = replace(header,"Material"_,"bMaterial"_); //FIXME: whole words only
           header = replace(header,"Image"_,"bImage"_); //FIXME: whole words only
           header = replace(header,"Key"_,"bKey"_); //FIXME: whole words only
           error(header);
       }
       while(file) { // Fixes pointers
           const BlockHeader& header = file.read<BlockHeader>();
           string identifier(header.identifier,4);
           BinaryData data (file.Data::read(header.size));
           const Struct& type = structs[header.type];

           if(identifier == "SC\0\0"_) {
#if 0
               scene = (Scene*)data.buffer.data;
#else
               error("");
#endif
           }
           else if(identifier == "DNA1"_ ) continue;
           else if(identifier == "ENDB"_) break;

           if(header.size >= header.count*type.size && type.fields && header.type != 0) {
               assert(header.size == header.count*type.size);
               for(uint unused i: range(header.count)) fix(blocks, type, data.Data::read(type.size));
           }
       }
   }

Scene parseScene(ref<byte> file) {
    load(file);
    error("");
}

#endif
