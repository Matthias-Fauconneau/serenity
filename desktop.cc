#include "process.h"
#include "window.h"
#include "display.h"
#include "calendar.h"
#include "feeds.h"
//#include "launcher.h"
ICON(shutdown)

struct Desktop : Application {
    Text status;
    Feeds feeds;
    Scroll<HTML> page;
    //List<Command> shortcuts;// = readShortcuts();
    Clock clock = i({ 64 });
    Calendar calendar;
    VBox timeBox = i({ &clock, &calendar });
    HBox applets ;//= i({ &feeds, &timeBox/*, &shortcuts*/ });
    Window window = i({&applets,int2(256,255)/*int2(0,-16)*/,string("Desktop"_),shutdownIcon()});
    //Command shutdown {share(shutdownIcon()),"Shutdown"_,"/sbin/poweroff"_,{}) };
    Desktop() {
        clock.timeout.connect(&window, &Window::render);
        feeds.listChanged.connect(&window,&Window::update);
        feeds.pageChanged.connect(this,&Desktop::showPage);
        window.localShortcut(Right).connect(&feeds, &Feeds::readNext);
        //window.localShortcut(Extra).connect(&feeds, &Feeds::readNext);
        //window.localShortcut(Power).connect(this, &Desktop::showDesktop);
        window.localShortcut(Escape).connect(this, &Desktop::showDesktop);
        window.show();
    }
    void showDesktop() {
        if(window.widget != &applets) { window.setWidget(&applets); window.update(); window.render(); }
        //else window.setWidget(&shutdown);
        else quit(); //DEBUG
    }
    void showPage(const ref<byte>& link) {
        if(!link) { showDesktop(); return; }
        window.setWidget( &page.parent() );
        page.contentChanged.connect(&window, &Window::update);
        page.go(link);
        status.setText("Loading "_+link); status.render(int2(0,0));
    }
};
Application(Desktop)
