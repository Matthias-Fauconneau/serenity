#include "process.h"
#include "window.h"
#include "display.h"
#include "interface.h"
#include "launcher.h"
#include "calendar.h"
#include "feeds.h"
#include "popup.h"

template struct Array<Command>;
template struct ListSelection<Command>;
template struct List<Command>;
template struct Popup<Command>;

ICON(shutdown);

struct Desktop : Application {
    Feeds feeds;
    List<Command> shortcuts = readShortcuts();
     Clock clock { 128 };
     Calendar calendar;
    VBox timeBox { &clock, &calendar };
    HBox applets { &feeds, &timeBox, &shortcuts };
    Window window{&applets,""_,Image<byte4>(),int2(0,display.y-16)};
    Popup<Command> shutdownPopup { Command(move(shutdownIcon),"Shutdown"_,"/sbin/poweroff"_,{}) };
    Desktop(array<string>&& arguments) {
        if(contains(arguments,"setAllRead"_)) feeds.setAllRead();
        feeds.contentChanged.connect(&window,&Window::render);
        clock.timeout.connect(&window, &Window::render);
        //window.setType(Atom(_NET_WM_WINDOW_TYPE_DESKTOP));
        window.show();
        window.localShortcut(Key::Escape).connect(&shutdownPopup,&Popup<Command>::toggle);
        window.localShortcut(Key::Power).connect(this,&Desktop::keyPress);
    }
    void keyPress() { if(window.hasFocus()) shutdownPopup.toggle(); }
};
Application(Desktop)
