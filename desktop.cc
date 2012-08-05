#include "process.h"
#include "window.h"
#include "display.h"
//#include "launcher.h"
#include "calendar.h"
#include "feeds.h"
//#include "popup.h"
//ICON(shutdown)
#include "array.cc"

struct Desktop : Application {
    Text status;
    Feeds feeds;
    Scroll<HTML> page;
    //List<Command> shortcuts;// = readShortcuts();
    Clock clock { 64 };
    Calendar calendar;
    VBox timeBox { &clock, &calendar };
    HBox applets{ &feeds, &timeBox/*, &shortcuts*/ };
    Window window = i({&applets,int2(0,::display().y-16)});
    //Command shutdown {move(shutdownIcon),"Shutdown"_,"/sbin/poweroff"_,{}) };
    Desktop() {
        clock.timeout.connect(&window, &Window::render);
        feeds.listChanged.connect(&window,&Window::render);
        feeds.pageChanged.connect(this,&Desktop::showPage);
        window.localShortcut(Key::RightArrow).connect(&feeds, &Feeds::readNext);
        window.localShortcut(Key::Extra).connect(&feeds, &Feeds::readNext);
        window.localShortcut(Key::Power).connect(this, &Desktop::showDesktop);
        window.localShortcut(Key::Escape).connect(this, &Desktop::showDesktop);
        window.show();
    }
    void showDesktop() {
        if(window.widget != &applets) window.setWidget(&applets);
        //else window.setWidget(&shutdown);
        else quit(); //DEBUG
    }
    void showPage(const ref<byte>& link) {
        if(!link) { showDesktop(); return; }
        window.setWidget( &page.parent() );
        page.contentChanged.connect(&window, &Window::render);
        page.go(link);
        status.setText("Loading "_+link); status.render(int2(0,0));
    }
};
Application(Desktop)
