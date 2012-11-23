/// \file desktop.cc Background shell application
#include "process.h"
#include "window.h"
#include "calendar.h"
#include "feeds.h"

struct Search : TextInput {
    string browser;
    signal<> triggered;
    bool keyPress(Key key) override;
};

bool Search::keyPress(Key key) {
    if(key == Return) {
        array<string> args; args<<string("google.com/search?q="_+text); execute("/usr/bin/chromium-browser"_,args,false);
        setText(string()); triggered(); return true;
    }
    else return TextInput::keyPress(key);
}

/// Executes \a path with \a args when pressed
struct Command : Item {
    string path; array<string> args;
    Command(Image&& icon, string&& text, string&& path, array<string>&& args) :
        Linear(Left),Item(move(icon),move(text)),path(move(path)),args(move(args)){}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button) override;
};

bool Command::mouseEvent(int2, int2, Event event, Button button) {
    if(event == Press && button == LeftButton) { execute(path,args,false); }
    return false;
}

map<ref<byte>,ref<byte> > readSettings(const ref<byte>& file) {
    map<ref<byte>,ref<byte> > entries;
    for(TextData s(file);s;) {
        if(s.matchAny("[#"_)) s.line();
        else {
            ref<byte> key = s.until('='), value=s.line();
            entries.insertMulti(key,value);
        }
        s.whileAny("\n"_);
    }
    return entries;
}

/// Displays a feed reader, an event calendar and an application launcher (activated by taskbar home button)
struct Desktop {
    Feeds feeds;
    Scroll<HTML> page;
    Clock clock __( 64 );
    Events calendar;
    VBox timeBox;//  __(&clock, &calendar);
    List<Command> shortcuts;
    HBox applets;// __(&feeds, &timeBox, &shortcuts);
    Window window __(&applets,0,"Desktop"_,Image(),"_NET_WM_WINDOW_TYPE_DESKTOP"_);
    Window browser __(0,0,"Browser"_);
    Desktop() {
        if(!existsFile("launcher"_,config())) warn("No launcher settings [.config/launcher]");
        else {
            auto apps = readFile("launcher"_,config());
            for(const ref<byte>& desktop: split(apps,'\n')) {
                if(startsWith(desktop,"#"_)) continue;
                if(!existsFile(desktop)) { warn("Missing settings","'"_+desktop+"'"_); continue; }
                string file = readFile(desktop);
                map<ref<byte>,ref<byte> > entries = readSettings(file);

                static constexpr ref<byte> iconPaths[] = {
                    "/usr/share/pixmaps/"_,
                    "/usr/share/icons/oxygen/32x32/apps/"_,
                    "/usr/share/icons/hicolor/32x32/apps/"_,
                    "/usr/share/icons/oxygen/32x32/actions/"_,
                };
                Image icon;
                for(const ref<byte>& folder: iconPaths) {
                    string path = folder+entries["Icon"_]+".png"_;
                    if(existsFile(path)) { icon=resize(decodeImage(readFile(path)), 32,32); break; }
                }
                assert(icon,entries["Icon"_]);
                string exec = string(section(entries["Exec"_],' '));
                string path = copy(exec);
                if(!existsFile(path)) path="/usr/bin/"_+exec;
                if(!existsFile(path)) path="/usr/local/bin/"_+exec;
                if(!existsFile(path)) path="/bin/"_+exec;
                if(!existsFile(path)) path="/sbin/"_+exec;
                if(!existsFile(path)) { warn("Executable not found",exec); continue; }
                array<string> arguments;  arguments<<string(section(entries["Exec"_],' ',1,-1));
                for(string& arg: arguments) arg=replace(arg,"\"%c\""_,entries["Name"_]);
                for(uint i=0; i<arguments.size();) if(arguments[i].contains('%')) arguments.removeAt(i); else i++;
                shortcuts << Command(move(icon),string(entries["Name"_]),move(path),move(arguments));
            }
        }

        timeBox<<&clock<<&calendar;
        applets<<&feeds<<&timeBox<<&shortcuts;
        clock.timeout.connect(&window, &Window::render);
        feeds.listChanged.connect(&window,&Window::render);
        feeds.pageChanged.connect(this,&Desktop::showPage);
        browser.localShortcut(Escape).connect(&browser, &Window::destroy);
        browser.localShortcut(RightArrow).connect(&feeds, &Feeds::readNext);
    }
    void showPage(const ref<byte>& link, const ref<byte>& title, const Image& favicon) {
        if(!link) { exit(); return; } // Exits application to free any memory leaks (will be restarted by .xinitrc)
        page.delta=0;
        page.contentChanged.connect(&browser, &Window::render);
        if(!browser.created) browser.create(); //might have been closed by user
        browser.setTitle(title);
        browser.setIcon(favicon);
        browser.setType("_NET_WM_WINDOW_TYPE_NORMAL"_);
        page.go(link);
        browser.widget=&page.area(); browser.show();
    }
} application;
