#pragma once
/// \file widget.h Widget interface to compose user interfaces
#include "graphics.h"

/// Key symbols
enum Key {
    Space=' ',
    Escape=0xff1b, Backspace=0xff08, Tab, Return=0xff0d,
    Home=0xff50, LeftArrow, UpArrow, RightArrow, DownArrow, PageUp, PageDown, End, PrintScreen=0xff61,
    Execute, Insert,
    KP_Enter=0xff8d, KP_Asterisk=0xffaa, KP_Plus, KP_Separator, KP_Minus, KP_Decimal, KP_Slash, KP_0,KP_1,KP_2,KP_3,KP_4,KP_5,KP_6,KP_7,KP_8,KP_9,
    F1=0xffbe,F2,F3,F4,F5,F6,F7,F8,F9,F10,F11,
    ShiftKey=0xffe1, ControlKey=0xffe3,
    Delete=0xffff,
    Play=0x1008ff14, Media=0x1008ff32
};
inline String str(Key key) { return str(uint(key)); }
enum Modifiers { NoModifiers=0, Shift=1<<0, Control=1<<2, Alt=1<<3, NumLock=1<<4/*, Meta=1<<6*/ };

/// Abstract component to compose user interfaces
struct Widget {
    Widget(){}
    default_move(Widget);
    virtual ~Widget() {}
// Layout
    /// Preferred size (positive means preferred, negative means expanding (i.e benefit from extra space))
    /// \note space is first allocated to preferred widgets, then to expanding widgets.
    virtual int2 sizeHint(int2) { return sizeHint(); }
    /// Renders this widget.
    Image target;
    void render(const Image& target) { this->target=share(target); render(); }
    virtual void render()=0;
    /// Renders a partial view of this widget with an offset (Default implementation).
    virtual void render(const Image& target, int2 offset, int2 size) {
        Image buffer (size.x, size.y);
        buffer.buffer.clear(0);
        render(buffer);
        blit(target, offset, buffer);
    }
// Event
    /// Mouse event type
    enum Event { Press, Release, Motion, Enter, Leave };
    /// Mouse buttons
    enum Button { NoButton, LeftButton, MiddleButton, RightButton, WheelUp, WheelDown };
    /// Override \a mouseEvent to handle or forward user input
    /// \note \a mouseEvent is first called on the root Window#widget
    /// \return Whether the mouse event was accepted
    virtual bool mouseEvent(int2 cursor, int2 size, Event event, Button button) { (void)cursor, (void)size, (void)event, (void)button; return false; }
    /// Convenience overload for layout implementation
    bool mouseEvent(Rect rect, int2 cursor, Event event, Button button) { return mouseEvent(cursor-rect.position(),rect.size(),event,button); }
    /// Override \a keyPress to handle or forward user input
    /// \note \a keyPress is directly called on the current focus
    /// \return Whether the key press was accepted
    virtual bool keyPress(Key key, Modifiers modifiers) { (void)key, (void) modifiers; return false; }
    virtual bool keyRelease(Key key, Modifiers modifiers) { (void)key, (void) modifiers; return false; }
private:
    virtual int2 sizeHint() { return 0; }
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
///
void putImage(const Image& target);

/// Configures global display context to render to an image
// In this module because the definition depends on display and widget
Image renderToImage(Widget& widget, int2 size);
