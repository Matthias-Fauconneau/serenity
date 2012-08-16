#include "process.h"
#include "file.h"
#include "mpg123.h"
#include "asound.h"
#include "interface.h"
#include "layout.h"
#include "window.h"
#include "text.h"
ICON(play)
ICON(pause)
ICON(next)

struct Player : Application {
    array<string> folders;
    array<string> files;

    AudioFile media;
    AudioOutput audio __({&media,&AudioFile::read});

    ToggleButton playButton __(share(playIcon()), share(pauseIcon()));
    TriggerButton nextButton __(share(nextIcon()));
    Text elapsed __(string("00:00"_));
    Slider slider;
    Text remaining __(string("00:00"_));
    HBox toolbar __(&playButton, &nextButton, &elapsed, &slider, &remaining);
    Scroll< List<Text> > albums;
    Scroll< List<Text> > titles;
    HBox main __( &albums.parent(), &titles.parent() );
    VBox layout __( &toolbar, &main );
    Window window __(&layout, int2(512,512), "Player"_, pauseIcon());

    Player() {
        //window.globalShortcut(XF86AudioPlay).connect(this, &Player::togglePlay);
        window.localShortcut(Escape).connect(this, &Player::quit);
        playButton.toggled.connect(this, &Player::setPlaying);
        nextButton.triggered.connect(this, &Player::next);
        slider.valueChanged.connect(this, &Player::seek);
        media.timeChanged.connect(this, &Player::update);
        albums.activeChanged.connect(this, &Player::playAlbum);
        titles.activeChanged.connect(this, &Player::playTitle);
        media.audioOutput= __(audio.frequency,audio.channels);

        folders = listFiles("Music"_,Sort|Folders);
        assert(folders);
        for(string& folder : folders) albums << Text(string(section(folder,'/',-2,-1)), 10);

        /*for(string&& path: arguments) {
            assert(exists(path),path);
            if(isFolder(path)) play(path); else appendFile(move(path));
        }*/
        if(!files && exists("/Music/.last"_)) {
            string last = readFile("/Music/.last"_);
            string folder = string(section(last,'/',0,3));
            albums.index = folders.indexOf(folder);
            array<string> files = listFiles(folder,Recursive|Sort|Files);
            uint i=0; for(;i<files.size();i++) if(files[i]==last) break;
            for(;i<files.size();i++) appendFile(move(files[i]));
        }
        if(files) next();
        window.show();
        setPriority(-20);
    }
    void appendFile(string&& path) {
        string title = string(section(section(path,'/',-2,-1),'.',0,-2));
        uint i=title.indexOf('-'); i++; //skip album name
        while(i<title.size() && title[i]>='0'&&title[i]<='9') i++; //skip track number
        while(i<title.size() && (title[i]==' '||title[i]=='.'||title[i]=='-'||title[i]=='_')) i++; //skip whitespace
        titles << Text(replace(title.slice(i),"_"_," "_), 16);
        files << move(path);
    }
    void play(const string& path) {
        assert(isFolder(path));
        array<string> files = listFiles(path,Recursive|Sort|Files);
        for(string& file: files) appendFile(move(file));
        window.setSize(int2(-16,-16)); layout.update(); window.render();
    }
    void playAlbum(uint index) {
        stop(); files.clear(); titles.clear();
        window.setTitle(albums.active().text);
        play(folders[index]);
    }
    void playTitle(uint index) {
        window.setTitle(titles.active().text);
        media.open(files[index]);
        audio.start();
        setPlaying(true);
        writeFile("/Music/.last"_,files[index]);
    }
    void next() {
        if(!titles.count()) return;
        if(!playButton.enabled) setPlaying(true);
        if(titles.index+1<titles.count()) playTitle(++titles.index);
        else window.setTitle(albums.active().text);
        titles.ensureVisible(titles.active());
    }
    void togglePlay() { setPlaying(!playButton.enabled); }
    void setPlaying(bool play) {
        playButton.enabled=play;
        if(play) { audio.start(); window.setIcon(playIcon()); }
        else { audio.stop(); window.setIcon(pauseIcon()); }
    }
    void stop() {
        setPlaying(false);
        media.close();
        elapsed.text=string("00:00"_);
        slider.value = -1;
        remaining.text=string("00:00"_);
        titles.index=-1;
    }
    void seek(int position) { media.seek(position); }
    void update(int position, int duration) {
        if(position == duration) next();
        if(!window.mapped || slider.value == position) return;
        slider.value = position; slider.maximum=duration;
        elapsed.text=dec(uint64(position/60),2)+":"_+dec(uint64(position%60),2);
        remaining.text=dec(uint64((duration-position)/60),2)+":"_+dec(uint64((duration-position)%60),2);
        toolbar.update(); window.render();
    }
};
Application(Player)
