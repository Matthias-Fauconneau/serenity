#pragma once
#include "string.h"
#include <poll.h>

/// Application should be inherited to compile an application into an executable
struct Application {
	Application();
	/// Initialize using the given command line \a args
	virtual void start(array<string>&& args) =0;
};

/// Poll is an interface for objects needing to participate in event handling
struct Poll {
	/// Add this to the process-wide event loop
	void registerPoll();
	/// Remove this from the process-wide event loop
	void unregisterPoll();
	/// Linux poll file descriptor
	virtual pollfd poll() =0;
	/// Callback on new events
	virtual bool event(pollfd p) =0;
};

/// signals and slots

template<typename... Args> struct signal : array< delegate<Args...> > {
	void emit(Args... args) { for(auto slot: *this) slot.method(slot._this, args...);  }
	template <class C> void connect(C* _this, void (C::*method)(Args...)) {
		*this << delegate<Args...>(_this, method);
	}
};
#define connect(signal, slot) signal.connect(this, &std::remove_reference<decltype(*this)>::type::slot);
//#define emit(signal, args...) signal.emit(args);
