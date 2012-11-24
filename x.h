#pragma once
/// \file x.h X11 protocol
#include "core.h"

struct sockaddr_un { uint16 family=1; char path[108]={}; };
enum {IPC_NEW=0, IPC_RMID=0, IPC_CREAT=01000};

#define fixed(T) _packed ; static_assert(sizeof(T)==31,"")

enum ValueMask { BackgroundPixmap=1<<0, BackgroundPixel=1<<1, BorderPixmap=1<<2, BorderPixel=1<<3, BitGravity=1<<4, WinGravity=1<<5, OverrideRedirect=1<<9, SaveUnder=1<<10, EventMask=1<<11, ColorMap=1<<13, CursorMask=1<<14 };
enum EventMask { KeyPressMask=1<<0, KeyReleaseMask=1<<1, ButtonPressMask=1<<2, ButtonReleaseMask=1<<3, EnterWindowMask=1<<4, LeaveWindowMask=1<<5, PointerMotionMask=1<<6, ExposureMask=1<<15, StructureNotifyMask=1<<17, SubstructureNotifyMask=1<<19, SubstructureRedirectMask=1<<20, FocusChangeMask=1<<21, PropertyChangeMask=1<<22 };
enum { KeyPress=2, KeyRelease, ButtonPress, ButtonRelease, MotionNotify, EnterNotify, LeaveNotify, FocusIn, FocusOut, KeymapNotify, Expose, GraphicsExpose, NoExpose, VisibilityNotify, CreateNotify, DestroyNotify, UnmapNotify, MapNotify, MapRequest, ReparentNotify, ConfigureNotify, ConfigureRequest, GravityNotify, ResizeRequest, CirculateNotify, CirculateRequest, PropertyNotify, SelectionClear, SelectionRequest, SelectionNotify, ColormapNotify , ClientMessage, MappingNotify };
enum ModifierMask { ShiftMask=1<<0, LockMask=1<<1, ControlMask=1<<2, Mod1Mask=1<<3, Mod2Mask=1<<4, Mod3Mask=1<<5, Mod4Mask=1<<6, Mod5Mask=1<<7, Button1Mask=1<<8, Button2Mask=1<<9, Button3Mask=1<<10, Button4Mask=1<<11, Button5Mask=1<<12, AnyModifier=1<<15 };
enum MapState { IsUnmapped, IsUnviewable, IsViewable };
enum ConfigureMask { X=1<<0, Y=1<<1, W=1<<2, H=1<<3, StackMode=1<<6 };
enum StackMode { Above,Below,TopIf,BottomIf,Opposite };

struct XError { uint8 code; uint16 seq; uint id; uint16 minor; uint8 major; byte pad[21]; } fixed(XError);
union XEvent {
    struct { uint8 key; uint16 seq; uint time,root,event,child; int16 rootX,rootY,x,y; int16 state; int8 sameScreen; } _packed;
    struct { byte detail; uint16 seq; uint window; uint8 mode; } _packed focus;
    struct { byte pad; uint16 seq; uint window; uint16 x,y,w,h,count; } _packed expose;
    struct { byte pad; uint16 seq; uint parent,window; int16 x,y,w,h,border; int8 override_redirect; } _packed create;
    struct { byte pad; uint16 seq; uint event,window; int8 override_redirect; } _packed map;
    struct { byte pad; uint16 seq; uint event,window; int8 from_configure; } _packed unmap;
    struct { byte pad; uint16 seq; uint parent,window; } _packed map_request;
    struct { byte pad; uint16 seq; uint event,window,above; int16 x,y,w,h,border; int8 override_redirect; } _packed configure;
    struct { byte stackMode; uint16 seq; uint parent,window,sibling; int16 x,y,w,h,border; int16 valueMask; } _packed configure_request;
    struct { byte pad; uint16 seq; uint window, atom, time; uint8 state; } _packed property;
    struct { byte pad; uint16 seq; uint time, requestor,selection,target,property; } _packed selection;
    struct { byte format; uint16 seq; uint window, type; uint data[5]; } _packed client;
    byte pad[31];
} fixed(XEvent);

struct ConnectionSetup { byte bom='l', pad=0; int16 major=11,minor=0; int16 nameSize=0, dataSize=0, pad2=0; };
struct ConnectionSetupReply { int8 status,reason; int16 major,minor,additionnal; int32 release, ridBase, ridMask, motionBufferSize; int16 vendorLength, maxRequestSize; int8 numScreens, numFormats, imageByteOrder, bitmapBitOrder, bitmapScanlineUnit, bitmapScanlinePad, minKeyCode, maxKeyCode; int32 pad2; };
struct XFormat { uint8 depth, bitsPerPixel, scanlinePad; int32 pad; };
struct Screen { int32 root, colormap, white, black, inputMask; int16 width, height, widthMM, heightMM, minMaps, maxMaps; int32 visual;
                int8 backingStores, saveUnders, depth, numDepths; };
struct Depth { int8 depth; int16 numVisualTypes; int32 pad; };
struct VisualType { uint id; uint8 class_, bpp; int16 colormapEntries; int32 red,green,blue,pad; };

struct CreateWindow { int8 req=1, depth=32; uint16 size=15; uint id=0,parent=0; uint16 x,y,width,height,border=0,class_=1; uint visual;
                      uint mask=BackgroundPixmap|BorderPixel|BitGravity|WinGravity|OverrideRedirect|EventMask|ColorMap;
                                        uint backgroundPixmap=0,borderPixel=0, bitGravity=10, winGravity=10, overrideRedirect, eventMask, colormap; };
struct SetWindowEventMask { int8 req=2; uint16 size=4; uint window, mask=EventMask; uint eventMask; };
struct SetWindowCursor { int8 req=2, pad=0; uint16 size=4; uint window, mask=CursorMask; uint cursor; };
struct GetWindowAttributes { int8 req=3; uint16 size=2; uint window; };
struct GetWindowAttributesReply { int8 backingStore; uint16 seq; uint length, visual; int16 class_; int8 bit,win; uint planes, pixel; int8 saveUnder, mapIsInstalled, mapState, overrideRedirect; uint colormap, allEventMask, yourEventMask; int16 nopropagate, pad; } _packed;
struct DestroyWindow { int8 req=4; uint16 size=2; uint id; };
struct MapWindow { int8 req=8, pad=0; uint16 size=2; uint id;};
struct UnmapWindow { int8 req=10, pad=0; uint16 size=2; uint id;};
struct ConfigureWindow { int8 req=12, pad=0; uint16 size=8; uint id; int16 mask=X|Y|W|H|StackMode,pad2=0; uint x,y,w,h,stackMode; };
struct SetPosition { int8 req=12, pad=0; uint16 size=5; uint id; int16 mask=X|Y,pad2=0; uint x,y; };
struct SetSize { int8 req=12, pad=0; uint16 size=5; uint id; int16 mask=W|H,pad2=0; uint w,h; };
struct SetGeometry { int8 req=12, pad=0; uint16 size=7; uint id; int16 mask=X|Y|W|H,pad2=0; uint x,y,w,h; };
struct RaiseWindow { int8 req=12, pad=0; uint16 size=4; uint id; int16 mask=StackMode, pad3=0; uint stackMode=Above; };
struct GetGeometry{ int8 req=14, pad=0; uint16 size=2; uint id; };
struct GetGeometryReply { byte depth; uint16 seq; uint length; uint root; int16 x,y,w,h,border; byte pad[10]; } fixed(GetGeometryReply);
struct QueryTree { int8 req=15, pad=0; uint16 size=2; uint id; };
struct QueryTreeReply { byte pad; uint16 seq; uint length; uint root,parent; uint16 count; byte pad2[14]; } fixed(QueryTreeReply);
struct InternAtom { int8 req=16,exists=0; uint16 size=2; int16 length, pad=0; };
struct InternAtomReply { byte pad; uint16 seq; uint length,atom; byte pad2[20]; } fixed(InternAtomReply);
struct GetAtomName { int8 req=17,pad=0; uint16 size=2; uint atom; };
struct GetAtomNameReply { byte pad; uint16 seq; uint length; uint16 size; byte pad2[22]; } fixed(GetAtomNameReply);
struct ChangeProperty { int8 req=18,replace=0; uint16 size=6; uint window,property,type; uint8 format,pad[3]={}; uint length; };
struct GetProperty { int8 req=20,remove=0; uint16 size=6; uint window,property,type=0; uint offset=0,length=-1; };
struct GetPropertyReply { uint8 format; uint16 seq; uint size; uint type,bytesAfter,length,pad[3]; } fixed(GetPropertyReply);
struct GetSelectionOwner { uint8 req=23,pad=0; uint16 size=2; uint selection=1; };
struct GetSelectionOwnerReply { uint8 pad; uint16 seq; uint size; uint owner, pad2[5]; } fixed(GetSelectionOwnerReply);
struct ConvertSelection { uint8 req=24,pad=0; uint16 size=6; uint requestor=0,selection=1,target,property=0,time=0; };
struct SendEvent { int8 req=25,propagate=0; uint16 size=11; uint window; uint eventMask=0; uint8 type; XEvent event; };
struct GrabButton { int8 req=28,owner=0; uint16 size=6; uint window; uint16 eventMask=ButtonPressMask; uint8 pointerMode=0,keyboardMode=1; uint confine=0,cursor=0; uint8 button=0,pad; uint16 modifiers=AnyModifier; };
struct UngrabButton { int8 req=29,button=0; uint16 size=3; uint window; uint16 modifiers=AnyModifier, pad; };
struct GrabKey { int8 req=33,owner=0; uint16 size=4; uint window; uint16 modifiers=AnyModifier; uint8 keycode, pointerMode=1,keyboardMode=1, pad[3]; };
struct UngrabKey { int8 req=34; uint8 keycode; uint16 size=3; uint window; uint16 modifiers=AnyModifier; uint16 pad; };
struct ReplayKeyboard { int8 req=35, mode=5; uint16 size=2; uint time=0; };
struct ReplayPointer { int8 req=35, mode=2; uint16 size=2; uint time=0; };
struct SetInputFocus { int8 req=42,revertTo=1; uint16 size=3; uint window=1; uint time=0; };
struct CreatePixmap { int8 req=53,depth=32; uint16 size=4; uint pixmap,window; int16 w,h; };
struct FreePixmap { int8 req=54,pad=0; uint16 size=2; uint pixmap; };
struct CreateGC { int8 req=55,pad=0; uint16 size=4; uint context,window,mask=0; };
struct FreeGC { int8 req=60,pad=0; uint16 size=2; uint context; };
struct CopyArea { int8 req=62,pad=0; uint16 size=7; uint src,dst,gc; int16 srcX,srcY,dstX,dstY,w,h; };
struct PutImage { int8 req=72,format=2; uint16 size=6; uint drawable,context; int16 w,h,x=0,y=0; uint8 leftPad=0,depth=32; int16 pad=0; };
struct CreateColormap { int8 req=78,alloc=0; uint16 size=4; uint colormap,window,visual; };
struct FreeCursor { int8 req=95,pad=0; uint16 size=2; uint cursor; };
struct QueryExtension { int8 req=98,pad=0; uint16 size=2, length, pad2=0; };
struct QueryExtensionReply { byte pad; uint16 seq; uint length; uint8 present,major,firstEvent,firstError; byte pad2[20]; } fixed(QueryExtensionReply);
struct GetKeyboardMapping { int8 req=101; uint16 size=2; uint8 keycode, count=1; int16 pad=0; };
struct GetKeyboardMappingReply { uint8 numKeySymsPerKeyCode; uint16 seq; uint length; byte pad[24]; } fixed(GetKeyboardMappingReply);

constexpr ref<byte> requests[] = {"0"_,"CreateWindow"_,"ChangeWindowAttributes"_,"GetWindowAttributes"_,"DestroyWindow"_,"DestroySubwindows"_,"ChangeSaveSet"_,"ReparentWindow"_,"MapWindow"_,"MapSubwindows"_,"UnmapWindow"_,"UnmapSubwindows"_,"ConfigureWindow"_,"CirculateWindow"_,"GetGeometry"_,"QueryTree"_,"InternAtom"_,"GetAtomName"_,"ChangeProperty"_,"DeleteProperty"_,"GetProperty"_,"ListProperties"_,"SetSelectionOwner"_,"GetSelectionOwner"_,"ConvertSelection"_,"SendEvents"_,"GrabPointer"_,"UngrabPointer"_,"GrabButton"_,"UngrabButton"_,"ChangeActivePointerGrab"_,"GrabKeyboard"_,"UngrabKeyboard"_,"GrabKey"_,"UngrabKey"_,"AllowEvents"_,"GrabServer"_,"UngrabServer"_,"QueryPointer"_,"GetMotionEvents"_,"TranslateCoordinates"_,"WarpPointer"_,"SetInputFocus"_,"GetInputFocus"_,"QueryKeymap"_,"OpenFont"_,"CloseFont"_,"QueryFont"_,"QueryTextElements"_,"ListFonts"_,"ListFontsWithInfo"_,"SetFontPath"_,"GetFontPath"_,"CreatePixmap"_,"FreePixmap"_,"CreateGC"_,"ChangeGC"_,"CopyGC"_,"SetDashes"_,"SetClipRectangles"_,"FreeGC"_,"ClearArea"_,"CopyArea"_,"CopyPlane"_,"PolyPoint"_,"PolyLine"_,"PolySegment"_,"PolyRectange"_,"PolyArc"_,"FillPoly"_,"PolyFillRectangle"_,"PolyFillArc"_,"PutImage"_};
constexpr ref<byte> events[] = {"Error"_,"Reply"_,"KeyPress"_,"KeyRelease"_,"ButtonPress"_,"ButtonRelease"_,"MotionNotify"_,"EnterNotify"_,
                                "LeaveNotify"_,"FocusIn"_,"FocusOut"_,"KeymapNotify"_,"Expose"_,"GraphicsExpose"_,"NoExpose"_,"VisibilityNotify"_,
                                "CreateNotify"_,"DestroyNotify"_,"UnmapNotify"_,"MapNotify"_,"MapRequest"_,"ReparentNotify"_,"ConfigureNotify"_,
                                "ConfigureRequest"_,"GravityNotify"_,"ResizeRequest"_,"CirculateNotify"_,"CirculateRequest"_,"PropertyNotify"_,
                                "SelectionClear"_,"SelectionRequest"_,"SelectionNotify"_,"ColormapNotify "_,"ClientMessage"_,"MappingNotify"_};
constexpr ref<byte> errors[] = {""_,"Request"_,"Value"_,"Window"_,"Pixmap"_,"Atom"_,"Cursor"_,"Font"_,"Match"_,"Drawable"_,"Access"_,"Alloc"_,
                                  "Colormap"_,"GContext"_,"IDChoice"_,"Name"_,"Length"_,"Implementation"_};

namespace Shm {
extern int EXT, event, errorBase;
struct QueryVersion { int8 ext=EXT, req=0; uint16 size=1; };
struct QueryVersionReply { int8 sharedPixmaps; uint16 seq; uint length; uint16 major,minor,uid,gid; uint8 format,pad[15]; } _packed;
struct Attach { int8 ext=EXT, req=1; uint16 size=4; uint seg,shm; int8 readOnly=0, pad[3]={}; };
struct Detach { int8 ext=EXT, req=2; uint16 size=2; uint seg; };
struct PutImage { int8 ext=EXT, req=3; uint16 size=10; uint window,context; uint16 totalW, totalH, srcX=0, srcY=0, srcW, srcH,
                  dstX=0, dstY=0; uint8 depth=32,format=2,sendEvent=1,bpad=32; uint seg,offset=0; };
struct GetImage { int8 ext=EXT, req=4; uint16 size=8; uint window; uint16 x=0,y=0,w,h; uint mask=~0; uint8 format=2; uint seg,offset=0; };
struct GetImageReply { uint8 depth; uint16 seq; uint length; uint visual, size, pad[4]; } fixed(GetImageReply);
enum { Completion };
constexpr ref<byte> requests[] = {"QueryVersion"_,"Attach"_,"Detach"_,"PutImage"_,"GetImage"_};
constexpr ref<byte> errors[] = {"BadSeg"_};
constexpr int errorCount = sizeof(errors)/sizeof(*errors);
}

namespace Render {
enum PICTOP { Clear, Src, Dst, Over };
struct PictFormInfo { uint format; uint8 type,depth; uint16 direct[8]; uint colormap; };
struct PictVisual { uint visual, format; };
struct PictDepth { uint8 depth; uint16 numPictVisuals; uint pad; /*PictVisual[numPictVisuals]*/ };
struct PictScreen { uint numDepths; uint fallback; /*PictDepth[numDepths]*/ };

extern int EXT, event, errorBase;
struct QueryVersion { int8 ext=EXT,req=0; uint16 size=3; uint major=0,minor=11; };
struct QueryVersionReply { int8 pad; uint16 seq; uint length; uint major,minor,pad2[4]; } fixed(QueryVersionReply);
struct QueryPictFormats{ int8 ext=EXT,req=1; uint16 size=1; };
struct QueryPictFormatsReply { int8 pad; uint16 seq; uint length; uint numFormats,numScreens,numDepths,numVisuals,numSubpixels,pad2; } fixed(QueryPictFormatsReply);
struct CreatePicture { int8 ext=EXT,req=4; uint16 size=5; uint picture,drawable,format,valueMask=0; };
struct FreePicture { int8 ext=EXT,req=7; uint16 size=2; uint picture; };
struct Composite { int8 ext=EXT,req=8; uint16 size=9; uint8 op=Over; uint src,mask=0,dst; int16 srcX=0,srcY=0,maskX=0,maskY=0,dstX=0,dstY=0,width,height; };
struct CreateCursor { int8 ext=EXT,req=27; uint16 size=4; uint cursor,picture; uint16 x,y; };
constexpr ref<byte> requests[] = {"QueryVersion"_, "QueryPictFormats"_, "QueryPictIndexValues"_, "QueryFilters"_, "CreatePicture"_, "ChangePicture"_, "SetPictureClipRectangles"_, "SetPictureTransform"_, "SetPictureFilter"_, "FreePicture"_, "Composite"_};
constexpr ref<byte> errors[] = {"PictFormat"_, "Picture"_, "PictOp"_, "GlyphSet"_, "Glyph"_};
constexpr int errorCount = sizeof(errors)/sizeof(*errors);
}

/// Returns padding zeroes to append in order to align an array of \a size bytes to \a width
inline ref<byte> pad(uint width, uint size){ static byte zero[4]={}; assert(width<=sizeof(zero)); return ref<byte>(zero,align(width,size)-size); }
