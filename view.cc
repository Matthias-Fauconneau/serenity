#include "window.h"
#include "interface.h"
#include "png.h"

ImageView image = decodeImage(readFile(arguments()[0]));
unique<Window> view = ::window(&image);
FileWatcher fileWatcher(arguments()[0], [](string){ image = decodeImage(readFile(arguments()[0])); view->render(); });
