#include "MusicXML.h"
#include "png.h"
#include "interface.h"
#include "window.h"

struct MusicTest {
 Map map;
 Image image;
 ImageView view;
 unique<Window> window = nullptr;
 MusicTest() {
  String imageFile; int2 size;
  auto list = currentWorkingDirectory().list(Files);
  for(const String& file: list) {
   TextData s (file);
   if(!s.match(arguments()[0])) continue;
   if(!s.match(".")) continue;
   if(!s.isInteger()) continue;
   const uint w = s.integer(false);
   if(!s.match("x")) continue;
   if(!s.isInteger()) continue;
   const uint h = s.integer(false);
   if(s) continue;
   size = int2(w, h);
   imageFile = copyRef(file);
  }
  if(!imageFile) {
   Image image = decodePNG(readFile(arguments()[0]+".png"));
   size = image.size;
   imageFile = arguments()[0]+"."+strx(size);
   writeFile(imageFile, cast<byte>(image));
  }
  map = Map(imageFile);
  image = Image(cast<byte4>(unsafeRef(map)), size);
  view = cropRef(image,0,int2(1366*2,870));
  window = ::window(&view);
 }
} test;
