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
