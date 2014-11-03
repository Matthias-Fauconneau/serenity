#include <exiv2/exiv2.hpp> //exiv2
#include "exif.h"
#include "string.h"
String str(const std::string s) { return copyRef(string(s.data(), s.size())); }

map<String, Variant> parseExifTags(ref<byte> data) {
    using namespace Exiv2;
    Image::AutoPtr image = ImageFactory::open((const uint8*)data.data, data.size);
    image->readMetadata();
    map<String, Variant> tags;
    for(Exifdatum e : image->exifData()) {
        Variant value = 0;
        if(e.typeId()==unsignedRational || e.typeId()==signedRational) value = {e.toRational().first, e.toRational().second};
        else if(e.typeId()!=undefined) value = str(e.toString());
        tags.insertMulti(str(e.key()), move(value));
    }
    return tags;
}
