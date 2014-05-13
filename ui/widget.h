#pragma once
/// \file widget.h Widget interface to compose user interfaces
#include "vector.h"
#include "image.h"
#if !__arm__ //&& __GXX_EXPERIMENTAL_CXX0X__ /*!QtCreator*/
#define X11 1
#endif

/// Key symbols
enum Key {
#if X11
    Space=' ',
    Escape=0xff1b, Backspace=0xff08, Tab, Return=0xff0d,
    Home=0xff50, LeftArrow, UpArrow, RightArrow, DownArrow, PageUp, PageDown, End, PrintScreen=0xff61,
    Execute, Insert,
    KP_Enter=0xff8d, KP_Asterisk=0xffaa, KP_Plus, KP_Separator, KP_Minus, KP_Decimal, KP_Slash, KP_0,KP_1,KP_2,KP_3,KP_4,KP_5,KP_6,KP_7,KP_8,KP_9,
    ShiftKey=0xffe1, ControlKey=0xffe3,
    Delete=0xffff,
    Play=0x1008ff14, WWW=0x1008ff18, Email=0x1008ff19,
#else
    None, Escape, _1,_2,_3,_4,_5,_6,_7,_8,_9,_0, Minus, Equal, Backspace, Tab, Q,W,E,R,T,Y,U,I,O,P, LeftBrace, RightBrace, Return, LeftCtrl,
    A,S,D,F,G,H,J,K,L, Semicolon, Apostrophe, Grave, LeftShift, BackSlash, Z,X,C,V,B,N,M, Comma, Dot, Slash, RightShift, KP_Asterisk, LeftAlt,
    Space, KP_7 = 71, KP_8, KP_9, KP_Minus, KP_4, KP_5, KP_6, KP_Plus, KP_1, KP_2, KP_3, KP_0, KP_Slash=98,
    Home=102, UpArrow, PageUp, LeftArrow, RightArrow, End, DownArrow, PageDown, Insert, Delete, Macro, Mute, VolumeDown, VolumeUp,
    Power=116, Left=0x110, Right, Middle, Side, Extra, WheelDown=0x150, WheelUp,
#endif
};
inline String str(Key key) { return str(uint(key)); }
enum Modifiers { NoModifiers=0, Shift=1<<0, Control=1<<2, Alt=1<<3, NumLock=1<<4/*, Meta=1<<6*/ };

/// Abstract component to compose user interfaces
struct Widget {
// Layout
    /// Preferred size (positive means preferred, negative means expanding (i.e benefit from extra space))
    /// \note space is first allocated to preferred widgets, then to expanding widgets.
    virtual int2 sizeHint() { return -1; }
    /// Renders this widget.
    virtual void render(const Image& target)=0;

// Event
    /// Mouse event type
    enum Event { Press, Release, Motion, Enter, Leave };
    /// Mouse buttons
    enum Button { None, LeftButton, MiddleButton, RightButton, WheelUp, WheelDown };
    /// Override \a mouseEvent to handle or forward user input
    /// \note \a mouseEvent is first called on the root Window#widget
    /// \return Whether the mouse event was accepted
    virtual bool mouseEvent(int2 cursor, int2 size, Event event, Button button) { (void)cursor, (void)size, (void)event, (void)button; return false; }
    virtual bool mouseEvent(const Image& target unused, int2 cursor, int2 size, Event event, Button button) { return mouseEvent(cursor, size, event, button); } // Default implementation doesn't render
    /// Convenience overload for layout implementation
    bool mouseEvent(const Image& target, Rect rect, int2 cursor, Event event, Button button) { return mouseEvent(clip(target,rect),cursor-rect.position(),rect.size(),event,button); }

    /// Override \a keyPress to handle or forward user input
    /// \note \a keyPress is directly called on the current focus
    /// \return Whether the key press was accepted
    virtual bool keyPress(Key key, Modifiers modifiers) { (void)key, (void) modifiers; return false; }
    virtual bool keyRelease(Key key, Modifiers modifiers) { (void)key, (void) modifiers; return false; }
};

// Accessors to the window which received the current event (may only be called from event methods)
/// Reports keyboard events to this widget
void setFocus(Widget* widget);
/// Returns whether this widget has the keyboard focus
bool hasFocus(Widget* widget);
/// Reports further mouse motion events only to this widget (until mouse button is released)
void setDrag(Widget* widget);
/// Returns last text selection (or if clipboard is true, last copy)
String getSelection(bool clipboard=false);
/// Cursor icons
enum class Cursor { Arrow, Horizontal, Vertical, FDiagonal, BDiagonal, Move, Text };
/// Sets cursor to be shown when mouse is in the given rectangle
void setCursor(Rect region, Cursor cursor);

/// Configures global display context to render to an image
// In this module because the definition depends on display and widget
Image renderToImage(Widget& widget, int2 size);
