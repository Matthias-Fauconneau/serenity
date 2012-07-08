#include "window.h"
#include "interface.h"

template<class T> struct Popup : T {
    Window window{this,""_,Image<byte4>(),int2(300,300)};
    Popup(T&& t=T()) : T(move(t)) {
        //window.setType(Atom(_NET_WM_WINDOW_TYPE_DROPDOWN_MENU));
        //window.localShortcut("Leave"_).connect(&window,&Window::hide);
    }
    void toggle() { if(window.visible) window.hide(); else { T::update(); window.show(); } }
};
