//#include <exiv2/exiv2.hpp> //exiv2
#include "exif.h"
#include "string.h"
//String str(const std::string s) { return copyRef(string(s.data(), s.size())); }

map<String, Variant> parseExifTags(ref<byte> data) {
 map<String, Variant> tags;
#if EXIV2_HPP_
 using namespace Exiv2;
 Image::AutoPtr image = ImageFactory::open((const uint8*)data.data, data.size);
 image->readMetadata();
 for(Exifdatum e : image->exifData()) {
  Variant value = 0;
  if(e.typeId()==unsignedRational || e.typeId()==signedRational) value = {e.toRational().first, e.toRational().second};
  else if(e.typeId()!=undefined) value = str(e.toString());
  tags.insertMulti(str(e.key()), move(value));
 }
#else
 (void)data;
 //log("EXIF unsupported");
#endif
 return tags;
}
