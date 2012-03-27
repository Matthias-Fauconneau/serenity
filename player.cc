#include "process.h"
#include "file.h"
#include "ffmpeg.h"
#include "alsa.h"
#include "interface.h"
#include "window.h"
ICON(play);
ICON(pause);
ICON(next);

struct Player : Application {
    array<string> folders;
    array<string> files;

    AudioFile media;
    AudioOutput audio;

       ToggleButton playButton = ToggleButton(move(playIcon),move(pauseIcon));
       TriggerButton nextButton = TriggerButton(move(nextIcon));
       Text elapsed {"00:00"_};
       Slider slider;
       Text remaining {"00:00"_};
      HBox toolbar {&playButton, &nextButton, &elapsed, &slider, &remaining};
       Scroll<TextList> albums;
       Scroll<TextList> titles;
      HBox main { &albums.parent(), &titles.parent() };
     VBox layout { &toolbar, &main };
     Window window{&layout,"Player"_,copy(pauseIcon),int2(640,-16)};

    Player(array<string>&& arguments) {
        window.globalShortcut("XF86AudioPlay"_).connect(this, &Player::togglePlay);
        window.localShortcut("Escape"_).connect(this, &Player::quit);
        playButton.toggled.connect(this, &Player::setPlaying);
        nextButton.triggered.connect(this, &Player::next);
        slider.valueChanged.connect(this, &Player::seek);
        media.timeChanged.connect(this, &Player::update);
        albums.activeChanged.connect(this, &Player::playAlbum);
        titles.activeChanged.connect(this, &Player::play);
        media.audioOutput={audio.frequency,audio.channels};
        audio.read = {&media,&AudioFile::read};

        folders = listFiles("/Music"_,Sort|Folders);
        for(auto& folder : folders) albums << Text(section(folder,'/',-2,-1), 10);

        for(auto&& path: arguments) {
            assert(exists(path),path);
            if(isFolder(path)) playAlbum(path); else appendFile(move(path));
        }
        if(!files && exists("/Music/.last"_)) {
            string last = readFile("/Music/.last"_);
            string folder = section(last,'/',0,3);
            albums.index = indexOf(folders, folder);
            array<string> files = listFiles(folder,Recursive|Sort|Files);
            uint i=0; for(;i<files.size();i++) if(files[i]==last) break;
            for(;i<files.size();i++) appendFile(move(files[i]));
        }
        if(files) next();
        window.show();
        Window::sync();
        setPriority(-20);
    }
    ~Player() { if(titles.index<0) return; string& file = files[titles.index]; writeFile("/Music/.last"_,file,CWD,true); }
    void appendFile(string&& path) {
        string title = section(section(path,'/',-2,-1),'.',0,-2);
        uint i=indexOf(title, '-'); i++; //skip album name
        while(i<title.size() && title[i]>='0'&&title[i]<='9') i++; //skip track number
        while(i<title.size() && (title[i]==' '||title[i]=='.'||title[i]=='-'||title[i]=='_')) i++; //skip whitespace
        titles << Text(replace(slice(title,i),"_"_," "_), 16);
        files << move(path);
    }
    void playAlbum(const string& path) {
        assert(isFolder(path));
        array<string> files = listFiles(path,Recursive|Sort|Files);
        for(auto&& file: files) appendFile(move(file));
        layout.update(); window.render();
    }
    void playAlbum(int index) {
        stop(); files.clear(); titles.clear();
        window.setTitle(albums.active().text);
        playAlbum(folders[index]);
    }
    void play(int index) {
        window.setTitle(titles.active().text);
        media.open(files[index]);
        audio.start();
        setPlaying(true);
    }
    void next() {
        if(!playButton.enabled) setPlaying(true);
        if(titles.index+1<titles.count()) play(++titles.index);
        else window.setTitle(albums.active().text);
        titles.ensureVisible(titles.active());
    }
    void togglePlay() { setPlaying(!playButton.enabled); }
    void setPlaying(bool play) {
        playButton.enabled=play;
        if(play) { audio.start(); window.setIcon(playIcon); }
        else { audio.stop(); window.setIcon(pauseIcon); }
    }
    void stop() {
        setPlaying(false);
        media.close();
        elapsed.text="00:00"_;
        slider.value = -1;
        remaining.text="00:00"_;
        titles.index=-1;
    }
    void seek(int position) { media.seek(position); }
    void update(int position, int duration) {
        if(position == duration) next();
        if(!window.visible || slider.value == position) return;
        slider.value = position; slider.maximum=duration;
        elapsed.text=dec(uint64(position/60),2)+":"_+dec(uint64(position%60),2);
        remaining.text=dec(uint64((duration-position)/60),2)+":"_+dec(uint64((duration-position)%60),2);
        toolbar.update(); window.render();
    }
};
Application(Player)
