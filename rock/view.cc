#include "view.h"
#include "text.h"
#include "interface.h"
#include "window.h"

class(TextView, View), virtual Text {
    bool view(const string& metadata, const string& name, const buffer<byte>& data) override {
        if(text) return false;
        if(endsWith(metadata, "scalar"_)) { setText(name+": "_+data); title = String(name); return true; }
        else if(endsWith(metadata,"text"_)||endsWith(metadata,"label"_)||endsWith(metadata,("size"_))||endsWith(metadata,("map"_))) {
            assert_(data && isUTF8(data), name, metadata); setText(data); title = String(name); return true;
        }
        return false;
    }
    string name() override { return title; }
    String title;
};

class(ImageView, View), virtual ImageWidget {
    bool view(const string&, const string& name, const buffer<byte>& data) override {
        if(image || !imageFileFormat(data)) return false;
        image = decodeImage(data);
        title = String(name);
        return image ? true : false;
    }
    int2 sizeHint() override { while(!(image.size()<displaySize)) image=resize(image,image.size()/2); return ImageWidget::sizeHint(); }
    string name() override { return title; }
    String title;
};
