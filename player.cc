/// \file player.cc Music player
#include "thread.h"
#include "file.h"
#include "ffmpeg.h"
#include "audio.h"
#include "interface.h"
#include "selection.h"
#include "layout.h"
#include "window.h"
#include "text.h"
#include "time.h"
#include "image.h"
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
    AudioControl volume;
    static constexpr uint channels = 2;
    static constexpr uint periodSize = 8192;
    unique<AudioFile> file = 0;
    AudioOutput audio {{this,&Player::read}};
    mref<short2> lastPeriod;
    uint read(const mref<short2>& output) {
        assert_(audio.rate == file->rate);
        uint readSize = 0;
        for(mref<short2> chunk=output;;) {
            if(!file) return readSize;
            assert(readSize<output.size);
            if(audio.rate != file->rate) { queue(); return readSize; } // Returns partial period and schedule restart
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
        return readSize;
    }

// Interface
    ICON(random) ICON(random2) ToggleButton randomButton {randomIcon(), random2Icon()};
    ICON(next) TriggerButton nextButton{nextIcon()};
    ICON(play) ICON(pause) ToggleButton playButton{playIcon(), pauseIcon()};
    Text elapsed {"00:00"_};
    Slider slider;
    Text remaining {"00:00"_};
    HBox status {{&elapsed, &slider, &remaining}};
    HBox toolbar {{&randomButton, &playButton, &nextButton, &status}};
    Scroll<List<Text>> albums;
    Scroll<List<Text>> titles;
    HBox main {{ &albums, &titles }};
    VBox layout {{ &toolbar, &main },VBox::Spread};
    Window window {&layout, -int2(600,1024), "Player"_, pauseIcon()};

// Content
    String device; // Device underlying folder
    Folder folder;
    array<String> folders;
    array<String> files;
    array<String> randomSequence;

    Player() {
        albums.expanding=true; titles.expanding=true; titles.main=Linear::Center;
        window.actions[Escape] = []{ exit(); };
        window.actions[Space] = {this, &Player::togglePlay};

        window.globalAction(Play) = {this, &Player::togglePlay};
        window.globalAction(Media) = [this]{ if(window.mapped) window.hide(); else window.show(); };
        window.actions.insert(RightArrow, {this, &Player::next});

        randomButton.toggled = {this, &Player::setRandom};
        playButton.toggled = {this, &Player::setPlaying};
        nextButton.triggered = {this, &Player::next};

        slider.valueChanged = {this, &Player::seek};
        albums.activeChanged = {this, &Player::playAlbum};
        titles.activeChanged = {this, &Player::playTitle};

        if(arguments()) setFolder(arguments().first());
        else if(!folder) setFolder("/Music"_);
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
            if(!playButton.enabled) {
                audio.start(file->rate, periodSize);
                window.setIcon(playIcon());
            }
        } else {
            // Fades out the last period (assuming the hardware is not playing it (false if swap occurs right after pause))
            for(uint i: range(lastPeriod.size)) {
                float level = exp2(-12. * i / lastPeriod.size); // Linear perceived sound level
                lastPeriod[i] *= level;
            }
            lastPeriod=mref<short2>();
            if(audio) audio.stop();
            window.setIcon(pauseIcon());
            file->seek(max(0, int(file->position-lastPeriod.size)));
        }
        playButton.enabled=play;
        window.render();
        recordPosition();
    }
    void seek(int position) {
        if(file) { file->seek(position*file->rate); update(file->position/file->rate,file->duration/file->rate); /*audio->cancel();*/ }
    }
    void update(uint position, uint duration) {
        if(slider.value == (int)position || position>duration) return;
        slider.value = position; slider.maximum=duration;
        elapsed    = Text(String(dec(                position/60,2,'0')+":"_+dec(                 position%60,2,'0')),
                          16, 0, 1, 0, "DejaVuSans"_, true, 1, true, int2(64,32));
        remaining = Text(String(dec((duration-position)/60,2,'0')+":"_+dec((duration-position)%60,2,'0')),
                          16, 0, 1, 0, "DejaVuSans"_, true, 1, true, int2(64,32));
        {Rect toolbarRect = layout.layout(window.size)[0];
            Graphics update;
            update.append(toolbar.graphics(toolbarRect.size), vec2(toolbarRect.origin));
            window.render(move(update), toolbarRect.origin, toolbarRect.size);
        }
    }
    void event() override {
        if(audio) audio.stop();
        audio.start(file->rate, periodSize);
    }
} application;
