#pragma once
/// \file display.h
#include "thread.h"
#include "function.h" // onEvent
#include "map.h" // actions
#include "vector.h" // int2

static inline string padding(size_t size, uint width=4){
 return "\0\0\0\0"_.slice(0, align(width, size)-size);
}
static inline String pad(string t, uint width=4) {
 return t+padding(t.size, width);
}

/// Connection to an X display server
struct XDisplay : Socket, Poll {
 static bool hasServer();
 // Connection
 /// Synchronizes access to connection and event queue
 Lock lock;
 /// Event queue
 array<array<byte>> events;
 /// Signals events
 signal<const ref<byte>> onEvent;
 // Write
 uint16 sequence = 0;

 // Server
 /// Base resource id
 uint id = 0;
 // Display
 /// Root window
 uint root = 0;
 /// Root visual
 uint visual = 0;
 /// Screen size
 int2 size = 0;

 // Keyboard
 /// Keycode range
 uint8 minKeyCode=8, maxKeyCode=0xFF;

 // Methods
 XDisplay(Thread& thread=mainThread);
 // Connection
 // Read
 /// Event handler
 void event() override;
 /// Processes global events and dispatches signal
 void event(const ref<byte>);
 // Write
 uint16 send(ref<byte> data, int fd=-1);
 template<Type Request> uint16 send(Request request, const ref<byte> data, int fd=-1) {
  assert_(sizeof(request)%4==0 && sizeof(request) + align(4, data.size) == request.size*4, sizeof(request), data.size, request.size*4);
  return send(ref<byte>(data?raw(request)+pad(data):raw(request)), fd);
 }
 template<Type Request> uint16 send(Request request, int fd=-1) {
  return send(request, ref<byte>(), fd);
 }

 /// Reads reply checking for errors and queueing events
 array<byte> readReply(uint16 sequence, uint elementSize, buffer<int>& fds);

 template<Type Request, Type T> Type Request::Reply request(Request request, buffer<T>& output, buffer<int>& fds, const ref<byte> data={}, int fd=-1) {
  static_assert(sizeof(Type Request::Reply)==31,"");
  Locker lock(this->lock); // Prevents a concurrent thread from reading the reply and lock event queue
  uint16 sequence = send(request, data, fd);
  array<byte> replyData = readReply(sequence, sizeof(T), fds);
  Type Request::Reply reply = *(Type Request::Reply*)replyData.data;
  assert_(replyData.size == sizeof(Type Request::Reply)+reply.size*sizeof(T), replyData.size, sizeof(Type Request::Reply)+reply.size*sizeof(T), sizeof(T));
  output = copyRef(cast<T>(replyData.slice(sizeof(reply), reply.size*sizeof(T))));
  return reply;
 }

 template<Type Request, Type T> Type Request::Reply request(Request request, buffer<T>& output, const ref<byte> data={}, int fd=-1) {
  buffer<int> fds;
  Type Request::Reply reply = this->request(request, output, fds, data, fd);
  assert_(/*reply.fdCount==0 &&*/ fds.size == 0);
  return reply;
 }

 template<Type Request> Type Request::Reply requestFD(Request request, buffer<int>& fds, const ref<byte> data={}) {
  buffer<byte> output;
  Type Request::Reply reply = this->request(request, output, fds, data);
  assert_(reply.size == 0 && output.size ==0, reply.size, output.size);
  return reply;
 }

 template<Type Request> Type Request::Reply request(Request request, const ref<byte> data={}, int fd=-1) {
  buffer<byte> output;
  Type Request::Reply reply = this->request(request, output, data, fd);
  assert_(reply.size == 0 && output.size ==0, reply.size, output.size);
  return reply;
 }

 void waitEvent(uint8 type);

 // Keyboard
 /// Returns KeySym for key \a code and modifier \a state
 uint keySym(uint8 code, uint8 state);
 /// Returns KeyCode for \a sym
 uint8 keyCode(uint sym);

 /// Actions triggered when a key is pressed
 map<uint, function<void()>> actions;
 /// Registers global action on \a key
 function<void()>& globalAction(uint key);

 // Window
 /// Returns Atom for \a name
 uint Atom(const string name);
 /// Returns property \a name on \a window
 generic buffer<T> getProperty(uint window, string name, size_t length=2+128*128);
};

#include "image.h"
#include "thread.h"
#include "input.h"
struct _drmModeModeInfo;
struct _drmModeCrtc;

struct Keyboard : Device, Poll {
 function<void(Key)> keyPress;

 Keyboard(Thread& thread);
 void event() override;
};

struct Mouse : Device, Poll {
 int2 max;
 int2 cursor;
 Button button;
 function<void(int2, Event, Button)> mouseEvent;

 Mouse(Thread& thread);
 void event() override;
};

struct DRMWindow;
struct Display : Device, Poll {
 Keyboard keyboard;
 Thread mouseThread {-20};
 Mouse mouse;

 uint connector, crtc;
 unique<_drmModeModeInfo> mode;
 _drmModeCrtc* previousMode;
 struct {
  Image target;
  uint32 handle;
  uint32 fb;
  Map map;
 } buffers[2];

 array<DRMWindow*> windows;

 struct MouseEvent { int2 cursor; Event event; Button button; };
 Lock lock;
 array<MouseEvent> eventQueue;

 Display(Thread& thread=mainThread);
 virtual ~Display();

 void mouseEvent(int2, Event, Button);
 void event() override;
 void keyPress(Key);

 void swapBuffers();
};
