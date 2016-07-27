#include "cr2.h"
#include "time.h"

buffer<byte> stripThumbnails(const ref<byte> source, const CR2& cr2) {
 const size_t targetTiffHeaderSize = 0x3800;
 assert_(cr2.tiffHeaderSize > targetTiffHeaderSize);
 buffer<byte> target(targetTiffHeaderSize + cr2.dataSize);
 target.slice(0, targetTiffHeaderSize).copy(source.slice(0, targetTiffHeaderSize));
 BinaryData s(target);
 s.skip("II\x2A\x00");
 uint* ifdOffset = 0;
 for(size_t ifdIndex = 0;;ifdIndex++) { // EXIF, JPEG, RGB, LJPEG
  if(ifdIndex==1) ifdOffset = (uint*)s.data.begin()+s.index; // nextIFD after EXIF ...
  s.index = s.read32();
  if(ifdIndex==3) *ifdOffset = s.index; // links directly to LJPEG (skips JPEG and RGB thumbs)
  if(!s.index) break;
  uint16 entryCount = s.read();
  for(const CR2::Entry& e : s.read<CR2::Entry>(entryCount)) { CR2::Entry& entry = (CR2::Entry&)e;
   if(entry.tag == 0x2BC) { entry.count = 0; entry.value = 0; } // XMP
   if(entry.tag==0x103) { if(ifdIndex==3) assert_(entry.value==6); }
   if(entry.tag == 0x111) { entry.value = targetTiffHeaderSize; } // StripOffset
  }
 }
 target.slice(targetTiffHeaderSize).copy(source.slice(cr2.tiffHeaderSize, cr2.dataSize));
 log(str(source.size/1024/1024., 1u),"MB", str((source.size-target.size)/1024/1024., 1u),"MB", str(target.size/1024/1024., 1u),"MB",
     str(100.*(source.size-target.size)/target.size, 1u)+"%");
 return target;
}

buffer<byte> encodeRANS4(const ref<byte> source, const CR2& cr2) {
 buffer<byte> target(cr2.tiffHeaderSize + cr2.dataSize); // Assumes rANS4 < LJPEG
 target.slice(0, cr2.tiffHeaderSize+cr2.ljpeg.headerSize).copy(source.slice(0, cr2.tiffHeaderSize+cr2.ljpeg.headerSize));
 size_t size = encodeRANS4(target.slice(cr2.tiffHeaderSize+cr2.ljpeg.headerSize), cr2.image);
 if(!size) return {}; // Failed to encode (rANS4 > LJPEG)
 target.size = cr2.tiffHeaderSize + cr2.ljpeg.headerSize + size;
 // Updates header
 BinaryData TIFF(target);
 TIFF.skip("II\x2A\x00");
 for(;;) {
  TIFF.index = TIFF.read32();
  if(!TIFF.index) break;
  uint16 entryCount = TIFF.read();
  for(const CR2::Entry& e: TIFF.read<CR2::Entry>(entryCount)) { CR2::Entry& entry = (CR2::Entry&)e;
   if(entry.tag==0x103) { entry.value=0x879C; } // Compression
   if(entry.tag==0x117) entry.value = cr2.ljpeg.headerSize + size;
  }
 }
 return target;
}

buffer<byte> encodeLJPEG(const ref<byte> source, const CR2& cr2) {
 buffer<byte> target(cr2.tiffHeaderSize + cr2.dataSize*2); // Assumes JPEG < 2·rANS4
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

struct Compress {
 Compress() {
  array<String> allFiles = Folder(".").list(Files|Sorted);
  array<string> files;
  for(string argument: arguments()) {
   if(allFiles.contains(argument)) files.append(argument);
   else for(string name: allFiles) if (endsWith(name, argument)) files.append(name);
  }

  Time totalTime {true};
  size_t index = 0;
  // Removes JPGs with corresponding RAWs
  for(string name: files) {
   if(!endsWith(name, ".JPG")) continue;
   index++;
   log(index,"/",files.size, name);
   if(allFiles.contains(section(name,'.')+".CR2") || allFiles.contains(section(name,'.')+".ANS")) {
    assert_(existsFile(section(name,'.')+".CR2") || existsFile(section(name,'.')+".ANS"));
    log("Removing", name);
    //remove(name);
   }
  }
  log(totalTime);

  totalTime.reset();

  size_t totalSize = 0, stripSize = 0, ransSize = 0;
  for(string name: files) {
   if(!endsWith(name, ".CR2")) continue;
   index++;
   log(index,"/",files.size, name);
   if(1) { // Strips thumbnails
    Map source(name);
    CR2 cr2 (source, false /*onlyParse to skip verification*/); // Decodes
    buffer<byte> target = stripThumbnails(source, cr2);
    if(!existsFile(section(name,'.')+".cr2"))
     writeFile(section(name,'.')+".cr2", target);
    {CR2 target(Map(section(name,'.')+".cr2")); assert_(target.image == cr2.image && target.whiteBalance.G == cr2.whiteBalance.G);} // Verifies
    remove(name);
    rename(section(name,'.')+".cr2", name);
    stripSize += target.size;
   }
   if(1) { // Encodes rANS4
    if(existsFile(section(name,'.')+".ANS")) {
     size_t sourceSize = File(name).size();
     size_t targetSize = File(section(name,'.')+".ANS").size();
     log(str((sourceSize-              0)/1024/1024.,1u)+"MB", str(targetSize/1024/1024.,1u)+"MB",
         str((sourceSize-targetSize)/1024/1024.,1u)+"MB", str(100.*(sourceSize-targetSize)/targetSize,1u)+"%");
     continue;
    }
    Map source (name);
    CR2 cr2 (source); // Decodes LJPEG
    buffer<byte> target = encodeRANS4(source, cr2);
    if(!target) { log("Skipping", name); continue; }
    if(!existsFile(section(name,'.')+".ANS"))
     writeFile(section(name,'.')+".ANS", target);
    {CR2 ans(Map(section(name,'.')+".ANS")); assert_(ans.image == cr2.image && ans.whiteBalance.G == cr2.whiteBalance.G);} // Verifies

    totalSize += source.size;
    ransSize += target.size;
    log(str((source.size-              0)/1024/1024.,1u)+"MB", str(target.size/1024/1024.,1u)+"MB",
        str((source.size-target.size)/1024/1024.,1u)+"MB", str(100.*(source.size-target.size)/target.size,1u)+"%");
    //break;
    //remove(name);
   }
  }
  if(totalSize) log(str((totalSize-                0)/1024/1024.,1u)+"MB", str(ransSize/1024/1024.,1u)+"MB",
                    str((totalSize-ransSize)/1024/1024.,1u)+"MB", str(100.*(totalSize-ransSize)/ransSize, 1u)+"%");
  log(totalTime);
  totalTime.reset();
  for(string name: files) {
   if(!endsWith(name,".ANS")) continue;
   index++;
   log(index,"/",files.size, name);
   if(existsFile(section(name,'.')+".cr2")) continue;
   Map source(name);
   CR2 ans (source); // Decodes rANS4
   if(!existsFile(section(name,'.')+".cr2"))
    writeFile(section(name,'.')+".cr2", encodeLJPEG(source, ans));
   {CR2 cr2(Map(section(name,'.')+".cr2")); assert_(cr2.image == ans.image && cr2.whiteBalance.G == ans.whiteBalance.G);} // Verifies
   break; // TEST
   //remove(name);
  }
  log(totalTime);
 }
} app;
