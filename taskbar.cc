#include "process.h"
#include "file.h"
#include "time.h"
#include "interface.h"
#include "window.h"
#include "launcher.h"
#include "poll.h"
#include "dbus.h"
#include "array.cc" //array<Task>

static Display* x; //TODO: use Window::x
static XID active;

struct Task : Item {
    XID id;
    Task(XID id):id(id){} //for indexOf
    Task(XID id, Icon&& icon, Text&& text):Item(move(icon),move(text)),id(id){}
    bool selectEvent() override { //Raise
        XRaiseWindow(x, active=id);
        XSetInputFocus(x, id, RevertToPointerRoot, CurrentTime);
        XFlush(x);
        return true;
    }
    bool mouseEvent(int2, Event event, Button button) override {
        //TODO: preview on hover
        if(button!=LeftButton) return false;
        if(event==Press) {
            if(active==id) { //Set maximized
                XMoveResizeWindow(x,id, 0,16,Window::screen.x,Window::screen.y-16);
                XFlush(x);
                return true;
            }
        }
        if(event==Motion) { //Set windowed
            XMoveResizeWindow(x,id, 0,16,Window::screen.x/2,(Window::screen.y-16)/2);
            XFlush(x);
            return true;
        }
        return false;
    }
};
bool operator==(const Task& a,const Task& b){return a.id==b.id;}

struct StatusNotifierItem : TriggerButton {
    DBus::Object item;
    StatusNotifierItem(DBus::Object&& item, Image&& icon) : TriggerButton(move(icon)), item(move(item)) {}
    bool mouseEvent(int2 position, Event event, Button button) override {
        if(TriggerButton::mouseEvent(position,event,button)) {
            item("org.kde.StatusNotifierItem.Activate"_,0,0);
            return true;
        }
        return false;
    }
};

struct Clock : Text, Timer {
    signal<> timeout;
    signal<> triggered;
    Clock():Text(str(date(),"hh:mm"_)){ setAbsolute(currentTime()/60*60+60); }
    void expired() { text=str(date(),"hh:mm"_); update(); setAbsolute(currentTime()+60); timeout.emit(); }
    bool mouseEvent(int2, Event event, Button button) override {
        if(event==Press && button==LeftButton) { triggered.emit(); return true; }
        return false;
    }
};

/// Returns events occuring on \a query date (-1=unspecified)
array<string> getEvents(Date query) {
    array<string> events;
    TextBuffer s(readFile(".config/events"_,home()));

    map<string, array<Date> > exceptions; //Exceptions for recurring events
    while(s) { //first parse all exceptions (may occur after recurrence definitions)
        if(s.match("except "_)) { Date except=parse(s); s.skip(); string title=s.until("\n"_); exceptions[move(title)] << except; }
        else s.until("\n"_);
    }
    s.index=0;

    Date until; //End date for recurring events
    while(s) {
        s.skip();
        if(s.match("#"_)) s.until("\n"_); //comment
        else if(s.match("until "_)) { until=parse(s); } //apply to all following recurrence definitions
        else if(s.match("except "_)) s.until("\n"_); //already parsed
        else {
            Date date = parse(s); s.skip();
            Date end=date; if(s.match("-"_)) { end=parse(s); s.skip(); }
            string title = s.until("\n"_);
            if(query.day>=0) {
                if(date.day>=0) { if(date.day!=query.day) continue; }
                else if(query>until) continue;
            }
            if(query.month>=0) {
                if(date.month>=0) { if(date.month!=query.month) continue; }
                else if(query>until) continue;
            }
            if(query.year>=0) {
                if(date.year>=0) { if(date.year!=query.year) continue; }
                else if(query>until) continue;
            }
            if(query.hours>=0 && date.hours!=query.hours) continue;
            if(query.minutes>=0 && date.minutes!=query.minutes) continue;
            if(date.weekDay>=0 && date.weekDay!=query.weekDay) continue;
            for(Date date: exceptions[copy(title)]) if(date.day==query.day && date.month==query.month) goto skip;
            insertSorted(events, string(str(date,"hh:mm"_)+(date!=end?"-"_+str(end,"hh:mm"_):""_)+": "_+title));
            skip:;
        }
    }
    return events;
}

struct Month : Grid<Text> {
    Date active;
    array<Date> dates;
    int todayIndex;
    Month() : Grid(7,8) {}
    void setActive(Date active) {
        clear(); dates.clear();
        todayIndex=-1; this->active=active;
        static const string days[7] = {"Mo"_,"Tu"_,"We"_,"Th"_,"Fr"_,"Sa"_,"Su"_};
        const int nofDays[12] = { 31, !(active.year%4)&&(active.year%400)?29:28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        for(int i=0;i<7;i++) {
            append(copy(days[i]));
            dates << Date(-1,-1,-1,-1,-1,-1,i);
        }
        int first = (35+active.weekDay+1-active.day)%7;
        for(int i=0;i<first;i++) { //previous month
            int previousMonth = (active.month+11)%12;
            int day = nofDays[previousMonth]-first+i+1;
            dates << Date(count()%7, day, previousMonth);
            append(Text(format(Italic)+dec(day,2)));
        }
        Date today=::date();
        for(int i=1;i<=nofDays[active.month];i++) { //current month
            bool isToday = today.month==active.month && i==today.day;
            if(isToday) todayIndex=count();
            dates << Date(count()%7, i, active.month);
            append(string((isToday?format(Bold):""_)+dec(i,2))); //current day
        }
        for(int i=1;count()<7*8;i++) { //next month
            dates << Date(count()%7, i, (active.month+1)%12);
            append(Text(format(Italic)+dec(i,2)));
        }
        Selection::setActive(todayIndex);
        update();
    }
    void previousMonth() { active.month--; if(active.month<0) active.year--, active.month=11; setActive(active); }
    void nextMonth() { active.month++; if(active.month>11) active.year++, active.month=0; setActive(active); }
};

struct Calendar {
    HList<Text> date = { "<"_, ""_, ">"_ }; //< date >
    Month month;
    Text events;
    Menu menu { &date, &month, &space, &events, &space };
    Window window{&menu,""_,Image(),int2(300,300)};
    Calendar() {
        date[0].textClicked.connect(this, &Calendar::previousMonth);
        date[2].textClicked.connect(this, &Calendar::nextMonth);
        window.setType("_NET_WM_WINDOW_TYPE_DROPDOWN_MENU"_);
        window.setOverrideRedirect(true);
        menu.close.connect(&window,&Window::hide);
    }
    void previousMonth() {
        month.previousMonth(); date[1].setText( format(Bold)+str(month.active,"MMMM yyyy"_) );
        events.setText(""_); menu.update(); window.render(); }
    void nextMonth() {
        month.nextMonth(); date[1].setText( format(Bold)+str(month.active,"MMMM yyyy"_) );
        events.setText(""_); menu.update(); window.render();
    }
    void activeChanged(int index) {
        string text;
        Date date = month.dates[index];
        text << string(format(Bold)+(index==month.todayIndex?"Today"_:str(date))+format(Regular)+"\n"_);
        text << join(::getEvents(date),"\n"_)+"\n"_;
        if(index==month.todayIndex) {
            Date date = month.dates[index+1];
            text << format(Bold)+"Tomorrow"_+format(Regular)+"\n"_;
            text << join(::getEvents(date),"\n"_);
        }
        events.setText(move(text));
        menu.update(); window.render();
    }
    bool mouseEvent(int2, Event event, Button) {
        if(event==Leave) { window.hide(); return true; }
        return false;
    }
    void show() {
        if(window.visible) { window.hide(); return; }
        date[1].setText( format(Bold)+str(::date(),"dddd, dd MMMM yyyy"_) );
        month.activeChanged.connect(this,&Calendar::activeChanged);
        month.setActive(::date());
        menu.update();
        window.setPosition(int2(-300,16)); window.show();
    }
    void checkAlarm() { if(getEvents(::date(currentTime()+5*60))) show(); }
};

ICON(shutdown);
struct Desktop {
    List<Command> shortcuts { readShortcuts() };
    List<Command> system { Command(move(shutdownIcon),"Shutdown"_,"/sbin/poweroff"_,{}) };
    HBox applets { &space, &shortcuts, &system };
    Window window{&applets,""_,Image(),int2(0,0),255};
    Desktop() { window.setType("_NET_WM_WINDOW_TYPE_DESKTOP"_); }
};

ICON(button);
struct TaskBar : Poll {
    DBus dbus;
    signal<int> tasksChanged;

      TriggerButton start;
       Launcher launcher;
      Bar<Task> tasks;
      Bar<StatusNotifierItem> status;
      Clock clock;
       Calendar calendar;
     HBox panel {&start, &tasks, &status, &clock };
    Window window{&panel,""_,Image(),int2(0,-1),255};

    string getTitle(XID id) { return Window::getProperty<char>(id,"_NET_WM_NAME"); }
    Image getIcon(XID id) {
        array<ulong> buffer = Window::getProperty<ulong>(id,"_NET_WM_ICON");
        if(buffer.size()<=4) return Image();
        array<byte4> image(buffer.size()); image.setSize(buffer.size());
        for(uint i=0;i<buffer.size()-2;i++) image[i] = *(byte4*)&buffer[i+2];
        return resize(Image(move(image), buffer[0], buffer[1]), 16,16);
    }
    int addTask(XID id) {
        string title = getTitle(id);
        if(!title) return -1;
        Image icon = getIcon(id);
        if(!icon) return -1;
        tasks << Task(id,move(icon),move(title));
        return tasks.array::size()-1;
    }
    void initializeTasks() {
        XID root,parent; XID* children; uint count=0; XQueryTree(x,DefaultRootWindow(x),&root,&parent,&children,&count);
        array<XID> list(children,count);
        for(XID id: list) {
            addTask(id);
            XSelectInput(x, id, StructureNotifyMask|SubstructureNotifyMask|PropertyChangeMask);
            XGrabButton(x,Button1,AnyModifier,id,False,ButtonPressMask,GrabModeSync,GrabModeAsync,None,None);
        }
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
            if(exists(path)) { icon=resize(decodeImage(readFile(path)), 16,16); break; }
        }
        if(!icon) {
            auto icons = item.get< array<DBusIcon> >("IconPixmap"_);
            assert(icons,name,"not found");
            DBusIcon dbusIcon = move(icons.first());
            icon=swap(resize(Image(cast<byte4>(move(dbusIcon.data)),dbusIcon.width,dbusIcon.height),16,16));
        }
        status << StatusNotifierItem(move(item), move(icon));
        panel.update(); if(window.id) window.render();
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
        XSelectInput(x,DefaultRootWindow(x),SubstructureNotifyMask|SubstructureRedirectMask|PropertyChangeMask|ButtonPressMask);
        initializeTasks();
        XFlush(x);

        if(dbus) {
            dbus.bind("Get"_,this,&TaskBar::Get);
            dbus.bind("RegisterStatusNotifierItem"_,this,&TaskBar::registerStatusNotifierItem);
            dbus.connect("NameOwnerChanged"_,this,&TaskBar::removeStatusNotifierItem);
            DBus::Object DBus = dbus("org.freedesktop.DBus/org/freedesktop/DBus"_);
            array<string> names = DBus("org.freedesktop.DBus.ListNames"_);
            for(string& name: names) if(startsWith(name,"org.kde.StatusNotifierItem"_)) registerStatusNotifierItem(move(name));
            DBus("org.freedesktop.DBus.RequestName"_, "org.kde.StatusNotifierWatcher"_, (uint)0);
        }

        start.image = resize(buttonIcon, 16,16);
        start.triggered.connect(&launcher,&Launcher::show);
        tasks.expanding=true;
        clock.timeout.connect(&window, &Window::render);
        clock.timeout.connect(&calendar, &Calendar::checkAlarm);
        clock.triggered.connect(&calendar,&Calendar::show);

        window.setType("_NET_WM_WINDOW_TYPE_DOCK"_);
        window.setOverrideRedirect(true);
        window.setPosition(int2(0,0));
        window.show();
    }
    void event(pollfd) override {
        bool needUpdate = false;
        while(XEventsQueued(x, QueuedAfterFlush)) { XEvent e; XNextEvent(x,&e);
            if(e.type==CreateNotify) {
                XID id = e.xcreatewindow.window;
                if(id==window.id) continue;
                XSelectInput(x,id , StructureNotifyMask|SubstructureNotifyMask|PropertyChangeMask);
                XGrabButton(x,Button1,AnyModifier,id,False,ButtonPressMask,GrabModeSync,GrabModeAsync,None,None);
                continue;
            } else if(e.type == MapRequest || e.type == MapNotify) {
                XID id = e.xmaprequest.window;
                XMapWindow(x, id);
                XRaiseWindow(x, active=id);
                XSetInputFocus(x, id, RevertToPointerRoot, CurrentTime);
                int i = indexOf(tasks, Task(id));
                if(i<0) i=addTask(id), tasksChanged.emit(tasks.array::size());
                if(i<0) continue;
                tasks.index=i;
            } else if(e.type == ConfigureRequest) {
                XID id = e.xconfigure.window;
                XWindowAttributes wa; XGetWindowAttributes(x, id, &wa);
                wa.x=max(0,wa.x); wa.y=max(id==window.id?0:16,wa.y);
                wa.width=min(Window::screen.x,wa.width); wa.height=min(Window::screen.y-16,wa.height);
                if(!wa.override_redirect) wa.x = (Window::screen.x - wa.width)/2, wa.y = (Window::screen.y - wa.height)/2;
                XMoveResizeWindow(x,id, wa.x,wa.y,wa.width,wa.height);
                continue;
            } else if(e.type == ButtonPress) {
                XID id = e.xbutton.window;
                XMapWindow(x, id);
                XRaiseWindow(x, active=id);
                XSetInputFocus(x, id, RevertToPointerRoot, CurrentTime);
                XAllowEvents(x, ReplayPointer, CurrentTime);
                int i = indexOf(tasks, Task(id));
                if(i>=0) tasks.index=i;
            } else if(e.type==PropertyNotify) {
                XID id = e.xproperty.window;
                int i = indexOf(tasks, Task(id));
                if(i<0) i=addTask(id);
                if(i<0) continue;
                if(e.xproperty.atom==Atom(_NET_WM_NAME)) tasks[i].get<Text>().setText( getTitle(id) );
                else if(e.xproperty.atom==Atom(_NET_WM_ICON)) tasks[i].get<Icon>().image = getIcon(id);
                else continue;
            } else if(e.type == DestroyNotify) {
                int i = indexOf(tasks, Task(e.xdestroywindow.window));
                if(i<0) continue;
                tasks.removeAt(i);
                if(tasks.index==uint(i)) {
                    if(i>0) tasks.setActive(i-1);
                    else if(tasks) tasks.setActive(i);
                    else tasks.index=-1;
                }
                tasksChanged.emit(tasks.array::size());
            } else continue;
            needUpdate = true;
        }
        if(needUpdate && window.visible) { panel.update(); window.render(); }
   }
};

struct Shell : Application {
    TaskBar taskbar;
    Desktop desktop;
    Shell(array<string>&&) {
        taskbar.tasksChanged.connect(this,&Shell::tasksChanged);
        tasksChanged(taskbar.tasks.array::size());
    }
    void tasksChanged(int count) { if(!count) desktop.window.show(); else desktop.window.hide(); }
};
Application(Shell)
