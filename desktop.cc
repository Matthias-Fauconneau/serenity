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
    Scroll<HTML> page;
    //List<Command> shortcuts;// = readShortcuts();
     //Clock clock { 128 };
     //Calendar calendar;
    //VBox timeBox { &clock, &calendar };
    HBox applets; //{ &feeds /*, &timeBox *//*, &shortcuts*/ };
    Window window = i({&applets,int2(0,::display().y-16)});
    //Popup<Command> shutdownPopup { Command(move(shutdownIcon),"Shutdown"_,"/sbin/poweroff"_,{}) };
    Desktop() {
        applets << &feeds; //clang doesn't support expression in initializer_list
        //clock.timeout.connect(&window, &Window::render);
        feeds.listChanged.connect(&window,&Window::render);
        feeds.pageChanged.connect(this,&Desktop::showPage);
        window.localShortcut(Key::Right).connect(&feeds, &Feeds::readNext);
        window.localShortcut(Key::Extra).connect(&feeds, &Feeds::readNext);
        //window.localShortcut(Key::Escape).connect(&window, &Window::hide); //back to desktop
        //window.localShortcut(Key::Power).connect(&window, &Window::hide); //back to desktop
        //window.localShortcut(Key::Escape).connect(&shutdownPopup,&Window::toggle);
        //window.localShortcut(Key::Power).connect(&shutdownPopup,&Window::toggle);
        window.localShortcut(Key::Escape).connect(&window,&Window::hide); //DEBUG: quit desktop (return to X)
        window.show();
    }
    void showPage(const ref<byte>& link) {
        window.setWidget( &page.parent() );
        page.contentChanged.connect(&window, &Window::render);
        page.go(link);
    }
};
Application(Desktop)
