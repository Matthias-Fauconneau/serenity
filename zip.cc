#include "zip.h"
#include "inflate.h"

#include "array.cc"
Array(ZipFile)

struct LocalHeader {
    ubyte signature[4] = {'P','K', 3, 4};  //local file header signature
    uint16 features; //version needed to extract
    uint16 flag; //general purpose bit flag
    uint16 compression; //compression method
    uint16 modifiedTime; //last mod file time
    uint16 modifiedDate; //last mod file date
    uint crc; //crc-32
    uint compressedSize; //compressed size
    uint size; //uncompressed size
    uint16 nameLength; //file name length
    uint16 extraLength; //extra field length
    //file name (variable size)
    //extra field (variable size)
} packed;

struct DataDescriptor {
    uint crc; //crc-32
    uint compressedSize; //compressed size
    uint size; //uncompressed size
} packed;

struct FileHeader {
    ubyte signature[4] = {'P','K', 1, 2}; //central file header signature
    uint16 zipVersion; //version made by
    uint16 features; //version needed to extract
    uint16 flag; //general purpose bit flag
    uint16 compression; //compression method
    uint16 modifiedTime; //last mod file time
    uint16 modifiedDate; //last mod file date
    uint crc; //crc-32
    uint compressedSize; //compressed size
    uint size; //uncompressed size
    uint16 nameLength; //file name length
    uint16 extraLength; //extra field length
    uint16 commentLength; //file comment length
    uint16 disk; //disk number start
    uint16 attributes; //internal file attributes
    uint externalAttributes; //external file attributes
    uint offset; //relative offset of local header
    //file name (variable size)
    //extra field (variable size)
    //file comment (variable size)
} packed;

struct DirectoryEnd {
    ubyte signature[4] = {'P','K', 5, 6};
    uint16 disk;
    uint16 startDisk;
    uint16 nofEntries;
    uint16 nofTotalEntries;
    uint size;
    uint offset;
    uint16 commentLength;
} packed;

map<string, ZipFile > readZip(DataBuffer s) {
    map<string, ZipFile > files;
    s.seekLast(raw(DirectoryEnd().signature));
    DirectoryEnd directory = s.read();
    for(int i=0;i<directory.nofEntries;i++) {
        s.seek(directory.offset);
        FileHeader header = s.read();
        string name = s.read(header.nameLength);
        s.advance(header.extraLength);
        s.advance(header.commentLength);
        directory.offset = s.index;
        if(!endsWith(name,"/"_)) {
            s.seek(header.offset);
            LocalHeader local = s.read();
            s.advance(local.nameLength);
            s.advance(local.extraLength);
            files.insert(move(name), ZipFile(s.read(header.compressedSize), header.compression==8));
        }
    }
    return files;
}
