#pragma once
#include "core.h"

struct Display;
struct Visual;
typedef ulong KeySym;
#if __WORDSIZE == 64
typedef ulong Atom;
typedef ulong XID;
#else //ulong==uint but require extra useless array<ulong> instance
typedef uint Atom;
typedef uint XID;
#endif
typedef void* GC;
struct XEvent;
#if 0
struct XShmSegmentInfo {
    size_t shmseg;
    int shmid;
    char *shmaddr;
    int readOnly;
};
int shmdt(const void* addr);
int shmget(int key, size_t size, int flag);
void* shmat(int id, const void* addr, int flag);

struct Display { void* private1[2]; int fd;int private2[3]; void* private3; XID private4[3]; int private5; void* private6;
    int private7[5]; void* private8; int private9[2]; void* private10[2]; int private11; ulong private12[2]; void* private13[4];
    uint private14; void* private15[3]; int private16[2]; struct Screen* screens; };
struct Screen { void* private1; Display* display; XID root; int width, height; };
struct XVisualInfo { Visual* visual; ulong id; int screen,depth,c_class; ulong private1[3]; int private2[2]; };
struct XImage { int width, height, private1[2]; char *data; int private2[5]; int stride; int private3; ulong private4[3];
                void* private5[2]; int (*destroy_image)(XImage*); };
struct XWindowAttributes { int x, y, width, height, border, depth; Visual *visual; XID root; int private1[4]; ulong private2[2];
bool private3; XID private4; bool private5; int map_state; long private6[3]; bool override_redirect; Screen* screen; };
struct XSetWindowAttributes {  ulong bg_pixmap, bg_pixel, border_pixmap, border_pixel; int unused1[3]; ulong unused2[2];
                               bool unused3; long event_mask; long unused4; bool override_redirect; XID colormap, cursor; };
union XEvent {
    struct { int type; ulong serial; bool send_event; Display *display; XID window;
             XID root,subwindow; ulong time; int x, y, x_root, y_root; uint state, code; };
    long pad[24];
};
struct XClientMessageEvent { int type; ulong serial; bool send_event; Display *display; XID window;
                             Atom message_type; int format; long data[5]; };
struct XConfigureRequestEvent { int type; ulong serial; bool send_event; Display *display; XID parent, window;
                                int x, y, width, height, border_width; XID above; int detail; ulong value_mask; };
struct XPropertyEvent { int type; ulong serial; bool send_event; Display *display; XID window; Atom atom; };

enum { TrueColor=4 };
enum { KeyPress=2, KeyRelease, ButtonPress, ButtonRelease, MotionNotify, EnterNotify, LeaveNotify, Expose=12, VisibilityNotify=15,
       CreateNotify, DestroyNotify, UnmapNotify, MapNotify, MapRequest, ReparentNotify, ConfigureNotify, ConfigureRequest,
       PropertyNotify=28, ClientMessage=33 };
enum { Button1Mask=1<<8, AnyModifier=1<<15 };
enum { KeyPressMask=1<<0, KeyReleaseMask=1<<1, ButtonPressMask=1<<2, ButtonReleaseMask=1<<3,
       EnterWindowMask=1<<4, LeaveWindowMask=1<<5, PointerMotionMask=1<<6, ExposureMask=1<<15,
       StructureNotifyMask=1<<17, SubstructureNotifyMask=1<<19, SubstructureRedirectMask=1<<20, PropertyChangeMask=1<<22 };
enum { CWBackPixel=1<<1, CWBorderPixel=1<<3, CWOverrideRedirect=1<<9, CWEventMask=1<<11, CWColormap=1<<13, CWCursor=1<<14 };
enum { IsViewable=2 };
enum { CWX=1<<0, CWY=1<<1, CWWidth=1<<2, CWHeight=1<<3 };

Display* XOpenDisplay(const char*);
int XFree(void*);
int XConnectionNumber(Display*);
int XSelectInput(Display*, XID, long);
int XPending(Display*);
int XNextEvent(Display*, XEvent*);
int XFlush(Display*);
int XSync(Display*, bool);
int XSendEvent(Display*, XID, bool, long, XEvent*);
int XAllowEvents(Display*, int, ulong);

struct XErrorEvent { int type; Display* display; XID id; ulong serial; ubyte error_code; };
typedef int (*XErrorHandler) (Display*, XErrorEvent*);
int XGetErrorText(Display*, int, char*, int);
XErrorHandler XSetErrorHandler(XErrorHandler);

XID XCreateWindow(Display*, XID, int, int, uint, uint, uint, int, uint, Visual*, ulong, XSetWindowAttributes*);
GC XCreateGC(Display*, XID, ulong, void*);
XID XCreateColormap(Display*, XID, Visual*, int);
XID XCreateFontCursor(Display*, uint);
int XMatchVisualInfo(Display*, int, int, int, XVisualInfo*);

XImage *XShmCreateImage(Display*, Visual*, uint, int, char*, XShmSegmentInfo*, uint, uint);
bool XShmAttach(Display*, XShmSegmentInfo*);
bool XShmDetach(Display*, XShmSegmentInfo*);
bool XShmPutImage(Display*, XID, GC, XImage*, int, int, int, int, uint, uint, bool);

int XGetWindowProperty(Display*, XID, Atom, long, long, bool, Atom, Atom*, int*, ulong*, ulong*, ubyte**);
int XChangeProperty(Display*, XID, Atom, Atom, int, int, const ubyte*, int);
int XGetWindowAttributes(Display*, XID, XWindowAttributes*);
int XChangeWindowAttributes(Display*, XID, ulong, XSetWindowAttributes*);
int XQueryTree(Display*, XID, XID*, XID*, XID**, uint*);

KeySym XkbKeycodeToKeysym(Display*, uint, int, int);
KeySym XStringToKeysym(const char*);
ubyte XKeysymToKeycode(Display*, KeySym);
int XGrabKey(Display*, int, uint, XID, bool, int, int);
int XGrabButton(Display*, uint, uint, XID, int, uint, int, int, XID, XID);

int XMapWindow(Display*, XID);
int XUnmapWindow(Display*, XID);
int XRaiseWindow(Display*, XID);
int XMoveWindow(Display*, XID, int, int);
int XResizeWindow(Display*, XID, uint, uint);
int XMoveResizeWindow(Display*, XID, int, int, uint, uint);

int XGetInputFocus(Display*, XID*, int*);
int XSetInputFocus(Display*, XID, int, ulong);
XID XGetSelectionOwner(Display*, Atom);
int XConvertSelection(Display*, Atom, Atom, Atom, XID, ulong);

/*enum Key {
    Escape=0xff1b, Return=0xff0d, Delete=0xffff, BackSpace=0xff08,
    Home=0xff50, Left, Up, Right, Down, End=0xff57
};*/
#endif
