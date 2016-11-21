#include "window.h"
#include "interface.h"
#include "jpeg.h"

struct Sort {
 const Folder& folder = currentWorkingDirectory();
 Folder minus {"-",folder,true};
 Folder equal {"=",folder,true};
 Folder plus {"+",folder,true};

 String name;
 ImageView image;
 unique<Window> view = nullptr;
 Sort() {
  update();
  view = ::window(&image);
  view->actions[LeftArrow] = [this](){ rename(folder, name, minus, name); };
  view->actions[DownArrow] = [this](){ rename(folder, name, minus, name); };
  view->actions[UpArrow] = [this](){ rename(folder, name, minus, name); };
 }
 void update() {
  name = move(folder.list(Files).first());
  image = decodeJPEG(readFile(name));
  if(view) view->render();
  log(name, image.image.size);
 }
} app;
