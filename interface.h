#include "common.h"
#include <poll.h>

struct Poll {
	Poll(bool autoRegister=true) { if(autoRegister) registerPoll(); }
	void registerPoll();
	void unregisterPoll();
	virtual pollfd poll() =0;
	virtual bool event(pollfd p) =0;
};

struct Application {
	Application();
	virtual void start(array<string>&& args) =0;
};

struct Widget : array<Widget*> {
	int count() { return array::size; }

	enum Event { Motion, LeftButton, RightButton, MiddleButton, WheelDown, WheelUp /*XK_ ...*/ };
	enum State { Released=0, Pressed=1 };
	virtual bool event(int2 position, int event, int state) {
		for(Widget* child : *this) {
			if(position>child->position && position<child->position+child->size) {
				if(child->event(position-child->position,event,state)) return true;
			}
		}
		return false;
	}

	int2 position, size;
	virtual void render(vec2 scale, vec2 offset) { for(Widget* child : *this) child->render(scale,offset+vec2(child->position)*scale); }
	virtual int2 sizeHint() =0;
	virtual void update() {
		for(Widget* child : *this) { child->position=position; child->size=size; child->update(); } //default stack layout
	};
	virtual void debug() {
		log_(position.x);log_(",");log_(position.y);log_(" ");log_(size.x);log_("x");log_(size.y);
		if(array::size) { log_(" {"); for(Widget* child : *this) { child->debug();log_(" ");} log_("}"); }
	}
};

struct Window : Widget {
	int2 sizeHint() { return int2(0,0); }
	virtual void rename(const string& name) =0;
	virtual void resize(int2 size) =0;
	virtual void render() =0;
	static Window* instance();
};

struct Horizontal : Widget {
	int margin = 4;
	int2 sizeHint();
	void update();
};

struct Vertical : Widget {
	int margin = 4;
	int2 sizeHint();
	void update();
};

struct List : Vertical {
	int current=-1;
	signal(int) currentChanged;
	inline Widget* currentItem() { return at(current); }

	bool event(int2 position, int event, int state);
	void render(vec2 scale, vec2 offset);
};

struct Font;
struct Text : Widget {
	Font* font;
	string text;
	int fontSize = 16;
	Text(string&& text, int fontSize);
	int2 sizeHint();
	void render(vec2 scale, vec2 offset);
};

struct GLTexture;
struct Image;
struct Button : Widget {
	GLTexture* enable=0;
	GLTexture* disable=0;
	int size=32;
	bool toggle=false;
	bool enabled=false;
	signal(bool) triggered;

	Button(const Image& icon,bool toggle=false);
	Button(const Image& enable,const Image& disable);
	int2 sizeHint();
	void render(vec2 scale, vec2 offset);
	bool event(int2, int event, int state);
};

struct Slider : Widget {
	int height = 32;
	int minimum=0,value=-1,maximum=0;
	signal(int) valueChanged;

	int2 sizeHint();
	void render(vec2 scale, vec2 offset);
	bool event(int2 position, int event, int state);
};


#define ICON(name) \
	extern uint8 _binary_## name ##_png_start[]; \
	extern uint8 _binary_## name ##_png_end[]; \
	static Image name ## Icon (_binary_## name ##_png_start,_binary_## name ##_png_end-_binary_## name ##_png_start);
