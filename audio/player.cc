/// \file player.cc Music player
#include "thread.h"
#include "file.h"
#include "audio.h"
#include "asound.h"
#include "interface.h"
#include "selection.h"
#include "ui/layout.h"
#include "window.h"
#include "text.h"
#include "time.h"
#include "core/image.h"
#include "png.h"

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
struct Player : Poll {
// Playback
    //AudioControl volume;
	static constexpr uint channels = 2;
    static constexpr uint periodSize = 0;
	unique<FFmpeg> file = 0;
    AudioOutput audio {{this,&Player::read}};
    mref<short2> lastPeriod;
    size_t read(mref<short2> output) {
        //assert_(audio.rate == file->audioFrameRate); Might fail on track change
        uint readSize = 0;
        for(mref<short2> chunk=output;;) {
            if(!file) return readSize;
            assert(readSize<output.size);
            if(audio.rate != file->audioFrameRate) { queue(); return readSize; } // Returns partial period and schedule restart
            size_t read = file->read16(mcast<int16>(chunk));
            assert(read<=chunk.size, read);
            chunk = chunk.slice(read); readSize += read;
            if(readSize == output.size) { update(file->audioTime/file->audioFrameRate,file->duration/file->audioFrameRate); break; } // Complete chunk
            else next(); // End of file
        }
        if(!lastPeriod) for(uint i: range(output.size)) { // Fades in
            float level = exp(12. * ((float) i / output.size - 1) ); // Linear perceived sound level
            output[i][0] *= level;
            output[i][1] *= level;
        }
        lastPeriod = output;
        return readSize;
    }

// Interface
    ICON(random) ICON(random2) ToggleButton randomButton {randomIcon(), random2Icon()};
    ICON(next) TriggerButton nextButton{nextIcon()};
    ICON(play) ICON(pause) ToggleButton playButton{playIcon(), pauseIcon()};
    Text elapsed {"00:00"};
    Slider slider;
    Text remaining {"00:00"};
    HBox status {{&elapsed, &slider, &remaining}};
	HBox toolbar {{&randomButton, &playButton, &nextButton, &status}};
    Scroll<List<Text>> albums;
    Scroll<List<Text>> titles;
    HBox main {{ &albums, &titles }};
	VBox layout {{ &toolbar, &main }};
	unique<Window> window = ::window(&layout, -int2(1050, 1680)/2);

// Content
    String device; // Device underlying folder
    Folder folder;
    array<String> folders;
    array<String> files;
    array<String> randomSequence;

    Player() {
		window->setIcon(pauseIcon());
		albums.scrollbar=false; albums.expanding=true; titles.expanding=true; titles.main=Linear::Center;
		window->actions[Space] = {this, &Player::togglePlay};

		window->globalAction(Play) = {this, &Player::togglePlay};
		window->globalAction(Media) = [this]{ if(window->mapped) window->hide(); else window->show(); };
		window->actions.insert(RightArrow, {this, &Player::next});

        randomButton.toggled = {this, &Player::setRandom};
        playButton.toggled = {this, &Player::setPlaying};
        nextButton.triggered = {this, &Player::next};

        slider.valueChanged = {this, &Player::seek};
        albums.activeChanged = {this, &Player::playAlbum};
        titles.activeChanged = {this, &Player::playTitle};

        if(arguments()) setFolder(arguments()[0]);
        else if(!folder) setFolder("/Music");
		window->show();
        mainThread.setPriority(-20);
    }
    ~Player() { recordPosition(); /*Records current position*/ }
    void recordPosition() {
        assert_(titles.index<files.size && file);
        if(/*writableFile(".last", folder) &&*/ titles.index<files.size && file)
            writeFile(".last",str(files[titles.index]+'\0'+str(file->audioTime/file->audioFrameRate)+(randomSequence?"\0random"_:""_)), folder, true);
    }
    void setFolder(string path) {
        assert(folder.name() != path);
        if(folder.name() == path) return;
        folders.clear(); albums.clear(); files.clear(); titles.clear(); randomSequence.clear();
        folder = path;
        folders = folder.list(Folders|Sorted);
        for(string folder: folders) albums.append( section(folder,'/',-2,-1) );
        if(existsFile(".last", folder)) {
            String mark = readFile(".last", folder);
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
				seek(parseInteger(section(mark,'\0',1,2)));
            }
            return;
        }
        if(randomButton.enabled) setRandom(true); // Regenerates random sequence for folder
        updatePlaylist();
        if(files) playTitle(0);
    }
    void insertFile(int index, const string folder, const string file, bool withAlbumName) {
    string fileName = section(file,'/',-2,-1);
    if(fileName.contains('.')) fileName = section(fileName,'.',0,-2);
     String title = copyRef(fileName);
        uint i=title.indexOf('-'); i++; //skip album name
        while(i<title.size && title[i]>='0'&&title[i]<='9') i++; //skip track number
        while(i<title.size && (title[i]==' '||title[i]=='.'||title[i]=='-'||title[i]=='_')) i++; //skip whitespace
        title = replace(title.slice(i),"_"," ");
        if(withAlbumName) title = folder + " - " + title;
        titles.insertAt(index, Text(title, 16));
		files.insertAt(index, str(folder+'/'+file));
    }
    void queueFile(const string folder, const string file, bool withAlbumName) { insertFile(titles.size, folder, file, withAlbumName); }
    void playAlbum(const string album) {
        assert(existsFolder(album,folder),album);
        array<String> files = Folder(album,folder).list(Recursive|Files|Sorted);
        for(const String& file: files) queueFile(album, file, false);
        titles.index=-1; next();
    }
    void playAlbum(uint index) {
        files.clear(); titles.clear();
		window->setTitle(toUTF8(albums[index].text));
        playAlbum(folders[index]);
    }
    void playTitle(uint index) {
        titles.index = index;
		window->setTitle(toUTF8(titles[index].text));
		file = unique<FFmpeg>(folder.name()+'/'+files[index]);
        if(!file->file) { file=0; log("Error reading", folder.name()+'/'+files[index]); return; }
		assert(file->channels == 2);
        setPlaying(true);
    }
    void next() {
        if(titles.index+1<titles.count()) playTitle(titles.index+1);
        else if(albums.index+1<albums.count()) playAlbum(++albums.index);
        else if(albums.count()) playAlbum(albums.index=0);
        else { setPlaying(false); file = 0; return; }
        updatePlaylist();
    }
    void setRandom(bool random) {
        randomSequence.clear();
        randomButton.enabled = random;
        if(random) {
			main = ref<Widget*>{&titles}; // Hide albums
            // Explicits random sequence to: resume the sequence from the last played file, ensure files are played once in the sequence.
            randomSequence = shuffle(folder.list(Recursive|Files|Sorted));
            titles.shrink(titles.index+1); this->files.shrink(titles.index+1); // Replaces all queued titles with the next tracks drawn from the random sequence
            updatePlaylist();
		} else main = ref<Widget*>{&albums,&titles}; // Show albums
    }
    void updatePlaylist() {
        if(!randomSequence) return;
        uint randomIndex = 0; // Index of active title in random sequence
        if(titles.index < files.size) {
            for(uint i: range(randomSequence.size)) if(randomSequence[i]==files[titles.index]) { randomIndex=i; break; }
        }
        while(titles.index < 16) { // Regenerates history of 16 previous random tracks
            int index = randomIndex - titles.index - 1 + randomSequence.size;
            string path = randomSequence[index%randomSequence.size];
            string folder = section(path,'/',0,1), file = section(path,'/',1,-1);
            insertFile(0, folder, file, true);
            titles.index++;
        }
        randomIndex += files.size-titles.index; // Assumes already queued tracks are from randomSequence
        while(titles.count() < titles.index + 16) { // Schedules at least 16 tracks drawing from random sequence as needed
            string path = randomSequence[randomIndex%randomSequence.size];
            string folder = section(path,'/',0,1), file = section(path,'/',1,-1);
            queueFile(folder, file, true);
            randomIndex++;
        }
        while(titles.count() > 32 && titles.index > 0) { titles.removeAt(0); files.removeAt(0); titles.index--; } // Limits total size by removing oldest tracks
    }
    void togglePlay() { setPlaying(!playButton.enabled); }
    void setPlaying(bool play) {
        if(play) {
            assert_(file);
            if(!playButton.enabled) {
                audio.start(file->audioFrameRate, periodSize, 16, 2);
				window->setIcon(playIcon());
            }
        } else {
            // Fades out the last period (assuming the hardware is not playing it (false if swap occurs right after pause))
            for(uint i: range(lastPeriod.size)) {
                float level = exp2(-12. * i / lastPeriod.size); // Linear perceived sound level
                lastPeriod[i] *= level;
            }
            lastPeriod=mref<short2>();
            if(audio) audio.stop();
			window->setIcon(pauseIcon());
            file->seek(max(0, int(file->audioTime-lastPeriod.size)));
        }
        playButton.enabled=play;
		window->render();
        recordPosition();
    }
    void seek(int position) {
        if(file) { file->seek(position*file->audioFrameRate); update(file->audioTime/file->audioFrameRate,file->duration/file->audioFrameRate); /*audio->cancel();*/ }
    }
    void update(uint position, uint duration) {
        if(slider.value == (int)position || position>duration) return;
        slider.value = position; slider.maximum=duration;
		elapsed    = Text(String(str(                position/60,2u,'0')+':'+str(                 position%60,2u,'0')),
						  16, 0, 1, 0, "DejaVuSans", true, 1, 0, int2(64,32));
		remaining = Text(String(str((duration-position)/60,2u,'0')+':'+str((duration-position)%60,2u,'0')),
						  16, 0, 1, 0, "DejaVuSans", true, 1, 0, int2(64,32));
		{Rect toolbarRect = layout.layout(vec2(window->size))[0];
			shared<Graphics> update;
			update->graphics.insert(vec2(toolbarRect.origin()), toolbar.graphics(toolbarRect.size(), toolbarRect));
			window->render(move(update), int2(toolbarRect.origin()), int2(toolbarRect.size()));
        }
    }
    void event() override {
        if(audio) audio.stop();
        if(file) audio.start(file->audioFrameRate, periodSize, 16, 2);
    }
} application;
