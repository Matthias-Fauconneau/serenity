#include "input.h"
#include "poll.h"
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

Input::Input(const char* path) {
    fd = open(path, O_RDONLY|O_NONBLOCK);
    assert(fd>0);
    registerPoll({fd, POLLIN, 0});
}

Input::~Input() { close(fd); }

void Input::event(pollfd) {
    input_event e;
    while(read(fd, &e, sizeof(e)) > 0) {
        if(e.type == EV_KEY) { //button
            signal<>* shortcut = keyPress.find(e.code);
            log(e.code,shortcut?"!":"");
            if(shortcut) shortcut->emit();
        }
    }
}
