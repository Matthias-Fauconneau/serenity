#include "thread.h"
#include "keyboard.h"
#include "window.h"
#include "interface.h"

static Image image() {
 //Image target(int2(1920, 210));
 Image target(int2(1366, 210*1366/1920));
 target.clear(byte4(0xFF,0,0xFF));
 Keyboard keyboard;
 keyboard.left.append(69);
 keyboard.right.append(80);
 keyboard.render(target);
 return target;
}
ImageView view = image();
unique<Window> test = ::window(&view);
