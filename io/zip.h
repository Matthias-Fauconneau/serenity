#include "inflate.h"

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

struct DirectoryEnd {
	byte signature[4] = {'P','K', 5, 6}; // End of central directory signature
	uint16 disk; // Number of this disk
	uint16 startDisk; // Number of the disk with the start of the central directory
	uint16 nofEntries; // Total number of entries in the central directory on this disk
	uint16 nofTotalEntries; // Number of entries in the central directory
	uint32 size; // Size of the central directory
	uint32 offset; // Offset of start of central directory with respect to the starting disk number
	uint16 commentLength; // .ZIP file comment length
} packed;

buffer<byte> extractZIPFile(ref<byte> zip, ref<byte> fileName) {
	for(int i=zip.size-sizeof(DirectoryEnd); i>=0; i--) {
		if(zip.slice(i, sizeof(DirectoryEnd::signature)) == ref<byte>(DirectoryEnd().signature, 4)) {
			const DirectoryEnd& directory = raw<DirectoryEnd>(zip.slice(i, sizeof(DirectoryEnd)));
			size_t offset = directory.offset;
			buffer<string> files (directory.nofEntries, 0);
			for(size_t unused entryIndex: range(directory.nofEntries)) {
				const FileHeader& header =  raw<FileHeader>(zip.slice(offset, sizeof(FileHeader)));
				string name = zip.slice(offset+sizeof(header), header.nameLength);
				if(name.last() != '/') {
					const LocalHeader& local = raw<LocalHeader>(zip.slice(header.offset, sizeof(LocalHeader)));
					ref<byte> compressed = zip.slice(header.offset+sizeof(local)+local.nameLength+local.extraLength, local.compressedSize);
					assert_(header.compression == 8);
					if(name == fileName) return inflate(compressed, buffer<byte>(local.size));
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
