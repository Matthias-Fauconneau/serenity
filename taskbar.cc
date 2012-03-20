#include "process.h"
#include "time.h"
#include "interface.h"
#include "window.h"
#include "launcher.h"
#include "poll.h"
#include "array.cc"

#include "dbus.h"
struct StatusNotifierItem : TriggerButton {
    DBus::Object item;
    StatusNotifierItem(DBus::Object&& item, Image&& icon) : TriggerButton(move(icon)), item(move(item)) {}
    bool mouseEvent(int2 position, Event event, Button button) override {
        if(TriggerButton::mouseEvent(position,event,button)) {
            item.noreply("org.kde.StatusNotifierItem.Activate"_,0,0);
            return true;
        }
        return false;
    }
};

struct Task : Item {
    XID id;
    Task(XID id):id(id){} //for indexOf
    Task(XID id, Icon&& icon, Text&& text):Item(move(icon),move(text)),id(id){}
};
bool operator==(const Task& a,const Task& b){return a.id==b.id;}

struct Clock : Text, Timer {
    signal<> render;
    signal<> triggered;
    Clock():Text(date("hh:mm"_)){ setAbsolute(getUnixTime()/60*60+60); }
    void expired() { text=date("hh:mm"_); update(); setAbsolute(getUnixTime()+60); render.emit(); }
    bool mouseEvent(int2, Event event, Button button) override {
        if(event==Press && button==LeftButton) { triggered.emit(); return true; }
        return false;
    }
};

ICON(shutdown);

struct Desktop {
    Space space;
    List<Command> shortcuts { readShortcuts() };
    List<Command> system { Command(move(shutdownIcon),"Shutdown"_,"/sbin/poweroff"_) };
    HBox applets { &space, &shortcuts, &system };
    Window window{&applets,int2(0,0),""_,Image(),255};
    Desktop() { window.setType("_NET_WM_WINDOW_TYPE_DESKTOP"_); }
};

struct Month : Grid<Text> {
    Month() : Grid(7,6) {
        Date date = currentDate();
        static const string days[7] = {"Mo"_,"Tu"_,"We"_,"Th"_,"Fr"_,"Sa"_,"Su"_};
        const int nofDays[12] = { 31, !(date.year%4)&&(date.year%400)?29:28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        for(int i=0;i<7;i++) append(copy(days[i]));
        int first = (35+date.weekDay+1-date.day)%7;
        for(int i=0;i<first;i++) append(Text(dec(nofDays[(date.month+11)%12]-first+i+1,2),16,128)); //previous month
        for(int i=0;i<nofDays[date.month];i++) append(dec(i+1,2)); //current month
        for(int i=1;count()<7*6;i++) append(Text(dec(i,2),16,128)); //next month
    }
};

struct Calendar {
      Text date { ::date("dddd, dd MMMM yyyy"_) };
      Month month;
     Menu menu { &date, &month };
    Window window{&menu,int2(-2,-2)};
    Calendar() {
        window.setType("_NET_WM_WINDOW_TYPE_DROPDOWN_MENU"_);
        window.setOverrideRedirect(true);
        menu.close.connect(&window,&Window::hide);
    }
    bool mouseEvent(int2, Event event, Button) {
        if(event==Leave) { window.hide(); return true; }
        return false;
    }
    void show() { window.show(); window.setPosition(int2(-menu.Widget::size.x,0)); }
};

ICON(button);

struct TaskBar : Poll {
    Display* x;
    DBus dbus;
    bool ownWM=false; //when no WM is detected, basic window management will be provided
    signal<int> tasksChanged;

      TriggerButton start;
       Launcher launcher;
      Bar<Task> tasks;
      Bar<StatusNotifierItem> status;
      Clock clock;
       Calendar calendar;
     HBox panel {&start, &tasks, &status, &clock };
    Window window{&panel,int2(0,-1),"TaskBar"_,Image(),255};

    string getTitle(XID id) { return Window::getProperty<char>(id,"_NET_WM_NAME"); }
    Image getIcon(XID id) {
        array<ulong> buffer = Window::getProperty<ulong>(id,"_NET_WM_ICON");
        if(buffer.size()<=2) return Image();
        array<byte4> image(buffer.size());
        for(uint i=0;i<buffer.size()-2;i++) image << *(byte4*)&buffer[i+2];
        return resize(Image(move(image), buffer[0], buffer[1]), 16,16);
    }
    bool skipTaskbar(XID id) {
        return Window::getProperty<Atom>(id,"_NET_WM_WINDOW_TYPE") != array<Atom>{Atom(_NET_WM_WINDOW_TYPE_NORMAL)}
        || contains(Window::getProperty<Atom>(id,"_NET_WM_STATE"),Atom(_NET_WM_SKIP_TASKBAR));
    }
    int addTask(XID id) {
        if(skipTaskbar(id)) return -1;
        string title = getTitle(id);
        if(!title) return -1;
        Image icon = getIcon(id);
        tasks << Task(id,move(icon),move(title));
        return tasks.array::size()-1;
    }

    void registerStatusNotifierItem(string service) {
        for(const auto& s: status) if(s.item.target == service) return;

        DBus::Object DBus = dbus("org.freedesktop.DBus/org/freedesktop/DBus"_);
        DBus("org.freedesktop.DBus.AddMatch"_,string("member='NameOwnerChanged',arg0='"_+service+"'"_));

        DBus::Object item(&dbus,move(service),"/StatusNotifierItem"_);
        Image icon;
        string name = item.get<string>("IconName"_);
        for(const string& folder: iconPaths) {
            string path = replace(folder,"$size"_,"16x16"_)+name+".png"_;
            if(exists(path)) { icon=resize(Image(readFile(path)), 16,16); break; }
        }
        if(!icon) {
            auto icons = item.get< array<DBusIcon> >("IconPixmap"_);
            assert(icons,name,"not found");
            DBusIcon dbusIcon = move(icons.first());
            icon=swap(resize(Image(cast<byte4>(move(dbusIcon.data)),dbusIcon.width,dbusIcon.height),16,16));
        }
        status << StatusNotifierItem(move(item), move(icon));
        panel.update(); window.render();
    }
    variant<int> Get(string interface, string property) {
        if(interface=="org.kde.StatusNotifierWatcher"_ && property=="ProtocolVersion"_) return 1;
        else error(interface, property);
    }
    void removeStatusNotifierItem(string name, string, string) {
        for(uint i=0;i<status.array<StatusNotifierItem>::size();i++) if(status[i].item.target == name) status.removeAt(i);
        panel.update(); window.render();
    }

    static int xErrorHandler(Display* x, XErrorEvent* error) {
        char buffer[64]; XGetErrorText(x,error->error_code,buffer,sizeof(buffer)); log(buffer);
        return 0;
    }
    TaskBar() {
        XSetErrorHandler(xErrorHandler);
        x = XOpenDisplay(0);
        XSetWindowAttributes attributes; attributes.cursor=XCreateFontCursor(x,68);
        XChangeWindowAttributes(x,DefaultRootWindow(x),CWCursor,&attributes);
        registerPoll({XConnectionNumber(x), POLLIN});
        array<XID> list = Window::getProperty<XID>(DefaultRootWindow(x),"_NET_CLIENT_LIST");
        if(!list && !Window::getProperty<XID>(DefaultRootWindow(x),"_NET_SUPPORTING_WM_CHECK")) {
            XID root,parent; XID* children; uint count=0; XQueryTree(x,DefaultRootWindow(x),&root,&parent,&children,&count);
            list = array<XID>(children,count);
            ownWM = true;
        }
        for(XID id: list) {
            addTask(id);
            XSelectInput(x, id, StructureNotifyMask|SubstructureNotifyMask|PropertyChangeMask);
        }

        dbus.bind("Get"_,this,&TaskBar::Get);
        dbus.bind("RegisterStatusNotifierItem"_,this,&TaskBar::registerStatusNotifierItem);
        dbus.connect("NameOwnerChanged"_,this,&TaskBar::removeStatusNotifierItem);
        DBus::Object DBus = dbus("org.freedesktop.DBus/org/freedesktop/DBus"_);
        array<string> names = DBus("org.freedesktop.DBus.ListNames"_);
        for(string& name: names) if(startsWith(name,"org.kde.StatusNotifierItem"_)) registerStatusNotifierItem(move(name));
        DBus("org.freedesktop.DBus.RequestName"_, "org.kde.StatusNotifierWatcher"_, (uint)0);

         start.image = resize(buttonIcon, 16,16);
         start.triggered.connect(&launcher,&Launcher::show);
         tasks.expanding=true;
         tasks.activeChanged.connect(this,&TaskBar::raise);
         clock.render.connect(&window, &Window::render);
         clock.triggered.connect(&calendar,&Calendar::show);
        panel.update();

        window.setType("_NET_WM_WINDOW_TYPE_DOCK"_);
        window.show();
        window.setPosition(int2(0,0));
        XSelectInput(x,DefaultRootWindow(x),SubstructureNotifyMask|PropertyChangeMask);
        XFlush(x);
    }
    void raise(int) {
        XMapWindow(x, tasks.active().id);
        XRaiseWindow(x, tasks.active().id);
        XSetInputFocus(x, tasks.active().id, RevertToPointerRoot, CurrentTime);
        XEvent xev; clear(xev);
        xev.type = ClientMessage;
        xev.xclient.window = tasks.active().id;
        xev.xclient.message_type = Atom(_NET_ACTIVE_WINDOW);
        xev.xclient.format = 32;
        xev.xclient.data.l[0] = 2;
        xev.xclient.data.l[1] = 0;
        xev.xclient.data.l[2] = 0;
        XSendEvent(x, DefaultRootWindow(x), 0, SubstructureNotifyMask, &xev);
        XFlush(x);
    }
    void event(pollfd) override {
        bool needUpdate = false;
        while(XEventsQueued(x, QueuedAfterFlush)) { XEvent e; XNextEvent(x,&e);
            if(e.type==PropertyNotify && e.xproperty.window==DefaultRootWindow(x) && e.xproperty.atom == Atom(_NET_ACTIVE_WINDOW)) {
                XID id = Window::getProperty<XID>(DefaultRootWindow(x),"_NET_ACTIVE_WINDOW").first();
                int i = indexOf(tasks, Task(id));
                if(i<0) i=addTask(id);
                if(i<0) continue;
                tasks.index=i;
            } else if(e.type>=CreateNotify && e.type<=PropertyNotify) {
                //offset for window field
#define o(name) offsetof(X##name##Event,window)
                const int window[13] = {o(CreateWindow),o(DestroyWindow),o(Unmap),o(Map),o(MapRequest),o(Reparent),o(Configure),
                                        o(ConfigureRequest), o(Gravity),o(ResizeRequest),o(Circulate),o(CirculateRequest),o(Property)};
#undef o
                XID id = *(XID*)((byte*)&e+window[e.type-CreateNotify]);
                int i = indexOf(tasks, Task(id));
                if(i<0) {
                    if(e.type == CreateNotify || e.type==ReparentNotify) XSelectInput(x, id, StructureNotifyMask|SubstructureNotifyMask|PropertyChangeMask);
                    else if(e.type == MapNotify) i=addTask(id);
                    else continue;
                } else {
                    if(e.type == PropertyNotify) {
                        if(e.xproperty.atom==Atom(_NET_WM_NAME)) tasks[i].text.text = getTitle(id);
                        else if(e.xproperty.atom==Atom(_NET_WM_ICON)) tasks[i].icon = getIcon(id);
                        else if(e.xproperty.atom==Atom(_NET_WM_WINDOW_TYPE) || e.xproperty.atom==Atom(_NET_WM_STATE)) {
                            if(skipTaskbar(id)) tasks.removeAt(i);
                        } else continue;
                    }
                    else if(e.type == DestroyNotify || e.type == UnmapNotify || e.type==ReparentNotify) tasks.removeAt(i);
                    else continue;
                }
            }
            needUpdate = true;
        }
        if(needUpdate) {
            panel.update(); window.render();
            tasksChanged.emit(tasks.array::size());
        }
   }
};

struct Shell : Application {
    TaskBar taskbar;
    Desktop desktop;
    Shell() {
        taskbar.tasksChanged.connect(this,&Shell::tasksChanged);
        tasksChanged(taskbar.tasks.array::size());
    }
    void tasksChanged(int count) { desktop.window.setVisible(!count); }
} shell;
