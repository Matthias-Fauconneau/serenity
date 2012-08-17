#include "process.h"
#include "file.h"
#include "time.h"
#include "interface.h"
#include "window.h"
#include "launcher.h"
#include "calendar.h"
#include "x.h"
#include "linux.h"

ICON(button)
struct Taskbar : Application, Poll {
    static Taskbar* taskbar;
    struct Task : Item {
        uint id;
        Task(uint id):id(id){} //for indexOf
        Task(uint id, Image<byte4>&& icon, string&& text):Item(move(icon),move(text)),id(id){}
        bool selectEvent() override { /*taskbar->raise(id);*/ return true; }
        /*bool mouseEvent(int2, Event event, Key button) override {
            //TODO: preview on hover
            if(button!=LeftButton) return false;
            if(event==KeyPress && getWindows().last()==id) { MoveResizeWindow(x,id, 0, 16, display.x,display.y-16); return true; } //Set Maximized
            if(event==Motion) { XMoveResizeWindow(x,id, display.x/4,16+(display.y-16)/4,display.x/2,(display.y-16)/2); return true; } //Set windowed
            return false;
        }*/
    };

    const int x;
    bool wm=false; //TODO: detect
    signal<int> tasksChanged;

    TriggerButton start __(resize(buttonIcon(), 16,16));
    Launcher launcher;
    Bar<Task> tasks;
    Clock clock __(16);
    Popup<Calendar> calendar;
    HBox panel __(&start, &tasks, &clock);
    Window window __(&panel,int2(0,16));

    /// Popup launcher or raise desktop on second click
    void startButton() {
        if(launcher.window.mapped) launcher.window.hide(); else { launcher.window.show(); return; }
        tasks.setActive(-1); window.render();
        /*for(uint id: getWindows()) {
            array<Atom> type=Window::getAtomProperty(id,"_NET_WM_WINDOW_TYPE"_);
            if(type.size()>=1 && (type[0]==Atom(_NET_WM_WINDOW_TYPE_DESKTOP)||type[0]==Atom(_NET_WM_WINDOW_TYPE_DOCK))) {
                XRaiseWindow(x, id);
            }
        }*/
    }

    int addTask(uint id) {
        /*XWindowAttributes wa; XGetWindowAttributes(x, id, &wa); if(wa.override_redirect||wa.map_state!=IsViewable) return -1;
        array<Atom> type=Window::getAtomProperty(id,"_NET_WM_WINDOW_TYPE"_);
        if(type.size()>=1 && type.first()!=Atom(_NET_WM_WINDOW_TYPE_NORMAL)) return -1;
        if(contains(Window::getAtomProperty(id,"_NET_WM_STATE"),Atom(_NET_WM_SKIP_TASKBAR))) return -1;*/
        string title = getTitle(id);
        if(!title) return -1;
        Image<byte4> icon = getIcon(id);
        tasks << Task(id,move(icon),move(title));
        return tasks.array::size()-1;
    }

    Taskbar() : x(socket(PF_LOCAL, SOCK_STREAM, 0)) {
        taskbar=this;
        // Setups X connection
        string path = "/tmp/.X11-unix/X"_+getenv("DISPLAY"_).slice(1);
        sockaddr_un addr; copy(addr.path,path.data(),path.size());
        check_(connect(x,(sockaddr*)&addr,2+path.size()),path);
        {ConnectionSetup r;
            string authority = getenv("HOME"_)+"/.Xauthority"_;
            if(exists(authority)) write(x, string(raw(r)+readFile(authority).slice(18,align(4,(r.nameSize=18))+(r.dataSize=16))));
            else write(x,raw(r)); }
        {ConnectionSetupReply r=read<ConnectionSetupReply>(x); assert(r.status==1);
            read(x,r.additionnal*4-(sizeof(ConnectionSetupReply)-8)); }
        //XSetWindowAttributes attributes; attributes.cursor=XCreateFontCursor(x,68); XChangeWindowAttributes(root,CWCursor,&attributes);

        {ChangeWindowAttribute r; r.window=window.root; r.eventMask=SubstructureNotifyMask|PropertyChangeMask|ButtonPressMask;
            if(wm) r.eventMask|=SubstructureRedirectMask;
            write(x,raw(r)); }

        /*for(XID id: getWindows()) {
            addTask(id);
            XSelectInput(x, id, StructureNotifyMask|SubstructureNotifyMask|PropertyChangeMask);
            XWindowAttributes wa; XGetWindowAttributes(x, id, &wa); if(wa.override_redirect) continue;
            XGrabButton(x,1,AnyModifier,id,0,ButtonPressMask,0,1,0,0);
        }*/
        registerPoll(__(x, POLLIN));

        start.triggered.connect(this,&Taskbar::startButton);
        launcher.window.setPosition(int2(0, 0));
        tasks.expanding=true;
        clock.timeout.connect(&window, &Window::render);
        clock.timeout.connect(&calendar, &Calendar::checkAlarm);
        clock.triggered.connect(&calendar.window,&Window::toggle);
        calendar.eventAlarm.connect(&calendar.window,&Window::show);
        calendar.window.setPosition(int2(-300, 16));
        window.show();
    }

    void event(const pollfd& poll) { if(poll.fd==x) { uint8 type = read<uint8>(x); readEvent(type); } }
    void readEvent(uint8 type) {
        /***/ if(type==0) { XError e=read<XError>(x);
            error("Error",e.code<sizeof(xerror)/sizeof(*xerror)?xerror[e.code]:dec(e.code),"seq:",e.seq,"id",e.id,"request",
                  e.major<sizeof(xrequest)/sizeof(*xrequest)?xrequest[e.major]:dec(e.major),"minor",e.minor);
        } else if(type==1) { error("Unexpected reply");
        } else { XEvent unused e=read<XEvent>(x); type&=0b01111111; //msb set if sent by SendEvent
            /*bool needUpdate = false;
            while(XPending(x)) { XEvent e; XNextEvent(x,&e); XID id = e.window;
                if(e.type==CreateNotify) {
                    XSelectInput(x,id, StructureNotifyMask|SubstructureNotifyMask|PropertyChangeMask);
                    XWindowAttributes wa; XGetWindowAttributes(x, id, &wa); if(wa.override_redirect) continue;
                    XGrabButton(x,1,AnyModifier,id,0,ButtonPressMask,0,1,0,0);
                    continue;
                } else if(e.type == MapRequest) {
                    XMapWindow(x, id);
                    raise(id);
                    int i = indexOf(tasks, Task(id));
                    if(i<0) { i=addTask(id); if(i>=0) tasksChanged.emit(tasks.array::size()); }
                    if(i<0) continue;
                    tasks.index=i;
                } else if(e.type == ConfigureRequest) {
                    XConfigureRequestEvent& c = (XConfigureRequestEvent&)e;
                    id = c.window;
                    XWindowAttributes wa; XGetWindowAttributes(x, id, &wa);
                    if(c.value_mask & CWX) wa.x=c.x; if(c.value_mask & CWY) wa.y=c.y;
                    if(c.value_mask & CWWidth) wa.width=c.width; if(c.value_mask & CWHeight) wa.height=c.height;
                    array<Atom> motif = Window::getAtomProperty(id,"_MOTIF_WM_HINTS"_);
                    array<Atom> type = Window::getAtomProperty(id,"_NET_WM_WINDOW_TYPE"_);
                    if(!wa.override_redirect &&
                            (!type || type[0]==Atom(_NET_WM_WINDOW_TYPE_NORMAL) || type[0]==Atom(_NET_WM_WINDOW_TYPE_DESKTOP))
                            && (!motif || motif[0]!=3 || motif[1]!=0)) {
                        wa.width=min(display.x,wa.width); wa.height=min(display.y-16,wa.height);
                        wa.x = (display.x - wa.width)/2;
                        wa.y = 16+(display.y-16-wa.height)/2;
                    }
                    XMoveResizeWindow(x,id, wa.x,wa.y,wa.width,wa.height);
                    continue;
                } else if(e.type == ButtonPress) {
                    raise(id);
                    XAllowEvents(x, 2, 0);
                    int i = indexOf(tasks, Task(id));
                    if(i>=0) tasks.index=i;
                } else if(e.type==PropertyNotify) {
                    int i = indexOf(tasks, Task(id));
                    if(i<0) i=addTask(id);
                    if(i<0) continue;
                    Atom atom = ((XPropertyEvent&)e).atom;
                    if(atom==Atom(_NET_WM_NAME)) tasks[i].get<Text>().setText( getTitle(id) );
                    else if(atom==Atom(_NET_WM_ICON)) tasks[i].get<Icon>().image = getIcon(id);
                    else continue;
                } else if(e.type == UnmapNotify || e.type == DestroyNotify || e.type == ReparentNotify || e.type == VisibilityNotify) {
                    int i = indexOf(tasks, Task(id));
                    if(i>=0) {
                        tasks.removeAt(i);
                        if(tasks.index == uint(i)) tasks.index=-1;
                        tasksChanged.emit(tasks.array::size());
                    }

                    array<XID> windows = getWindows();
                    if(windows) {
                        XID id=windows.last();
                        //XSetInputFocus(x, id, RevertToPointerRoot, CurrentTime);
                        if(tasks.index==uint(-1)) tasks.setActive(indexOf(tasks, Task(id)));
                    }
                }
                needUpdate = true;
            }
            if(needUpdate && window.visible) { panel.update(); window.render(); }*/
            log("Event", type<sizeof(xevent)/sizeof(*xevent)?xevent[type]:str(type));
        }
    }

    template<class T> T readReply() { for(;;) { uint8 type = read<uint8>(x); if(type==1) return read<T>(x); else readEvent(type); } }
    uint Atom(const ref<byte>& name) {
        {InternAtom r; r.length=name.size; r.size+=align(4,r.length)/4; write(x,string(raw(r)+name+pad(4,r.length)));}
        {InternAtomReply r=readReply<InternAtomReply>(); return r.atom; }
    }

    array<byte> getProperty(uint window, const ref<byte>& name) {
        {GetProperty r; r.window=window; r.property=Atom(name); write(x,string(raw(r)));}
        {GetPropertyReply r=readReply<GetPropertyReply>(); return read(x, r.length*r.format/8); }
    }

    string getTitle(uint id) {
        string name = getProperty(id,"_NET_WM_NAME"_);
        if(!name) name = getProperty(id,"WM_NAME"_);
        return move(name);
    }

    Image<byte4> getIcon(uint id) {
        array<byte> buffer = getProperty(id,"_NET_WM_ICON"_);
        if(buffer.size()<=8) return Image<byte4>();
        array<byte4> image(buffer.size()/4-2); image.setSize(buffer.size()/4-2);
        for(uint i=0;i<buffer.size()/4-2;i++) image[i] = *(byte4*)&buffer[8+i*4];
        return resize(Image<byte4>(move(image), *(uint*)&buffer[0], *(uint*)&buffer[4]), 16,16);
    }

    /*static XID* topLevelWindowList=0;
    static array<XID> getWindows() {
    if(topLevelWindowList) XFree(topLevelWindowList);
    XID root,parent; uint count=0; XQueryTree(x,x->screens[0].root,&root,&parent,&topLevelWindowList,&count);
    return array<XID>(topLevelWindowList,count);
    }*/

    void raise(uint unused id) {
        /*XRaiseWindow(x, id);
    XSetInputFocus(x, id, 1, 0);
    for(XID w: getWindows()) {
        if(Window::getAtomProperty(w,"WM_TRANSIENT_FOR") == array<Atom>{id}) {
            XRaiseWindow(x, w);
            XSetInputFocus(x, w, 1, 0);
        }
    }*/
    }

};
Taskbar* Taskbar::taskbar;
bool operator==(const Taskbar::Task& a,const Taskbar::Task& b){return a.id==b.id;}
Application(Taskbar)
