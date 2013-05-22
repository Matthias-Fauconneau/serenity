/// \file player.cc Music player
#include "thread.h"
#include "file.h"
#include "asound.h"
#include "interface.h"
#include "layout.h"
#include "window.h"
#include "text.h"
#include "ffmpeg.h"

/// Music player with a two-column interface (albums/track), gapless playback and persistence of last track+position
struct Player {
// Gapless playback
    static constexpr uint channels = 2;
    AudioFile file;
    AudioOutput output{{this,&Player::read}, 44100, 8192};
    uint read(int16* output, uint outputSize) {
        uint readSize = 0;
        for(;;) {
            if(!file) return readSize;
            uint need = outputSize-readSize;
            uint read = file.read(output, need);
            assert(read<=need);
            output += read*channels; readSize += read;
            if(readSize != outputSize) next();
            else {
                update(file.position/file.rate,file.duration/file.rate);
                return readSize;
            }
        }
    }

// Interface
    ICON(play) ICON(pause) ToggleButton playButton{playIcon(), pauseIcon()};
    ICON(next) TriggerButton nextButton{nextIcon()};
    Text elapsed = "00:00"_;
    Slider slider;
    Text remaining = "00:00"_;
    HBox toolbar;//{&playButton, &nextButton, &elapsed, &slider, &remaining};
    Scroll< List<Text>> albums;
    Scroll< List<Text>> titles;
    HBox main;//{ &albums.area(), &titles.area() };
    VBox layout;//{ &toolbar, &main };
    Window window {&layout, int2(-1050/2,-1680/2), "Player"_, pauseIcon()};

// Content
    array<string> folders;
    array<string> files;

    Player() {
        albums.always=titles.always=true;
        elapsed.minSize.x=remaining.minSize.x=64;
        toolbar<<&playButton<<&nextButton<<&elapsed<<&slider<<&remaining;
        main<<&albums.area()<<&titles.area();
        layout<<&toolbar<<&main;

        albums.expanding=true; titles.main=Linear::Center;
        window.localShortcut(Escape).connect(&exit);
        window.localShortcut(Key(' ')).connect(this, &Player::togglePlay);
        window.globalShortcut(Play).connect(this, &Player::togglePlay);
        playButton.toggled.connect(this, &Player::setPlaying);
        nextButton.triggered.connect(this, &Player::next);
        slider.valueChanged.connect(this, &Player::seek);
        albums.activeChanged.connect(this, &Player::playAlbum);
        titles.activeChanged.connect(this, &Player::playTitle);

        folders = Folder("Music"_).list(Folders);
        assert(folders);
        for(string& folder : folders) albums << string(section(folder,'/',-2,-1));

        if(existsFile("Music/.last"_)) {
            string mark = readFile("Music/.last"_);
            ref<byte> last = section(mark,0);
            ref<byte> album = section(last,'/',0,1);
            ref<byte> title = section(last,'/',1,-1);
            if(existsFolder(album,"Music"_)) {
                albums.index = folders.indexOf(album);
                array<string> files = Folder(album,"Music"_).list(Recursive|Files);
                uint i=0; for(;i<files.size;i++) if(files[i]==title) break;
                for(;i<files.size;i++) queueFile(files[i], album);
                if(files) {
                    next();
                    seek(toInteger(section(mark,0,1,2)));
                }
            }
        }
        window.render();
    }
    void queueFile(const ref<byte>& file, const ref<byte>& folder) {
        string title = string(section(section(file,'/',-2,-1),'.',0,-2));
        uint i=title.indexOf('-'); i++; //skip album name
        while(i<title.size && title[i]>='0'&&title[i]<='9') i++; //skip track number
        while(i<title.size && (title[i]==' '||title[i]=='.'||title[i]=='-'||title[i]=='_')) i++; //skip whitespace
        titles << Text(replace(title.slice(i),"_"_," "_), 16);
        files <<  folder+"/"_+file;
    }
    void playAlbum(const ref<byte>& album) {
        assert(existsFolder(album,"Music"_),album);
        array<string> files = Folder(album,"Music"_).list(Recursive|Files);
        for(const string& file: files) queueFile(file, album);
        window.setSize(int2(-512,-512));
        titles.index=-1; next();
    }
    void playAlbum(uint index) {
        files.clear(); titles.clear();
        window.setTitle(toUTF8(albums[index].text));
        playAlbum(folders[index]);
    }
    void playTitle(uint index) {
        window.setTitle(toUTF8(titles[index].text));
        if(!file.openPath(string("/Music/"_+files[index]))) return;
        assert(output.channels==file.channels);
        if(output.rate!=file.rate) {
            assert(file.rate, files[index]);
            output.~AudioOutput();
            new (&output) AudioOutput({this,&Player::read}, file.rate, 8192);
            output.start();
        }
        setPlaying(true);
    }
    void next() {
        if(titles.index+1<titles.count()) playTitle(++titles.index);
        else if(albums.index+1<albums.count()) playAlbum(++albums.index);
        else if(albums.count()) playAlbum(albums.index=0);
        else { window.setTitle("Player"_); stop(); return; }
    }
    void togglePlay() { setPlaying(!playButton.enabled); }
    void setPlaying(bool play) {
        if(play == playButton.enabled) return;
        if(play) { output.start(); window.setIcon(playIcon()); }
        else { output.stop(); window.setIcon(pauseIcon()); }
        playButton.enabled=play; window.render();
    }
    void stop() {
        setPlaying(false);
        file.close();
        elapsed.setText(string("00:00"_));
        slider.value = -1;
        remaining.setText(string("00:00"_));
        titles.index=-1;
    }
    void seek(int position) {
        if(file) { file.seek(position); update(file.position/file.rate,file.duration/file.rate); }
    }
    void update(uint position, uint duration) {
        if(slider.value == (int)position) return;
        writeFile("/Music/.last"_,string(files[titles.index]+"\0"_+dec(position)));
        if(!window.mapped) return;
        slider.value = position; slider.maximum=duration;
        elapsed.setText(string(dec(position/60,2)+":"_+dec(position%60,2)));
        remaining.setText(string(dec((duration-position)/60,2)+":"_+dec((duration-position)%60,2)));
        window.render();
    }
} application;
