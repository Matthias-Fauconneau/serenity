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
 BinaryData s(file.slice(4));
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
  Time jpegDecTime, ransEncTime, totalTime {true};
  size_t totalSize = 0, stripSize = 0, ransSize = {};
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
    buffer<byte> file = readFile(name);
    totalSize += file.size;

    // Decodes lossless JPEG
    jpegDecTime.start();
    CR2 cr2(file);
    jpegDecTime.stop();

    // Encodes rANS4
    ransEncTime.start();
    size_t ransSize = encodeRANS4(file.slice(cr2.tiffHeaderSize+cr2.ljpeg.headerSize), cr2.image);
    ransEncTime.stop();

    // Updates header
    for(BinaryData s(file.slice(4));;) {
     s.index = s.read32();
     if(!s.index) break;
     uint16 entryCount = s.read();
     for(const CR2::Entry& e: s.read<CR2::Entry>(entryCount)) { CR2::Entry& entry = (CR2::Entry&)e;
      if(entry.tag==0x103) { assert_(entry.value==6); entry.value=0x879C; } // Compression
      if(entry.tag==0x117) entry.value = cr2.ljpeg.headerSize + ransSize;
     }
    }

    size_t jpegSize = file.size;
    file.size = cr2.tiffHeaderSize + cr2.ljpeg.headerSize + ransSize;
    writeFile(section(name,'.')+".ANS", file);

    break; // TEST

    log(str((jpegSize-              0)/1024/1024.,1u)+"MB", str(file.size/1024/1024.,1u)+"MB",
        str((jpegSize-file.size)/1024/1024.,1u)+"MB", str(100.*(jpegSize-file.size)/file.size,1u)+"%");
   }
  }
  log(jpegDecTime, ransEncTime, totalTime);
  if(totalSize) log(str((totalSize-                0)/1024/1024.,1u)+"MB", str(ransSize/1024/1024.,1u)+"MB",
                    str((totalSize-ransSize)/1024/1024.,1u)+"MB", str(100.*(totalSize-ransSize)/ransSize, 1u)+"%");
  Time ransDecTime, jpegEncTime;
  totalTime.reset();
  for(string name: files) {
   if(!endsWith(toLower(name), ".ans")) continue;
   array<byte> file = readFile(name);
   file.reserve(file.size*2); // rANS4 to JPEG expands (reallocates before references are taken)

   // Decodes rANS4
   ransDecTime.start();
   CR2 cr2(file);
   ransDecTime.stop();

   // Encodes LJPEG
   jpegEncTime.start();
   size_t jpegSize = encode(cr2.ljpeg, file.slice(cr2.tiffHeaderSize+cr2.ljpeg.headerSize), cr2.image);
   jpegEncTime.stop();

   // Updates header
   for(BinaryData s(file.slice(4));;) {
    s.index = s.read32();
    if(!s.index) break;
    uint16 entryCount = s.read();
    for(const CR2::Entry& e : s.read<CR2::Entry>(entryCount)) { CR2::Entry& entry = (CR2::Entry&)e;
     if(entry.tag==0x103) { assert_(entry.value==0x879C); entry.value=6; } // Compression
     if(entry.tag==0x117) entry.value = cr2.ljpeg.headerSize + jpegSize;
    }
   }

   file.size = cr2.tiffHeaderSize + cr2.ljpeg.headerSize + jpegSize;
   assert_(file.size <= file.capacity);
   writeFile(section(name,'.')+".cr2", file);
  }
  log(ransDecTime, jpegEncTime, totalTime);
 }
} app;
