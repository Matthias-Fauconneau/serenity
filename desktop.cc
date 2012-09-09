#include "process.h"
#include "window.h"
#include "calendar.h"
#include "feeds.h"

constexpr ref<byte> iconPaths[4] = {
    "/usr/share/pixmaps/"_,
    "/usr/share/icons/oxygen/$size/apps/"_,
    "/usr/share/icons/hicolor/$size/apps/"_,
    "/usr/local/share/icons/hicolor/$size/apps/"_
};

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

struct Command : Item {
    string path; array<string> args;
    Command(Image&& icon, string&& text, string&& path, array<string>&& args) :
        Linear(Left),Item(move(icon),move(text)),path(move(path)),args(move(args)){}
    bool mouseEvent(int2 cursor, int2 size, Event event, MouseButton) override;
};

bool Command::mouseEvent(int2, int2, Event event, MouseButton button) {
    if(event == Press && button == LeftButton) { execute(path,args,false); }
    return false;
}

map<string,string> readSettings(const ref<byte>& path) {
    map<string,string> entries;
    if(!existsFile(path)) { warn("Missing settings","'"_+path+"'"_); return entries; }
    for(TextData s=readFile(path);s;) {
        if(s.matchAny("[#"_)) s.until('\n');
        else {
            ref<byte> key = s.until('='), value=s.until('\n');
            entries.insertMulti(string(key),string(value));
        }
        s.whileAny("\n"_);
    }
    return entries;
}

struct Desktop : Application {
    Feeds feeds;
    Scroll<HTML> page;
    Clock clock __( 64 );
    Events calendar;
    VBox timeBox;//  __(&clock, &calendar);
    List<Command> shortcuts;
    HBox applets;// __(&feeds, &timeBox, &shortcuts);
    Window window __(&applets,0,"Desktop"_,Image(),"_NET_WM_WINDOW_TYPE_DESKTOP"_);
    Window browser __(&page.area(),0,"Browser"_);
    ICON(shutdown)
    Desktop() {
        if(!existsFile("launcher"_,config())) warn("No launcher settings [.config/launcher]");
        else {
            auto apps = readFile("launcher"_,config());
            for(const ref<byte>& desktop: split(apps,'\n')) {
                if(startsWith(desktop,"#"_)) continue;
                map<string,string> entries = readSettings(desktop);
                Image icon;
                for(const ref<byte>& folder: iconPaths) {
                    string path = replace(folder,"$size"_,"32x32"_)+entries[string("Icon"_)]+".png"_;
                    if(existsFile(path)) { icon=resize(decodeImage(readFile(path)), 32,32); break; }
                }
                string path = string(section(entries[string("Exec"_)],' '));
                if(!existsFile(path)) path="/usr/bin/"_+path;
                if(!existsFile(path)) { warn("Executable not found",path); continue; }
                array<string> arguments;  arguments<<string(section(entries[string("Exec"_)],' ',1,-1));
                for(string& arg: arguments) arg=replace(arg,"\"%c\""_,entries[string("Name"_)]);
                for(uint i=0;i<arguments.size();) if(arguments[i].contains('%')) arguments.removeAt(i); else i++;
                shortcuts << Command(move(icon),move(entries[string("Name"_)]),move(path),move(arguments));
            }
        }
        shortcuts<<Command(share(shutdownIcon()),string("Shutdown"_),string("/sbin/poweroff"_),__());

        timeBox<<&clock<<&calendar;
        applets<<&feeds<<&timeBox<<&shortcuts;
        clock.timeout.connect(&window, &Window::render);
        feeds.listChanged.connect(&window,&Window::render);
        feeds.pageChanged.connect(this,&Desktop::showPage);
        browser.localShortcut(Escape).connect(&browser, &Window::hide);
        browser.localShortcut(RightArrow).connect(&feeds, &Feeds::readNext);
        window.show();
    }
    void showPage(const ref<byte>& link, const ref<byte>& title, const Image& favicon) {
        if(!link) { browser.hide(); return; }
        page.delta=0;
        page.contentChanged.connect(&browser, &Window::render);
        browser.setTitle(title);
        browser.setIcon(favicon);
        page.go(link);
        browser.show();
    }
};
Application(Desktop)
