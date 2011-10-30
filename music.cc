#include "common.h"
#include "media.h"
#include "interface.h"

#include "gl.h"

ICON(play);
ICON(pause);
ICON(next);

struct Music : Application {
	AudioFile* file;
	AudioOutput* alsa;
	array<string> playlist;

	Window* window;
	Button* playButton; Button* nextButton; Text* elapsed; Slider* slider; Text* remaining;
	List* titles; //List* durations;
	//List* filesystem;

	void start(array<string>&& args) {
		file = AudioFile::instance();
		connect(file->timeChanged, update, _1, _2);

		alsa = AudioOutput::instance();
		alsa->setInput(file);

		titles = new List;
		connect(titles->currentChanged, play, _1);
		for(auto& arg : args) {
			if(!exists(arg)) log("File not found",arg);
			array<string> list;
			if(isDirectory(arg)) list=listFiles(arg); else list<<move(arg);
			for(auto& file : list) {
				if(file.endsWith(_(".mp3"))) {
					playlist << move(file);
					auto text = new Text(section(section(file,'/',-2,-1),'.'),24);
					*titles << text; //TODO: metadata parsing
				}
			}
		}
		window = Window::instance();
		window->resize(int2(1024,600));
		auto layout = new Vertical;
		auto toolbar = new Horizontal;
		*toolbar << (playButton=new Button(playIcon,pauseIcon));
		connect(playButton->triggered, togglePlay, _1);
		*toolbar << (nextButton=new Button(nextIcon));
		connect(nextButton->triggered, next);
		*toolbar << (elapsed=new Text(_("--:--"),24));
		*toolbar << (slider=new Slider());
		*toolbar << (remaining=new Text(_("--:--"),24));
		*layout << toolbar << titles;
		*window << layout;
		connect(slider->valueChanged, seek, _1);

		if(playlist.size) play(0); else { window->update(); window->render(); }
	}
	void play(int index) {
		titles->current=index;
		const string& path = playlist[index];
		playButton->enabled=true;
		slider->value = -1;
		file->open(path);
		window->rename(((Text*)titles->currentItem())->text);
		alsa->start();
	}
	void next() { stop(); if(titles->current+1<titles->count()) play(++titles->current); }
	void togglePlay(bool play) { if(play) alsa->start(); else alsa->stop(); window->render(); }
	void stop() { alsa->stop(); file->close(); }
	void seek(int position) { file->seek(position); }
	void update(int position, int duration) {
		if(position == duration) next();
		if(slider->value == position) return;
		slider->value = position; slider->maximum=duration;
		elapsed->text = toString(position/60,10,2)+_(":")+toString(position%60,10,2);
		remaining->text = toString((duration-position)/60,10,2)+_(":")+toString((duration-position)%60,10,2);
		window->update();
		window->render();
	}
} music;

// -> process.cc

static array<Poll*> polls;
void Poll::registerPoll() { polls.appendOnce((Poll*)this); }
void Poll::unregisterPoll() { polls.removeOne((Poll*)this); }

Application* app=0;
Application::Application() { assert(!app,"Multiple application compiled in executable"); app=this; }
int main(int argc, const char** argv) {
	array<string> args;
	for(int i=1;i<argc;i++) args << string(argv[i],strlen(argv[i]));
	assert(app,"No application compiled in executable");
	app->start(move(args));
	pollfd pollfds[polls.size];
	for(int i=0;i<polls.size;i++) pollfds[i] = polls[i]->poll();
	for(;;) {
		assert(::poll(pollfds,polls.size,-1)>0,"poll");
		for(int i=0;i<polls.size;i++) if(pollfds[i].revents) if(!polls[i]->event(pollfds[i])) return 0;
	}
	return 0;
}
