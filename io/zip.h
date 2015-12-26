#include "inflate.h"
#include "map.h"

struct LocalHeader {
 byte signature[4] = {'P','K', 3, 4}; // Local file header signature
 uint16 features; // Version needed to extract
 uint16 flag; // General purpose bit flag
 uint16 compression; // Compression method
 uint16 modifiedTime; // Last modified file time
 uint16 modifiedDate; // Last modified file date
 uint32 crc; // CRC-32
 uint32 compressedSize;
 uint32 size; // Uncompressed size
 uint16 nameLength; // File name length
 uint16 extraLength; // Extra field length
 // File name
 // Extra field
} packed;

struct DataDescriptor {
 uint32 crc; // CRC-32
 uint32 compressedSize;
 uint32 size; // Uncompressed size
} packed;

struct FileHeader {
 byte signature[4] = {'P','K', 1, 2}; // Central file header signature
 uint16 zipVersion; // Version made by
 uint16 features; // Version needed to extract
 uint16 flag; // General purpose bit flag
 uint16 compression; // Compression method
 uint16 modifiedTime; // Last modified file time
 uint16 modifiedDate; // Last modified file date
 uint32 crc; // CRC-32
 uint32 compressedSize;
 uint32 size; // Uncompressed size
 uint16 nameLength; // File name length
 uint16 extraLength; // Extra field length
 uint16 commentLength; // File comment length
 uint16 disk; // Disk number start
 uint16 attributes; // Internal file attributes
 uint32 externalAttributes; // External file attributes
 uint32 offset; // Relative offset of local header
 // File name
 // Extra field
 // File comment
} packed;

/*struct DirectoryEnd {
        byte signature[4] = {'P','K', 5, 6}; // End of central directory signature
    uint16 disk; // Number of this disk
    uint16 startDisk; // Number of the disk with the start of the central directory
    uint16 nofEntries; // Total number of entries in the central directory on this disk
    uint16 nofTotalEntries; // Number of entries in the central directory
    uint32 size; // Size of the central directory
    uint32 offset; // Offset of start of central directory with respect to the starting disk number
    uint16 commentLength; // .ZIP file comment length
} packed;*/

struct DirectoryEnd64 {
 byte signature[4] = {'P','K', 6, 6}; // End of central directory signature
 uint64 size; // Size of central directory
 uint16 writeVersion;
 uint16 readVersion;
 uint32 disk; // Number of this disk
 uint32 startDisk; // Number of the disk with the start of the central directory
 uint64 diskEntryCount; // Total number of entries in the central directory on this disk
 uint64 totalEntryCount; // Number of entries in the central directory
 uint64 centralDirectorySize;
 uint64 offset; // Offset of start of central directory with respect to the starting disk number
 uint16 commentLength; // .ZIP file comment length
} packed;


struct Zip64Extra {
 byte signature[2] = {1, 0}; // End of central directory signature
 uint16 thisExtraSize;
 //uint64 uncompressedSize;
 //uint64 compressedSize;
 uint64 offset;
 //uint32 disk;
} packed;

buffer<byte> extractZIPFile(ref<byte> zip, ref<byte> fileName) {
 for(size_t i=zip.size-sizeof(DirectoryEnd64); i>0; i--) {
  if(zip.slice(i, sizeof(DirectoryEnd64::signature)) == ref<byte>(DirectoryEnd64().signature, 4)) {
   const DirectoryEnd64& directory = raw<DirectoryEnd64>(zip.slice(i, sizeof(DirectoryEnd64)));
   size_t offset = directory.offset;
   buffer<string> files (directory.totalEntryCount, 0);
   for(size_t unused entryIndex: range(directory.totalEntryCount)) {
    const FileHeader& header =  raw<FileHeader>(zip.slice(offset, sizeof(FileHeader)));
    string name = zip.slice(offset+sizeof(header), header.nameLength);
    if(name.last() != '/') {
     size_t fileOffset = header.offset;
     if(header.extraLength) {
      string extra = zip.slice(offset+sizeof(FileHeader)+name.size, header.extraLength);
      const Zip64Extra& zip64 = raw<Zip64Extra>(extra.slice(0, sizeof(Zip64Extra)));
      fileOffset = zip64.offset;
     }
     const LocalHeader& local = raw<LocalHeader>(zip.slice(fileOffset, sizeof(LocalHeader)));
     ref<byte> compressed = zip.slice(fileOffset+sizeof(LocalHeader)+local.nameLength+local.extraLength, local.compressedSize);
     assert_(header.compression == 8);
     if(name == fileName) return inflate(compressed, buffer<byte>(align(2048,local.size), local.size));
     files.append(name);
    }
    offset += sizeof(header)+header.nameLength+header.extraLength+header.commentLength;
   }
   error("No such file", fileName,"in",files);
   return {};
  }
 }
 error("Missing end of central directory signature");
 return {};
}

map<string, size_t> listZIPFile(ref<byte> zip) {
 for(size_t i=zip.size-sizeof(DirectoryEnd64); i>0; i--) {
  if(zip.slice(i, sizeof(DirectoryEnd64::signature)) == ref<byte>(DirectoryEnd64().signature, 4)) {
   const DirectoryEnd64& directory = raw<DirectoryEnd64>(zip.slice(i, sizeof(DirectoryEnd64)));
   size_t offset = directory.offset;
   buffer<string> files (directory.totalEntryCount, 0);
   buffer<size_t> sizes (directory.totalEntryCount, 0);
   for(size_t unused entryIndex: range(directory.totalEntryCount)) {
    const FileHeader& header =  raw<FileHeader>(zip.slice(offset, sizeof(FileHeader)));
    string name = zip.slice(offset+sizeof(FileHeader), header.nameLength);
    if(name.last() != '/') {
     size_t fileOffset = header.offset;
     if(header.extraLength) {
      string extra = zip.slice(offset+sizeof(header)+name.size, header.extraLength);
      const Zip64Extra& zip64 = raw<Zip64Extra>(extra.slice(0, sizeof(Zip64Extra)));
      fileOffset = zip64.offset;
     }
     const LocalHeader& local = raw<LocalHeader>(zip.slice(fileOffset, sizeof(LocalHeader)));
     files.append(name);
     sizes.append(local.size);
    }
    offset += sizeof(header)+header.nameLength+header.extraLength+header.commentLength;
   }
   return {::move(files), ::move(sizes)};
  }
 }
 error("Missing end of central directory signature");
 return {};
}
