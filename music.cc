#include "process.h"
#include "file.h"
#include "media.h"
#include "interface.h"

ICON(play);
ICON(pause);
ICON(next);

struct Music : Application {
	AudioFile file;
	AudioOutput audio;
	array<string> folders;
	array<string> files;

	Window window = Window(int2(640,640));
	VBox layout;
	 HBox toolbar;
	  ToggleButton playButton = ToggleButton(playIcon,pauseIcon);
	  TriggerButton nextButton = TriggerButton(nextIcon);
	  Text elapsed = Text(16,_("--:--"));
	  Slider slider;
	  Text remaining = Text(16,_("--:--"));
	 HBox main;
	  TextList albums; TextList titles; TextList durations;
	uint playHotKey = window.addHotKey(_("XF86AudioPlay"));

	void start(array<string>&& arguments) {
		toolbar << &playButton << &nextButton << &elapsed << &slider << &remaining;
		main << &albums << &titles;
		layout << &toolbar << &main;
		window << &layout;
		albums.margin=0;

		connect(window.keyPress, keyPress);
		connect(playButton.toggled, togglePlay);
		connect(nextButton.triggered, next);
		connect(slider.valueChanged, seek);
		connect(file.timeChanged, update);
		connect(albums.currentChanged, playAlbum);
		connect(titles.currentChanged, play);
		audio.setInput(&file);

		folders = listFiles(_("/root/Music"));
		for(auto&& folder : folders) albums << Text(10,section(folder,'/',-2,-1));

		for(auto&& path: arguments) {
			assert(exists(path),"Invalid URL",path);
			if(isDirectory(path)) playAlbum(path); else appendFile(move(path));
		}
		if(files.size) next(); else { window.update(); window.render(); }
	}
	void keyPress(uint key) {
		if(key == playHotKey) togglePlay(!playButton.enabled);
	}
	void appendFile(string&& path) {
		if(!path.endsWith(_(".mp3"))) { log("Unsupported format",path); return; }
		files << move(path);
		titles << Text(16,section(section(path,'/',-2,-1),'.')); //TODO: metadata parsing
	}
	void playAlbum(const string& path) {
		assert(isDirectory(path));
		array<string> files = listFiles(path,Recursive|Sort);
		for(auto&& file: files) appendFile(move(file));
		window.update();
		window.render();
	}
	void playAlbum(int index) {
		stop();
		titles.items.clear();
		playAlbum(folders[index]);
	}
	void play(int index) {
		playButton.enabled=true;
		file.open(files[index]);
		window.rename(titles.current().text);
		audio.start();
	}
	void next() {
		if(titles.index+1<titles.count()) play(++titles.index);
	}
	void togglePlay(bool play) {
		if(play) { audio.start(); playButton.enabled=true; }
		else { audio.stop(); playButton.enabled=false; }
		window.render();
	}
	void stop() {
		audio.stop();
		file.close();
		elapsed.text=_("--:--");
		slider.value = -1;
		remaining.text=_("--:--");
		titles.index=-1;
	}
	void seek(int position) {
		file.seek(position);
	}
	void update(int position, int duration) {
		if(position == duration) next();
		if(slider.value == position) return;
		slider.value = position; slider.maximum=duration;
		elapsed.text = toString(position/60,10,2)+_(":")+toString(position%60,10,2);
		remaining.text = toString((duration-position)/60,10,2)+_(":")+toString((duration-position)%60,10,2);
		window.update();
		window.render();
	}
} music;
