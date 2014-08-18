/// \file player.cc Music player
#include "thread.h"
#include "file.h"
#include "ffmpeg.h"
#include "resample.h"
#include "audio.h"
#include "interface.h"
#include "layout.h"
#include "window.h"
#include "text.h"
#include "time.h"
#include "image.h"
#include "png.h"
#if REMOVABLES
#include <sys/inotify.h>
#include <sys/mount.h>
#include <sys/stat.h>
#if DBUS
#include "dbus.h"
DBus system (DBus::System);
#endif

/// Watches a folder for new files
struct FileWatcher : File, Poll {
    FileWatcher(string path, function<void(string)> fileCreated, function<void(string)> fileDeleted)
        : File(inotify_init1(IN_CLOEXEC)), Poll(File::fd), watch(check(inotify_add_watch(File::fd, strz(path), IN_CREATE|IN_DELETE))),
          fileCreated(fileCreated), fileDeleted(fileDeleted) {}
    void event() override {
        while(poll()) {
            ::buffer<byte> buffer = readUpTo(2*sizeof(inotify_event)+4); // Maximum size fitting only a single event (FIXME)
            inotify_event e = *(inotify_event*)buffer.data;
            string name = str((const char*)buffer.slice(__builtin_offsetof(inotify_event, name), e.len-1).data);
            if((e.mask&IN_CREATE) && fileCreated) fileCreated(name);
            if((e.mask&IN_DELETE) && fileDeleted) fileDeleted(name);
        }
    }
    const uint watch;
    function<void(string)> fileCreated;
    function<void(string)> fileDeleted;
};
#endif

/// Shuffles an array of elements
generic buffer<T> shuffle(buffer<T>&& a) {
    Random random; // Unseeded (to return the same sequence for a given size)
    for(uint i: range(a.size)) {
        uint j = random%(i+1);
        swap(a[i], a[j]);
    }
    return move(a);
}

/// Music player with a two-column interface (albums/track), gapless playback and persistence of last track+position
struct Player {
// Playback
    AudioControl volume;
    static constexpr uint channels = 2;
    static constexpr uint periodSize = 8192;
    unique<AudioFile> file = 0;
    AudioOutput audio {{this,&Player::read}};
    mref<short2> lastPeriod;
    uint read(const mref<short2>& output) {
        if(audio.rate != file->rate) { // Previous call returned last partial period. Audio output can now be reset.
            audio.stop();
            audio.start(file->rate, periodSize);
            assert_(audio.rate == file->rate);
            return 0;
        }
        uint readSize = 0;
        for(mref<short2> chunk=output;;) {
            if(!file) break;
            assert(readSize<output.size);
            if(audio.rate != file->rate) break; // Returns partial period before closing
            size_t read = file->read(chunk);
            assert(read<=chunk.size, read);
            chunk = chunk.slice(read); readSize += read;
            if(readSize == output.size) { update(file->position/file->rate,file->duration/file->rate); break; } // Complete chunk
            else next(); // End of file
        }
        if(!lastPeriod) for(uint i: range(output.size)) { // Fades in
            float level = exp(12. * ((float) i / output.size - 1) ); // Linear perceived sound level
            output[i][0] *= level;
            output[i][1] *= level;
        }
        lastPeriod = output;
        assert_(readSize == output.size);
        return readSize;
    }

// Interface
    ICON(random) ICON(random2) ToggleButton randomButton {randomIcon(), random2Icon()};
    ICON(next) TriggerButton nextButton{nextIcon()};
    ICON(play) ICON(pause) ToggleButton playButton{playIcon(), pauseIcon()};
    ICON(eject) TriggerButton ejectButton{ejectIcon(), true};
    Text elapsed {"00:00"_};
    Slider slider;
    Text remaining {"00:00"_};
    HBox status {{&elapsed, &slider, &remaining}};
    HBox toolbar {{&randomButton, &playButton, &nextButton, &ejectButton, &status}};
    /*Scroll<*/ List<Text> /*>*/ albums; //FIXME: Scroll
    /*Scroll<*/ List<Text> /*>*/ titles; //FIXME: Scroll
    HBox main {{ &albums, &titles }};
    VBox layout {{ &toolbar, &main }};
    Window window {&layout, -int2(600,1024), "Player"_, pauseIcon()};

// Content
    String device; // Device underlying folder
    Folder folder;
    array<String> folders;
    array<String> files;
    array<String> randomSequence;
#if REMOVABLES
    FileWatcher fileWatcher {"/dev"_,{this, &Player::deviceCreated}, {this,&Player::deviceDeleted}};
#endif

    Player() {
        window.background = White;
        albums.always=titles.always=true;
        elapsed.minSize.x=remaining.minSize.x=64;

        albums.expanding=true; titles.expanding=true; titles.main=Linear::Center;
        window.actions[Escape] = []{ exit(); };
        window.actions[Space] = {this, &Player::togglePlay};
#if ARM
        window.actions[Extra] = {this, &Player::togglePlay};
        window.longActions[Extra]= {this, &Player::next};
        window.actions[Power] = {&window, &Window::toggleDisplay};
        window.longActions[Power] = [this](){ window.setDisplay(false); execute("/sbin/poweroff"_); };
#else
        window.globalAction(Play) = {this, &Player::togglePlay};
        //window.globalAction(F8) = {this, &Player::togglePlay}; // Chromebook Mute
        //window.globalAction(F9) = [this]{  volume=volume-1; }; // Chromebook Decrease volume (handled by kmix)
        //window.globalAction(F10) = [this]{ volume=volume+1; }; // Chromebook Increase volume (handled by kmix)
        window.actions.insert(RightArrow, {this, &Player::next});
#endif
        randomButton.toggled = {this, &Player::setRandom};
        playButton.toggled = {this, &Player::setPlaying};
        nextButton.triggered = {this, &Player::next};
#if REMOVABLES
        ejectButton.triggered = {this, &Player::eject};
#endif
        slider.valueChanged = {this, &Player::seek};
        albums.activeChanged = {this, &Player::playAlbum};
        titles.activeChanged = {this, &Player::playTitle};

        if(arguments()) setFolder(arguments().first());
        else {
#if REMOVABLES
            for(string device: Folder("/dev"_).list(Drives)) if(mount(device)) break;
#endif
            if(!folder) setFolder("/Music"_);
        }
        window.show();
        mainThread.setPriority(-20);
    }
    ~Player() { recordPosition(); /*Records current position*/ }
    void recordPosition() {
        assert_(titles.index<files.size && file);
        if(/*writableFile(".last"_, folder) &&*/ titles.index<files.size && file)
            writeFile(".last"_,files[titles.index]+"\0"_+dec(file->position/file->rate)+(randomSequence?"\0random"_:""_), folder);
    }
    void setFolder(string path) {
        assert(folder.name() != path);
        if(folder.name() == path) return;
        folders.clear(); albums.clear(); files.clear(); titles.clear(); randomSequence.clear();
        folder = path;
        folders = folder.list(Folders|Sorted);
        for(string folder: folders) albums.append( section(folder,'/',-2,-1) );
        if(existsFile(".last"_, folder)) {
            String mark = readFile(".last"_, folder);
            string last = section(mark, '\0');
            string album = section(last, '/', 0, 1);
            string file = section(last, '/', 1, -1);
            if(existsFolder(album, folder)) {
                if(section(mark,'\0',2,3)) {
                    queueFile(album, file, true);
                    if(files) playTitle(0);
                    setRandom(true);
                } else {
                    albums.index = folders.indexOf(album);
                    array<String> files = Folder(album, folder).list(Recursive|Files|Sorted);
                    uint i=0; for(;i<files.size;i++) if(files[i]==file) break;
                    for(;i<files.size;i++) queueFile(album, files[i], false);
                    if(this->files) playTitle(0);
                }
                seek(fromInteger(section(mark,'\0',1,2)));
            }
            return;
        }
        if(randomButton.enabled) setRandom(true); // Regenerates random sequence for folder
        updatePlaylist();
        if(files) playTitle(0);
    }
    void queueFile(const string& folder, const string& file, bool withAlbumName) {
        String title = String(section(section(file,'/',-2,-1),'.',0,-2));
        uint i=title.indexOf('-'); i++; //skip album name
        while(i<title.size && title[i]>='0'&&title[i]<='9') i++; //skip track number
        while(i<title.size && (title[i]==' '||title[i]=='.'||title[i]=='-'||title[i]=='_')) i++; //skip whitespace
        title = replace(title.slice(i),"_"_," "_);
        if(withAlbumName) title = folder + " - "_ + title;
        titles.append(title, 16);
        files.append( folder+"/"_+file );
    }
    void playAlbum(const string& album) {
        assert(existsFolder(album,folder),album);
        array<String> files = Folder(album,folder).list(Recursive|Files|Sorted);
        for(const String& file: files) queueFile(album, file, false);
        titles.index=-1; next();
    }
    void playAlbum(uint index) {
        files.clear(); titles.clear();
        window.setTitle(toUTF8(albums[index].text));
        playAlbum(folders[index]);
    }
    void playTitle(uint index) {
        titles.index = index;
        window.setTitle(toUTF8(titles[index].text));
        file = unique<AudioFile>(folder.name()+"/"_+files[index]);
        if(!file->file) { file=0; log("Error reading", folder.name()+"/"_+files[index]); return; }
        assert(file->channels==AudioOutput::channels);
        setPlaying(true);
    }
    void next() {
        if(titles.index+1<titles.count()) playTitle(titles.index+1);
        else if(albums.index+1<albums.count()) playAlbum(++albums.index);
        else if(albums.count()) playAlbum(albums.index=0);
        else { setPlaying(false); if(file) file->close(); return; }
        updatePlaylist();
    }
    void setRandom(bool random) {
        main.clear();
        randomSequence.clear();
        randomButton.enabled = random;
        if(random) {
            main << &titles; // Hide albums
            // Explicits random sequence to: resume the sequence from the last played file, ensure files are played once in the sequence.
            randomSequence = shuffle(folder.list(Recursive|Files|Sorted));
            titles.shrink(titles.index+1); this->files.shrink(titles.index+1); // Replaces all queued titles with the next tracks drawn from the random sequence
            updatePlaylist();
        } else main<<&albums<<&titles; // Show albums
    }
    void updatePlaylist() {
        if(!randomSequence) return;
        uint randomIndex = 0, listIndex = 0;
        if(titles.index < files.size) {
            listIndex=titles.index;
            for(uint i: range(randomSequence.size)) if(randomSequence[i]==files[titles.index]) { randomIndex=i; break; }
        }
        randomIndex += files.size-listIndex; // Assumes already queued tracks are from randomSequence
        while(titles.count() < listIndex + 16) { // Schedules at least 16 tracks drawing from random sequence as needed
            string path = randomSequence[randomIndex%randomSequence.size];
            string folder = section(path,'/',0,1), file = section(path,'/',1,-1);
            queueFile(folder, file, true);
            randomIndex++;
        }
        while(titles.count() > 64 && titles.index > 0) { titles.removeAt(0); files.removeAt(0); titles.index--; } // Limits total size
    }
    void togglePlay() { setPlaying(!playButton.enabled); }
    void setPlaying(bool play) {
        if(play) {
            assert_(file);
            audio.start(file->rate, periodSize);
            window.setIcon(playIcon());
        } else {
            // Fades out the last period (assuming the hardware is not playing it (false if swap occurs right after pause))
            for(uint i: range(lastPeriod.size)) {
                float level = exp2(-12. * i / lastPeriod.size); // Linear perceived sound level
                lastPeriod[i] *= level;
            }
            lastPeriod=mref<short2>();
            audio.stop();
            window.setIcon(pauseIcon());
            file->seek(max(0, int(file->position-lastPeriod.size)));
        }
        playButton.enabled=play;
        window.render();
        recordPosition();
    }
    void seek(int position) {
        if(file) { file->seek(position*file->rate); update(file->position/file->rate,file->duration/file->rate); /*resampler.clear();*/ /*audio->cancel();*/ }
    }
    void update(uint position, uint duration) {
        if(slider.value == (int)position) return;
        slider.value = position; slider.maximum=duration;
        elapsed.setText(String(dec(position/60,2,'0')+":"_+dec(position%60,2,'0')));
        if(position<duration) remaining.setText(String(dec((duration-position)/60,2,'0')+":"_+dec((duration-position)%60,2,'0')));
        if(window.pixmap) {
            renderBackground(status.target, window.background); //FIXME: with Oxygen
            status.render();
            window.putImage(Rect(window.size)/*status.target*/); window.present();
        }
    }
#if REMOVABLES
    String cmdline = File("/proc/cmdline"_).readUpTo(256);
    void deviceCreated(string name) { mount(name); }
    bool mount(string name) {
        TextData s (name);
        if(!s.match("sd"_)) return false; // Only acts on new SATA/USB drives
        if(!s.word()) return false;
        if(!s.whileInteger()) return false; // Only acts on partitions
        String device = "/dev/"_+name;
        //if(File(device,root(), Descriptor).type() != FileType::Drive) return false; // Only acts on drives
        struct stat stat; ::stat(strz(device),&stat); if((stat.st_mode&__S_IFMT) !=__S_IFBLK) return false; // Only acts on drives
        if(find(cmdline, device)) return false; // Do not act on root drive
#if DBUS
        DBus::Object uDevices{&system, String("org.freedesktop.UDisks2"_),String("/org/freedesktop/UDisks2/block_devices"_)};
        while(!uDevices.children().contains(name)) sched_yield(); // FIXME: connect to InterfaceAdded instead of busy looping
        DBus::Object uDevice{&system, String("org.freedesktop.UDisks2"_),"/org/freedesktop/UDisks2/block_devices/"_+name};
        DBus::Object uDrive{&system, String("org.freedesktop.UDisks2"_),uDevice.get<String>("org.freedesktop.UDisks2.Block.Drive"_)};
        if(!uDrive.get<uint>("org.freedesktop.UDisks2.Drive.Removable"_)) return false; // Only acts on removable drives
#endif
        String target;
        String mounts = File("/proc/self/mounts"_).readUpTo(2048);
        for(TextData s(mounts); s; s.line()) if(s.match(device)) { s.skip(" "_); target = String(s.until(' ')); break; }
        bool wasPlaying = playButton.enabled;
        if(wasPlaying) setPlaying(false);
        if(!target) { // Mounts drive
#if MOUNT
            log_(str("Mounting ",device));
#if DBUS
            target = uDevice("org.freedesktop.UDisks2.Filesystem.Mount"_, Dict());
            if(!target) { log(" failed"); return false; }
            log_(str(" on", target));
#else
            target = "/media/"_+name;
            log_(str(" on",target));
            Folder(target, root(), true);
            auto mount = [](string device, string target) {
                for(string fs: {"vfat"_,"ext4"_}) {
                    if( ::mount(strz(device),strz(target),strz(fs),MS_NOATIME|MS_NODEV|MS_NOEXEC,0) == OK ) // not MS_RDONLY to allow writing .last mark
                        return true;
                }
                return false;
            };
            if(!mount(device, target)) { log(" failed"); return false; }
#endif
            log(" succeeded");
#else
            return false;
#endif
        }
        this->device = String(name);
        ejectButton.hidden = false;
        if(existsFolder("Music"_,target)) setFolder(target+"/Music"_);
        else setFolder(target);
        if(wasPlaying) setPlaying(true);
        return true;
    }
    void deviceDeleted(string name) { if(name == device) eject(); }
    void eject() {
        ejectButton.hidden = true;
        bool wasPlaying = playButton.enabled;
        if(wasPlaying) setPlaying(false);
        setFolder(arguments() ? arguments().first() : "/Music"_);
#if !DBUS
        String target = "/media/"_+device;
        umount2(strz(target), 0);
        removeFolder(target);
#endif
        device.clear();
        if(wasPlaying) setPlaying(true);
    }
#endif // REMOVABLES
} application;
