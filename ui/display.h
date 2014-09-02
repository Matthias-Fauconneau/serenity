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
    uint8 minKeyCode=8, maxKeyCode=0xFF;

// Methods
    Display();
// Connection
    // Read
     /// Event handler
     void event() override;
    // Write
     template<Type Request> uint16 send(Request request, const ref<byte>& data=""_) {
         assert_(sizeof(request)%4==0 && sizeof(request) + align(4, data.size) == request.size*4, sizeof(request), data.size, request.size*4);
         write(string(raw(request)+pad(array<byte>(data))));
         sequence++;
         return sequence;
     }

     /// Reads reply checking for errors and queueing events
     buffer<byte> readReply(uint16 sequence, uint elementSize);

     template<Type Request, Type T> typename Request::Reply request(Request request, buffer<T>& output, const ref<byte>& data=""_) {
         static_assert(sizeof(typename Request::Reply)==31,"");
         Locker lock(this->lock); // Prevents a concurrent thread from reading the reply and lock event queue
         uint16 sequence = send(request, data);
         buffer<byte> r = readReply(sequence, sizeof(T));
         auto reply = *(typename Request::Reply*)r.data;
         assert_(r.size == sizeof(typename Request::Reply)+reply.size*sizeof(T), r.size, reply.size);
         output = cast<T>(bufferCopy(r.slice(sizeof(reply),reply.size*sizeof(T))));
         return reply;
     }

     template<Type Request> typename Request::Reply request(Request request, const ref<byte>& data=""_) {
         buffer<byte> output;
         auto reply = this->request(request, output, data);
         assert_(reply.size == 0 && output.size ==0, reply.size, output.size);
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
