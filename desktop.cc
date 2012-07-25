#include "process.h"
#include "window.h"
#include "display.h"
//#include "launcher.h"
//#include "calendar.h"
#include "feeds.h"
//#include "popup.h"
#include "array.cc"

//ICON(shutdown)

struct Desktop : Application {
    Feeds feeds;
    //List<Command> shortcuts;// = readShortcuts();
     //Clock clock { 128 };
     //Calendar calendar;
    //VBox timeBox { &clock, &calendar };
    HBox applets; //{ &feeds /*, &timeBox *//*, &shortcuts*/ };
    Window window{&applets,int2(0,display.y-16)};
    //Popup<Command> shutdownPopup { Command(move(shutdownIcon),"Shutdown"_,"/sbin/poweroff"_,{}) };
    Desktop(array<string>&& /*arguments*/) {
        applets << &feeds; //clang doesn't support expression in initializer_list
        //if(contains(arguments,"setAllRead"_)) feeds.setAllRead();
        feeds.contentChanged.connect(&window,&Window::render);
        //clock.timeout.connect(&window, &Window::render);
        //window.localShortcut(Key::Escape).connect(&shutdownPopup,&Window::toggle);
        //window.localShortcut(Key::Power).connect(&shutdownPopup,&Window::toggle);
        window.localShortcut(Key::Escape).connect(&window,&Window::hide);
        window.show();
    }
};
Application(Desktop)
