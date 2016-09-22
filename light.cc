#include "png.h"
#include "interface.h"
#include "window.h"

Folder folder (arguments()[0]);
ImageView view (decodeImage(readFile(folder.list(Files)[0], folder)));
unique<Window> mainWindow = ::window(&view);

struct Light {
    Light() {

    }
};
