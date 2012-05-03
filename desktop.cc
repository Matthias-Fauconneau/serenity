#include "process.h"
#include "interface.h"
#include "window.h"
#include "launcher.h"
#include "calendar.h"
#include "feeds.h"

ICON(shutdown);

struct Desktop : Application {
#if __arm__
    Input buttons {"/dev/input/event4"};
#endif
    Feeds feeds;
    List<Command> shortcuts = readShortcuts();
     Clock clock { 128 };
     Calendar calendar;
    VBox timeBox { &clock, &calendar };
    HBox applets { &feeds, &timeBox, &shortcuts };
    Window window{&applets,""_,Image(),int2(0,Window::screen.y-16)};
    Popup<Command> shutdownPopup { Command(move(shutdownIcon),"Shutdown"_,"/sbin/poweroff"_,{}) };
    Desktop(array<string>&& arguments) {
        if(contains(arguments,"setAllRead"_)) feeds.setAllRead();
        feeds.contentChanged.connect(&window,&Window::update);
        clock.timeout.connect(&window, &Window::render);
        window.setType(Atom(_NET_WM_WINDOW_TYPE_DESKTOP));
        window.show();
        window.localShortcut("Escape"_).connect(&shutdownPopup,&Popup<Command>::toggle);
#if __arm__
        buttons.keyPress[KEY_POWER].connect(this,&Desktop::keyPress);
#endif
    }
    void keyPress() { if(window.hasFocus()) shutdownPopup.toggle(); }
};
Application(Desktop)
