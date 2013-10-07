#include "scene.h"
#include "view.h"
#include "window.h"

struct Sponza {
    Window window {0, int2(1050,590), "Sponza"_, Image(), Window::OpenGL};
    Scene scene;
    View view {scene};
    Sponza() {
        window.localShortcut(Escape).connect([]{exit();});
        window.widget=window.focus=window.directInput=&view;
        view.contentChanged.connect(&window,&Window::render);
        //view.statusChanged.connect([this](string status){window.setTitle(status);});
        window.clearBackground = false;
        window.show();
    }
} application;
