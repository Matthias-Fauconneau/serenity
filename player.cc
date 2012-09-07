#include "process.h"
#include "file.h"
#include "mpg123.h"
#include "asound.h"
#include "interface.h"
#include "layout.h"
#include "window.h"
#include "text.h"

struct Player : Application {
    array<string> folders;
    array<string> files;

    AudioFile media;
    AudioOutput audio __({&media,&AudioFile::read});

    ICON(play) ICON(pause) ToggleButton playButton __(share(playIcon()), share(pauseIcon()));
    ICON(next) TriggerButton nextButton __(share(nextIcon()));
    Text elapsed __(string("00:00"_));
    Slider slider;
    Text remaining __(string("00:00"_));
    HBox toolbar;//__(&playButton, &nextButton, &elapsed, &slider, &remaining);
    Scroll< List<Text> > albums;
    Scroll< List<Text> > titles;
    HBox main;// __( &albums.area(), &titles.area() );
    VBox layout;// __( &toolbar, &main );
    Window window __(&layout, int2(-512,-512), "Player"_, pauseIcon());

    Player() {
        albums.always=titles.always=true;
        elapsed.minSize.x=remaining.minSize.x=64;
        toolbar<<&playButton<<&nextButton<<&elapsed<<&slider<<&remaining;
        main<<&albums.area()<<&titles.area();
        layout<<&toolbar<<&main;

        albums.expanding=true; titles.main=Linear::Center;
        window.localShortcut(Escape).connect(this, &Player::quit);
        window.localShortcut(Key(' ')).connect(this, &Player::togglePlay);
        window.globalShortcut(Play).connect(this, &Player::togglePlay);
        playButton.toggled.connect(this, &Player::setPlaying);
        nextButton.triggered.connect(this, &Player::next);
        slider.valueChanged.connect(this, &Player::seek);
        media.timeChanged.connect(this, &Player::update);
        albums.activeChanged.connect(this, &Player::playAlbum);
        titles.activeChanged.connect(this, &Player::playTitle);
        media.audioOutput= __(audio.rate,audio.channels);

        folders = listFiles("Music"_,Sort|Folders);
        assert(folders);
        for(string& folder : folders) albums << Text(string(section(folder,'/',-2,-1)), 16);

        int time=0;
        if(!files && existsFile("Music/.last"_)) {
            string mark = readFile("Music/.last"_);
            ref<byte> last = section(mark,0);
            time = toInteger(section(mark,0,1,2)); log(mark,section(mark,0,1,2),time);
            string folder = string(section(last,'/',0,2));
            albums.index = folders.indexOf(folder);
            array<string> files = listFiles(folder,Recursive|Sort|Files);
            uint i=0; for(;i<files.size();i++) if(files[i]==last) break;
            for(;i<files.size();i++) appendFile(move(files[i]));
        }
        window.setSize(int2(-512,-512)); window.show();
        if(files) next();
        if(time) seek(time);
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
        window.setSize(int2(-512,-512));
        next();
    }
    void playAlbum(uint index) {
        stop(); files.clear(); titles.clear();
        window.setTitle(albums[index].text);
        play(folders[index]);
    }
    void playTitle(uint index) {
        window.setTitle(titles[index].text);
        media.open(files[index]);
        audio.start();
        setPlaying(true);
        writeFile("/Music/.last"_,string(files[index]+"\0"_+dec(0)));
    }
    void next() {
        if(!titles.count()) return;
        if(!playButton.enabled) setPlaying(true);
        if(titles.index+1<titles.count()) playTitle(++titles.index);
        else if(albums.index<albums.count()) window.setTitle(albums.active().text);
        //titles.ensureVisible(titles.active());
    }
    void togglePlay() { setPlaying(!playButton.enabled); }
    void setPlaying(bool play) {
        if(play) { audio.start(); window.setIcon(playIcon()); }
        else { audio.stop(); window.setIcon(pauseIcon()); }
        playButton.enabled=play; window.render();
    }
    void stop() {
        setPlaying(false);
        media.close();
        elapsed.setText(string("00:00"_));
        slider.value = -1;
        remaining.setText(string("00:00"_));
        titles.index=-1;
    }
    void seek(int position) { media.seek(position); }
    void update(int position, int duration) {
        if(position == duration) next();
        if(slider.value == position) return;
        writeFile("/Music/.last"_,string(files[titles.index]+"\0"_+dec(position)));
        if(!window.mapped) return;
        slider.value = position; slider.maximum=duration;
        elapsed.setText(dec(uint64(position/60),2)+":"_+dec(uint64(position%60),2));
        remaining.setText(dec(uint64((duration-position)/60),2)+":"_+dec(uint64((duration-position)%60),2));
        window.render();
    }
};
Application(Player)
