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
        bool mouseEvent(int2, int2, Event event, Button) override {
            if(event==Press) {
                parent->raise(id);
                if(parent->tasks.index!=uint(-1) && &parent->tasks.active()==this) {SetGeometry r; r.id=id; r.x=0, r.y=16; r.w=display.x; r.h=display.y-16; parent->send(raw(r));}
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

    array<uint> windows;

    ICON(button) TriggerButton start __(resize(buttonIcon(), 16,16));
    Launcher launcher;
    Bar<Task> tasks;
    Clock clock __(16);
    Calendar calendar;
    Window popup __(&calendar,int2(256,-1));
    HBox panel;//__(&start, &tasks, &clock);
    Window window __(&panel,int2(0,16));
    uint root=window.root;
    uint desktop=0;

    Taskbar() {
        window.anchor=Window::Top;
        panel<<&start<<&tasks<<&clock;
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

        {SetWindowEventMask r; r.window=root; r.eventMask=SubstructureNotifyMask|SubstructureRedirectMask; send(raw(r));}
        window.setCursor(Window::Arrow,root);

        array<uint> windows;
        {QueryTree r; r.id=root; send(raw(r));}
        {QueryTreeReply r = readReply<QueryTreeReply>(); windows=read<uint>(fd, r.count);}
        for(uint id: windows) addWindow(id);

        start.triggered.connect(this,&Taskbar::startButton);
        launcher.window.autoResize=true;
        launcher.window.anchor=Window::TopLeft;
        tasks.expanding=true;
        tasks.activeChanged.connect(this, &Taskbar::raiseTask);
        clock.timeout.connect(&window, &Window::render);
        clock.timeout.connect(&calendar, &Calendar::checkAlarm);
        clock.triggered.connect(&calendar,&Calendar::reset);
        clock.triggered.connect(&popup,&Window::toggle);
        calendar.eventAlarm.connect(&popup,&Window::show);
        calendar.side=Linear::Right;
        popup.hideOnLeave = true;
        popup.autoResize=true;
        popup.anchor = Window::TopRight;
        window.show();
    }
    ~Taskbar() { {SetInputFocus r; r.window=1; send(raw(r));} close(fd); }

    void processEvent(uint8 type, const Event& e) {
        if(type==0) return;
        if(type==1) error("Unexpected reply");
        type&=0b01111111; //msb set if sent by SendEvent
        if(type==CreateNotify) { uint id=e.create.window;
            if(e.create.override_redirect) return;
            addWindow(id);
            return;
        } else if(type == MapRequest) { uint id=e.map_request.window;
            {GetGeometry r; r.id=id; send(raw(r));} GetGeometryReply g=readReply<GetGeometryReply>(); int x=g.x,y=g.y,w=g.w,h=g.h;
            array<uint> motif = getProperty<uint>(id,"_MOTIF_WM_HINTS"_), type = getProperty<uint>(id,"_NET_WM_WINDOW_TYPE"_);
            if((!type || type[0]==Atom("_NET_WM_WINDOW_TYPE_NORMAL"_)) && (!motif || motif[0]!=3 || motif[1]!=0)) {
                w=min<int16>(display.x,w); h=min<int16>(display.y-16,h);
                x = (display.x-w)/2; y = 16+(display.y-16-h)/2;
            }
            if(x!=g.x || y!=g.y || w!=g.w || h!=g.h){SetGeometry r; r.id=id; r.x=x, r.y=y; r.w=w; r.h=h; send(raw(r));}
            {MapWindow r; r.id=id; send(raw(r));}
            raise(id);
            int i = tasks.indexOf(id);
            if(i<0) i=addWindow(id);
            if(i<0) return;
            tasks.index=i;
        } else if(type == ConfigureRequest) { uint id = e.configure_request.window; log(id);
            {GetGeometry r; r.id=id; send(raw(r));} GetGeometryReply g=readReply<GetGeometryReply>(); int x=g.x,y=g.y,w=g.w,h=g.h;
            const auto& c = e.configure_request;
            if(c.valueMask & X) x=c.x; if(c.valueMask & Y) y=c.y; if(c.valueMask & W) w=c.w; if(c.valueMask & H) h=c.h;
            array<uint> motif = getProperty<uint>(id,"_MOTIF_WM_HINTS"_), type = getProperty<uint>(id,"_NET_WM_WINDOW_TYPE"_);
            if((!type || type[0]==Atom("_NET_WM_WINDOW_TYPE_NORMAL"_)) && (!motif || motif[0]!=3 || motif[1]!=0)) {
                w=min<int16>(display.x,w); h=min<int16>(display.y-16,h);
                x = (display.x - w)/2; y = 16+(display.y-16-h)/2;
            }
            if(c.valueMask&StackMode) {ConfigureWindow r; r.id=id; r.x=x, r.y=y; r.w=w; r.h=h; r.stackMode=e.configure_request.stackMode; send(raw(r));}
            else {SetGeometry r; r.id=id; r.x=x, r.y=y; r.w=w; r.h=h; send(raw(r));}
            return;
        } else if(type == ButtonPress) { uint id = e.event;
            raise(id);
            send(raw(AllowEvents()));
            int i = tasks.indexOf(id);
            if(i>=0) tasks.index=i;
        } else if(type==PropertyNotify) { uint id=e.property.window;
            if(e.property.atom==Atom("_NET_WM_NAME"_)) {
                int i = tasks.indexOf(id); if(i<0) i=addWindow(id); if(i<0) return;
                tasks[i].text.setText( getTitle(id) );
            } else if(e.property.atom==Atom("_NET_WM_ICON"_)) {
                int i = tasks.indexOf(id); if(i<0) i=addWindow(id); if(i<0) return;
                tasks[i].icon.image = getIcon(id);
            } else return;
        } else if(type == DestroyNotify) { uint id=e.property.window;
            windows.removeAll(id);
            int i = tasks.indexOf(id);
            if(i>=0) {
                tasks.removeAt(i);
                if(tasks.index == uint(i)) {
                    tasks.index=-1;
                    if(tasks) raise(tasks.last().id);
                }
            }
        } else if(type==MapNotify||type==UnmapNotify||type==ConfigureNotify||type==ClientMessage||type==ReparentNotify||type==MappingNotify) {
        } else log("Event", type<sizeof(::events)/sizeof(*::events)?::events[type]:str(type));
        window.render();
    }

    /// Adds \a id to \a windows and to \a tasks if necessary
    int addWindow(uint id) {
        if(id==root) return -1;
        if(!windows.contains(id)) {
            windows << id;
            {SetWindowEventMask r; r.window=id; r.eventMask=StructureNotifyMask|SubstructureNotifyMask|PropertyChangeMask; send(raw(r));}
        }
        if(getProperty<uint>(id,"_NET_WM_WINDOW_TYPE"_)==Atom("_NET_WM_WINDOW_TYPE_DESKTOP"_)) desktop=id;
        GetWindowAttributes r; r.window=id; send(raw(r)); GetWindowAttributesReply wa = readReply<GetWindowAttributesReply>();
        if(wa.overrideRedirect||wa.mapState!=IsViewable) return -1;
        array<uint> type = getProperty<uint>(id,"_NET_WM_WINDOW_TYPE"_);
        if(type.size()==0 || type.first()!=Atom("_NET_WM_WINDOW_TYPE_NORMAL"_)) return -1;
        if(getProperty<uint>(id,"_NET_WM_STATE"_).contains(Atom("_NET_WM_SKIP_TASKBAR"_))) return -1;
        string title = getTitle(id); if(!title) return -1;
        Image icon = getIcon(id);
        tasks << Task(this,id,move(icon),move(title));
        {GrabButton r; r.window=id; send(raw(r));}
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

    void raiseTask(uint index) { raise(tasks[index].id); }
    void raise(uint id) {
        {RaiseWindow r; r.id=id; send(raw(r));}
        GetWindowAttributes r; r.window=id; send(raw(r)); GetWindowAttributesReply wa = readReply<GetWindowAttributesReply>();
        if(wa.mapState==IsViewable) {SetInputFocus r; r.window=id; send(raw(r));}
        for(uint w: windows) if(getProperty<uint>(w,"WM_TRANSIENT_FOR"_) == id) raise(w);
        if(popup.mapped) raise(popup.id);
    }

    void startButton() { // Opens launcher or raise desktop on second click
        if(!launcher.window.mapped) launcher.window.show();
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
