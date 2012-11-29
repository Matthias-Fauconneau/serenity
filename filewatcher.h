#include "process.h"
#include "function.h"
#include "linux.h"
#include <sys/inotify.h>
struct FileWatcher : Poll {
    File inotify = inotify_init();
    int watch=0;
    FileWatcher(){fd=inotify.fd; events=POLLIN; registerPoll();}
    void setPath(ref<byte> path) { watch = check(inotify_add_watch(inotify.fd, strz(path), IN_MODIFY)); }
    signal<> fileModified;
    void event() override { inotify.readUpTo(64); fileModified(); }
};

