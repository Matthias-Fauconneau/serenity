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
    Events calendar;
    VBox timeBox;//  __(&clock, &calendar);
    HBox applets;// __(&feeds, &timeBox, &shortcuts);
    Window window __(&applets,int2(0,0),""_,Image(),"_NET_WM_WINDOW_TYPE_DESKTOP"_,Bottom);
    Window browser __(&page.area(),int2(0,0),"Browser"_);
    ICON(shutdown) Command shutdown __(share(shutdownIcon()),string("Shutdown"_),string("/sbin/poweroff"_),{});
    Desktop() {
        timeBox<<&clock<<&calendar; applets<<&feeds<<&timeBox<<&shortcuts; shutdown.main=Linear::Center;
        clock.timeout.connect(&window, &Window::render);
        feeds.listChanged.connect(&window,&Window::render);
        feeds.pageChanged.connect(this,&Desktop::showPage);
        browser.localShortcut(Escape).connect(&browser, &Window::hide);
        browser.localShortcut(RightArrow).connect(&feeds, &Feeds::readNext);
        //browser.localShortcut(Extra).connect(&feeds, &Feeds::readNext);
        //window.localShortcut(Power).connect(&shutdown, &Window::toggle);
        window.localShortcut(Escape).connect(this, &Application::quit);
        window.show();
    }
    void showPage(const ref<byte>& link, const ref<byte>& title, const Image& favicon) {
        if(!link) { browser.hide(); return; }
        page.delta=int2(0,0);
        page.contentChanged.connect(&browser, &Window::render); browser.setIcon(favicon); browser.setTitle(title);
        page.go(link);
        browser.show();
    }
};
Application(Desktop)
