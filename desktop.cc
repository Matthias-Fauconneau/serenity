#include "process.h"
#include "window.h"
#include "calendar.h"
#include "feeds.h"
#include "launcher.h"

struct Desktop : Application {
    Text status;
    Feeds feeds;
    Scroll<HTML> page;
    List<Command> shortcuts = readShortcuts();
    Clock clock __( 64 );
    Calendar calendar;
    VBox timeBox;//  __(&clock, &calendar);
    HBox applets;// __(&feeds, &timeBox, &shortcuts);
    Window window __(&applets,int2(0,0),"Desktop"_);
    ICON(shutdown) Command shutdown __(share(shutdownIcon()),string("Shutdown"_),string("/sbin/poweroff"_),{});
    Desktop() { timeBox<<&clock<<&calendar; applets<<&feeds<<&timeBox<<&shortcuts; shutdown.main=Linear::Center;
        clock.timeout.connect(&window, &Window::render);
        feeds.listChanged.connect(&window,&Window::render);
        feeds.pageChanged.connect(this,&Desktop::showPage);
        window.localShortcut(RightArrow).connect(&feeds, &Feeds::readNext);
        //window.localShortcut(Extra).connect(&feeds, &Feeds::readNext);
        //window.localShortcut(Power).connect(this, &Desktop::showDesktop);
        window.localShortcut(Escape).connect(this, &Desktop::showDesktop);
        window.show();
    }
    void showDesktop() {
        if(window.widget != &applets) { window.widget= &applets; window.setTitle("Desktop"_); }
        else quit(); //{ window.widget= &shutdown; window.setTitle("Shutdown?"_); }
        window.render();
    }
    void showPage(const ref<byte>& link, const ref<byte>& title, const Image& favicon) {
        if(!link) { showDesktop(); return; }
        window.widget= &page.area();
        page.contentChanged.connect(&window, &Window::render);
        page.go(link);
        window.setTitle(title); window.setIcon(favicon);
    }
};
Application(Desktop)
