#include "process.h"
#include "file.h"
#include "media.h"
#include "interface.h"
#include <sys/resource.h>
ICON(play);
ICON(pause);
ICON(next);

struct Player : Application {
    array<string> folders;
    array<string> files;

    AudioFile media;
    AudioOutput audio;

	VBox layout;
    Window window = Window(layout,int2(640,640),"Player"_);
	 HBox toolbar;
	  ToggleButton playButton = ToggleButton(playIcon,pauseIcon);
	  TriggerButton nextButton = TriggerButton(nextIcon);
      Text elapsed = Text(16,"00:00"_);
	  Slider slider;
      Text remaining = Text(16,"00:00"_);
	 HBox main;
	  TextList albums; TextList titles; TextList durations;
    uint playHotKey = window.addHotKey("XF86AudioPlay"_);

    void start(array<string>&& arguments) {
        window.setIcon(pauseIcon);
        toolbar << playButton << nextButton << elapsed << slider << remaining;
        main << albums.parent() << titles.parent();
        layout << toolbar << main;

        window.keyPress.connect(this, &Player::keyPress);
		playButton.toggled.connect(this, &Player::togglePlay);
		nextButton.triggered.connect(this, &Player::next);
		slider.valueChanged.connect(this, &Player::seek);
        media.timeChanged.connect(this, &Player::update);
        albums.activeChanged.connect(this, &Player::playAlbum);
        titles.activeChanged.connect(this, &Player::play);
        audio.setInput(&media);
        setpriority(PRIO_PROCESS,0,0);

        folders = listFiles("/Music"_,Sort|Folders); //TODO: async IO
        for(auto& folder : folders) albums << Text(10,section(folder,'/',-2,-1));

		for(auto&& path: arguments) {
            assert(exists(path),path);
			if(isDirectory(path)) playAlbum(path); else appendFile(move(path));
		}
        if(!files && exists("/Music/.last"_)) {
            string last = mapFile("/Music/.last"_);
            string folder = section(last,'/',0,-2);
            array<string> files = listFiles(folder,Recursive|Sort|Files);
            int i=0; for(;i<files.size;i++) if(files[i]==last) break;
            for(;i<files.size;i++) appendFile(move(files[i]));
        }
        window.setVisible(true);
        layout.update();
        if(files) next();
    }
    ~Player() { if(titles.index<0) return; string& file = files[titles.index]; write(createFile("/Music/.last"_),&file,file.size); }
    void keyPress(Event key) {
        /**/ if(key == Quit) running = false;
        else if(key == playHotKey) togglePlay(!playButton.enabled);
    }
	void appendFile(string&& path) {
		string title = section(section(path,'/',-2,-1),'.',0,-2);
        int i=title.indexOf('-'); i++; //skip album name
        while(title[i]>='0'&&title[i]<='9') i++; //skip track number
        while(title[i]==' '||title[i]=='.'||title[i]=='-'||title[i]=='_') i++; //skip whitespace
		titles << Text(16,title.slice(i).replace('_',' '));
        files << move(path);
	}
	void playAlbum(const string& path) {
		assert(isDirectory(path));
        array<string> files = listFiles(path,Recursive|Sort|Files);
		for(auto&& file: files) appendFile(move(file));
        layout.update(); window.render();
	}
	void playAlbum(int index) {
		stop();
        files.clear(); titles.clear();
		playAlbum(folders[index]);
	}
    void play(int index) {
        window.rename(titles.active().text);
		playButton.enabled=true;
        media.open(files[index]);
        audio.start();
	}
	void next() {
        if(!playButton.enabled) togglePlay(true);
		if(titles.index+1<titles.count()) play(++titles.index);
	}
	void togglePlay(bool play) {
        playButton.enabled=play;
        if(play) { audio.start(); window.setIcon(playIcon); }
        else { audio.stop(); window.setIcon(pauseIcon); }
    }
	void stop() {
        togglePlay(false);
        media.close();
        elapsed.setText("00:00"_);
		slider.value = -1;
        remaining.setText("00:00"_);
		titles.index=-1;
	}
    void seek(int position) { media.seek(position); }
	void update(int position, int duration) {
		if(position == duration) next();
		if(!window.visible || slider.value == position) return;
		slider.value = position; slider.maximum=duration;
        elapsed.setText(toString(position/60,10,2)+":"_+toString(position%60,10,2));
        remaining.setText(toString((duration-position)/60,10,2)+":"_+toString((duration-position)%60,10,2));
		window.render();
	}
} player;
