#pragma once
/// \file x.h X protocol
#include "core.h"
namespace X11 {
struct Event {
    uint8 type;
    struct Error { uint8 code; uint16 seq; uint id; uint16 minor; uint8 major; } packed;
    struct Generic { uint8 ext; uint16 seq; uint size; uint16 type; } packed;
    union {
        Error error;
		struct { byte padOrFdCount; uint16 seq; uint size; byte pad1[24]; } packed reply;
        struct { uint8 key; uint16 seq; uint time,root,event,child; int16 rootX,rootY,x,y; int16 state; int8 sameScreen; } packed; // input
        struct { byte detail; uint16 seq; uint window; uint8 mode; } packed focus;
        struct { byte pad; uint16 seq; uint window; uint16 x,y,w,h,count; } packed expose;
        struct { byte pad; uint16 seq; uint parent,window; int16 x,y,w,h,border; int8 override_redirect; } packed create;
        struct { byte pad; uint16 seq; uint event,window; int8 override_redirect; } packed map;
        struct { byte pad; uint16 seq; uint event,window; int8 from_configure; } packed unmap;
		struct { byte pad; uint16 seq; uint parent,window; } packed mapRequest;
        struct { byte pad; uint16 seq; uint event,window,above; int16 x,y,w,h,border; int8 override_redirect; } packed configure;
		struct { byte stackMode; uint16 seq; uint parent,window,sibling; int16 x,y,w,h,border; int16 valueMask; } packed configureRequest;
        struct { byte pad; uint16 seq; uint window, atom, time; uint8 state; } packed property;
		struct { byte pad; uint16 seq; uint time,owner,requestor,selection,target,property; } packed selectionRequest;
		struct { byte pad; uint16 seq; uint time,          requestor,selection,target,property; } packed selectionNotify;
        struct { byte format; uint16 seq; uint window, type; uint data[5]; } packed client;
        Generic genericEvent;
        byte pad[31];
    } packed;
} packed;
static_assert(sizeof(Event)==32,"");

enum ValueMask { BackgroundPixmap=1<<0, BackgroundPixel=1<<1, BorderPixmap=1<<2, BorderPixel=1<<3, BitGravity=1<<4,
                 WinGravity=1<<5, OverrideRedirect=1<<9, SaveUnder=1<<10, EventMask=1<<11, ColorMap=1<<13, CursorMask=1<<14 };
enum EventMask { KeyPressMask=1<<0, KeyReleaseMask=1<<1, ButtonPressMask=1<<2, ButtonReleaseMask=1<<3, EnterWindowMask=1<<4,
                 LeaveWindowMask=1<<5, PointerMotionMask=1<<6, ExposureMask=1<<15, StructureNotifyMask=1<<17,
                 SubstructureNotifyMask=1<<19, SubstructureRedirectMask=1<<20, FocusChangeMask=1<<21, PropertyChangeMask=1<<22 };
enum { Error, Reply, KeyPress, KeyRelease, ButtonPress, ButtonRelease, MotionNotify, EnterNotify, LeaveNotify, FocusIn, FocusOut, KeymapNotify,
       Expose, GraphicsExpose, NoExpose, VisibilityNotify, CreateNotify, DestroyNotify, UnmapNotify, MapNotify, MapRequest, ReparentNotify,
       ConfigureNotify, ConfigureRequest, GravityNotify, ResizeRequest, CirculateNotify, CirculateRequest, PropertyNotify,
       SelectionClear, SelectionRequest, SelectionNotify, ColormapNotify , ClientMessage, MappingNotify, GenericEvent };
enum ModifierMask { ShiftMask=1<<0, LockMask=1<<1, ControlMask=1<<2,
		    Mod1Mask=1<<3, Mod2Mask=1<<4, Mod3Mask=1<<5, Mod4Mask=1<<6, Mod5Mask=1<<7,
		    Button1Mask=1<<8, Button2Mask=1<<9, Button3Mask=1<<10, Button4Mask=1<<11, Button5Mask=1<<12, AnyModifier=1<<15 };
enum MapState { IsUnmapped, IsUnviewable, IsViewable };
enum ConfigureMask { X=1<<0, Y=1<<1, W=1<<2, H=1<<3, StackMode=1<<6 };
enum StackMode { Above,Below,TopIf,BottomIf,Opposite };

struct ConnectionSetup { byte bom='l', pad=0; int16 major=11,minor=0; uint16 nameSize=0, dataSize=0, pad2=0; };
struct ConnectionSetupReply1 { int8 status,reason; int16 major,minor,additionnal; };
struct ConnectionSetupReply2 { int32 release, ridBase, ridMask, motionBufferSize; int16 vendorLength, maxRequestSize; int8 numScreens, numFormats, imageByteOrder, bitmapBitOrder, bitmapScanlineUnit, bitmapScanlinePad, minKeyCode, maxKeyCode; int32 pad2; };
struct XFormat { uint8 depth, bitsPerPixel, scanlinePad; int32 pad; };
struct Screen { int32 root, colormap, white, black, inputMask; int16 width, height, widthMM, heightMM, minMaps, maxMaps; int32 visual;
                int8 backingStores, saveUnders, depth, numDepths; };
struct XDepth { int8 depth; int16 numVisualTypes; int32 pad; };
struct VisualType { uint id; uint8 class_, bpp; int16 colormapEntries; int32 red,green,blue,pad; };

struct CreateWindow {
    int8 req = 1, depth = 32; uint16 size = 15; uint id = 0, parent = 0; uint16 x = 0,y = 0, width,height, border=0, class_=1; uint visual;
    uint mask = BackgroundPixmap|BorderPixel|BitGravity|WinGravity|OverrideRedirect|EventMask|ColorMap;
    uint backgroundPixmap = 0, borderPixel = 0, bitGravity = 10, winGravity = 10, overrideRedirect = 0;
    uint eventMask = StructureNotifyMask|KeyPressMask|KeyReleaseMask|ButtonPressMask|ButtonReleaseMask| EnterWindowMask|LeaveWindowMask
                              | PointerMotionMask|ExposureMask;
    uint colormap;
};
struct SetWindowEventMask { int8 req=2; uint16 size=4; uint window, mask=EventMask; uint eventMask; };
struct SetWindowCursor { int8 req=2, pad=0; uint16 size=4; uint window, mask=CursorMask; uint cursor; };
struct GetWindowAttributes {
    int8 req=3; uint16 size=2; uint window;
    struct Reply {
        int8 backingStore; uint16 seq; uint size, visual; int16 class_; int8 bit,win; uint planes, pixel;
        int8 saveUnder, mapIsInstalled, mapState, overrideRedirect; uint colormap, allEventMask, yourEventMask; int16 nopropagate, pad;
    } packed;
};
struct DestroyWindow { int8 req=4, pad=0; uint16 size=2; uint id; };
struct MapWindow { int8 req=8, pad=0; uint16 size=2; uint id;};
struct UnmapWindow { int8 req=10, pad=0; uint16 size=2; uint id;};
struct ConfigureWindow { int8 req=12, pad=0; uint16 size=8; uint id; int16 mask=X|Y|W|H|StackMode,pad2=0; uint x,y,w,h,stackMode; };
struct SetPosition { int8 req=12, pad=0; uint16 size=5; uint id; int16 mask=X|Y,pad2=0; uint x,y; };
struct SetSize { int8 req=12, pad=0; uint16 size=5; uint id; int16 mask=W|H,pad2=0; uint w,h; };
struct SetGeometry { int8 req=12, pad=0; uint16 size=7; uint id; int16 mask=X|Y|W|H,pad2=0; uint x,y,w,h; };
struct RaiseWindow { int8 req=12, pad=0; uint16 size=4; uint id; int16 mask=StackMode, pad3=0; uint stackMode=Above; };
struct GetGeometry{
    int8 req=14, pad=0; uint16 size=2; uint id;
    struct Reply { byte depth; uint16 seq; uint size; uint root; int16 x,y,w,h,border; byte pad[10]; } packed;
};
struct QueryTree {
    int8 req=15, pad=0; uint16 size=2; uint id;
    struct Reply { byte pad; uint16 seq; uint size; uint root,parent; uint16 count; byte pad2[14]; } packed;
};
struct InternAtom {
    int8 req=16,exists=0; uint16 size=2; uint16 length, pad=0;
    struct Reply { byte pad; uint16 seq; uint size, atom; byte pad2[20]; } packed;
};
struct GetAtomName {
	int8 req=17,pad=0; uint16 size=2; uint atom;
	struct Reply { byte pad; uint16 seq; uint size; uint16 length; byte pad2[22]; } packed;
};
struct ChangeProperty { int8 req=18,replace=0; uint16 size=6; uint window,property,type; uint8 format,pad[3]={}; uint length; };
struct GetProperty {
    int8 req=20,remove=0; uint16 size=6; uint window,property,type=0; uint offset=0,length=-1;
    struct Reply { uint8 format; uint16 seq; uint size; uint type,bytesAfter,length,pad[3]; } packed;
};
struct SetSelectionOwner {
	uint8 req=22,pad=0; uint16 size=4; uint owner=0, selection=1, time = 0;
};
struct GetSelectionOwner {
    uint8 req=23,pad=0; uint16 size=2; uint selection=1;
    struct Reply { uint8 pad; uint16 seq; uint size; uint owner, pad2[5]; } packed;
};
struct ConvertSelection { uint8 req=24,pad=0; uint16 size=6; uint requestor=0,selection=1,target,property=0,time=0; };
struct SendEvent { int8 req=25,propagate=0; uint16 size=11; uint window; uint eventMask=0; Event event; };
struct GrabButton { int8 req=28,owner=0; uint16 size=6; uint window; uint16 eventMask=ButtonPressMask; uint8 pointerMode=0,keyboardMode=1; uint confine=0,cursor=0; uint8 button=0,pad; uint16 modifiers=AnyModifier; };
struct UngrabButton { int8 req=29,button=0; uint16 size=3; uint window; uint16 modifiers=AnyModifier, pad; };
struct GrabKey { int8 req=33,owner=0; uint16 size=4; uint window; uint16 modifiers=0x8000; uint8 keycode, pointerMode=1,keyboardMode=1, pad[3]; };
struct UngrabKey { int8 req=34; uint8 keycode; uint16 size=3; uint window; uint16 modifiers=AnyModifier; uint16 pad; };
struct ReplayKeyboard { int8 req=35, mode=5; uint16 size=2; uint time=0; };
struct ReplayPointer { int8 req=35, mode=2; uint16 size=2; uint time=0; };
struct SetInputFocus { int8 req=42,revertTo=1; uint16 size=3; uint window=1; uint time=0; };
struct CreatePixmap { int8 req=53,depth=32; uint16 size=4; uint pixmap,window; uint16 w,h; };
struct FreePixmap { int8 req=54,pad=0; uint16 size=2; uint pixmap; };
struct CreateGC { int8 req=55,pad=0; uint16 size=4; uint context,window,mask=0; };
struct FreeGC { int8 req=60,pad=0; uint16 size=2; uint context; };
struct CopyArea { int8 req=62,pad=0; uint16 size=7; uint src,dst,gc; int16 srcX,srcY,dstX,dstY,w,h; };
struct PutImage { int8 req=72,format=2; uint16 size=6; uint drawable,context; uint16 w,h,x=0,y=0; uint8 leftPad=0,depth=32; int16 pad=0; };
struct CreateColormap { int8 req=78,alloc=0; uint16 size=4; uint colormap,window,visual; };
struct FreeCursor { int8 req=95,pad=0; uint16 size=2; uint cursor; };
struct QueryExtension {
    int8 req=98,pad=0; uint16 size=2, length, pad2=0;
	struct Reply { byte pad; uint16 seq; uint size; uint8 present,major,firstEvent,firstError; byte pad2[20]; } packed;
};
struct ListExtensions {
	int8 req=99, pad=0; uint16 size=1;
	struct Reply { uint8 extensionCount; uint16 seq; uint size; byte pad[24]; } packed;
};
struct GetKeyboardMapping {
    int8 req=101, pad=0; uint16 size=2; uint8 keycode, count=1; int16 pad2=0;
    struct Reply { uint8 numKeySymsPerKeyCode; uint16 seq; uint size; byte pad[24]; } packed;
};

constexpr string requests[] = {"0","CreateWindow","ChangeWindowAttributes","GetWindowAttributes","DestroyWindow","DestroySubwindows","ChangeSaveSet","ReparentWindow","MapWindow","MapSubwindows","UnmapWindow","UnmapSubwindows",
							   "ConfigureWindow","CirculateWindow","GetGeometry","QueryTree","InternAtom","GetAtomName","ChangeProperty","DeleteProperty","GetProperty","ListProperties","SetSelectionOwner","GetSelectionOwner","ConvertSelection",
							   "SendEvents","GrabPointer","UngrabPointer","GrabButton","UngrabButton","ChangeActivePointerGrab","GrabKeyboard","UngrabKeyboard","GrabKey","UngrabKey","AllowEvents","GrabServer","UngrabServer","QueryPointer",
							   "GetMotionEvents","TranslateCoordinates","WarpPointer","SetInputFocus","GetInputFocus","QueryKeymap","OpenFont","CloseFont","QueryFont","QueryTextElements","ListFonts","ListFontsWithInfo","SetFontPath","GetFontPath",
							   "CreatePixmap","FreePixmap","CreateGC","ChangeGC","CopyGC","SetDashes","SetClipRectangles","FreeGC","ClearArea","CopyArea","CopyPlane","PolyPoint","PolyLine","PolySegment","PolyRectange","PolyArc","FillPoly",
							   "PolyFillRectangle","PolyFillArc","PutImage","GetImage","PolyText8", "PolyText16", "ImageText8", "ImageText16", "CreateColormap"};
constexpr string events[] = {"Error","Reply","KeyPress","KeyRelease","ButtonPress","ButtonRelease","MotionNotify","EnterNotify",
                              "LeaveNotify","FocusIn","FocusOut","KeymapNotify","Expose","GraphicsExpose","NoExpose","VisibilityNotify",
                              "CreateNotify","DestroyNotify","UnmapNotify","MapNotify","MapRequest","ReparentNotify","ConfigureNotify",
                              "ConfigureRequest","GravityNotify","ResizeRequest","CirculateNotify","CirculateRequest","PropertyNotify",
                              "SelectionClear","SelectionRequest","SelectionNotify","ColormapNotify ","ClientMessage","MappingNotify"};
constexpr string errors[] = {"","Request","Value","Window","Pixmap","Atom","Cursor","Font","Match","Drawable","Access","Alloc",
                              "Colormap","GContext","IDChoice","Name","Length","Implementation"};
}

namespace Shm {
extern int EXT, event, errorBase;
struct QueryVersion {
    int8 ext=EXT, req=0; uint16 size=1;
    struct Reply { int8 sharedPixmaps; uint16 seq; uint size; uint16 major,minor,uid,gid; uint8 format,pad[15]; } packed;
};
struct Attach { int8 ext=EXT, req=1; uint16 size=4; uint seg, shm; int8 readOnly=0, pad[3]={}; };
struct Detach { int8 ext=EXT, req=2; uint16 size=2; uint seg; };
struct PutImage { int8 ext=EXT, req=3; uint16 size=10; uint window,context; uint16 totalW, totalH, srcX, srcY, srcW, srcH,
		  dstX, dstY; uint8 depth=32,format=2,sendEvent=1,bpad=32; uint seg,offset=0; };
struct GetImage {
    int8 ext=EXT, req=4; uint16 size=8; uint window; uint16 x=0,y=0,w,h; uint mask=~0; uint8 format=2; uint seg,offset=0;
    struct Reply { uint8 depth; uint16 seq; uint size; uint visual, length, pad[4]; } packed;
};
enum { Completion };
constexpr string requests[] = {"QueryVersion","Attach","Detach","PutImage","GetImage"};
constexpr string errors[] = {"BadSeg"};
}

namespace XRender {
enum PICTOP { Clear, Src, Dst, Over };
struct PictFormInfo { uint format; uint8 type,depth; uint16 direct[8]; uint colormap; };
struct PictVisual { uint visual, format; };
struct PictDepth { uint8 depth; uint16 numPictVisuals; uint pad; /*PictVisual[numPictVisuals]*/ };
struct PictScreen { uint numDepths; uint fallback; /*PictDepth[numDepths]*/ };

extern int EXT, errorBase;
struct QueryVersion {
    int8 ext=EXT,req=0; uint16 size=3; uint major=0,minor=11;
    struct Reply { int8 pad; uint16 seq; uint size; uint major,minor,pad2[4]; } packed;
};
struct QueryPictFormats{
    int8 ext=EXT,req=1; uint16 size=1;
    struct Reply { int8 pad; uint16 seq; uint size; uint numFormats,numScreens,numDepths,numVisuals,numSubpixels,pad2; } packed;
};
struct CreatePicture { int8 ext=EXT, req=4; uint16 size=5; uint picture, drawable, format, valueMask=0; };
struct FreePicture { int8 ext=EXT, req=7; uint16 size=2; uint picture; };
struct Composite { int8 ext=EXT, req=8; uint16 size=9; uint8 op=Over; uint src,mask=0,dst; int16 srcX=0,srcY=0,maskX=0,maskY=0,dstX=0,dstY=0,width,height; };
struct CreateCursor { int8 ext=EXT, req=27; uint16 size=4; uint cursor, picture; uint16 x,y; };
constexpr string requests[] = {"QueryVersion", "QueryPictFormats", "QueryPictIndexValues", "QueryFilters", "CreatePicture", "ChangePicture",
			       "SetPictureClipRectangles", "SetPictureTransform", "SetPictureFilter", "FreePicture", "Composite"};
constexpr string errors[] = {"","PictFormat", "Picture", "PictOp", "GlyphSet", "Glyph"};
}

namespace Present {
extern int EXT;
enum { ConfigureNotifyMask=1<<0, CompleteNotifyMask=1<<1, RedirectNotifyMask=1<<2 };
struct Pixmap { int8 ext=EXT,req=1; uint16 size=18; uint window, pixmap, serial=0, validArea=0, updateArea=0; int16 xOffset=0, yOffset=0; uint targetCRTC=0;
                uint waitFence=0, idleFence=0; uint options=0; uint64 targetMSC=0, divisor=0, remainder=0; };
struct NotifyMSC { int8 ext=EXT,req=2; uint16 size=10; uint window, serial=0, pad; uint64 targetMSC=0, divisor=0, remainder=0; };
struct SelectInput { int8 ext=EXT, req=3; uint16 size=4; uint eid, window, eventMask=CompleteNotifyMask; };
enum { ConfigureNotify, CompleteNotify };
struct CompleteNotify { uint8 type; X11::Event::Generic genericEvent; uint8 kind, mode; uint event_id, window, serial; uint64 ust, msc; } packed;
}

namespace DRI3 {
extern int EXT;
struct QueryVersion {
	int8 ext=EXT,req=0; uint16 size=3; uint major=1,minor=0;
	struct Reply { int8 pad; uint16 seq; uint size; uint major,minor,pad2[4]; } packed;
};
struct Open {
	int8 ext=EXT, req=1; uint16 size=3; uint drawable, provider=0;
	struct Reply { uint8 nfd; uint16 seq; uint size; uint pad[6]; } packed;
} packed;
struct PixmapFromBuffer {
	int8 ext=EXT, req=2; uint16 size=6; uint pixmap, drawable, bufferSize; uint16 width, height, stride; uint8 depth=32, bpp=32; //FD fd;
} packed;
struct BufferFromPixmap {
	int8 ext=EXT, req=3; uint16 size=2; uint pixmap;
	struct Reply { uint8 nfd; uint16 seq; uint size; uint16 width, height, stride; uint8 depth, bpp; uint pad[4]; } packed;
} packed;
constexpr string requests[] = {"QueryVersion","Open","PixmapFromBuffer","BufferFromPixmap"};
}
