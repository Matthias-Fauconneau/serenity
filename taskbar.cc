#include "process.h"
#include "file.h"
#include "time.h"
#include "interface.h"
#include "window.h"
#include "launcher.h"
#include "calendar.h"
#include "x.h"
#include "linux.h"

struct Taskbar : Application, Poll {
    struct Task : Item {
        Taskbar* parent=0;
        uint id;
        Task(uint id):id(id){} //for indexOf
        Task(Taskbar* parent, uint id, Image&& icon, string&& text):Linear(Left),Item(move(icon),move(text)),parent(parent),id(id){}
        bool selectEvent() override { parent->raise(id); return true; }
        bool mouseEvent(int2, int2, Event event, Button button) override {
            if(event==Press && button==LeftButton) return false;
            {SetInputFocus r; r.window=id; parent->send(raw(r));}
             if(parent->getProperty<uint>(parent->root,"_NET_ACTIVE_WINDOW"_)==id) { // Maximize
                SetGeometry r; r.id=id; r.x=0, r.y=16; r.w=display.x; r.h=display.y-16; parent->send(raw(r));
                return true;
            }
            return false;
        }
        bool keyPress(Key key) override {
            if(key != Escape) return false;
            {SendEvent r; r.window=id; r.type=ClientMessage;
                auto& e=r.event.client; e.format=32; e.window=id; e.type=parent->Atom("WM_PROTOCOLS"_);
                clear(e.data); e.data[0]=parent->Atom("WM_DELETE_WINDOW"_); parent->send(raw(r));}
            return true;
        }
    };

    bool ownWM=true;
    array<uint> windows;

    ICON(button) TriggerButton start __(resize(buttonIcon(), 16,16));
    Launcher launcher;
    Bar<Task> tasks;
    Clock clock __(16);
    Calendar calendar;
    Window popup __(&calendar, int2(-256,-256));
    HBox panel;//__(&start, &tasks, &clock);
    Window window __(&panel,int2(0,16),""_,Image(),"_NET_WM_WINDOW_TYPE_DOCK"_);
    uint root=window.root;
    uint desktop=0;

    Taskbar() {
        panel<<&start<<&tasks<<&clock;
        debug( window.localShortcut(Escape).connect(this, &Application::quit); )
        registerPoll(socket(PF_LOCAL, SOCK_STREAM, 0));
        string path = "/tmp/.X11-unix/X"_+getenv("DISPLAY"_).slice(1);
        sockaddr_un addr; copy(addr.path,path.data(),path.size());
        check_(connect(fd,(sockaddr*)&addr,2+path.size()),path);
        {ConnectionSetup r;
            string authority = getenv("HOME"_)+"/.Xauthority"_;
            if(existsFile(authority)) send(string(raw(r)+readFile(authority).slice(18,align(4,(r.nameSize=18))+(r.dataSize=16))));
            else send(raw(r)); }
        {ConnectionSetupReply r=read<ConnectionSetupReply>(fd); assert(r.status==1);
            read(fd,r.additionnal*4-(sizeof(ConnectionSetupReply)-8)); }

        debug( array<string> args; args<<string("-w"_)<<string("/usr/bin/taskbar"_); execute("/usr/bin/killall"_,args); )

        /*array<uint> wm=getProperty<uint>(root,"_NET_SUPPORTING_WM_CHECK"_);
        if(wm && getProperty<uint>(wm[0],"_NET_SUPPORTING_WM_CHECK"_)) ownWM=false;
        else {
            {ChangeProperty r; r.window=root; r.property=Atom("_NET_SUPPORTING_WM_CHECK"_); r.type=Atom("WINDOW"_); r.format=32;
            r.length=1; r.size+=r.length; send(string(raw(r)+raw(window.id+Window::XWindow)));}
            {ChangeProperty r; r.window=window.id+Window::XWindow; r.property=Atom("_NET_SUPPORTING_WM_CHECK"_); r.type=Atom("WINDOW"_); r.format=32;
            r.length=1; r.size+=r.length; send(string(raw(r)+raw(window.id+Window::XWindow)));}
        }*/

        {SetWindowEventMask r; r.window=root; r.eventMask=SubstructureNotifyMask|PropertyChangeMask|ButtonPressMask;
            if(ownWM) r.eventMask|=SubstructureRedirectMask; send(raw(r)); }
        array<uint> windows;
        if(ownWM) {
            {QueryTree r; r.id=root; send(raw(r));}
            {QueryTreeReply r = readReply<QueryTreeReply>(); windows=read<uint>(fd, r.count);}
        } else windows=getProperty<uint>(root,"_NET_CLIENT_LIST"_);
        for(uint id: windows) {
            addWindow(id);
            {SetWindowEventMask r; r.window=id; r.eventMask=StructureNotifyMask|SubstructureNotifyMask|PropertyChangeMask;
                send(raw(r));}
            if(ownWM){GrabButton r; r.window=id; send(raw(r));}
        }

        start.triggered.connect(this,&Taskbar::startButton);
        launcher.window.setPosition(int2(0, 0));
        tasks.expanding=true;
        clock.timeout.connect(&window, &Window::render);
        clock.timeout.connect(&calendar, &Calendar::checkAlarm);
        clock.triggered.connect(&calendar,&Calendar::reset);
        clock.triggered.connect(&popup,&Window::toggle);
        calendar.eventAlarm.connect(&popup,&Window::show);
        popup.hideOnLeave = true;
        popup.setPosition(int2(-popup.size.x, window.size.y));
        window.show();
    }

    void processEvent(uint8 type, const Event& e) {
        if(type==0) return;
        if(type==1) error("Unexpected reply");
        type&=0b01111111; //msb set if sent by SendEvent
        if(type==CreateNotify) { uint id=e.create.window;
            if(e.create.override_redirect) return;
            {SetWindowEventMask r; r.window=id; r.eventMask=StructureNotifyMask|SubstructureNotifyMask|PropertyChangeMask;
                send(raw(r));}
            if(ownWM){GrabButton r; r.window=id; send(raw(r));}
            return;
        } else if(type == MapRequest) { uint id=e.map_request.window;
            {MapWindow r; r.id=id; send(raw(r));}
            raise(id);
            int i = tasks.indexOf(id);
            if(i<0) i=addWindow(id);
            if(i<0) return;
            tasks.index=i;
        } else if(type == ConfigureRequest) { uint id = e.configure_request.window;
            {GetGeometry r; r.id=id; send(raw(r));}
            GetGeometryReply w=readReply<GetGeometryReply>();
            const auto& c = e.configure_request;
            if(c.valueMask & X) w.x=c.x; if(c.valueMask & Y) w.y=c.y; if(c.valueMask & W) w.w=c.w; if(c.valueMask & H) w.h=c.h;
            array<uint> motif = getProperty<uint>(id,"_MOTIF_WM_HINTS"_), type = getProperty<uint>(id,"_NET_WM_WINDOW_TYPE"_);
            GetWindowAttributes r; r.window=id; send(raw(r)); GetWindowAttributesReply wa = readReply<GetWindowAttributesReply>();
            if(!wa.overrideRedirect && (!type || type[0]==Atom("_NET_WM_WINDOW_TYPE_NORMAL"_)) && (!motif || motif[0]!=3 || motif[1]!=0)) {
                w.w=min<int16>(display.x,w.w); w.h=min<int16>(display.y-16,w.h);
                w.x = (display.x - w.w)/2; w.y = 16+(display.y-16-w.h)/2;
            }
            if(c.valueMask&StackMode) {ConfigureWindow r; r.id=id; r.x=w.x, r.y=w.y; r.w=w.w; r.h=w.h; r.stackMode=e.configure_request.stackMode; send(raw(r));}
            else {SetGeometry r; r.id=id; r.x=w.x, r.y=w.y; r.w=w.w; r.h=w.h; send(raw(r));}
            return;
        } else if(type == ButtonPress) { uint id = e.event;
            raise(id);
            send(raw(AllowEvents()));
            int i = tasks.indexOf(id);
            if(i>=0) tasks.index=i;
        } else if(type==PropertyNotify) { uint id=e.property.window;
            if(id==root && e.property.atom==Atom("_NET_ACTIVE_WINDOW"_)) {
                array<uint> active = getProperty<uint>(root,"_NET_ACTIVE_WINDOW"_);
                if(active) { int i = tasks.indexOf(active.first()); if(i<0) return; tasks.setActive(i); }
            } else if(id==root && e.property.atom==Atom("_NET_CLIENT_LIST"_)) {
                windows.clear(); tasks.clear();
                for(uint id: getProperty<uint>(root,"_NET_CLIENT_LIST"_)) {
                    addWindow(id);
                    {SetWindowEventMask r; r.window=id; r.eventMask=StructureNotifyMask|SubstructureNotifyMask|PropertyChangeMask;
                        send(raw(r));}
                    if(ownWM){GrabButton r; r.window=id; send(raw(r));}
                }
            } else if(e.property.atom==Atom("_NET_WM_NAME"_)) {
                int i = tasks.indexOf(id); /*if(i<0) i=addWindow(id);*/ if(i<0) return;
                tasks[i].text.setText( getTitle(id) );
            } else if(e.property.atom==Atom("_NET_WM_ICON"_)) {
                int i = tasks.indexOf(id); /*if(i<0) i=addWindow(id);*/ if(i<0) return;
                tasks[i].icon.image = getIcon(id);
            } else return;
        } else if(type == DestroyNotify) { uint id=e.property.window;
            windows.removeAll(id);
            int i = tasks.indexOf(id);
            if(i>=0) {
                tasks.removeAt(i);
                if(tasks.index == uint(i)) tasks.index=-1;
            }
            if(windows) {
                uint id=windows.last();
                if(ownWM) {SetInputFocus r; r.window=id; send(raw(r));}
                if(tasks.index==uint(-1)) tasks.setActive(tasks.indexOf(id));
            }
        } else if(type==MapNotify||type==UnmapNotify||type==ConfigureNotify||type==ClientMessage||type==ReparentNotify) {
        } else log("Event", type<sizeof(::events)/sizeof(*::events)?::events[type]:str(type));
        window.render();
    }

    /// Adds \a id to \a windows and to \a tasks if necessary
    int addWindow(uint id) {
        if(windows.contains(id)) return -1;
        windows << id;
        if(getProperty<uint>(id,"_NET_WM_WINDOW_TYPE"_)==Atom("_NET_WM_WINDOW_TYPE_DESKTOP"_)) desktop=id;
        if(id==root) return -1;
        GetWindowAttributes r; r.window=id; send(raw(r)); GetWindowAttributesReply wa = readReply<GetWindowAttributesReply>();
        if(wa.overrideRedirect||wa.mapState!=IsViewable) return -1;
        array<uint> type = getProperty<uint>(id,"_NET_WM_WINDOW_TYPE"_);
        if(type.size()==0 || type.first()!=Atom("_NET_WM_WINDOW_TYPE_NORMAL"_)) return -1;
        if(getProperty<uint>(id,"_NET_WM_STATE"_).contains(Atom("_NET_WM_SKIP_TASKBAR"_))) return -1;
        string title = getTitle(id); if(!title) return -1;
        Image icon = getIcon(id);
        tasks << Task(this,id,move(icon),move(title));
        return tasks.array::size()-1;
    }

    string getTitle(uint id) {
        string name = getProperty<byte>(id,"_NET_WM_NAME"_);
        if(!name) name = getProperty<byte>(id,"WM_NAME"_);
        return move(name);
    }
    Image getIcon(uint id) {
        array<uint> buffer = getProperty<uint>(id,"_NET_WM_ICON"_,2+128*128);
        if(buffer.size()<3) return Image();
        uint w=buffer[0], h=buffer[1];
        if(buffer.size()<2+w*h) return Image();
        return resize(Image(array<byte4>(cast<byte4>(buffer.slice(2,w*h))),w,h,true), 16, 16);
    }

    map<string, uint> cache;
    uint Atom(const ref<byte>& name) {
        uint& atom = cache[string(name)];
        if(!atom) {
            {InternAtom r; r.length=name.size; r.size+=align(4,r.length)/4; send(string(raw(r)+name+pad(4,r.length)));}
            {InternAtomReply r=readReply<InternAtomReply>(); atom=r.atom;}
        }
        return atom;
    }
    template<class T> array<T> getProperty(uint window, const ref<byte>& name, uint size=128*128+2) {
        {GetProperty r; r.window=window; r.property=Atom(name); r.length=size; send(raw(r));}
        {GetPropertyReply r=readReply<GetPropertyReply>(); int size=r.length*r.format/8;
            array<T> a; if(size) a=read<T>(fd,size/sizeof(T)); int pad=align(4,size)-size; if(pad) read(fd, pad); return a; }
    }

    void raise(uint id) {
        if(ownWM) {
            {RaiseWindow r; r.id=id; send(raw(r));}
            {SetInputFocus r; r.window=id; send(raw(r));}
            for(uint w: windows) {
                if(getProperty<uint>(w,"WM_TRANSIENT_FOR"_) == id) {
                    {RaiseWindow r; r.id=id; send(raw(r));}
                    {SetInputFocus r; r.window=id; send(raw(r));}
                }
            }
        } else {
            {SendEvent r; r.window=root; r.eventMask=SubstructureNotifyMask; r.type=ClientMessage;
                auto& e=r.event.client; e.format=32; e.window=id; e.type=Atom("_NET_ACTIVE_WINDOW"_);
                clear(e.data); e.data[0]=2; send(raw(r));}
        }
    }

    void startButton() { // Opens launcher or raise desktop on second click
        if(!launcher.window.mapped) { launcher.window.show(); raise(launcher.window.id); }
        else {
            launcher.window.hide();
            if(tasks.index<tasks.count()) { tasks.setActive(-1); window.render(); }
            if(desktop) raise(desktop);
        }
    }

    uint16 sequence=-1;
    void send(const ref<byte>& request) { write(fd, request); sequence++; }

    struct QEvent { uint8 type; Event event; } packed;
    array<QEvent> queue;

    template<class T> T readReply() {
        for(;;) { uint8 type = read<uint8>(fd);
            if(type==0){Error e=read<Error>(fd); if(e.code!=3) window.processEvent(0,(Event&)e); if(e.seq==sequence) { T t; clear(t); return t; }}
            else if(type==1) return read<T>(fd);
            else queue << QEvent __(type, read<::Event>(fd)); //queue events to avoid reentrance
        }
    }

    void event() {
        uint8 type = read<uint8>(fd);
        processEvent(type, read<Event>(fd));
        while(queue) { QEvent e=queue.takeFirst(); processEvent(e.type, e.event); }
    }
};
bool operator==(const Taskbar::Task& a,const Taskbar::Task& b){return a.id==b.id;}
Application(Taskbar)
