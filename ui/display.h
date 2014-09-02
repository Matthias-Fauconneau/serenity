#pragma once
/// \file display.h
#include "thread.h"
#include "map.h"
#include "data.h"

inline buffer<byte> pad(array<byte>&& o, uint width=4){ o.grow(align(width,o.size)); return move(o); }

/// Connection to an X display server
struct Display : Socket, Poll {
// Connection
    /// Synchronizes access to connection and event queue
    Lock lock;
    /// Event queue
    array<buffer<byte>> events;
    /// Signals events
    signal<const ref<byte>&> onEvent;
    // Write
     uint16 sequence = 0;

// Server
     /// Base resource id
     uint id;
// Display
    /// Root window
    uint root;
    /// Root visual
    uint visual;
    /// Screen size
    int screenX, screenY;

// Keyboard
    /// Keycode range
    uint minKeyCode=8, maxKeyCode=255;

// Methods
    Display();
// Connection
    // Read
     /// Event handler
     void event() override;
    // Write
     uint16 send(const ref<byte>& request);
     template<Type Request> uint16 send(Request request, const ref<byte>& data=""_) {
         assert_(sizeof(request) + data.size == request.size*4);
         return send(string(raw(request)+pad(array<byte>(data))));
     }

     /// Reads reply checking for errors and queueing events
     buffer<byte> readReply(uint16 sequence);

     template<Type Request, Type T> typename Request::Reply request(Request request, buffer<T>& output, const ref<byte>& data=""_) {
         static_assert(sizeof(typename Request::Reply)==31,"");
         Locker lock(this->lock); // Prevents a concurrent thread from reading the reply and lock event queue
         uint16 sequence = send(request, data);
         buffer<byte> reply = readReply(sequence);
         output = cast<T>(bufferCopy(reply.slice(32/*sizeof(XEvent)*/)));
         return *(typename Request::Reply*)reply.data;
     }

     template<Type Request> typename Request::Reply request(Request request, const ref<byte>& data=""_) {
         buffer<byte> output;
         auto reply = this->request(request, output, data);
         assert_(reply.size == 32/*sizeof(XEvent)*//4 && output.size ==0, reply.size, output.size);
         return reply;
     }

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
     uint Atom(const string& name);
};
