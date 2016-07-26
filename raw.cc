#include "cr2.h"
#include "time.h"
inline double log2(double x) { return __builtin_log2(x); }

void stripThumbnails(mref<byte>& file) {
#if 0
 CR2 cr2(file, true);
 const size_t source = cr2.data.begin()-file.begin();
 const size_t target = 0x3800;
 assert_(cr2.ifdOffset.size == 5 && source > target);
 for(CR2::Entry* entry: cr2.entriesToFix) {
  if(entry->tag == 0x111) { entry->value = target; } // StripOffset
  if(entry->tag == 0x2BC) { entry->count = 0; entry->value = 0; } // XMP
 }
 *cr2.ifdOffset[1] = *cr2.ifdOffset[3]; // Skips JPEG and RGB thumbs
 for(size_t i: range(cr2.data.size)) file[target+i] = file[source+i];
#endif
 BinaryData s(file);
 s.skip("II\x2A\x00");
 uint* ifdOffset = 0;
 size_t source = 0;
 const size_t target = 0x3800;
 for(size_t ifdIndex = 0;;ifdIndex++) { // EXIF, JPEG, RGB, LJPEG
  if(ifdIndex==1) ifdOffset = (uint*)s.data.begin()+s.index; // nextIFD after EXIF ...
  s.index = s.read32();
  if(ifdIndex==3) *ifdOffset = s.index; // links directly to LJPEG (skips JPEG and RGB thumbs)
  if(!s.index) break;
  uint16 entryCount = s.read();
  for(const CR2::Entry& e : s.read<CR2::Entry>(entryCount)) { CR2::Entry& entry = (CR2::Entry&)e;
   if(entry.tag == 0x2BC) { entry.count = 0; entry.value = 0; } // XMP
   if(entry.tag == 0x111) { source = entry.value; entry.value = target; } // StripOffset
  }
 }
 for(size_t i: range(file.size-source)) file[target+i] = file[source+i];
 size_t originalSize = file.size;
 file.size -= source-target;
 log(str(originalSize/1024/1024., 1u),"MB", str((source-target)/1024/1024., 1u),"MB", str(100.*(source-target)/file.size, 1u)+"%", str(file.size/1024/1024., 1u),"MB");
}

//buffer<byte> encodeRANS4(const ref<byte> source) { return encodeRANS4(source, CR2(source)); }
buffer<byte> encodeRANS4(const ref<byte> source, const CR2& cr2) {
 buffer<byte> target(cr2.tiffHeaderSize + cr2.ljpeg.headerSize + cr2.dataSize); // Assumes rANS4 < LJPEG
 target.slice(0, cr2.tiffHeaderSize+cr2.ljpeg.headerSize).copy(source.slice(0, cr2.tiffHeaderSize+cr2.ljpeg.headerSize));
 size_t size = encodeRANS4(target.slice(cr2.tiffHeaderSize+cr2.ljpeg.headerSize), cr2.image);
 target.size = cr2.tiffHeaderSize + cr2.ljpeg.headerSize + size;

 // Updates header
 BinaryData TIFF(target);
 TIFF.skip("II\x2A\x00");
 for(;;) {
  TIFF.index = TIFF.read32();
  if(!TIFF.index) break;
  uint16 entryCount = TIFF.read();
  for(const CR2::Entry& e: TIFF.read<CR2::Entry>(entryCount)) { CR2::Entry& entry = (CR2::Entry&)e;
   if(entry.tag==0x103) { assert_(entry.value==6); entry.value=0x879C; } // Compression
   if(entry.tag==0x117) entry.value = cr2.ljpeg.headerSize + size;
  }
 }

 return target;
}

//buffer<byte> encodeLJPEG(const ref<byte> source) { return encodeLJPEG(source, CR2(source)); }
buffer<byte> encodeLJPEG(const ref<byte> source, const CR2& cr2) {
 buffer<byte> target(cr2.tiffHeaderSize + cr2.ljpeg.headerSize + cr2.dataSize*2); // Assumes JPEG < 2·rANS4
 target.slice(0, cr2.tiffHeaderSize+cr2.ljpeg.headerSize).copy(source.slice(0, cr2.tiffHeaderSize+cr2.ljpeg.headerSize));
 size_t size = encode(cr2.ljpeg, target.slice(cr2.tiffHeaderSize+cr2.ljpeg.headerSize), cr2.image);
 target.size = cr2.tiffHeaderSize + cr2.ljpeg.headerSize + size;

 // Updates header
 BinaryData TIFF(target);
 TIFF.skip("II\x2A\x00");
 for(;;) {
  TIFF.index = TIFF.read32();
  if(!TIFF.index) break;
  uint16 entryCount = TIFF.read();
  for(const CR2::Entry& e: TIFF.read<CR2::Entry>(entryCount)) { CR2::Entry& entry = (CR2::Entry&)e;
   if(entry.tag==0x103) { assert_(entry.value==0x879C); entry.value=6; } // Compression
   if(entry.tag==0x117) entry.value = cr2.ljpeg.headerSize + size;
  }
 }

 return target;
}

struct Raw {
 Raw() {
  array<String> files = Folder(".").list(Files|Sorted);
  size_t imageCount = 0;
  for(string name: files) {
   if(!endsWith(toLower(name), ".cr2")) continue;
   if(0) {
    String jpgName = section(name,'.')+".JPG";
    if(files.contains(jpgName)) {
     assert_(existsFile(section(name,'.')+".CR2"));
     log("Removing", jpgName);
     remove(jpgName);
    }
   }
   imageCount++;
  }
  Time totalTime {true};
  size_t totalSize = 0, stripSize = 0, ransSize = 0;
  size_t index = 0;
  for(string name: files) {
   if(!endsWith(toLower(name), ".cr2")) continue;
   index++;
   log(index,"/",imageCount,name);
   if(0) { // Strips thumbnails
    buffer<byte> file = readFile(name);
    stripThumbnails(file);
    //CR2(file); // Validates
    writeFile(name, file, currentWorkingDirectory(), true);
    stripSize += file.size;
   }
   if(1) { // Encodes rANS4
    if(existsFile(section(name,'.')+".ANS")) {
     size_t sourceSize = File(name).size();
     size_t targetSize = File(section(name,'.')+".ANS").size();
     log(str((sourceSize-              0)/1024/1024.,1u)+"MB", str(targetSize/1024/1024.,1u)+"MB",
         str((sourceSize-targetSize)/1024/1024.,1u)+"MB", str(100.*(sourceSize-targetSize)/targetSize,1u)+"%");
     break; // TEST
     continue;
    }
    Map source (name);
    CR2 cr2 (source); // Decodes LJPEG
    buffer<byte> target = encodeRANS4(source, cr2);
    writeFile(section(name,'.')+".ANS", target);
    {CR2 ans(Map(section(name,'.')+".ANS")); assert_(ans.image == cr2.image && ans.whiteBalance.G == cr2.whiteBalance.G);} // Verifies

    totalSize += source.size;
    ransSize += target.size;
    break;
    log(str((source.size-              0)/1024/1024.,1u)+"MB", str(target.size/1024/1024.,1u)+"MB",
        str((source.size-target.size)/1024/1024.,1u)+"MB", str(100.*(source.size-target.size)/target.size,1u)+"%");
    remove(name);
   }
  }
  if(totalSize) log(str((totalSize-                0)/1024/1024.,1u)+"MB", str(ransSize/1024/1024.,1u)+"MB",
                    str((totalSize-ransSize)/1024/1024.,1u)+"MB", str(100.*(totalSize-ransSize)/ransSize, 1u)+"%");
  log(totalTime);
  totalTime.reset();
  for(string name: files) {
   if(!endsWith(toLower(name), ".ans")) continue;
   Map source(name);
   CR2 cr2 (source); // Decodes rANS4
   writeFile(section(name,'.')+".cr2", encodeLJPEG(source, cr2));
  }
  log(totalTime);
 }
} app;
