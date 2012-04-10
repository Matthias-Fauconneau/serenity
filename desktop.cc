#include "process.h"
#include "interface.h"
#include "window.h"
#include "launcher.h"
#include "calendar.h"
#include "feeds.h"

struct Desktop : Application {
    Feeds feeds;
    List<Command> shortcuts = readShortcuts();
     Clock clock { 128 };
     Calendar calendar;
    VBox timeBox { &clock, &calendar };
    HBox applets { &feeds, &timeBox, &shortcuts };
    Window window{&applets,"Desktop"_,Image(),int2(0,Window::screen.y-16)};
    Desktop(array<string>&&) { /*window.setType(Atom("_NET_WM_WINDOW_TYPE_DESKTOP"_));*/ }
};
Application(Desktop)
