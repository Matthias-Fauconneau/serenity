#pragma once
#include "string.h"
#include "gl.h"
#include "process.h"
#include "font.h"

/// Widget is an abstract component to compose user interfaces
struct Widget {
	int2 position, size;

	enum Event { Motion, LeftButton, RightButton, MiddleButton, WheelDown, WheelUp /*TODO: X keys*/ };
	enum State { Released=0, Pressed=1 };
	/// Preferred size (0 means expand)
	virtual int2 sizeHint() { return int2(0,0); }
	/// Render this widget. use \a view to scale from widget coordinates (0-size) to viewport
	virtual void render(vec2 scale, vec2 offset) =0;
	/// Notify objects to process \a position,\a size or derived member changes
	virtual void update() {}
	/// Notify objects to process a new user event
	virtual bool event(int2 /*position*/, int /*event*/, int /*state*/) { return false; }

	//operator Widget() { return *this; } //hack to allow . syntax in Layout<Widget*>
};

/// Layout is a widget presenting other widgets
struct Layout : Widget {
	/// Allow to specialize child widgets storage (\sa WidgetLayout ItemLayout)
	virtual virtual_iterator<Widget> begin() =0;
	virtual virtual_iterator<Widget> end() =0;
	virtual int count() =0;
	virtual Widget& operator[](int) =0;

	bool event(int2 position, int event, int state) {
		for(auto& child : *this) {
			if(position > child.position && position < child.position+child.size) {
				if(child.event(position-child.position,event,state)) return true;
			}
		}
		return false;
	}
	void render(vec2 scale, vec2 offset) {
		for(auto& child : *this) child.render(scale,offset+vec2(child.position)*scale);
	}
};

/// implements Layout storage using array<Widget*> (i.e by reference)
template<class L> struct WidgetLayout : L {
	array<Widget*> widgets;
	virtual_iterator<Widget> begin() { return new dereference_iterator<Widget>(widgets.begin()); }
	virtual_iterator<Widget> end() { return new dereference_iterator<Widget>(widgets.end()); }
	int count() { return widgets.size; }
	WidgetLayout& operator <<(Widget* w) { widgets << w; return *this; }
	Widget& operator[](int i) { return *widgets[i]; }
};

/// implements Layout storage using array<T> (i.e by value)
template<class L, class T> struct ItemLayout : L {
	array<T> items;
	virtual_iterator<Widget> begin() { return new value_iterator<Widget>(items.begin()); }
	virtual_iterator<Widget> end() { return new value_iterator<Widget>(items.end()); }
	int count() { return items.size; }
	ItemLayout& operator <<(T&& t) { items << move(t); return *this; }
	Widget& operator[](int i) { return items[i]; }
};

typedef struct _XDisplay Display;
typedef struct __GLXcontextRec* GLXContext;
typedef unsigned long XWindow;
struct Window : Poll {
	Display* x;
	GLXContext ctx;
	XWindow window;
	signal<uint> keyPress;
	bool visible=true;
	Widget& widget;

	Window(int2 size, Widget& widget);
	~Window();
	pollfd poll() override;
	bool event(pollfd) override;
	uint addHotKey(const string& key);
	void rename(const string& name);
	void resize(int2 size);
	void render();
};

struct Horizontal : Layout {
	int margin = 1;
	int2 sizeHint();
	void update();
};
typedef WidgetLayout<Horizontal> HBox;

struct Vertical : Layout {
	int margin = 1;
	int2 sizeHint();
	void update();
};
typedef WidgetLayout<Vertical> VBox;

struct List : Vertical {
	int index=-1;
	signal<int> currentChanged;

	bool event(int2 position, int event, int state);
	void render(vec2 scale, vec2 offset);
};

template<class T> struct ValueList : ItemLayout<List, T> {
	inline T& current() { return this->items[this->index]; }
};

struct Text : Widget {
	int size;
	Font& font;
	string text;
	struct Blit { vec2 min, max; uint id; };
	array<Blit> blits;
	Text(int size, string&& text);
	int2 sizeHint();
	void render(vec2 scale, vec2 offset);
};

typedef ValueList<Text> TextList;

struct GLTexture;
struct Image;
struct TriggerButton : Widget {
	GLTexture icon;
	int size=32;
	signal<> triggered;
	TriggerButton(const Image& icon);
	int2 sizeHint();
	void render(vec2 scale, vec2 offset);
	bool event(int2, int event, int state);
};

struct ToggleButton : Widget {
	GLTexture enableIcon, disableIcon;
	int size=32;
	bool enabled=false;
	signal<bool> toggled;
	ToggleButton(const Image& enable,const Image& disable);
	int2 sizeHint();
	void render(vec2 scale, vec2 offset);
	bool event(int2, int event, int state);
};

struct Slider : Widget {
	int height = 32;
	int minimum=0,value=-1,maximum=0;
	signal<int> valueChanged;

	int2 sizeHint();
	void render(vec2 scale, vec2 offset);
	bool event(int2 position, int event, int state);
};


#define ICON(name) \
	extern uint8 _binary_## name ##_png_start[]; \
	extern uint8 _binary_## name ##_png_end[]; \
	static Image name ## Icon (_binary_## name ##_png_start,_binary_## name ##_png_end-_binary_## name ##_png_start);
