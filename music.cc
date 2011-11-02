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
	array<string> files;

	Window window = Window(int2(1024,600));
	VBox layout;
	 HBox toolbar;
	  ToggleButton playButton = ToggleButton(playIcon,pauseIcon);
	  TriggerButton nextButton = TriggerButton(nextIcon);
	  Text elapsed;
	  Slider slider;
	  Text remaining;
	 HBox main;
	  TextList albums; TextList titles; TextList durations;

	void start(array<string>&& args) {
		connect(file.timeChanged, update);
		audio.setInput(&file);

		connect(playButton.toggled, togglePlay);
		connect(nextButton.triggered, next);
		connect(slider.valueChanged, seek);

		for(auto& arg: args) {
			if(!exists(arg)) log("File not found",arg);
			array<string> list;
			if(isDirectory(arg)) list=listFiles(arg,true); else list<<move(arg);
			for(auto& file: list) {
				if(file.endsWith(_(".mp3"))) {
					files << move(file);
					titles << Text(16,section(section(file,'/',-2,-1),'.')); //TODO: metadata parsing
				}
			}
		}

		toolbar << &playButton << &nextButton << &elapsed << &slider << &remaining;
		main << &titles;
		layout << &toolbar << &main;
		window << &layout;

		if(files.size) next(); else { window.update(); window.render(); }
	}
	void play(int index) {
		titles.index=index;
		const string& path = files[index];
		playButton.enabled=true;
		file.open(path);
		window.rename(titles.current().text);
		audio.start();
	}
	void next() { if(titles.index+1<titles.count()) play(++titles.index); }
	void togglePlay(bool play) { if(play) audio.start(); else audio.stop(); window.render(); }
	void stop() {
		audio.stop();
		file.close();
		elapsed.text=_("--:--");
		slider.value = -1;
		remaining.text=_("--:--");
	}
	void seek(int position) { file.seek(position); }
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
